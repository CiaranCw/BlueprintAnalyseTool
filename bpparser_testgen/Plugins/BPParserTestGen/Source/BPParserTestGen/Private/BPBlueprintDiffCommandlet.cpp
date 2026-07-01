// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPBlueprintDiffCommandlet.h"
#include "BPParserTestGenModule.h"
#include "BPBlueprintDiff.h"

int32 UBPBlueprintDiffCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> M;
	ParseCommandLine(*Params, Tokens, Switches, M);

	FBPBlueprintDiff::FInput In;
	In.OldAssetPath = M.FindRef(TEXT("OldAssetPath"));
	In.NewAssetPath = M.FindRef(TEXT("NewAssetPath"));
	In.OldFile = M.FindRef(TEXT("OldFile"));
	In.NewFile = M.FindRef(TEXT("NewFile"));
	In.OutputDir = M.FindRef(TEXT("OutputDir"));

	const int32 Code = FBPBlueprintDiff::Run(In);
	UE_LOG(LogBPParserTestGen, Display, TEXT("BPBlueprintDiff: exit %d"), Code);
	return Code;
}
