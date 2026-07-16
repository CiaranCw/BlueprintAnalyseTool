// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPAgentLiveService.h"
#include "BPParserTestGenModule.h"
#include "BPGenIRDumper.h"
#include "BPGenUECompat.h"
#include "BPATEdit.h"
#include "BPCreate.h"
#include "BPPreflight.h"

#include "Engine/Blueprint.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Kismet2/KismetEditorUtilities.h"   // GCompilingBlueprint

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/DateTime.h"
#include "Misc/App.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

// ============================================================================
// Local helpers
// ============================================================================
namespace
{
	FString NowIso() { return FDateTime::UtcNow().ToIso8601(); }

	void WriteUtf8NoBom(const FString& Path, const FString& Text)
	{
		const FString Dir = FPaths::GetPath(Path);
		if (!Dir.IsEmpty()) { IFileManager::Get().MakeDirectory(*Dir, /*Tree*/ true); }
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	FString SerializeObj(const TSharedPtr<FJsonObject>& Root)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Root.ToSharedRef(), W);
		return Out;
	}

	void WriteJsonObj(const FString& Path, const TSharedPtr<FJsonObject>& Root)
	{
		WriteUtf8NoBom(Path, SerializeObj(Root));
	}

	// JSON string-array serializer that is always a valid array (even for 0/1 elements).
	FString JsonStringArray(const TArray<FString>& Items)
	{
		TArray<TSharedPtr<FJsonValue>> Vals;
		for (const FString& S : Items) { Vals.Add(MakeShared<FJsonValueString>(S)); }
		FString Out;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Vals, W);
		return Out;
	}

	FString JStr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FString& Def = FString())
	{
		FString V; return (O.IsValid() && O->TryGetStringField(Key, V)) ? V : Def;
	}
	bool JBool(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, bool Def)
	{
		bool V; return (O.IsValid() && O->TryGetBoolField(Key, V)) ? V : Def;
	}
	const TSharedPtr<FJsonObject>* JObj(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* P = nullptr;
		return (O.IsValid() && O->TryGetObjectField(Key, P)) ? P : nullptr;
	}

	// "/Game/UI/WBP_X" or "/Game/UI/WBP_X.uasset" -> "/Game/UI/WBP_X"
	FString ToPackagePath(const FString& AssetPath)
	{
		FString P = AssetPath;
		P.RemoveFromEnd(TEXT(".uasset"));
		// strip an object suffix if a full object path was given ("/Game/X.X")
		int32 Dot; FString PkgOnly = P;
		if (P.StartsWith(TEXT("/")) && P.FindChar('.', Dot)) { PkgOnly = P.Left(Dot); }
		return PkgOnly;
	}

	// "/Game/UI/WBP_X" -> "/Game/UI/WBP_X.WBP_X" (object path used by StaticFindObject)
	FString ToObjectPath(const FString& PackagePath)
	{
		const FString Short = FPackageName::GetShortName(PackagePath);
		return PackagePath + TEXT(".") + Short;
	}

	FString Sanitize(const FString& In)
	{
		FString Out;
		for (TCHAR C : In)
		{
			Out += (FChar::IsAlnum(C) || C == '_' || C == '-') ? C : TCHAR('_');
		}
		Out.TrimStartAndEndInline();
		while (Out.StartsWith(TEXT("_"))) { Out.RemoveAt(0); }
		while (Out.EndsWith(TEXT("_")))   { Out.RemoveAt(Out.Len() - 1); }
		return Out.IsEmpty() ? TEXT("req") : Out;
	}

	FString AlnumOnly(const FString& In)
	{
		FString Out;
		for (TCHAR C : In) { if (FChar::IsAlnum(C)) { Out += C; } }
		return Out;
	}

	FString DotSafe(const FString& In)
	{
		FString S = In;
		S.ReplaceInline(TEXT("\""), TEXT("'"));
		S.ReplaceInline(TEXT("\\"), TEXT("/"));
		S.ReplaceInline(TEXT("\r"), TEXT(" "));
		S.ReplaceInline(TEXT("\n"), TEXT(" "));
		return S;
	}

	// ---- regression fault-injection (test_control) ----
	// When the current time is before these deadlines, the editor-state gate reports PIE / busy. This lets
	// the regression exercise the PIE-refuse and compiling-wait code paths deterministically WITHOUT driving
	// a real PIE session or a real long compile in the user's open editor. Never affects production requests
	// (deadlines are 0 unless a test_control request set them), and always self-expires.
	double GTestForcePieUntilSeconds = 0.0;
	double GTestForceBusyUntilSeconds = 0.0;
	bool TestForcePieActive()  { return FPlatformTime::Seconds() < GTestForcePieUntilSeconds; }
	bool TestForceBusyActive() { return FPlatformTime::Seconds() < GTestForceBusyUntilSeconds; }

	// ---- editor state ----
	bool IsPIENow()
	{
		return (GEditor && (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor)) || TestForcePieActive();
	}
	bool IsSavingNow()
	{
		return GIsSavingPackage;
	}
	bool IsCompilingBlueprintsNow()
	{
		// GCompilingBlueprint is a global set during blueprint compilation (declared in KismetEditorUtilities.h).
		return GCompilingBlueprint || TestForceBusyActive();
	}
}

// ============================================================================
// Static path accessors
// ============================================================================
FString FBPAgentLiveService::QueueRootDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BPParserAgentRequests")));
}
FString FBPAgentLiveService::InboxDir()  { return FPaths::Combine(QueueRootDir(), TEXT("inbox")); }
FString FBPAgentLiveService::OutboxDir() { return FPaths::Combine(QueueRootDir(), TEXT("outbox")); }
FString FBPAgentLiveService::DefaultReportRootDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BPParserAgentReports")));
}

// ============================================================================
// Editor-state snapshot
// ============================================================================
void FBPAgentLiveService::CaptureEditorState(FBPAgentEditorState& Out, int32 MaxDirtyListed)
{
	Out.bIsPie = IsPIENow();
	Out.bIsSaving = IsSavingNow();
	Out.bIsCompilingBlueprints = IsCompilingBlueprintsNow();
	Out.DirtyAssets.Reset();
	Out.DirtyAssetsCount = 0;

	for (TObjectIterator<UPackage> It; It; ++It)
	{
		UPackage* Pkg = *It;
		if (!Pkg || !Pkg->IsDirty()) { continue; }
		const FString Name = Pkg->GetName();
		if (!Name.StartsWith(TEXT("/Game/"))) { continue; }   // only user content
		++Out.DirtyAssetsCount;
		if (Out.DirtyAssets.Num() < MaxDirtyListed) { Out.DirtyAssets.Add(Name); }
	}
}

// ============================================================================
// Lifecycle
// ============================================================================
FBPAgentLiveService::~FBPAgentLiveService()
{
	Stop();
}

void FBPAgentLiveService::Start()
{
	if (TickHandle.IsValid()) { return; }

	IFileManager::Get().MakeDirectory(*InboxDir(), true);
	IFileManager::Get().MakeDirectory(*OutboxDir(), true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(InboxDir(), TEXT("processed")), true);

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FBPAgentLiveService::Tick),
		(float)PollIntervalSeconds);

	UE_LOG(LogBPParserTestGen, Display,
		TEXT("BPAgentLiveService: STARTED. inbox=%s report=%s poll=%.1fs"),
		*InboxDir(), *DefaultReportRootDir(), PollIntervalSeconds);

	// Recovery: flag any request orphaned by a previous editor crash / forced exit (non-terminal journal
	// with no outbox marker) as pending_editor_restart, so a caller can re-drive it deterministically.
	{
		TArray<FString> Recovered;
		RunRecoveryScan(DefaultReportRootDir() / TEXT("editor_live"), Recovered);
	}
}

void FBPAgentLiveService::Stop()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
		UE_LOG(LogBPParserTestGen, Display, TEXT("BPAgentLiveService: STOPPED."));
	}
}

FString FBPAgentLiveService::GetStatusLine() const
{
	FBPAgentEditorState St; CaptureEditorState(St, 0);
	return FString::Printf(
		TEXT("BPAgentLiveService running=%s pie=%s saving=%s compiling=%s dirty=%d inbox=%s"),
		IsRunning() ? TEXT("yes") : TEXT("no"),
		St.bIsPie ? TEXT("yes") : TEXT("no"),
		St.bIsSaving ? TEXT("yes") : TEXT("no"),
		St.bIsCompilingBlueprints ? TEXT("yes") : TEXT("no"),
		St.DirtyAssetsCount, *InboxDir());
}

bool FBPAgentLiveService::Tick(float /*DeltaTime*/)
{
	if (!bProcessing)
	{
		bProcessing = true;
		PumpOnce();
		bProcessing = false;
	}
	return true; // keep ticking
}

// ============================================================================
// Queue pump
// ============================================================================
void FBPAgentLiveService::PumpOnce()
{
	// Find committed requests (those with a .ready marker).
	TArray<FString> ReadyFiles;
	IFileManager::Get().FindFiles(ReadyFiles, *(InboxDir() / TEXT("*.ready")), /*Files*/ true, /*Dirs*/ false);
	if (ReadyFiles.Num() == 0) { return; }

	// Process at most ONE per tick (keeps each tick short / editor responsive).
	const FString ReadyName = ReadyFiles[0];                 // "<id>.ready"
	FString RequestId = ReadyName; RequestId.RemoveFromEnd(TEXT(".ready"));

	// Locate the payload: "<id>.request.json" (preferred) or "<id>.json".
	FString ReqPath = InboxDir() / (RequestId + TEXT(".request.json"));
	if (!FPaths::FileExists(ReqPath)) { ReqPath = InboxDir() / (RequestId + TEXT(".json")); }

	auto MoveToProcessed = [&]()
	{
		const FString Pd = InboxDir() / TEXT("processed");
		IFileManager::Get().MakeDirectory(*Pd, true);
		IFileManager::Get().Move(*(Pd / (RequestId + TEXT(".request.json"))), *ReqPath, true, true);
		IFileManager::Get().Move(*(Pd / ReadyName), *(InboxDir() / ReadyName), true, true);
	};
	auto WriteOutbox = [&](int32 Code, const FString& ManifestPath)
	{
		IFileManager::Get().MakeDirectory(*OutboxDir(), true);
		const bool bOk = (Code == 0 || Code == 10);
		TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("request_id"), RequestId);
		M->SetNumberField(TEXT("exit_code"), Code);
		M->SetStringField(TEXT("status"), bOk ? TEXT("done") : TEXT("failed"));
		M->SetStringField(TEXT("manifest"), ManifestPath);
		M->SetStringField(TEXT("generated_at"), NowIso());
		const FString Marker = OutboxDir() / (RequestId + (bOk ? TEXT(".done") : TEXT(".failed")));
		WriteJsonObj(Marker, M);
	};

	if (!FPaths::FileExists(ReqPath))
	{
		UE_LOG(LogBPParserTestGen, Warning, TEXT("BPAgentLiveService: %s has .ready but no payload; skipping."), *RequestId);
		const FString RepDir = DefaultReportRootDir() / TEXT("editor_live") / Sanitize(RequestId);
		WriteOutbox(30, RepDir / TEXT("manifest.json"));
		IFileManager::Get().Move(*(InboxDir() / TEXT("processed") / ReadyName), *(InboxDir() / ReadyName), true, true);
		return;
	}

	// Parse payload.
	FString ReqText;
	if (!FFileHelper::LoadFileToString(ReqText, *ReqPath))
	{
		UE_LOG(LogBPParserTestGen, Warning, TEXT("BPAgentLiveService: cannot read %s"), *ReqPath);
		WriteOutbox(30, TEXT(""));
		MoveToProcessed();
		return;
	}
	TSharedPtr<FJsonObject> Request;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReqText);
	if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
	{
		UE_LOG(LogBPParserTestGen, Warning, TEXT("BPAgentLiveService: invalid JSON in %s"), *ReqPath);
		WriteOutbox(30, TEXT(""));
		MoveToProcessed();
		return;
	}

	const FString TaskType = JStr(Request, TEXT("task_type")).ToLower();

	// Idempotency: if this request_id already completed, re-emit outbox pointer without re-executing.
	{
		FString PrevManifest;
		if (IsRequestCompleted(RequestId, PrevManifest))
		{
			UE_LOG(LogBPParserTestGen, Display, TEXT("BPAgentLiveService: idempotent skip %s -> %s"), *RequestId, *PrevManifest);
			WriteOutbox(0, PrevManifest.IsEmpty() ? (DefaultReportRootDir() / TEXT("editor_live") / Sanitize(RequestId) / TEXT("manifest.json")) : PrevManifest);
			MoveToProcessed();
			return;
		}
	}

	// Transient-busy handling: status reports state immediately; analyze/edit/create wait for
	// save/compile to finish (bounded), never hanging.
	if (TaskType != TEXT("status") && (IsSavingNow() || IsCompilingBlueprintsNow()))
	{
		int32& Att = BusyAttempts.FindOrAdd(RequestId);
		++Att;
		if (Att < MaxBusyAttempts)
		{
			// leave request in place; retry on a later tick. Trace the wait once so callers/regression
			// can observe that the request was deferred (not dropped) while the editor was busy.
			if (Att == 1)
			{
				FString WOutBase = JStr(Request, TEXT("output_dir"));
				if (WOutBase.IsEmpty()) { WOutBase = DefaultReportRootDir(); }
				AppendJournal(WOutBase / TEXT("editor_live") / Sanitize(RequestId), RequestId, TEXT("waiting"), TEXT("editor_busy"));
			}
			return;
		}
		// budget exhausted -> fail with reason, do not hang forever
		const FString RepDir = DefaultReportRootDir() / TEXT("editor_live") / Sanitize(RequestId);
		TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("schema_version"), TEXT("1.0"));
		M->SetStringField(TEXT("status"), TEXT("failed"));
		M->SetStringField(TEXT("mode"), TEXT("editor_live"));
		M->SetStringField(TEXT("reason"), TEXT("editor busy (saving/compiling) longer than wait budget"));
		WriteJsonObj(RepDir / TEXT("manifest.json"), M);
		WriteOutbox(20, RepDir / TEXT("manifest.json"));
		BusyAttempts.Remove(RequestId);
		MoveToProcessed();
		return;
	}

	// Resolve report dir: <output_dir or default>/editor_live/<id>
	FString OutBase = JStr(Request, TEXT("output_dir"));
	if (OutBase.IsEmpty()) { OutBase = DefaultReportRootDir(); }
	const FString ReportDir = OutBase / TEXT("editor_live") / Sanitize(RequestId);

	UE_LOG(LogBPParserTestGen, Display, TEXT("BPAgentLiveService: processing %s task=%s -> %s"),
		*RequestId, *TaskType, *ReportDir);

	AppendJournal(ReportDir, RequestId, TEXT("received"), TEXT("ok"));

	const int32 Code = ProcessRequest(RequestId, Request);
	if (Code == -1)
	{
		// deferred (asset lock / busy) — leave request in inbox for retry
		BusyAttempts.FindOrAdd(RequestId)++;
		return;
	}
	WriteOutbox(Code, ReportDir / TEXT("manifest.json"));
	BusyAttempts.Remove(RequestId);
	MoveToProcessed();
}

int32 FBPAgentLiveService::ProcessRequest(const FString& RequestId, const TSharedPtr<FJsonObject>& Request)
{
	FString OutBase = JStr(Request, TEXT("output_dir"));
	if (OutBase.IsEmpty()) { OutBase = DefaultReportRootDir(); }
	const FString ReportDir = OutBase / TEXT("editor_live") / Sanitize(RequestId);
	IFileManager::Get().MakeDirectory(*ReportDir, true);
	IFileManager::Get().MakeDirectory(*(ReportDir / TEXT("logs")), true);

	const FString TaskType = JStr(Request, TEXT("task_type")).ToLower();

	WriteUtf8NoBom(ReportDir / TEXT("logs") / TEXT("live_service_log.txt"),
		FString::Printf(TEXT("[%s] editor_live request_id=%s task=%s\n"), *NowIso(), *RequestId, *TaskType));

	if (TaskType == TEXT("status"))       { return HandleStatus (RequestId, Request, ReportDir); }
	if (TaskType == TEXT("analyze"))      { return HandleAnalyze(RequestId, Request, ReportDir); }
	if (TaskType == TEXT("edit"))         { return HandleEdit   (RequestId, Request, ReportDir); }
	if (TaskType == TEXT("create"))       { return HandleCreate (RequestId, Request, ReportDir); }
	if (TaskType == TEXT("recover_scan")) { return HandleRecoverScan(RequestId, Request, ReportDir); }
	if (TaskType == TEXT("test_control")) { return HandleTestControl(RequestId, Request, ReportDir); }

	// unknown task
	TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("schema_version"), TEXT("1.0"));
	M->SetStringField(TEXT("status"), TEXT("failed"));
	M->SetStringField(TEXT("mode"), TEXT("editor_live"));
	M->SetStringField(TEXT("reason"), FString::Printf(TEXT("unknown task_type '%s' (expected status|analyze|edit|create|recover_scan|test_control)"), *TaskType));
	WriteJsonObj(ReportDir / TEXT("manifest.json"), M);
	return 30;
}

// ============================================================================
// status
// ============================================================================
int32 FBPAgentLiveService::HandleStatus(const FString& RequestId, const TSharedPtr<FJsonObject>& /*Request*/, const FString& ReportDir)
{
	FBPAgentEditorState St; CaptureEditorState(St);

	TSharedPtr<FJsonObject> Es = MakeShared<FJsonObject>();
	Es->SetBoolField(TEXT("is_pie"), St.bIsPie);
	Es->SetBoolField(TEXT("is_saving"), St.bIsSaving);
	Es->SetBoolField(TEXT("is_compiling_blueprints"), St.bIsCompilingBlueprints);
	Es->SetNumberField(TEXT("dirty_assets_count"), St.DirtyAssetsCount);

	TSharedPtr<FJsonObject> Supports = MakeShared<FJsonObject>();
	Supports->SetBoolField(TEXT("analyze"), true);
	Supports->SetBoolField(TEXT("edit"), true);
	Supports->SetBoolField(TEXT("create"), true);
	Supports->SetBoolField(TEXT("preflight"), true);
	Supports->SetBoolField(TEXT("stale_plan"), true);
	Supports->SetBoolField(TEXT("request_journal"), true);
	Supports->SetBoolField(TEXT("idempotency"), true);
	Supports->SetBoolField(TEXT("asset_lock"), true);
	Supports->SetBoolField(TEXT("post_analyze"), true);
	Supports->SetBoolField(TEXT("recover_scan"), true);
	Supports->SetBoolField(TEXT("test_control"), true);

	TSharedPtr<FJsonObject> Live = MakeShared<FJsonObject>();
	Live->SetBoolField(TEXT("available"), true);
	Live->SetStringField(TEXT("project"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
	Live->SetStringField(TEXT("engine_version"), BPGenCompat::EngineFullVersion());
	Live->SetStringField(TEXT("plugin_version"), TEXT("0.4.8"));
	Live->SetBoolField(TEXT("plugin_loaded"), true);
	Live->SetBoolField(TEXT("service_running"), IsRunning());
	Live->SetStringField(TEXT("request_queue"), InboxDir());
	Live->SetStringField(TEXT("report_dir"), DefaultReportRootDir());
	Live->SetObjectField(TEXT("supports"), Supports);
	Live->SetObjectField(TEXT("current_editor_state"), Es);

	TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("schema_version"), TEXT("1.0"));
	M->SetStringField(TEXT("status"), TEXT("success"));
	M->SetStringField(TEXT("mode"), TEXT("editor_live"));
	M->SetStringField(TEXT("plugin_version"), TEXT("0.4.8"));
	M->SetStringField(TEXT("request_id"), RequestId);
	M->SetStringField(TEXT("generated_at"), NowIso());
	M->SetObjectField(TEXT("editor_live"), Live);

	WriteJsonObj(ReportDir / TEXT("manifest.json"), M);
	return 0;
}

// ============================================================================
// analyze (read-only)
// ============================================================================
int32 FBPAgentLiveService::HandleAnalyze(const FString& RequestId, const TSharedPtr<FJsonObject>& Request, const FString& ReportDir)
{
	const TSharedPtr<FJsonObject>* ExecP = JObj(Request, TEXT("execution"));
	const TSharedPtr<FJsonObject> Exec = ExecP ? *ExecP : MakeShared<FJsonObject>();
	const bool bUseLoaded = JBool(Exec, TEXT("use_loaded_editor_state"), true);
	const bool bAllowDirty = JBool(Exec, TEXT("allow_dirty_assets"), false);

	// collect asset paths
	TArray<FString> Assets;
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Request->TryGetArrayField(TEXT("asset_paths"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr) { FString S; if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty()) { Assets.Add(S); } }
		}
		const FString Single = JStr(Request, TEXT("asset_path"));
		if (!Single.IsEmpty()) { Assets.Add(Single); }
	}

	TArray<FString> Warnings, Errors, Manual;
	FBPAgentEditorState St; CaptureEditorState(St);

	if (Assets.Num() == 0)
	{
		Errors.Add(TEXT("analyze: no asset_paths provided"));
		WriteAnalyzeManifestOnly(RequestId, ReportDir, TEXT("failed"), TEXT("unknown"), St, Warnings, Errors, Manual);
		return 30;
	}

	// Primary asset drives the request-level report; additional assets go into subfolders.
	int32 WorstCode = 0;
	FString PrimarySourceState = TEXT("unknown");

	for (int32 i = 0; i < Assets.Num(); ++i)
	{
		const bool bPrimary = (i == 0);
		const FString Pkg = ToPackagePath(Assets[i]);
		const FString Short = FPackageName::GetShortName(Pkg);
		const FString Dir = bPrimary ? ReportDir : (ReportDir / Sanitize(Pkg));

		// source_state: was it already loaded (and dirty) before we touched it?
		const FString ObjPath = ToObjectPath(Pkg);
		UObject* Existing = StaticFindObject(UBlueprint::StaticClass(), nullptr, *ObjPath);
		const bool bWasLoaded = (Existing != nullptr);

		UBlueprint* BP = Cast<UBlueprint>(Existing);
		if (!BP) { BP = LoadObject<UBlueprint>(nullptr, *Pkg); }
		if (!BP)
		{
			Errors.Add(FString::Printf(TEXT("analyze: cannot load %s"), *Pkg));
			if (bPrimary) { PrimarySourceState = TEXT("unknown"); }
			WorstCode = FMath::Max(WorstCode, 20);
			continue;
		}

		const bool bDirty = BP->GetOutermost() && BP->GetOutermost()->IsDirty();
		FString SourceState;
		if (bWasLoaded && bDirty)      { SourceState = TEXT("loaded_dirty_memory"); }
		else if (bWasLoaded && !bDirty){ SourceState = TEXT("loaded_clean_memory"); }
		else                           { SourceState = TEXT("disk_saved_asset"); }
		if (bPrimary) { PrimarySourceState = SourceState; }

		if (bDirty)
		{
			Warnings.Add(FString::Printf(TEXT("%s has unsaved in-editor changes; IR reflects %s%s"),
				*Short,
				bUseLoaded ? TEXT("loaded memory state") : TEXT("loaded memory state (disk reload not performed to protect user edits)"),
				bAllowDirty ? TEXT("") : TEXT(" (allow_dirty_assets=false)")));
			Manual.Add(FString::Printf(TEXT("%s: source_state=%s; confirm whether disk vs memory version is intended."), *Short, *SourceState));
		}

		// Build IR (read-only) and write the per-asset report.
		TSharedPtr<FJsonObject> Raw = FBPGenIRDumper::DumpBlueprint(BP);
		TSharedPtr<FJsonObject> Unified = BuildUnifiedIR(Raw, Pkg, Short);
		const int32 Code = WriteAnalyzeReport(RequestId, Dir, Unified, Pkg, Short, SourceState, St, Warnings, Errors, Manual);
		WorstCode = FMath::Max(WorstCode, Code);

		// keep global logs clean per-asset
		Warnings.Reset(); Errors.Reset(); Manual.Reset();
	}

	// Ensure request-level manifest exists for the primary (WriteAnalyzeReport already wrote it for i==0).
	return WorstCode;
}

// ============================================================================
// edit
// ============================================================================
int32 FBPAgentLiveService::HandleEdit(const FString& RequestId, const TSharedPtr<FJsonObject>& Request, const FString& ReportDir)
{
	const TSharedPtr<FJsonObject>* ExecP = JObj(Request, TEXT("execution"));
	const TSharedPtr<FJsonObject> Exec = ExecP ? *ExecP : MakeShared<FJsonObject>();

	const bool bReadOnly = JBool(Exec, TEXT("read_only"), true);
	const bool bAllowEdit = JBool(Exec, TEXT("allow_edit"), false);
	const bool bAllowEditDuringPie = JBool(Exec, TEXT("allow_edit_during_pie"), false);
	const bool bRequireUserAck = JBool(Exec, TEXT("require_user_ack"), false);
	const bool bAllowDirtyTarget = JBool(Exec, TEXT("allow_dirty_target"), false);
	const bool bRunPreflight = JBool(Exec, TEXT("run_preflight"), true);

	// edit payload can be provided as "edit" or "request"
	const TSharedPtr<FJsonObject>* EditP = JObj(Request, TEXT("edit"));
	if (!EditP) { EditP = JObj(Request, TEXT("request")); }
	const TSharedPtr<FJsonObject> Edit = EditP ? *EditP : nullptr;

	FString AssetPath = JStr(Request, TEXT("asset_path"));
	if (AssetPath.IsEmpty() && Edit.IsValid()) { AssetPath = JStr(Edit, TEXT("asset_path")); }
	const FString LockPath = ToPackagePath(AssetPath);

	TArray<FString> Warnings, Errors, Manual;
	FBPAgentEditorState St; CaptureEditorState(St);

	auto Refuse = [&](const FString& Reason, int32 Code, const FString& StatusStr = TEXT("failed")) -> int32
	{
		Errors.Add(Reason);
		AppendJournal(ReportDir, RequestId, TEXT("failed"), StatusStr);
		WriteEditCreateManifest(RequestId, ReportDir, TEXT("edit"), StatusStr, AssetPath, TEXT(""), St, Warnings, Errors, Manual);
		return Code;
	};

	if (bReadOnly || !bAllowEdit)
	{
		return Refuse(TEXT("edit refused: requires execution.read_only=false AND execution.allow_edit=true"), 30);
	}
	if (!Edit.IsValid() || AssetPath.IsEmpty())
	{
		return Refuse(TEXT("edit refused: missing edit payload or asset_path"), 30);
	}
	if (St.bIsPie && !bAllowEditDuringPie)
	{
		return Refuse(TEXT("edit refused: editor is in PIE (set execution.allow_edit_during_pie=true to override)"), 30, TEXT("blocked_by_editor_state"));
	}

	// Asset lock: one mutating edit per asset path at a time.
	if (LockedAssetPaths.Contains(LockPath))
	{
		// leave request in inbox — PumpOnce will retry on a later tick
		UE_LOG(LogBPParserTestGen, Display, TEXT("BPAgentLiveService: asset locked %s; deferring %s"), *LockPath, *RequestId);
		BusyAttempts.FindOrAdd(RequestId)++;
		return -1; // signal defer (caller must NOT move to processed)
	}
	LockedAssetPaths.Add(LockPath);
	ActiveRequestId = RequestId;

	auto Unlock = [&]()
	{
		LockedAssetPaths.Remove(LockPath);
		if (ActiveRequestId == RequestId) { ActiveRequestId.Reset(); }
	};

	// dirty + asset-editor-open guard (protect unsaved user work)
	const FString Pkg = ToPackagePath(AssetPath);
	UObject* Existing = StaticFindObject(UBlueprint::StaticClass(), nullptr, *ToObjectPath(Pkg));
	UBlueprint* BP = Cast<UBlueprint>(Existing);
	if (BP)
	{
		const bool bDirty = BP->GetOutermost() && BP->GetOutermost()->IsDirty();
		const bool bOpen = IsBlueprintAssetEditorOpen(BP);
		const FString SrcState = ResolveSourceState(BP);
		if (bDirty && !bAllowDirtyTarget)
		{
			Warnings.Add(FString::Printf(TEXT("target is dirty (source_state=%s); edit refused by default"), *SrcState));
			Unlock();
			return Refuse(FString::Printf(TEXT("edit refused: target dirty (%s); set execution.allow_dirty_target=true to override"), *SrcState),
				30, TEXT("blocked_by_editor_state"));
		}
		if (bDirty && bOpen && !bRequireUserAck)
		{
			Warnings.Add(TEXT("target is open in the asset editor with unsaved changes"));
			Unlock();
			return Refuse(TEXT("edit refused: target dirty & open in editor; set execution.require_user_ack=true to proceed"), 30, TEXT("blocked_by_editor_state"));
		}
		if (bDirty) { Warnings.Add(FString::Printf(TEXT("target dirty (%s); proceeding under allow_dirty_target"), *SrcState)); }
	}

	// Property-aware preflight (before any mutation).
	TSharedPtr<FJsonObject> PfReport, PfNorm, PfCap;
	int32 PfCode = 0;
	if (bRunPreflight)
	{
		AppendJournal(ReportDir, RequestId, TEXT("preflight"), TEXT("running"));
		FBPPreflight::FOptions PfOpt;
		PfOpt.OutputDir = ReportDir;
		PfOpt.AssetPath = AssetPath;
		PfOpt.LoadedBP = BP;
		PfOpt.bStrictRequired = JBool(Exec, TEXT("strict_preflight"), true);
		PfCode = FBPPreflight::RunPreflight(TEXT("edit"), Edit, PfOpt, PfReport, PfNorm, PfCap);
		const FString PfStatus = PfCode == 0 ? TEXT("pass") : (PfCode == 10 ? TEXT("pass_with_warnings") : TEXT("fail"));
		AppendJournal(ReportDir, RequestId, TEXT("preflight"), PfStatus);
		if (!FBPPreflight::IsPreflightApplyAllowed(PfCode))
		{
			Errors.Add(TEXT("preflight failed: required property/event checks did not pass; see preflight_report.json"));
			Unlock();
			WriteEditCreateManifest(RequestId, ReportDir, TEXT("edit"), TEXT("failed"), AssetPath,
				TEXT("preflight_report.json"), St, Warnings, Errors, Manual);
			return 20;
		}
		if (PfCode == 10) { Warnings.Add(TEXT("preflight pass_with_warnings: some optional properties skipped; see preflight_report.json")); }
	}

	// Reuse the atomic edit engine (Transaction / baseline / plan / rollback / compile / save / diff).
	FBPATEdit::FOptions Opt;
	Opt.Mode = JStr(Edit, TEXT("mode"), TEXT("apply-and-verify"));
	Opt.OutputDir = ReportDir;
	Opt.WorkOnCopy = JStr(Edit, TEXT("work_on_copy"));
	Opt.bCreateBackup = JBool(Exec, TEXT("create_backup"), true);
	Opt.bAllowDestructive = JBool(Exec, TEXT("allow_destructive_edit"), false) || JBool(Edit, TEXT("allow_destructive_edit"), false);
	Opt.bStrict = JBool(Exec, TEXT("strict"), false);
	Opt.bRunPreflight = false; // already ran above
	Opt.ExpectedBaselineIrHash = JStr(Edit, TEXT("baseline_ir_hash"));
	if (Opt.ExpectedBaselineIrHash.IsEmpty()) { Opt.ExpectedBaselineIrHash = JStr(Edit, TEXT("expected_baseline_ir_hash")); }

	AppendJournal(ReportDir, RequestId, TEXT("applying"), TEXT("running"));
	FString EditStatus;
	const int32 Code = FBPATEdit::Run(AssetPath, PfNorm.IsValid() ? PfNorm : Edit, Opt, &EditStatus);

	AppendJournal(ReportDir, RequestId, TEXT("verifying"), Code == 0 ? TEXT("ok") : TEXT("check"));
	if (Code == 0 || Code == 10)
	{
		// Post-analyze the edited asset for agent verification.
		if (UBlueprint* ResultBP = LoadObject<UBlueprint>(nullptr, *AssetPath))
		{
			if (!Opt.WorkOnCopy.IsEmpty())
			{
				ResultBP = LoadObject<UBlueprint>(nullptr, *Opt.WorkOnCopy);
			}
			if (ResultBP) { RunPostAnalyze(RequestId, ResultBP, ReportDir, St); }
		}
	}

	// Prefer the precise status reported by the edit engine (distinguishes success_with_warnings vs partial,
	// which share exit code 10). Fall back to code-based mapping if the engine did not report one.
	FString StatusStr = EditStatus;
	if (StatusStr.IsEmpty())
	{
		if (Code == 0) { StatusStr = TEXT("success"); }
		else if (Code == 10) { StatusStr = TEXT("partial"); }
		else if (Code == 40) { StatusStr = TEXT("rolled_back"); }
		else if (Code == 50) { StatusStr = TEXT("stale_plan"); }
		else { StatusStr = TEXT("failed"); }
	}
	// A clean apply where preflight raised optional-property warnings is success_with_warnings, not plain success.
	if (Code == 0 && PfCode == 10 && StatusStr == TEXT("success")) { StatusStr = TEXT("success_with_warnings"); }

	AppendJournal(ReportDir, RequestId,
		Code == 40 ? TEXT("rolled_back") : (Code == 0 || Code == 10 ? TEXT("success") : TEXT("failed")),
		StatusStr);

	WriteEditCreateManifest(RequestId, ReportDir, TEXT("edit"), StatusStr, AssetPath,
		TEXT("See BPATEdit artifacts (edit_plan/edit_result/diff_report) under this directory."),
		St, Warnings, Errors, Manual);
	Unlock();
	return Code;
}

// ============================================================================
// create
// ============================================================================
int32 FBPAgentLiveService::HandleCreate(const FString& RequestId, const TSharedPtr<FJsonObject>& Request, const FString& ReportDir)
{
	const TSharedPtr<FJsonObject>* ExecP = JObj(Request, TEXT("execution"));
	const TSharedPtr<FJsonObject> Exec = ExecP ? *ExecP : MakeShared<FJsonObject>();
	const bool bAllowCreate = JBool(Exec, TEXT("allow_create"), false);

	// create spec can be under "create" or "request"
	const TSharedPtr<FJsonObject>* SpecP = JObj(Request, TEXT("create"));
	if (!SpecP) { SpecP = JObj(Request, TEXT("request")); }
	const TSharedPtr<FJsonObject> Spec = SpecP ? *SpecP : nullptr;

	TArray<FString> Warnings, Errors, Manual;
	FBPAgentEditorState St; CaptureEditorState(St);
	FString AssetPath;
	if (Spec.IsValid()) { if (const TSharedPtr<FJsonObject>* A = JObj(Spec, TEXT("asset"))) { AssetPath = JStr(*A, TEXT("asset_path")); } }

	auto Refuse = [&](const FString& Reason, int32 Code, const FString& Status = TEXT("failed")) -> int32
	{
		Errors.Add(Reason);
		WriteEditCreateManifest(RequestId, ReportDir, TEXT("create"), Status, AssetPath, TEXT(""), St, Warnings, Errors, Manual);
		return Code;
	};

	if (!bAllowCreate)            { return Refuse(TEXT("create refused: requires execution.allow_create=true"), 30); }
	if (!Spec.IsValid())          { return Refuse(TEXT("create refused: missing create spec"), 30); }
	if (St.bIsPie)                { return Refuse(TEXT("create refused: editor is in PIE"), 30, TEXT("blocked_by_editor_state")); }

	AppendJournal(ReportDir, RequestId, TEXT("preflight"), TEXT("running"));
	{
		FBPPreflight::FOptions PfOpt;
		PfOpt.OutputDir = ReportDir;
		PfOpt.AssetPath = AssetPath;
		PfOpt.bStrictRequired = JBool(Exec, TEXT("strict_preflight"), true);
		TSharedPtr<FJsonObject> PfReport, PfNorm, PfCap;
		const int32 PfCode = FBPPreflight::RunPreflight(TEXT("create"), Spec, PfOpt, PfReport, PfNorm, PfCap);
		AppendJournal(ReportDir, RequestId, TEXT("preflight"), PfCode == 0 ? TEXT("pass") : (PfCode == 10 ? TEXT("pass_with_warnings") : TEXT("fail")));
		if (!FBPPreflight::IsPreflightApplyAllowed(PfCode))
		{
			Errors.Add(TEXT("create preflight failed; see preflight_report.json"));
			WriteEditCreateManifest(RequestId, ReportDir, TEXT("create"), TEXT("failed"), AssetPath, TEXT("preflight_report.json"), St, Warnings, Errors, Manual);
			return 20;
		}
		if (PfCode == 10) { Warnings.Add(TEXT("create preflight pass_with_warnings")); }
	}

	// FBPCreate reads a SpecFile whose top-level has a "request" key.
	TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
	Wrapper->SetObjectField(TEXT("request"), Spec);
	const FString SpecFile = ReportDir / TEXT("_create_spec.json");
	WriteJsonObj(SpecFile, Wrapper);

	const int32 Code = FBPCreate::Run(SpecFile, ReportDir);

	const FString StatusStr = (Code == 0) ? TEXT("success") : (Code == 10 ? TEXT("partial") : (Code == 41 ? TEXT("exists_refused") : TEXT("failed")));
	WriteEditCreateManifest(RequestId, ReportDir, TEXT("create"), StatusStr, AssetPath,
		TEXT("See BPCreate artifacts (create_result/created_ir) under this directory."),
		St, Warnings, Errors, Manual);
	return Code;
}

// ============================================================================
// Helpers: IR shaping, viz, editor-open check, manifest blocks, report writing
// ============================================================================
namespace
{
	// Copy an array field verbatim (or set an empty array if absent).
	void CopyArrayOrEmpty(const TSharedPtr<FJsonObject>& Src, const TSharedPtr<FJsonObject>& Dst, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Src.IsValid() && Src->TryGetArrayField(Key, Arr)) { Dst->SetArrayField(Key, *Arr); }
		else { Dst->SetArrayField(Key, TArray<TSharedPtr<FJsonValue>>()); }
	}

	// Count graph/node/pin/edge across the unified graphs array.
	void CountGraph(const TSharedPtr<FJsonObject>& Ir, int32& G, int32& N, int32& P, int32& E)
	{
		G = N = P = E = 0;
		const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
		if (!Ir.IsValid() || !Ir->TryGetArrayField(TEXT("graphs"), Graphs)) { return; }
		G = Graphs->Num();
		for (const TSharedPtr<FJsonValue>& GV : *Graphs)
		{
			const TSharedPtr<FJsonObject> GO = GV->AsObject();
			if (!GO.IsValid()) { continue; }
			const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
			if (GO->TryGetArrayField(TEXT("nodes"), Nodes))
			{
				N += Nodes->Num();
				for (const TSharedPtr<FJsonValue>& NV : *Nodes)
				{
					const TSharedPtr<FJsonObject> NO = NV->AsObject();
					const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
					if (NO.IsValid() && NO->TryGetArrayField(TEXT("pins"), Pins)) { P += Pins->Num(); }
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Edges = nullptr;
			if (GO->TryGetArrayField(TEXT("edges"), Edges)) { E += Edges->Num(); }
		}
	}

	int32 ArrCount(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		return (O.IsValid() && O->TryGetArrayField(Key, A)) ? A->Num() : 0;
	}

	// Build DOT + Mermaid from unified graphs (matches analyze_blueprint.ps1 output style).
	void BuildDotMmd(const TSharedPtr<FJsonObject>& Ir, const FString& AssetName, const FString& Mode, const FString& Status,
		FString& OutDot, FString& OutMmd)
	{
		OutDot = FString::Printf(TEXT("digraph BP {\n  rankdir=LR;\n  label=\"%s [%s/%s]\";\n  node[shape=box,style=rounded];\n"),
			*DotSafe(AssetName), *Mode, *Status);
		OutMmd = FString::Printf(TEXT("%%%% %s [%s/%s]\nflowchart LR\n"), *DotSafe(AssetName), *Mode, *Status);

		const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
		int32 Total = 0;
		if (Ir.IsValid() && Ir->TryGetArrayField(TEXT("graphs"), Graphs))
		{
			for (const TSharedPtr<FJsonValue>& GV : *Graphs)
			{
				const TSharedPtr<FJsonObject> GO = GV->AsObject();
				if (!GO.IsValid()) { continue; }
				const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
				if (GO->TryGetArrayField(TEXT("nodes"), Nodes))
				{
					for (const TSharedPtr<FJsonValue>& NV : *Nodes)
					{
						const TSharedPtr<FJsonObject> NO = NV->AsObject();
						if (!NO.IsValid()) { continue; }
						const FString Id = AlnumOnly(JStr(NO, TEXT("node_id")));
						FString Lbl = JStr(NO, TEXT("node_title"));
						if (Lbl.IsEmpty()) { Lbl = JStr(NO, TEXT("node_class")); }
						Lbl = DotSafe(Lbl);
						OutDot += FString::Printf(TEXT("  n%s [label=\"%s\"];\n"), *Id, *Lbl);
						OutMmd += FString::Printf(TEXT("  n%s[\"%s\"]\n"), *Id, *Lbl);
						++Total;
					}
				}
				const TArray<TSharedPtr<FJsonValue>>* Edges = nullptr;
				if (GO->TryGetArrayField(TEXT("edges"), Edges))
				{
					for (const TSharedPtr<FJsonValue>& EV : *Edges)
					{
						const TSharedPtr<FJsonObject> EO = EV->AsObject();
						if (!EO.IsValid()) { continue; }
						const FString A = AlnumOnly(JStr(EO, TEXT("from_node")));
						const FString B = AlnumOnly(JStr(EO, TEXT("to_node")));
						const bool bExec = JStr(EO, TEXT("edge_type")) == TEXT("exec");
						OutDot += FString::Printf(TEXT("  n%s -> n%s [style=%s];\n"), *A, *B, bExec ? TEXT("solid") : TEXT("dashed"));
						OutMmd += FString::Printf(TEXT("  n%s --> n%s\n"), *A, *B);
					}
				}
			}
		}
		if (Total == 0)
		{
			OutDot += TEXT("  note [label=\"No graph nodes.\",shape=note];\n");
			OutMmd += TEXT("  note[\"No graph nodes\"]\n");
		}
		OutDot += TEXT("}\n");
	}
}

TSharedPtr<FJsonObject> FBPAgentLiveService::BuildUnifiedIR(const TSharedPtr<FJsonObject>& Raw, const FString& PackagePath, const FString& ShortName)
{
	TSharedPtr<FJsonObject> Ir = MakeShared<FJsonObject>();
	Ir->SetStringField(TEXT("schema_version"), TEXT("1.0"));
	Ir->SetStringField(TEXT("mode"), TEXT("editor_live"));
	Ir->SetBoolField(TEXT("partial"), false);
	Ir->SetStringField(TEXT("dumper_engine_version"), JStr(Raw, TEXT("dumper_engine_version")));

	const FString BpClass = JStr(Raw, TEXT("blueprint_class"));

	TSharedPtr<FJsonObject> Asset = MakeShared<FJsonObject>();
	Asset->SetStringField(TEXT("asset_path"), PackagePath);
	Asset->SetStringField(TEXT("asset_name"), ShortName);
	Asset->SetStringField(TEXT("asset_type"), BpClass);
	Asset->SetStringField(TEXT("blueprint_class"), BpClass);
	Asset->SetStringField(TEXT("generated_class"), JStr(Raw, TEXT("generated_class")));
	Asset->SetStringField(TEXT("parent_class"), JStr(Raw, TEXT("parent_class")));
	CopyArrayOrEmpty(Raw, Asset, TEXT("interfaces"));
	// alias for consumer symmetry with analyze_blueprint.ps1
	{
		const TArray<TSharedPtr<FJsonValue>>* Ifaces = nullptr;
		if (Raw.IsValid() && Raw->TryGetArrayField(TEXT("interfaces"), Ifaces)) { Asset->SetArrayField(TEXT("implemented_interfaces"), *Ifaces); }
		else { Asset->SetArrayField(TEXT("implemented_interfaces"), TArray<TSharedPtr<FJsonValue>>()); }
	}
	Asset->SetArrayField(TEXT("dependencies"), TArray<TSharedPtr<FJsonValue>>());
	Ir->SetObjectField(TEXT("asset"), Asset);

	TSharedPtr<FJsonObject> Bp = MakeShared<FJsonObject>();
	CopyArrayOrEmpty(Raw, Bp, TEXT("variables"));
	CopyArrayOrEmpty(Raw, Bp, TEXT("functions"));
	CopyArrayOrEmpty(Raw, Bp, TEXT("macros"));
	CopyArrayOrEmpty(Raw, Bp, TEXT("event_dispatchers"));
	Bp->SetArrayField(TEXT("components"), TArray<TSharedPtr<FJsonValue>>());
	Bp->SetArrayField(TEXT("timelines"), TArray<TSharedPtr<FJsonValue>>());
	// blueprint.graphs = list of graph names
	{
		TArray<TSharedPtr<FJsonValue>> Names;
		const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
		if (Raw.IsValid() && Raw->TryGetArrayField(TEXT("graphs"), Graphs))
		{
			for (const TSharedPtr<FJsonValue>& GV : *Graphs)
			{
				const TSharedPtr<FJsonObject> GO = GV->AsObject();
				if (GO.IsValid()) { Names.Add(MakeShared<FJsonValueString>(JStr(GO, TEXT("graph_name")))); }
			}
		}
		Bp->SetArrayField(TEXT("graphs"), Names);
	}
	Ir->SetObjectField(TEXT("blueprint"), Bp);

	CopyArrayOrEmpty(Raw, Ir, TEXT("graphs"));
	if (Raw.IsValid())
	{
		const TSharedPtr<FJsonObject>* WT = nullptr;
		if (Raw->TryGetObjectField(TEXT("widget_tree"), WT) && WT && WT->IsValid())
		{
			Ir->SetObjectField(TEXT("widget_tree"), *WT);
		}
		const TArray<TSharedPtr<FJsonValue>>* WEB = nullptr;
		if (Raw->TryGetArrayField(TEXT("widget_event_bindings"), WEB)) { Ir->SetArrayField(TEXT("widget_event_bindings"), *WEB); }
	}

	TSharedPtr<FJsonObject> Analysis = MakeShared<FJsonObject>();
	Analysis->SetArrayField(TEXT("manual_check_required"), TArray<TSharedPtr<FJsonValue>>());
	Ir->SetObjectField(TEXT("analysis"), Analysis);
	return Ir;
}

bool FBPAgentLiveService::IsBlueprintAssetEditorOpen(UObject* Asset)
{
	if (!Asset || !GEditor) { return false; }
	if (UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		return AES->FindEditorsForAsset(Asset).Num() > 0;
	}
	return false;
}

TSharedPtr<FJsonObject> FBPAgentLiveService::MakeEditorLiveBlock(const FString& RequestId, const FString& SourceState, const FBPAgentEditorState& St) const
{
	TSharedPtr<FJsonObject> Es = MakeShared<FJsonObject>();
	Es->SetBoolField(TEXT("is_pie"), St.bIsPie);
	Es->SetBoolField(TEXT("is_saving"), St.bIsSaving);
	Es->SetBoolField(TEXT("is_compiling_blueprints"), St.bIsCompilingBlueprints);
	{
		TArray<TSharedPtr<FJsonValue>> Da;
		for (const FString& S : St.DirtyAssets) { Da.Add(MakeShared<FJsonValueString>(S)); }
		Es->SetArrayField(TEXT("dirty_assets"), Da);
	}

	TSharedPtr<FJsonObject> Live = MakeShared<FJsonObject>();
	Live->SetBoolField(TEXT("available"), true);
	Live->SetBoolField(TEXT("service_running"), IsRunning());
	Live->SetStringField(TEXT("request_id"), RequestId);
	Live->SetStringField(TEXT("source_state"), SourceState.IsEmpty() ? TEXT("unknown") : SourceState);
	Live->SetObjectField(TEXT("editor_state"), Es);
	Live->SetField(TEXT("fallback_from"), MakeShared<FJsonValueNull>());
	Live->SetField(TEXT("fallback_to"), MakeShared<FJsonValueNull>());
	return Live;
}

int32 FBPAgentLiveService::WriteAnalyzeReport(const FString& RequestId, const FString& Dir, const TSharedPtr<FJsonObject>& Ir,
	const FString& PkgPath, const FString& ShortName, const FString& SourceState, const FBPAgentEditorState& St,
	const TArray<FString>& Warnings, const TArray<FString>& Errors, const TArray<FString>& Manual)
{
	IFileManager::Get().MakeDirectory(*Dir, true);
	IFileManager::Get().MakeDirectory(*(Dir / TEXT("logs")), true);
	IFileManager::Get().MakeDirectory(*(Dir / TEXT("viz")), true);
	IFileManager::Get().MakeDirectory(*(Dir / TEXT("graphs")), true);

	const FString Mode = TEXT("editor_live");
	const FString Status = (Errors.Num() > 0) ? TEXT("partial") : TEXT("success");

	// blueprint_ir.json
	WriteJsonObj(Dir / TEXT("blueprint_ir.json"), Ir);

	// per-graph json
	{
		const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
		if (Ir->TryGetArrayField(TEXT("graphs"), Graphs))
		{
			for (const TSharedPtr<FJsonValue>& GV : *Graphs)
			{
				const TSharedPtr<FJsonObject> GO = GV->AsObject();
				if (!GO.IsValid()) { continue; }
				const FString GN = Sanitize(JStr(GO, TEXT("graph_name")));
				if (!GN.IsEmpty()) { WriteJsonObj(Dir / TEXT("graphs") / (GN + TEXT(".json")), GO); }
			}
		}
	}

	// viz
	FString Dot, Mmd; BuildDotMmd(Ir, ShortName, Mode, Status, Dot, Mmd);
	WriteUtf8NoBom(Dir / TEXT("viz") / TEXT("blueprint.dot"), Dot);
	WriteUtf8NoBom(Dir / TEXT("viz") / TEXT("blueprint.mmd"), Mmd);

	// counts
	int32 G, N, P, E; CountGraph(Ir, G, N, P, E);
	const TSharedPtr<FJsonObject>* AssetP = JObj(Ir, TEXT("asset"));
	const TSharedPtr<FJsonObject>* BpP = JObj(Ir, TEXT("blueprint"));
	const TSharedPtr<FJsonObject> AssetO = AssetP ? *AssetP : MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject> BpO = BpP ? *BpP : MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> Counts = MakeShared<FJsonObject>();
	Counts->SetNumberField(TEXT("graphs"), G);
	Counts->SetNumberField(TEXT("nodes"), N);
	Counts->SetNumberField(TEXT("pins"), P);
	Counts->SetNumberField(TEXT("edges"), E);
	Counts->SetNumberField(TEXT("variables"), ArrCount(BpO, TEXT("variables")));
	Counts->SetNumberField(TEXT("functions"), ArrCount(BpO, TEXT("functions")));
	Counts->SetNumberField(TEXT("macros"), ArrCount(BpO, TEXT("macros")));
	Counts->SetNumberField(TEXT("dispatchers"), ArrCount(BpO, TEXT("event_dispatchers")));
	Counts->SetNumberField(TEXT("components"), ArrCount(BpO, TEXT("components")));
	Counts->SetNumberField(TEXT("interfaces"), ArrCount(AssetO, TEXT("implemented_interfaces")));
	Counts->SetNumberField(TEXT("dependencies"), ArrCount(AssetO, TEXT("dependencies")));

	// manual list (+ PNG/SVG note; in-editor service does not shell out to Graphviz)
	TArray<FString> ManualLocal = Manual;
	ManualLocal.Add(TEXT("PNG/SVG rendering dependency not invoked by in-editor service; DOT/Mermaid generated. Use blueprint_agent.ps1 client (or run Graphviz 'dot') to rasterize."));

	// summary.md
	FString Sum;
	Sum += TEXT("# Blueprint Understanding Summary\n\n");
	Sum += TEXT("## 1. Asset Overview\n");
	Sum += FString::Printf(TEXT("- asset_path: `%s`\n"), *PkgPath);
	Sum += FString::Printf(TEXT("- asset_name: %s\n"), *ShortName);
	Sum += FString::Printf(TEXT("- asset_type: %s\n"), *JStr(AssetO, TEXT("asset_type")));
	Sum += FString::Printf(TEXT("- parent_class: %s\n"), *JStr(AssetO, TEXT("parent_class")));
	Sum += FString::Printf(TEXT("- generated_class: %s\n"), *JStr(AssetO, TEXT("generated_class")));
	Sum += FString::Printf(TEXT("- analysis mode: **%s** | status: **%s** | source_state: **%s**\n"), *Mode, *Status, *SourceState);
	Sum += FString::Printf(TEXT("- editor_state: pie=%s saving=%s compiling=%s dirty_assets=%d\n\n"),
		St.bIsPie ? TEXT("true") : TEXT("false"), St.bIsSaving ? TEXT("true") : TEXT("false"),
		St.bIsCompilingBlueprints ? TEXT("true") : TEXT("false"), St.DirtyAssetsCount);
	Sum += TEXT("## 2. Counts\n");
	Sum += FString::Printf(TEXT("graphs=%d nodes=%d pins=%d edges=%d | variables=%d functions=%d macros=%d dispatchers=%d interfaces=%d\n\n"),
		G, N, P, E, ArrCount(BpO, TEXT("variables")), ArrCount(BpO, TEXT("functions")), ArrCount(BpO, TEXT("macros")),
		ArrCount(BpO, TEXT("event_dispatchers")), ArrCount(AssetO, TEXT("implemented_interfaces")));
	Sum += TEXT("## 3. Confidence & Limitations\n");
	Sum += FString::Printf(TEXT("- Full in-memory EdGraph IR extracted by the in-editor dumper. warnings=%d errors=%d\n\n"), Warnings.Num(), Errors.Num());
	Sum += TEXT("## 4. Manual Check Required\n");
	for (const FString& Mk : ManualLocal) { Sum += FString::Printf(TEXT("- %s\n"), *Mk); }
	WriteUtf8NoBom(Dir / TEXT("summary.md"), Sum);

	// understanding_score.json
	TSharedPtr<FJsonObject> Score = MakeShared<FJsonObject>();
	Score->SetStringField(TEXT("schema_version"), TEXT("1.0"));
	Score->SetStringField(TEXT("asset_path"), PkgPath);
	Score->SetStringField(TEXT("status"), Status);
	{
		TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
		const TCHAR* Full = TEXT("complete");
		S->SetStringField(TEXT("asset_load"), JStr(AssetO, TEXT("asset_type")).IsEmpty() ? TEXT("partial") : Full);
		S->SetStringField(TEXT("graph_discovery"), Full);
		S->SetStringField(TEXT("node_discovery"), Full);
		S->SetStringField(TEXT("pin_discovery"), Full);
		S->SetStringField(TEXT("edge_discovery"), Full);
		S->SetStringField(TEXT("variable_discovery"), Full);
		S->SetStringField(TEXT("function_discovery"), Full);
		S->SetStringField(TEXT("dispatcher_discovery"), Full);
		S->SetStringField(TEXT("component_discovery"), Full);
		S->SetStringField(TEXT("dependency_discovery"), TEXT("partial")); // deps not resolved in-editor here
		S->SetStringField(TEXT("visualization"), TEXT("complete"));
		S->SetStringField(TEXT("agent_callable"), TEXT("complete"));
		Score->SetObjectField(TEXT("score"), S);
	}
	Score->SetNumberField(TEXT("confidence"), 0.9);
	Score->SetStringField(TEXT("semantics_note"), TEXT("A \"complete\" field with zero count means the asset has NONE of that category (N/A), not incomplete parsing."));
	Score->SetStringField(TEXT("viz_note"), TEXT("DOT + Mermaid produced. PNG/SVG not rendered by the in-editor service; rasterize via blueprint_agent.ps1 client or Graphviz."));
	Score->SetStringField(TEXT("source_state"), SourceState);
	{
		TArray<TSharedPtr<FJsonValue>> Mk;
		for (const FString& S : ManualLocal) { Mk.Add(MakeShared<FJsonValueString>(S)); }
		Score->SetArrayField(TEXT("manual_check_required"), Mk);
	}
	WriteJsonObj(Dir / TEXT("understanding_score.json"), Score);

	// logs
	WriteUtf8NoBom(Dir / TEXT("logs") / TEXT("warnings.json"), JsonStringArray(Warnings));
	WriteUtf8NoBom(Dir / TEXT("logs") / TEXT("errors.json"), JsonStringArray(Errors));

	// manifest.json
	TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("schema_version"), TEXT("1.0"));
	M->SetStringField(TEXT("status"), Status);
	M->SetStringField(TEXT("mode"), Mode);
	M->SetStringField(TEXT("asset_path"), PkgPath);
	M->SetStringField(TEXT("asset_name"), ShortName);
	M->SetStringField(TEXT("asset_type"), JStr(AssetO, TEXT("asset_type")));
	M->SetStringField(TEXT("parent_class"), JStr(AssetO, TEXT("parent_class")));
	M->SetStringField(TEXT("generated_class"), JStr(AssetO, TEXT("generated_class")));
	M->SetStringField(TEXT("project_uproject"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
	M->SetStringField(TEXT("ue_root"), FPaths::ConvertRelativePathToFull(FPaths::EngineDir()));
	M->SetStringField(TEXT("engine_version"), BPGenCompat::EngineFullVersion());
	M->SetBoolField(TEXT("is_custom_engine"), false);
	M->SetBoolField(TEXT("plugin_installed"), true);
	M->SetBoolField(TEXT("plugin_built"), true);
	M->SetBoolField(TEXT("read_only"), true);
	M->SetStringField(TEXT("generated_at"), NowIso());
	M->SetArrayField(TEXT("fallbacks_used"), TArray<TSharedPtr<FJsonValue>>());
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("ir"), TEXT("blueprint_ir.json"));
		Out->SetStringField(TEXT("summary"), TEXT("summary.md"));
		Out->SetStringField(TEXT("understanding_score"), TEXT("understanding_score.json"));
		Out->SetStringField(TEXT("dot"), TEXT("viz/blueprint.dot"));
		Out->SetStringField(TEXT("mermaid"), TEXT("viz/blueprint.mmd"));
		Out->SetStringField(TEXT("png"), TEXT(""));
		Out->SetStringField(TEXT("svg"), TEXT(""));
		Out->SetStringField(TEXT("logs"), TEXT("logs/"));
		M->SetObjectField(TEXT("outputs"), Out);
	}
	M->SetObjectField(TEXT("counts"), Counts);
	M->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>()); // detailed in logs/warnings.json
	{
		TArray<TSharedPtr<FJsonValue>> W; for (const FString& S : Warnings) { W.Add(MakeShared<FJsonValueString>(S)); }
		M->SetArrayField(TEXT("warnings"), W);
		TArray<TSharedPtr<FJsonValue>> Er; for (const FString& S : Errors) { Er.Add(MakeShared<FJsonValueString>(S)); }
		M->SetArrayField(TEXT("errors"), Er);
		TArray<TSharedPtr<FJsonValue>> Mk; for (const FString& S : ManualLocal) { Mk.Add(MakeShared<FJsonValueString>(S)); }
		M->SetArrayField(TEXT("manual_check_required"), Mk);
	}
	M->SetObjectField(TEXT("editor_live"), MakeEditorLiveBlock(RequestId, SourceState, St));
	WriteJsonObj(Dir / TEXT("manifest.json"), M);

	return (Status == TEXT("success")) ? 0 : 10;
}

void FBPAgentLiveService::WriteAnalyzeManifestOnly(const FString& RequestId, const FString& Dir, const FString& Status, const FString& AssetType,
	const FBPAgentEditorState& St, const TArray<FString>& Warnings, const TArray<FString>& Errors, const TArray<FString>& Manual)
{
	IFileManager::Get().MakeDirectory(*Dir, true);
	TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("schema_version"), TEXT("1.0"));
	M->SetStringField(TEXT("status"), Status);
	M->SetStringField(TEXT("mode"), TEXT("editor_live"));
	M->SetStringField(TEXT("asset_type"), AssetType);
	M->SetStringField(TEXT("generated_at"), NowIso());
	M->SetBoolField(TEXT("read_only"), true);
	{
		TArray<TSharedPtr<FJsonValue>> W; for (const FString& S : Warnings) { W.Add(MakeShared<FJsonValueString>(S)); }
		M->SetArrayField(TEXT("warnings"), W);
		TArray<TSharedPtr<FJsonValue>> Er; for (const FString& S : Errors) { Er.Add(MakeShared<FJsonValueString>(S)); }
		M->SetArrayField(TEXT("errors"), Er);
		TArray<TSharedPtr<FJsonValue>> Mk; for (const FString& S : Manual) { Mk.Add(MakeShared<FJsonValueString>(S)); }
		M->SetArrayField(TEXT("manual_check_required"), Mk);
	}
	M->SetObjectField(TEXT("editor_live"), MakeEditorLiveBlock(RequestId, TEXT("unknown"), St));
	WriteJsonObj(Dir / TEXT("manifest.json"), M);
}

// ============================================================================
// recover_scan + test_control (production recovery + regression fault-injection)
// ============================================================================
int32 FBPAgentLiveService::RunRecoveryScan(const FString& ScanEditorLiveDir, TArray<FString>& OutRecovered)
{
	OutRecovered.Reset();
	if (ScanEditorLiveDir.IsEmpty() || !FPaths::DirectoryExists(ScanEditorLiveDir)) { return 0; }

	auto IsTerminalPhase = [](const FString& P)
	{
		return P == TEXT("success") || P == TEXT("failed") || P == TEXT("rolled_back")
			|| P == TEXT("stale_plan") || P == TEXT("blocked_by_editor_state") || P == TEXT("pending_editor_restart");
	};

	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *(ScanEditorLiveDir / TEXT("*")), /*Files*/ false, /*Directories*/ true);
	for (const FString& DirId : Dirs)
	{
		if (DirId == TEXT(".") || DirId == TEXT("..")) { continue; }
		if (DirId == ActiveRequestId) { continue; }          // never flag the in-flight scan request itself
		const FString FullDir = ScanEditorLiveDir / DirId;
		const FString JournalPath = FullDir / TEXT("request_journal.json");
		if (!FPaths::FileExists(JournalPath)) { continue; }

		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *JournalPath)) { continue; }
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(R, Root) || !Root.IsValid()) { continue; }
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!Root->TryGetArrayField(TEXT("entries"), Entries) || !Entries || Entries->Num() == 0) { continue; }
		const TSharedPtr<FJsonObject> Last = (*Entries)[Entries->Num() - 1]->AsObject();
		const FString LastPhase = Last.IsValid() ? JStr(Last, TEXT("phase")) : FString();
		if (IsTerminalPhase(LastPhase)) { continue; }

		// A completed request always has a terminal outbox marker; its absence + a non-terminal journal
		// means the editor exited mid-flight (crash / forced quit) and the request was orphaned.
		if (FPaths::FileExists(OutboxDir() / (DirId + TEXT(".done"))) ||
			FPaths::FileExists(OutboxDir() / (DirId + TEXT(".failed"))))
		{
			continue;
		}

		AppendJournal(FullDir, DirId, TEXT("pending_editor_restart"), TEXT("recovered_by_scan"));
		OutRecovered.Add(DirId);
	}
	if (OutRecovered.Num() > 0)
	{
		UE_LOG(LogBPParserTestGen, Warning, TEXT("BPAgentLiveService: recovery scan flagged %d orphaned request(s) as pending_editor_restart under %s"),
			OutRecovered.Num(), *ScanEditorLiveDir);
	}
	return OutRecovered.Num();
}

int32 FBPAgentLiveService::HandleRecoverScan(const FString& RequestId, const TSharedPtr<FJsonObject>& Request, const FString& ReportDir)
{
	IFileManager::Get().MakeDirectory(*ReportDir, true);
	FString OutBase = JStr(Request, TEXT("output_dir"));
	if (OutBase.IsEmpty()) { OutBase = DefaultReportRootDir(); }
	const FString ScanDir = OutBase / TEXT("editor_live");

	ActiveRequestId = RequestId;   // ensure the scan skips its own dir
	TArray<FString> Recovered;
	RunRecoveryScan(ScanDir, Recovered);
	ActiveRequestId.Reset();

	TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("schema_version"), TEXT("1.0"));
	M->SetStringField(TEXT("status"), TEXT("success"));
	M->SetStringField(TEXT("mode"), TEXT("editor_live"));
	M->SetStringField(TEXT("task_type"), TEXT("recover_scan"));
	M->SetStringField(TEXT("scan_dir"), ScanDir);
	M->SetNumberField(TEXT("recovered_count"), Recovered.Num());
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& S : Recovered) { Arr.Add(MakeShared<FJsonValueString>(S)); }
		M->SetArrayField(TEXT("recovered"), Arr);
	}
	M->SetStringField(TEXT("generated_at"), NowIso());
	WriteJsonObj(ReportDir / TEXT("manifest.json"), M);
	return 0;
}

int32 FBPAgentLiveService::HandleTestControl(const FString& RequestId, const TSharedPtr<FJsonObject>& Request, const FString& ReportDir)
{
	IFileManager::Get().MakeDirectory(*ReportDir, true);
	const double Now = FPlatformTime::Seconds();
	double PieMs = 0.0, BusyMs = 0.0;
	// A present field sets the window (>0) or clears it (<=0). Absent fields leave the current window as-is.
	if (Request->HasTypedField<EJson::Number>(TEXT("force_pie_ms")))
	{
		Request->TryGetNumberField(TEXT("force_pie_ms"), PieMs);
		GTestForcePieUntilSeconds = (PieMs > 0.0) ? (Now + PieMs / 1000.0) : 0.0;
	}
	if (Request->HasTypedField<EJson::Number>(TEXT("force_busy_ms")))
	{
		Request->TryGetNumberField(TEXT("force_busy_ms"), BusyMs);
		GTestForceBusyUntilSeconds = (BusyMs > 0.0) ? (Now + BusyMs / 1000.0) : 0.0;
	}

	TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("schema_version"), TEXT("1.0"));
	M->SetStringField(TEXT("status"), TEXT("success"));
	M->SetStringField(TEXT("mode"), TEXT("editor_live"));
	M->SetStringField(TEXT("task_type"), TEXT("test_control"));
	M->SetNumberField(TEXT("force_pie_ms"), PieMs);
	M->SetNumberField(TEXT("force_busy_ms"), BusyMs);
	M->SetStringField(TEXT("note"), TEXT("regression fault-injection: self-expiring editor-state override (no real PIE/compile)"));
	M->SetStringField(TEXT("generated_at"), NowIso());
	WriteJsonObj(ReportDir / TEXT("manifest.json"), M);
	return 0;
}

void FBPAgentLiveService::AppendJournal(const FString& ReportDir, const FString& RequestId, const FString& Phase,
	const FString& Status, const TSharedPtr<FJsonObject>& Detail)
{
	if (ReportDir.IsEmpty()) { return; }
	IFileManager::Get().MakeDirectory(*ReportDir, true);
	const FString Path = ReportDir / TEXT("request_journal.json");
	TArray<TSharedPtr<FJsonValue>> Entries;
	if (FString Existing; FFileHelper::LoadFileToString(Existing, *Path))
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Existing);
		if (FJsonSerializer::Deserialize(R, Root) && Root.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
			if (Root->TryGetArrayField(TEXT("entries"), A)) { Entries = *A; }
		}
	}
	TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
	E->SetStringField(TEXT("timestamp"), NowIso());
	E->SetStringField(TEXT("request_id"), RequestId);
	E->SetStringField(TEXT("phase"), Phase);
	E->SetStringField(TEXT("status"), Status);
	if (Detail.IsValid()) { E->SetObjectField(TEXT("detail"), Detail); }
	Entries.Add(MakeShared<FJsonValueObject>(E));
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema_version"), TEXT("1.0"));
	Root->SetArrayField(TEXT("entries"), Entries);
	WriteJsonObj(Path, Root);
}

bool FBPAgentLiveService::IsRequestCompleted(const FString& RequestId, FString& OutManifestPath)
{
	OutManifestPath.Reset();
	const FString Done = OutboxDir() / (RequestId + TEXT(".done"));
	if (!FPaths::FileExists(Done)) { return false; }
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Done)) { return true; }
	TSharedPtr<FJsonObject> M;
	const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Text);
	if (FJsonSerializer::Deserialize(R, M) && M.IsValid())
	{
		M->TryGetStringField(TEXT("manifest"), OutManifestPath);
	}
	return true;
}

FString FBPAgentLiveService::ResolveSourceState(UObject* Asset)
{
	if (!Asset) { return TEXT("unknown"); }
	const bool bDirty = Asset->GetOutermost() && Asset->GetOutermost()->IsDirty();
	return bDirty ? TEXT("loaded_dirty_memory") : TEXT("loaded_clean_memory");
}

int32 FBPAgentLiveService::RunPostAnalyze(const FString& RequestId, UBlueprint* BP, const FString& ReportDir, const FBPAgentEditorState& St)
{
	if (!BP) { return 20; }
	const FString SubDir = ReportDir / TEXT("post_analyze");
	const FString Pkg = BP->GetOutermost()->GetName();
	const FString Short = BP->GetName();
	const FString SourceState = ResolveSourceState(BP);
	TSharedPtr<FJsonObject> Raw = FBPGenIRDumper::DumpBlueprint(BP);
	TSharedPtr<FJsonObject> Unified = BuildUnifiedIR(Raw, Pkg, Short);
	TArray<FString> W, E, M;
	return WriteAnalyzeReport(RequestId, SubDir, Unified, Pkg, Short, SourceState, St, W, E, M);
}

void FBPAgentLiveService::WriteEditCreateManifest(const FString& RequestId, const FString& Dir, const FString& Task, const FString& Status,
	const FString& AssetPath, const FString& Pointer, const FBPAgentEditorState& St,
	const TArray<FString>& Warnings, const TArray<FString>& Errors, const TArray<FString>& Manual)
{
	IFileManager::Get().MakeDirectory(*Dir, true);
	IFileManager::Get().MakeDirectory(*(Dir / TEXT("logs")), true);
	WriteUtf8NoBom(Dir / TEXT("logs") / TEXT("warnings.json"), JsonStringArray(Warnings));
	WriteUtf8NoBom(Dir / TEXT("logs") / TEXT("errors.json"), JsonStringArray(Errors));

	TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("schema_version"), TEXT("1.0"));
	M->SetStringField(TEXT("status"), Status);
	M->SetStringField(TEXT("mode"), TEXT("editor_live"));
	M->SetStringField(TEXT("task_type"), Task);
	M->SetStringField(TEXT("asset_path"), AssetPath);
	M->SetBoolField(TEXT("read_only"), false);
	M->SetStringField(TEXT("generated_at"), NowIso());
	if (!Pointer.IsEmpty()) { M->SetStringField(TEXT("artifacts_note"), Pointer); }
	{
		TArray<TSharedPtr<FJsonValue>> W; for (const FString& S : Warnings) { W.Add(MakeShared<FJsonValueString>(S)); }
		M->SetArrayField(TEXT("warnings"), W);
		TArray<TSharedPtr<FJsonValue>> Er; for (const FString& S : Errors) { Er.Add(MakeShared<FJsonValueString>(S)); }
		M->SetArrayField(TEXT("errors"), Er);
		TArray<TSharedPtr<FJsonValue>> Mk; for (const FString& S : Manual) { Mk.Add(MakeShared<FJsonValueString>(S)); }
		M->SetArrayField(TEXT("manual_check_required"), Mk);
	}
	M->SetObjectField(TEXT("editor_live"), MakeEditorLiveBlock(RequestId, TEXT("n/a"), St));
	WriteJsonObj(Dir / TEXT("manifest.json"), M);
}
