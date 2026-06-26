// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

struct FBPATAssetEnumerationEntry
{
	FString AssetPath;          // /Game/...
	FString PackagePath;        // /Game/....BPName
	FString AssetClassPath;     // /Script/Engine.Blueprint
	FString ParentClassPath;
};

struct FBPATAssetEnumerationFilter
{
	TArray<FString> ClassNames; // e.g. {"Blueprint","WidgetBlueprint","AnimBlueprint"}
	FString PathPrefix = TEXT("/Game/");
};

/**
 * Atomic capability #1.
 * Enumerates Blueprint-class assets via AssetRegistry. Read-only.
 *
 * V2 (待验证): UE 5.4 中 IAssetRegistry::GetAssetsByClass 是否需要 FTopLevelAssetPath。
 */
class FBPATAssetEnumerator
{
public:
	static TArray<FBPATAssetEnumerationEntry> Enumerate(const FBPATAssetEnumerationFilter& Filter);

	static bool LookupSingle(const FString& AssetPath, FBPATAssetEnumerationEntry& OutEntry);
};
