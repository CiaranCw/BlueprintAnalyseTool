// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "Util/BPATOutputDirManager.h"
#include "Util/BPATPathPolicy.h"
#include "BPATLog.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

namespace
{
	FString NormalizeAbs(const FString& In)
	{
		FString Norm = FPaths::ConvertRelativePathToFull(In);
		FPaths::NormalizeDirectoryName(Norm);
		return Norm;
	}
}

FBPATOutputLayout FBPATOutputDirManager::Prepare(const FString& OutputDir,
                                                 const FString& ProjectFilePath,
                                                 BPATOverwritePolicy::Type Policy,
                                                 bool bDryRun)
{
	FBPATOutputLayout Layout;

	const FString OutAbs = NormalizeAbs(OutputDir);
	const FString ProjAbs = NormalizeAbs(FPaths::GetPath(ProjectFilePath));
	Layout.ProjectName = FPaths::GetBaseFilename(ProjectFilePath);

	FBPATPathPolicy::SetOutputRoot(OutAbs, ProjAbs);

	Layout.OutputRootAbs        = OutAbs;
	Layout.ProjectOutputDirAbs  = FPaths::Combine(OutAbs, Layout.ProjectName);
	Layout.BlueprintsDirAbs     = FPaths::Combine(Layout.ProjectOutputDirAbs, TEXT("blueprints"));
	Layout.IndexDirAbs          = FPaths::Combine(Layout.ProjectOutputDirAbs, TEXT("_index"));
	Layout.LogsDirAbs           = FPaths::Combine(Layout.ProjectOutputDirAbs, TEXT("logs"));
	Layout.OverwritePolicy      = Policy;
	Layout.bDryRun              = bDryRun;

	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	PF.CreateDirectoryTree(*Layout.ProjectOutputDirAbs);
	PF.CreateDirectoryTree(*Layout.BlueprintsDirAbs);
	PF.CreateDirectoryTree(*Layout.IndexDirAbs);
	PF.CreateDirectoryTree(*Layout.LogsDirAbs);

	UE_LOG(LogBPAT, Log, TEXT("OutputDir prepared: %s (overwrite=%d dryrun=%d)"),
		*Layout.ProjectOutputDirAbs, (int32)Policy, (int32)bDryRun);

	return Layout;
}

FString FBPATOutputDirManager::MakeSafePath(const FString& AssetPath)
{
	FString Safe = AssetPath;
	if (Safe.StartsWith(TEXT("/")))
	{
		Safe.RightChopInline(1, EAllowShrinking::No);
	}
	Safe.ReplaceInline(TEXT("/"), TEXT("__"), ESearchCase::CaseSensitive);
	return Safe;
}

FString FBPATOutputDirManager::BlueprintAssetDir(const FBPATOutputLayout& Layout,
                                                  const FString& AssetPath)
{
	return FPaths::Combine(Layout.BlueprintsDirAbs, MakeSafePath(AssetPath));
}
