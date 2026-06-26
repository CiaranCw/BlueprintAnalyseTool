// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "Commandlets/BPATDumpCommandlet.h"
#include "BPATErrorCodes.h"
#include "BPATGlobals.h"
#include "BPATLog.h"
#include "Util/BPATOutputDirManager.h"
#include "Validators/BPATReadOnlyGuard.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"

UBPATDumpCommandlet::UBPATDumpCommandlet()
{
	IsClient    = false;
	IsServer    = false;
	IsEditor    = true;
	LogToConsole = true;
}

int32 UBPATDumpCommandlet::Main(const FString& Params)
{
	UE_LOG(LogBPAT, Log, TEXT("BPATDumpCommandlet starting. Schema=%s"), BPATGlobals::SchemaVersion);

	TMap<FString, FString> ParamsMap;
	TArray<FString> Tokens;
	TArray<FString> Switches;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	const FString OutputDir = ParamsMap.FindRef(TEXT("OutputDir"));
	if (OutputDir.IsEmpty())
	{
		UE_LOG(LogBPAT, Error, TEXT("%s: missing -OutputDir."), BPATErrorCodes::OutputDirInsideProject);
		return BPATExitCodes::InvalidArguments;
	}

	const FString OverwriteStr = ParamsMap.FindRef(TEXT("OverwritePolicy"));
	BPATOverwritePolicy::Type Policy = BPATOverwritePolicy::Skip;
	if (OverwriteStr.Equals(TEXT("overwrite"), ESearchCase::IgnoreCase)) Policy = BPATOverwritePolicy::Overwrite;
	else if (OverwriteStr.Equals(TEXT("fail"), ESearchCase::IgnoreCase)) Policy = BPATOverwritePolicy::Fail;

	const bool bDryRun       = ParamsMap.FindRef(TEXT("DryRun")) == TEXT("1");
	const bool bStrictReadOnly = ParamsMap.FindRef(TEXT("StrictReadOnly")) != TEXT("0");

	if (!bStrictReadOnly)
	{
		UE_LOG(LogBPAT, Error,
			TEXT("Phase-1 forbids -StrictReadOnly=0. Reject."));
		return BPATExitCodes::InvalidArguments;
	}

	FBPATOutputLayout Layout = FBPATOutputDirManager::Prepare(
		OutputDir,
		FPaths::GetProjectFilePath(),
		Policy, bDryRun);

	FBPATReadOnlyGuard Guard;
	Guard.Begin();

	// TODO(M1):
	//   1. Pick AssetPath / ProjectScan branch
	//   2. BPATAssetEnumerator
	//   3. for each asset:
	//        BPATBlueprintLoader
	//        BPATIRBuilder
	//        BPATIRValidator
	//        BPATIRSerializer
	//   4. Write _summary.json
	UE_LOG(LogBPAT, Warning, TEXT("BPATDumpCommandlet skeleton only; M1 implementation pending."));

	Guard.End();

	if (Guard.HasViolation())
	{
		UE_LOG(LogBPAT, Error,
			TEXT("%s: read-only violation detected during dump. Aborting."),
			BPATErrorCodes::ReadOnlyViolation);
		return BPATExitCodes::ReadOnlyViolation;
	}

	UE_LOG(LogBPAT, Log, TEXT("BPATDumpCommandlet finished."));
	return BPATExitCodes::AllSuccess;
}
