// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** Per-asset generation outcome. */
struct FBPGenAssetResult
{
	FString AssetPath;
	FString AssetType;          // "Enum" / "Struct" / "Interface" / "Actor" / "ActorComponent"
	bool    bCreated = false;
	bool    bSaved = false;
	FString CompileStatus;      // "up_to_date" / "warnings" / "error" / "not_compiled"
	TArray<FString> Notes;      // skipped nodes, partial coverage, manual-confirm flags
};

/** Aggregate report returned to the UI / commandlet. */
struct FBPGenReport
{
	int32 TotalAssets = 0;
	int32 CompiledOk = 0;
	int32 CompiledWithWarnings = 0;
	int32 Failed = 0;
	FString ReportFilePath;
	TArray<FBPGenAssetResult> Assets;
};

/**
 * Top-level orchestrator. Creates the /Game/BPParserTest folder, builds every
 * support asset and test blueprint, compiles + saves them, and writes a JSON
 * generation report to <Project>/Saved/BPParserTestReports/generation_log.json.
 */
class FBPGenOrchestrator
{
public:
	static FBPGenReport GenerateAll(bool bSave = true);
};
