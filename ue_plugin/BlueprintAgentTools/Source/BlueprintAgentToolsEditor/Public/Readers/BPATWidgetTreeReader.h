// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Schema/BPATBlueprintIR.h"

class UWidgetBlueprint;

/** Atomic capability #10. Structure-only widget tree (full); binding semantics deferred. */
class FBPATWidgetTreeReader
{
public:
	static FBPATWidgetTreeIR Read(const UWidgetBlueprint* WidgetBP);
};
