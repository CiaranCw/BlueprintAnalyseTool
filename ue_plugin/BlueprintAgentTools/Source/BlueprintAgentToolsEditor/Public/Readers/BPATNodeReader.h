// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Schema/BPATNodeIR.h"

class UEdGraph;

/** Atomic capability #5. */
class FBPATNodeReader
{
public:
	static TArray<FBPATNodeIR> ReadAll(const UEdGraph* Graph,
	                                    const FString& OwnerAssetPath,
	                                    const FString& GraphId);
};
