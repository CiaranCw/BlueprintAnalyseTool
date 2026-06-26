// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Schema/BPATBlueprintIR.h"

class UAnimBlueprint;

/**
 * Atomic capability #11.
 * Partial support: top-level AnimGraph nodes + edges only.
 * State machine semantics deferred to a later phase.
 */
class FBPATAnimGraphReader
{
public:
	static FBPATAnimGraphIR Read(const UAnimBlueprint* AnimBP);
};
