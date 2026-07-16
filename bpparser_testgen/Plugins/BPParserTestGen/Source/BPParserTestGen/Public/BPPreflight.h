// Copyright BlueprintAnalyseTool. All Rights Reserved.
//
// FBPPreflight: property-aware request preflight and normalization for create/edit apply.
#pragma once

#include "CoreMinimal.h"
#include "BPWidgetGen.h"

class UBlueprint;
class FJsonObject;

class FBPPreflight
{
public:
	struct FOptions
	{
		FString OutputDir;
		FString AssetPath;
		UBlueprint* LoadedBP = nullptr;
		bool bStrictRequired = true;
	};

	static int32 RunPreflight(const FString& TaskType, const TSharedPtr<FJsonObject>& Request, const FOptions& Opt,
		TSharedPtr<FJsonObject>& OutReport, TSharedPtr<FJsonObject>& OutNormalized, TSharedPtr<FJsonObject>& OutCapability);

	static FString ComputeIrHash(const TSharedPtr<FJsonObject>& Ir);
	static bool IsPreflightApplyAllowed(int32 PreflightExitCode);
};
