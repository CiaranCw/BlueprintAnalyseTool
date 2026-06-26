// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Schema/BPATAssetIR.h"

class UBlueprint;

/** Atomic capability #3. Asset-level metadata. */
class FBPATAssetInfoReader
{
public:
	static FBPATAssetInfo Read(const UBlueprint* Blueprint);
};
