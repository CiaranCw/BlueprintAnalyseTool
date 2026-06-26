// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Schema/BPATPinIR.h"

class UEdGraphNode;

/** Atomic capability #6. */
class FBPATPinReader
{
public:
	static TArray<FBPATPinIR> ReadAll(const UEdGraphNode* Node, const FString& NodeId);
};
