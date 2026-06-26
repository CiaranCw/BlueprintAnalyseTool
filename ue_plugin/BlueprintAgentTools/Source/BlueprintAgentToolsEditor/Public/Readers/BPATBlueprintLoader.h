// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UBlueprint;

struct FBPATBlueprintLoadStats
{
	double LoadElapsedMs = 0.0;
	int32 LoadedSubobjectCount = 0;
};

struct FBPATBlueprintLoadResult
{
	TWeakObjectPtr<UBlueprint> Blueprint;
	FBPATBlueprintLoadStats Stats;
	FString ErrorCode;        // empty if ok; else BPATErrorCodes::*
	FString ErrorMessage;
};

/**
 * Atomic capability #2.
 * Loads a UBlueprint by package path. Read-only.
 *
 * V8 (待验证): PostLoad on UBlueprint may trigger implicit compilation in
 * commandlet mode. Need to confirm and add SavePackage protection if so.
 */
class FBPATBlueprintLoader
{
public:
	static FBPATBlueprintLoadResult Load(const FString& PackagePath);
};
