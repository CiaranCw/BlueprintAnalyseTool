// Copyright BlueprintAnalyseTool. All Rights Reserved.
//
// BPGenUECompat: single place for UE-version-sensitive helpers so the plugin can
// move across UE 5.3 / 5.4 / 5.5 / 5.6 with minimal edits. Primary target: 5.4.
//
// Version-risk notes (kept here on purpose):
//  - UPackage::SavePackage(bool overload): present 5.x; deprecated path in newer.
//  - FBlueprintEditorUtils::ImplementNewInterface(FTopLevelAssetPath): 5.1+.
//  - FStructVariableDescription full def: UserDefinedStructure/UserDefinedStructEditorData.h.
//  - UK2Node_SwitchEnum is MinimalAPI -> set the public Enum field instead of SetEnum().
//  - SpawnActorFromClass is a ConstructObjectFromClass node: AllocateDefaultPins BEFORE PostPlacedNewNode.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"

namespace BPGenCompat
{
	/** "5.4" style major.minor string. */
	static inline FString EngineMajorMinor()
	{
		return FString::Printf(TEXT("%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION);
	}

	static inline FString EngineFullVersion()
	{
		return FEngineVersion::Current().ToString();
	}

	/**
	 * Save a package to its standard content path. Centralized so the SavePackage
	 * signature (which has churned across 5.x) only needs touching here.
	 */
	static inline bool SavePackageCompat(UPackage* Package, UObject* Asset)
	{
		if (!Package || !Asset) { return false; }
		Package->SetDirtyFlag(true);

		const FString PackageName = Package->GetName();
		const FString FileName = FPackageName::LongPackageNameToFilename(
			PackageName, FPackageName::GetAssetPackageExtension());

		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		Args.Error = GWarn;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
		return UPackage::SavePackage(Package, Asset, *FileName, Args);
#else
		// 5.0-5.3 also accept this overload; isolated here for clarity / future edits.
		return UPackage::SavePackage(Package, Asset, *FileName, Args);
#endif
	}
}
