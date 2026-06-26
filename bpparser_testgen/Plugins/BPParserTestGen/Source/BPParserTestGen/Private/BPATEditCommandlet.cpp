// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPATEditCommandlet.h"
#include "BPParserTestGenModule.h"
#include "BPATEdit.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

namespace
{
	bool ParseBoolParam(const TMap<FString, FString>& M, const TArray<FString>& Switches, const TCHAR* Key, bool Def)
	{
		if (const FString* V = M.Find(Key))
		{
			return V->Equals(TEXT("1")) || V->Equals(TEXT("true"), ESearchCase::IgnoreCase) || V->Equals(TEXT("yes"), ESearchCase::IgnoreCase);
		}
		// also accept bare -Switch form
		for (const FString& S : Switches) { if (S.Equals(Key, ESearchCase::IgnoreCase)) { return true; } }
		return Def;
	}
}

int32 UBPATEditCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> M;
	ParseCommandLine(*Params, Tokens, Switches, M);

	const FString AssetPath = M.FindRef(TEXT("AssetPath"));
	const FString ReqFile = M.FindRef(TEXT("EditRequestJson"));

	if (ReqFile.IsEmpty())
	{
		UE_LOG(LogBPParserTestGen, Error, TEXT("BPATEdit: missing -EditRequestJson=<file>"));
		return 30;
	}

	FString ReqText;
	if (!FFileHelper::LoadFileToString(ReqText, *ReqFile))
	{
		UE_LOG(LogBPParserTestGen, Error, TEXT("BPATEdit: cannot read request file %s"), *ReqFile);
		return 30;
	}

	TSharedPtr<FJsonObject> Request;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReqText);
	if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
	{
		UE_LOG(LogBPParserTestGen, Error, TEXT("BPATEdit: invalid JSON in %s"), *ReqFile);
		return 30;
	}

	FBPATEdit::FOptions Opt;
	Opt.Mode = M.Contains(TEXT("Mode")) ? M.FindRef(TEXT("Mode")) : Request->GetStringField(TEXT("mode"));
	if (Opt.Mode.IsEmpty()) { Opt.Mode = TEXT("plan-only"); }
	Opt.OutputDir = M.FindRef(TEXT("OutputDir"));
	Opt.WorkOnCopy = M.FindRef(TEXT("WorkOnCopy"));
	Opt.bCreateBackup = ParseBoolParam(M, Switches, TEXT("CreateBackup"), true);
	Opt.bAllowDestructive = ParseBoolParam(M, Switches, TEXT("AllowDestructiveEdit"), false);
	Opt.bStrict = ParseBoolParam(M, Switches, TEXT("Strict"), false);

	const int32 Code = FBPATEdit::Run(AssetPath, Request, Opt);
	UE_LOG(LogBPParserTestGen, Display, TEXT("BPATEdit: exit code %d"), Code);
	return Code;
}
