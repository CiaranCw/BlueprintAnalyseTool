// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Schema/BPATBlueprintIR.h"

class UBlueprint;

/** Atomic capability #12. Pure in-memory IR assembly. */
class FBPATIRBuilder
{
public:
	/**
	 * Drives readers in fixed order, assembles a full IR.
	 * Does NOT touch the filesystem. Does NOT call any reader that may
	 * modify the loaded UObject (read-only by construction).
	 */
	static FBPATBlueprintIR Build(const UBlueprint* Blueprint);
};
