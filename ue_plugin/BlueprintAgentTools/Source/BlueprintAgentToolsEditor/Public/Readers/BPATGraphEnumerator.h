// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;

struct FBPATGraphRef
{
	FString GraphId;
	FString GraphName;
	FString GraphType;        // BPATGraphType::*
	UEdGraph* RawGraph = nullptr;
};

/** Atomic capability #4. */
class FBPATGraphEnumerator
{
public:
	static TArray<FBPATGraphRef> Enumerate(const UBlueprint* Blueprint);
};
