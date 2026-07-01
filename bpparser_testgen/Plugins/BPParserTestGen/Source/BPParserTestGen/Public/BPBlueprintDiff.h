// Copyright BlueprintAnalyseTool. All Rights Reserved.
//
// BPBlueprintDiff: headless structural diff between two Blueprint assets.
// Reuses UE's own graph-diff (FGraphDiffControl::DiffGraphs) + this plugin's IR dumper
// (FBPGenIRDumper) to emit old.ir.json / new.ir.json / diff.json for an external viewer.
// This is the L3 "UE Worker" of the plugin-style architecture (see docs report) — it is
// host-agnostic and produces only files; no UI.
#pragma once

#include "CoreMinimal.h"

class FBPBlueprintDiff
{
public:
	struct FInput
	{
		// Mode A (distinct in-project assets, no name clash): both set.
		FString OldAssetPath;   // e.g. /Game/.../BP_Old
		FString NewAssetPath;   // e.g. /Game/.../BP_New
		// Mode B (two revisions of the same asset on disk): files + the logical asset path.
		FString OldFile;        // temp file holding the OLD revision (loaded isolated for diff)
		FString NewFile;        // file holding the NEW revision
		FString OutputDir;
	};

	/** Returns exit code: 0 success, 10 partial (loaded but issues), 20 failed, 30 bad input. */
	static int32 Run(const FInput& In);
};
