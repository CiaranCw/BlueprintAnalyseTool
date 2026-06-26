// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Schema/BPATBlueprintIR.h"
#include "Util/BPATOutputDirManager.h"

namespace BPATOutputLayers
{
	enum Type : uint8
	{
		Manifest = 1 << 0,
		Graphs   = 1 << 1,
		Nodes    = 1 << 2,
		Slices   = 1 << 3,
		All      = Manifest | Graphs | Nodes,
	};
}

struct FBPATSerializeOptions
{
	uint8 Layers = BPATOutputLayers::All;
	int32 InlineNodeLimitPerGraph = 200;
	int32 FullSplitTotalNodes = 1000;
	bool  bDryRun = false;
};

struct FBPATSerializeResult
{
	TArray<FString> WrittenFiles;
	TArray<FString> SkippedFiles;
	int64 TotalBytes = 0;
};

/** Atomic capability #14. Layered IR serialization. */
class FBPATIRSerializer
{
public:
	static FBPATSerializeResult WriteAll(const FBPATBlueprintIR& IR,
	                                     const FBPATOutputLayout& Layout,
	                                     const FBPATSerializeOptions& Options);
};
