// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPParserTestDumpCommandlet.h"
#include "BPParserTestGenModule.h"
#include "BPGenIRDumper.h"
#include "Misc/Paths.h"

int32 UBPParserTestDumpCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	const FString* AssetPathPtr = ParamsMap.Find(TEXT("AssetPath"));
	if (!AssetPathPtr || AssetPathPtr->IsEmpty())
	{
		UE_LOG(LogBPParserTestGen, Error, TEXT("BPParserTestDump: missing -AssetPath=/Game/..."));
		return 30;
	}

	FString OutDir;
	if (const FString* OutPtr = ParamsMap.Find(TEXT("OutputDir")))
	{
		OutDir = *OutPtr;
	}
	if (OutDir.IsEmpty())
	{
		OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BPParserTestReports"), TEXT("ir_dumps"));
	}

	const FString Written = FBPGenIRDumper::DumpAssetToFile(*AssetPathPtr, OutDir);
	if (Written.IsEmpty())
	{
		UE_LOG(LogBPParserTestGen, Error, TEXT("BPParserTestDump: dump failed for %s"), **AssetPathPtr);
		return 50;
	}

	UE_LOG(LogBPParserTestGen, Display, TEXT("BPParserTestDump: wrote %s"), *Written);
	return 0;
}
