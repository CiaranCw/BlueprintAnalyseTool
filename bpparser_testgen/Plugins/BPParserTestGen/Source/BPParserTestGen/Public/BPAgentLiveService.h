// Copyright BlueprintAnalyseTool. All Rights Reserved.
//
// BPAgentLiveService: an in-editor, file-queue Blueprint Agent service ("editor_live" mode).
//
// Purpose: let external AIs (Claude Code / Cursor / Codex) run analyze / edit / create against
// an ALREADY-OPEN UE Editor, without launching a new UnrealEditor-Cmd process per request.
//
// Transport (phase 1): local request-directory polling (no ports, no HTTP, company-network friendly).
//   inbox : <ProjectSaved>/BPParserAgentRequests/inbox/<id>.request.json   (payload)
//           <ProjectSaved>/BPParserAgentRequests/inbox/<id>.ready          (commit marker)
//   report: <output_dir>/editor_live/<id>/manifest.json  (+ blueprint_ir.json/summary.md/viz/logs)
//   outbox: <ProjectSaved>/BPParserAgentRequests/outbox/<id>.done | <id>.failed
//
// Threading: the poll/execute callback runs on the GameThread (FTSTicker on the core ticker), so all
// UObject access (IR dump / edit / create) is main-thread safe. A reentrancy guard + per-request
// attempt budget prevent overlap and hangs while the editor is busy (PIE / compiling / saving).
//
// Safety: analyze is strictly read-only (no save / no compile / no node mutation). edit / create must
// be explicitly authorised in the request and are refused during PIE / on dirty user-edited targets
// unless acknowledged. The user's blueprint assets are never modified by analyze or status.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class UBlueprint;
class UObject;
class FJsonObject;

/** Snapshot of the live editor state, surfaced in status/manifest for callers to reason about safety. */
struct FBPAgentEditorState
{
	bool bIsPie = false;
	bool bIsSaving = false;
	bool bIsCompilingBlueprints = false;
	int32 DirtyAssetsCount = 0;
	TArray<FString> DirtyAssets;   // "/Game/..." package names (capped)
};

class FBPAgentLiveService : public TSharedFromThis<FBPAgentLiveService>
{
public:
	FBPAgentLiveService() = default;
	~FBPAgentLiveService();

	/** Start polling the inbox (idempotent). No-op if already running. */
	void Start();

	/** Stop polling (idempotent). */
	void Stop();

	bool IsRunning() const { return TickHandle.IsValid(); }

	/** <ProjectSaved>/BPParserAgentRequests */
	static FString QueueRootDir();
	/** <ProjectSaved>/BPParserAgentRequests/inbox */
	static FString InboxDir();
	/** <ProjectSaved>/BPParserAgentRequests/outbox */
	static FString OutboxDir();
	/** Default report root when a request omits output_dir: <ProjectSaved>/BPParserAgentReports */
	static FString DefaultReportRootDir();

	/** Fill an editor-state snapshot (safe on GameThread). */
	static void CaptureEditorState(FBPAgentEditorState& Out, int32 MaxDirtyListed = 50);

	/** Human-readable one-line status (for console command / logs). */
	FString GetStatusLine() const;

private:
	/** Core ticker callback (GameThread). Returns true to keep ticking. */
	bool Tick(float DeltaTime);

	/** Scan inbox for committed requests and process at most one per tick. */
	void PumpOnce();

	/** Execute a single committed request. Writes outputs + outbox marker; returns process-style code. */
	int32 ProcessRequest(const FString& RequestId, const TSharedPtr<FJsonObject>& Request);

	// --- task handlers (all GameThread) ---
	int32 HandleStatus (const FString& RequestId, const TSharedPtr<FJsonObject>& Request, const FString& ReportDir);
	int32 HandleAnalyze(const FString& RequestId, const TSharedPtr<FJsonObject>& Request, const FString& ReportDir);
	int32 HandleEdit   (const FString& RequestId, const TSharedPtr<FJsonObject>& Request, const FString& ReportDir);
	int32 HandleCreate (const FString& RequestId, const TSharedPtr<FJsonObject>& Request, const FString& ReportDir);

	/** recover_scan: flag in-flight requests with a non-terminal journal + no outbox marker as
	 *  pending_editor_restart (orphaned by an editor crash / forced exit). Genuine recovery, no process kill. */
	int32 HandleRecoverScan(const FString& RequestId, const TSharedPtr<FJsonObject>& Request, const FString& ReportDir);

	/** test_control: fault-injection for regression ONLY — sets a bounded window during which the editor-state
	 *  gate reports PIE / busy, so the PIE-refuse and compiling-wait code paths can be exercised deterministically
	 *  without driving a real PIE session or a real long compile in the user's open editor. */
	int32 HandleTestControl(const FString& RequestId, const TSharedPtr<FJsonObject>& Request, const FString& ReportDir);

	/** Scan the report root for orphaned in-flight requests and mark them pending_editor_restart. */
	int32 RunRecoveryScan(const FString& ScanEditorLiveDir, TArray<FString>& OutRecovered);

	// --- report writers / helpers ---
	/** Wrap the raw dumper object into the unified IR shape (asset / blueprint / graphs) used by native_full. */
	static TSharedPtr<FJsonObject> BuildUnifiedIR(const TSharedPtr<FJsonObject>& Raw, const FString& PackagePath, const FString& ShortName);

	/** True if the given asset is currently open in an asset editor tab. */
	static bool IsBlueprintAssetEditorOpen(UObject* Asset);

	/** Build the shared "editor_live" manifest block (available/service_running/source_state/editor_state/fallback). */
	TSharedPtr<FJsonObject> MakeEditorLiveBlock(const FString& RequestId, const FString& SourceState, const FBPAgentEditorState& St) const;

	/** Write the full analyze deliverables (ir/summary/score/viz/logs/manifest). Returns 0 success / 20 failed. */
	int32 WriteAnalyzeReport(const FString& RequestId, const FString& Dir, const TSharedPtr<FJsonObject>& UnifiedIR,
		const FString& PkgPath, const FString& ShortName, const FString& SourceState, const FBPAgentEditorState& St,
		const TArray<FString>& Warnings, const TArray<FString>& Errors, const TArray<FString>& Manual);

	/** Minimal analyze manifest for the no-asset / load-failure case. */
	void WriteAnalyzeManifestOnly(const FString& RequestId, const FString& Dir, const FString& Status, const FString& AssetType,
		const FBPAgentEditorState& St, const TArray<FString>& Warnings, const TArray<FString>& Errors, const TArray<FString>& Manual);

	/** Append a journal entry (request_journal.json) for production tracing. */
	static void AppendJournal(const FString& ReportDir, const FString& RequestId, const FString& Phase,
		const FString& Status, const TSharedPtr<FJsonObject>& Detail = nullptr);

	/** True if outbox already has a terminal marker for this request_id (idempotency). */
	static bool IsRequestCompleted(const FString& RequestId, FString& OutManifestPath);

	/** Resolve source_state for a loaded blueprint package. */
	static FString ResolveSourceState(UObject* Asset);

	/** Post-apply analyze of the edited asset (read-only re-dump for diff verification). */
	int32 RunPostAnalyze(const FString& RequestId, UBlueprint* BP, const FString& ReportDir, const FBPAgentEditorState& St);

	/** Manifest for edit/create tasks (points at the reused engine's artifacts). */
	void WriteEditCreateManifest(const FString& RequestId, const FString& Dir, const FString& Task, const FString& Status,
		const FString& AssetPath, const FString& Pointer, const FBPAgentEditorState& St,
		const TArray<FString>& Warnings, const TArray<FString>& Errors, const TArray<FString>& Manual);

	FTSTicker::FDelegateHandle TickHandle;
	bool bProcessing = false;               // reentrancy guard
	double PollIntervalSeconds = 1.5;

	/** Per-request retry budget while the editor is transiently busy (compiling/saving). */
	TMap<FString, int32> BusyAttempts;
	int32 MaxBusyAttempts = 20;             // ~ MaxBusyAttempts * PollInterval seconds before giving up

	/** Asset-path lock: only one mutating request per asset at a time. */
	TSet<FString> LockedAssetPaths;
	FString ActiveRequestId;

	/** Pending requests waiting for the same asset lock (FIFO by request id sort). */
	TArray<FString> PendingAssetQueue;
};
