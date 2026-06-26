// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPParserTestGenCommandlet.h"
#include "BPParserTestGenModule.h"
#include "BPGenOrchestrator.h"

int32 UBPParserTestGenCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	ParseCommandLine(*Params, Tokens, Switches);

	const bool bNoSave = Switches.Contains(TEXT("NoSave"));

	UE_LOG(LogBPParserTestGen, Display, TEXT("=== BPParserTestGen commandlet START (NoSave=%s) ==="),
		bNoSave ? TEXT("true") : TEXT("false"));

	const FBPGenReport Report = FBPGenOrchestrator::GenerateAll(!bNoSave);

	UE_LOG(LogBPParserTestGen, Display,
		TEXT("=== BPParserTestGen DONE. attempted=%d ok=%d warn=%d fail=%d report=%s ==="),
		Report.TotalAssets, Report.CompiledOk, Report.CompiledWithWarnings, Report.Failed, *Report.ReportFilePath);

	// Non-zero only on hard failure so CI can detect it.
	return Report.Failed > 0 ? 1 : 0;
}
