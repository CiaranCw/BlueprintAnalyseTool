// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPCreateCommandlet.h"
#include "BPParserTestGenModule.h"
#include "BPCreate.h"

int32 UBPCreateCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches; TMap<FString,FString> M;
	ParseCommandLine(*Params, Tokens, Switches, M);
	const FString Spec = M.FindRef(TEXT("SpecFile"));
	const FString Out  = M.FindRef(TEXT("OutputDir"));
	if (Spec.IsEmpty()) { UE_LOG(LogBPParserTestGen, Error, TEXT("BPCreate: missing -SpecFile=<json>")); return 30; }
	const int32 Code = FBPCreate::Run(Spec, Out);
	UE_LOG(LogBPParserTestGen, Display, TEXT("BPCreate: exit %d"), Code);
	return Code;
}
