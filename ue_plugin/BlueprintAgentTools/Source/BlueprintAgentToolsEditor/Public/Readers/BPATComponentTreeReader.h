// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Schema/BPATAssetIR.h"

class UBlueprint;

/** Atomic capability #9. */
class FBPATComponentTreeReader
{
public:
	static FBPATComponentTreeIR Read(const UBlueprint* Blueprint);
};
