// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Schema/BPATAssetIR.h"

class UBlueprint;

/** Atomic capability #8. */
class FBPATMemberReader
{
public:
	static FBPATMemberSet Read(const UBlueprint* Blueprint);
};
