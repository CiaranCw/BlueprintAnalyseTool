// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPGenOrchestrator.h"
#include "BPGen.h"
#include "BPGenSupportAssets.h"
#include "BPGenTestBlueprints.h"
#include "BPParserTestGenModule.h"

#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/PackageName.h"
#include "Misc/EngineVersion.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	void SaveByPath(const FString& AssetPath)
	{
		if (AssetPath.IsEmpty()) { return; }
		const FString ShortName = FPackageName::GetShortName(AssetPath);
		const FString ObjectPath = AssetPath + TEXT(".") + ShortName;
		UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
		if (!Asset) { Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath); }
		if (Asset) { FBPGen::SaveAsset(Asset); }
		else { UE_LOG(LogBPParserTestGen, Warning, TEXT("SaveByPath: cannot resolve %s"), *AssetPath); }
	}

	TSharedPtr<FJsonObject> ResultToJson(const FBPGenAssetResult& A)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("asset_path"), A.AssetPath);
		O->SetStringField(TEXT("asset_type"), A.AssetType);
		O->SetBoolField(TEXT("created"), A.bCreated);
		O->SetBoolField(TEXT("saved"), A.bSaved);
		O->SetStringField(TEXT("compile_status"), A.CompileStatus);
		TArray<TSharedPtr<FJsonValue>> Notes;
		for (const FString& N : A.Notes) { Notes.Add(MakeShared<FJsonValueString>(N)); }
		O->SetArrayField(TEXT("notes"), Notes);
		return O;
	}
}

FBPGenReport FBPGenOrchestrator::GenerateAll(bool bSave)
{
	FBPGenReport Report;
	UE_LOG(LogBPParserTestGen, Display, TEXT("BPParserTestGen: generating /Game/BPParserTest ..."));

	// --- Build everything in dependency order ---
	TArray<FBPGenAssetResult>& A = Report.Assets;

	// Support assets first (test blueprints load these).
	A.Add(FBPGenSupportAssets::BuildEnum());
	A.Add(FBPGenSupportAssets::BuildStruct());
	A.Add(FBPGenSupportAssets::BuildInterface());
	A.Add(FBPGenSupportAssets::BuildTargetActor());
	A.Add(FBPGenSupportAssets::BuildTestComponent());

	// Save support assets now so LoadObject/LoadBPClass during BP builds resolves cleanly.
	if (bSave)
	{
		for (const FBPGenAssetResult& Sup : A) { SaveByPath(Sup.AssetPath); }
	}

	// Primary test blueprints.
	A.Add(FBPGenTestBlueprints::Build_BP01_PrimitivePins());
	A.Add(FBPGenTestBlueprints::Build_BP02_StructEnumContainers());
	A.Add(FBPGenTestBlueprints::Build_BP03_ObjectRefCastInterface());
	A.Add(FBPGenTestBlueprints::Build_BP04_ExecFlowControl());
	A.Add(FBPGenTestBlueprints::Build_BP05_FunctionsMacrosLocals());
	A.Add(FBPGenTestBlueprints::Build_BP06_DelegatesDispatchers());
	A.Add(FBPGenTestBlueprints::Build_BP07_LatentTimerAsync());
	A.Add(FBPGenTestBlueprints::Build_BP08_ComplexGameplay());
	A.Add(FBPGenTestBlueprints::Build_BP09_FormattingCommentsReroutes());
	A.Add(FBPGenTestBlueprints::Build_BP10_RoundTripMaster());
	A.Add(FBPGenTestBlueprints::Build_BP99_NegativeEdgeCases());

	// --- Save + tally ---
	for (FBPGenAssetResult& Res : A)
	{
		Report.TotalAssets++;
		if (bSave && Res.bCreated)
		{
			SaveByPath(Res.AssetPath);
			Res.bSaved = true;
		}

		if (Res.CompileStatus == TEXT("up_to_date")) { Report.CompiledOk++; }
		else if (Res.CompileStatus == TEXT("warnings")) { Report.CompiledWithWarnings++; }
		else if (Res.CompileStatus == TEXT("error")) { Report.Failed++; }
		else if (!Res.bCreated) { Report.Failed++; }
	}

	// --- Write generation report JSON ---
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("tool"), TEXT("BPParserTestGen"));
	Root->SetStringField(TEXT("generated_at_utc"), FDateTime::UtcNow().ToIso8601());
	Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Root->SetNumberField(TEXT("total_assets"), Report.TotalAssets);
	Root->SetNumberField(TEXT("compiled_ok"), Report.CompiledOk);
	Root->SetNumberField(TEXT("compiled_with_warnings"), Report.CompiledWithWarnings);
	Root->SetNumberField(TEXT("failed"), Report.Failed);

	TArray<TSharedPtr<FJsonValue>> AssetsJson;
	for (const FBPGenAssetResult& Res : A) { AssetsJson.Add(MakeShared<FJsonValueObject>(ResultToJson(Res))); }
	Root->SetArrayField(TEXT("assets"), AssetsJson);

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BPParserTestReports"));
	IFileManager::Get().MakeDirectory(*Dir, /*Tree*/ true);
	Report.ReportFilePath = FPaths::Combine(Dir, TEXT("generation_log.json"));
	FFileHelper::SaveStringToFile(Out, *Report.ReportFilePath);

	UE_LOG(LogBPParserTestGen, Display,
		TEXT("BPParserTestGen done. total=%d ok=%d warn=%d fail=%d -> %s"),
		Report.TotalAssets, Report.CompiledOk, Report.CompiledWithWarnings, Report.Failed, *Report.ReportFilePath);

	return Report;
}
