// Copyright BlueprintAnalyseTool. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace BPATOverwritePolicy
{
	enum Type
	{
		Skip,
		Overwrite,
		Fail,
	};
}

/** Per-run output directory layout. */
struct FBPATOutputLayout
{
	FString ProjectName;
	FString OutputRootAbs;       // <OutputDir>
	FString ProjectOutputDirAbs; // <OutputDir>/<ProjectName>
	FString BlueprintsDirAbs;    // <ProjectOutputDir>/blueprints
	FString IndexDirAbs;         // <ProjectOutputDir>/_index
	FString LogsDirAbs;          // <ProjectOutputDir>/logs

	BPATOverwritePolicy::Type OverwritePolicy = BPATOverwritePolicy::Skip;
	bool bDryRun = false;
};

/**
 * Validates --OutputDir and prepares the output directory tree.
 * Aborts (Fatal) on any rule violation. See docs/readonly_safety.md L1.
 */
class FBPATOutputDirManager
{
public:
	static FBPATOutputLayout Prepare(const FString& OutputDir,
	                                 const FString& ProjectFilePath,
	                                 BPATOverwritePolicy::Type Policy,
	                                 bool bDryRun);

	/** Per-asset directory under blueprints/<safe_path>/ */
	static FString BlueprintAssetDir(const FBPATOutputLayout& Layout,
	                                 const FString& AssetPath);

	/** Convert /Game/Foo/Bar -> Game__Foo__Bar */
	static FString MakeSafePath(const FString& AssetPath);
};
