// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "Util/BPATPathPolicy.h"
#include "BPATGlobals.h"
#include "BPATLog.h"
#include "BPATErrorCodes.h"
#include "Misc/Paths.h"

FString FBPATPathPolicy::OutputRootAbs;
FString FBPATPathPolicy::ProjectRootAbs;
bool    FBPATPathPolicy::bConfigured = false;

static FString NormalizeAbs(const FString& In)
{
	FString Norm = FPaths::ConvertRelativePathToFull(In);
	FPaths::NormalizeDirectoryName(Norm);
	return Norm;
}

void FBPATPathPolicy::SetOutputRoot(const FString& InOutputRootAbsolute,
                                    const FString& InProjectRootAbsolute)
{
	OutputRootAbs  = NormalizeAbs(InOutputRootAbsolute);
	ProjectRootAbs = NormalizeAbs(InProjectRootAbsolute);

	if (OutputRootAbs.StartsWith(ProjectRootAbs))
	{
		UE_LOG(LogBPAT, Fatal,
			TEXT("%s: OutputDir must NOT be inside ProjectPath. OutputDir=%s ProjectPath=%s"),
			BPATErrorCodes::OutputDirInsideProject,
			*OutputRootAbs, *ProjectRootAbs);
	}

	bConfigured = true;

	UE_LOG(LogBPAT, Log, TEXT("PathPolicy configured. OutputRoot=%s ProjectRoot=%s"),
		*OutputRootAbs, *ProjectRootAbs);
}

void FBPATPathPolicy::AssertWritable(const FString& AbsPath)
{
	checkf(bConfigured, TEXT("BPATPathPolicy::SetOutputRoot must be called before any write."));

	const FString Norm = NormalizeAbs(AbsPath);
	if (!Norm.StartsWith(OutputRootAbs))
	{
		UE_LOG(LogBPAT, Fatal,
			TEXT("%s: write path outside OutputRoot. Path=%s OutputRoot=%s"),
			BPATErrorCodes::ReadOnlyViolation, *Norm, *OutputRootAbs);
	}
}

bool FBPATPathPolicy::IsProtectedPackagePath(const FString& PackagePath)
{
	for (const FString& Prefix : BPATGlobals::GetProtectedPackagePrefixes())
	{
		if (PackagePath.StartsWith(Prefix))
		{
			return true;
		}
	}
	return false;
}
