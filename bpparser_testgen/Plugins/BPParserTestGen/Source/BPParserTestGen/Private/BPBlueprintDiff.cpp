// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPBlueprintDiff.h"
#include "BPParserTestGenModule.h"
#include "BPGenIRDumper.h"
#include "BPGenUECompat.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

#include "GraphDiffControl.h"
#include "DiffResults.h"
#include "DiffUtils.h"

#include "Misc/PackagePath.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"

namespace
{
	FString GuidStr(const FGuid& G) { return G.ToString(EGuidFormats::Digits); }

	void WriteJson(const FString& Path, const TSharedRef<FJsonObject>& Root)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Root, W);
		FFileHelper::SaveStringToFile(Out, *Path);
	}

	UBlueprint* FindBlueprintInPackage(UPackage* Package)
	{
		if (!Package) { return nullptr; }
		for (TObjectIterator<UBlueprint> It; It; ++It)
		{
			if (It->GetOutermost() == Package) { return *It; }
		}
		return nullptr;
	}

	// Collect graphs of a blueprint keyed by name, tagged with type.
	void CollectGraphs(UBlueprint* BP, TMap<FString, UEdGraph*>& Out, TMap<FString, FString>& Types)
	{
		if (!BP) { return; }
		auto Add = [&](const TArray<TObjectPtr<UEdGraph>>& List, const TCHAR* Type)
		{
			for (UEdGraph* G : List)
			{
				if (G) { Out.Add(G->GetName(), G); Types.Add(G->GetName(), Type); }
			}
		};
		Add(BP->UbergraphPages, TEXT("ubergraph"));
		Add(BP->FunctionGraphs, TEXT("function"));
		Add(BP->MacroGraphs, TEXT("macro"));
	}

	FString DiffTypeName(EDiffType::Type T)
	{
		switch (T)
		{
			case EDiffType::NODE_ADDED: return TEXT("NODE_ADDED");
			case EDiffType::NODE_REMOVED: return TEXT("NODE_REMOVED");
			case EDiffType::NODE_MOVED: return TEXT("NODE_MOVED");
			case EDiffType::NODE_COMMENT: return TEXT("NODE_COMMENT");
			case EDiffType::NODE_PIN_COUNT: return TEXT("NODE_PIN_COUNT");
			case EDiffType::NODE_PROPERTY: return TEXT("NODE_PROPERTY");
			case EDiffType::PIN_DEFAULT_VALUE: return TEXT("PIN_DEFAULT_VALUE");
			case EDiffType::PIN_TYPE_CATEGORY: return TEXT("PIN_TYPE_CATEGORY");
			case EDiffType::PIN_TYPE_SUBCATEGORY: return TEXT("PIN_TYPE_SUBCATEGORY");
			case EDiffType::PIN_TYPE_SUBCATEGORY_OBJECT: return TEXT("PIN_TYPE_SUBCATEGORY_OBJECT");
			case EDiffType::PIN_TYPE_IS_ARRAY: return TEXT("PIN_TYPE_IS_ARRAY");
			case EDiffType::PIN_TYPE_IS_REF: return TEXT("PIN_TYPE_IS_REF");
			case EDiffType::PIN_LINKEDTO_NUM_INC: return TEXT("PIN_LINKEDTO_NUM_INC");
			case EDiffType::PIN_LINKEDTO_NUM_DEC: return TEXT("PIN_LINKEDTO_NUM_DEC");
			case EDiffType::PIN_LINKEDTO_NODE: return TEXT("PIN_LINKEDTO_NODE");
			case EDiffType::PIN_LINKEDTO_PIN: return TEXT("PIN_LINKEDTO_PIN");
			default: return FString::Printf(TEXT("DIFF_%d"), (int32)T);
		}
	}

	// Semantic mapping: our stable diff type + logical/visual class.
	FString SemanticType(EDiffType::Type T)
	{
		switch (T)
		{
			case EDiffType::NODE_ADDED: return TEXT("NodeAdded");
			case EDiffType::NODE_REMOVED: return TEXT("NodeRemoved");
			case EDiffType::NODE_MOVED: return TEXT("NodeMoved");
			case EDiffType::NODE_COMMENT: return TEXT("CommentChanged");
			case EDiffType::NODE_PIN_COUNT: return TEXT("PinCountChanged");
			case EDiffType::NODE_PROPERTY: return TEXT("NodeModified");
			case EDiffType::PIN_DEFAULT_VALUE: return TEXT("PinDefaultValueChanged");
			case EDiffType::PIN_TYPE_CATEGORY:
			case EDiffType::PIN_TYPE_SUBCATEGORY:
			case EDiffType::PIN_TYPE_SUBCATEGORY_OBJECT:
			case EDiffType::PIN_TYPE_IS_ARRAY:
			case EDiffType::PIN_TYPE_IS_REF: return TEXT("PinTypeChanged");
			case EDiffType::PIN_LINKEDTO_NUM_INC: return TEXT("EdgeAdded");
			case EDiffType::PIN_LINKEDTO_NUM_DEC: return TEXT("EdgeRemoved");
			case EDiffType::PIN_LINKEDTO_NODE:
			case EDiffType::PIN_LINKEDTO_PIN: return TEXT("EdgeRewired");
			default: return DiffTypeName(T);
		}
	}

	bool IsVisualOnly(EDiffType::Type T)
	{
		return T == EDiffType::NODE_MOVED || T == EDiffType::NODE_COMMENT;
	}

	FString CategoryName(EDiffType::Category C)
	{
		switch (C)
		{
			case EDiffType::ADDITION: return TEXT("addition");
			case EDiffType::SUBTRACTION: return TEXT("subtraction");
			case EDiffType::MODIFICATION: return TEXT("modification");
			case EDiffType::MINOR: return TEXT("minor");
			default: return TEXT("control");
		}
	}

	FString Severity(EDiffType::Category C)
	{
		if (C == EDiffType::MINOR) { return TEXT("minor"); }
		if (C == EDiffType::CONTROL) { return TEXT("info"); }
		return TEXT("major");
	}
}

int32 FBPBlueprintDiff::Run(const FInput& In)
{
	auto Log = [](const FString& M) { UE_LOG(LogBPParserTestGen, Display, TEXT("BPBlueprintDiff: %s"), *M); };

	FString OutDir = In.OutputDir;
	if (OutDir.IsEmpty())
	{
		OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BPParserAgentReports"), TEXT("blueprint_diff"),
			FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")));
	}
	IFileManager::Get().MakeDirectory(*OutDir, true);

	// --- Load OLD and NEW blueprints ---
	UBlueprint* OldBP = nullptr;
	UBlueprint* NewBP = nullptr;

	if (!In.OldAssetPath.IsEmpty() && !In.NewAssetPath.IsEmpty())
	{
		// Mode A: two distinct in-project assets (no package-name clash).
		OldBP = LoadObject<UBlueprint>(nullptr, *In.OldAssetPath);
		NewBP = LoadObject<UBlueprint>(nullptr, *In.NewAssetPath);
	}
	else if (!In.OldFile.IsEmpty())
	{
		// Mode B: two revisions of the same asset on disk. Load OLD isolated via DiffUtils.
		const FPackagePath OldTemp = FPackagePath::FromLocalPath(In.OldFile);
		const FPackagePath OldOrig = In.OldAssetPath.IsEmpty() ? OldTemp : FPackagePath::FromPackageNameChecked(In.OldAssetPath);
		if (UPackage* OldPkg = DiffUtils::LoadPackageForDiff(OldTemp, OldOrig)) { OldBP = FindBlueprintInPackage(OldPkg); }

		if (!In.NewFile.IsEmpty())
		{
			const FPackagePath NewTemp = FPackagePath::FromLocalPath(In.NewFile);
			if (UPackage* NewPkg = DiffUtils::LoadPackageForDiff(NewTemp, NewTemp)) { NewBP = FindBlueprintInPackage(NewPkg); }
		}
		else if (!In.NewAssetPath.IsEmpty())
		{
			NewBP = LoadObject<UBlueprint>(nullptr, *In.NewAssetPath);
		}
	}
	else
	{
		Log(TEXT("bad input: require (OldAssetPath+NewAssetPath) or (OldFile[+NewFile|NewAssetPath])"));
		return 30;
	}

	if (!OldBP || !NewBP)
	{
		Log(FString::Printf(TEXT("failed to load blueprints (old=%d new=%d)"), OldBP != nullptr, NewBP != nullptr));
		return 20;
	}

	// --- Dump IR for both sides (viewer geometry) ---
	{
		TSharedRef<FJsonObject> OldIR = FBPGenIRDumper::DumpBlueprint(OldBP).ToSharedRef();
		OldIR->SetStringField(TEXT("side"), TEXT("old"));
		WriteJson(FPaths::Combine(OutDir, TEXT("old.ir.json")), OldIR);

		TSharedRef<FJsonObject> NewIR = FBPGenIRDumper::DumpBlueprint(NewBP).ToSharedRef();
		NewIR->SetStringField(TEXT("side"), TEXT("new"));
		WriteJson(FPaths::Combine(OutDir, TEXT("new.ir.json")), NewIR);
	}

	// --- Structural diff, graph by graph (reuse UE's FGraphDiffControl) ---
	TMap<FString, UEdGraph*> OldGraphs, NewGraphs; TMap<FString, FString> OldTypes, NewTypes;
	CollectGraphs(OldBP, OldGraphs, OldTypes);
	CollectGraphs(NewBP, NewGraphs, NewTypes);

	TSet<FString> AllNames;
	for (const auto& KV : OldGraphs) { AllNames.Add(KV.Key); }
	for (const auto& KV : NewGraphs) { AllNames.Add(KV.Key); }

	TArray<TSharedPtr<FJsonValue>> GraphsJson;
	int32 Total = 0, Logical = 0, Visual = 0;
	TMap<FString, int32> ByType;

	for (const FString& Name : AllNames)
	{
		UEdGraph* OG = OldGraphs.FindRef(Name);
		UEdGraph* NG = NewGraphs.FindRef(Name);

		TSharedPtr<FJsonObject> GJ = MakeShared<FJsonObject>();
		GJ->SetStringField(TEXT("graph"), Name);
		GJ->SetStringField(TEXT("graph_type"), NewTypes.Contains(Name) ? NewTypes[Name] : OldTypes.FindRef(Name));
		TArray<TSharedPtr<FJsonValue>> Changes;

		auto AddChange = [&](const FString& Type, const FString& Raw, const FString& Cat, const FString& Cls,
			const FString& Sev, UEdGraphNode* N1, UEdGraphNode* N2, UEdGraphPin* P1, UEdGraphPin* P2, const FString& Msg)
		{
			TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
			C->SetStringField(TEXT("type"), Type);
			C->SetStringField(TEXT("raw_diff_type"), Raw);
			C->SetStringField(TEXT("category"), Cat);
			C->SetStringField(TEXT("change_class"), Cls);
			C->SetStringField(TEXT("severity"), Sev);
			if (N1) { C->SetStringField(TEXT("node1_guid"), GuidStr(N1->NodeGuid)); C->SetStringField(TEXT("node1_title"), N1->GetNodeTitle(ENodeTitleType::ListView).ToString()); }
			if (N2) { C->SetStringField(TEXT("node2_guid"), GuidStr(N2->NodeGuid)); C->SetStringField(TEXT("node2_title"), N2->GetNodeTitle(ENodeTitleType::ListView).ToString()); }
			if (P1) { C->SetStringField(TEXT("pin1"), P1->PinName.ToString()); }
			if (P2) { C->SetStringField(TEXT("pin2"), P2->PinName.ToString()); }
			if (!Msg.IsEmpty()) { C->SetStringField(TEXT("message"), Msg); }
			Changes.Add(MakeShared<FJsonValueObject>(C));
			Total++;
			ByType.FindOrAdd(Type)++;
			if (Cls == TEXT("visual")) { Visual++; } else { Logical++; }
		};

		if (OG && NG)
		{
			TArray<FDiffSingleResult> Results;
			FGraphDiffControl::DiffGraphs(OG, NG, Results);
			for (const FDiffSingleResult& R : Results)
			{
				if (!R.IsRealDifference()) { continue; }
				// Pin-level diffs (e.g. PIN_DEFAULT_VALUE) leave Node1/Node2 null; recover the owning
				// node from the pin so the viewer can highlight/locate the affected node.
				UEdGraphNode* EN1 = R.Node1 ? R.Node1 : (R.Pin1 ? R.Pin1->GetOwningNodeUnchecked() : nullptr);
				UEdGraphNode* EN2 = R.Node2 ? R.Node2 : (R.Pin2 ? R.Pin2->GetOwningNodeUnchecked() : nullptr);
				AddChange(SemanticType(R.Diff), DiffTypeName(R.Diff), CategoryName(R.Category),
					IsVisualOnly(R.Diff) ? TEXT("visual") : TEXT("logical"), Severity(R.Category),
					EN1, EN2, R.Pin1, R.Pin2, R.DisplayString.ToString());
			}
		}
		else if (NG && !OG)
		{
			AddChange(TEXT("GraphAdded"), TEXT("GRAPH_ADDED"), TEXT("addition"), TEXT("logical"), TEXT("major"),
				nullptr, nullptr, nullptr, nullptr, FString::Printf(TEXT("Graph '%s' added (%d nodes)"), *Name, NG->Nodes.Num()));
		}
		else if (OG && !NG)
		{
			AddChange(TEXT("GraphRemoved"), TEXT("GRAPH_REMOVED"), TEXT("subtraction"), TEXT("logical"), TEXT("major"),
				nullptr, nullptr, nullptr, nullptr, FString::Printf(TEXT("Graph '%s' removed (%d nodes)"), *Name, OG->Nodes.Num()));
		}

		GJ->SetArrayField(TEXT("changes"), Changes);
		GraphsJson.Add(MakeShared<FJsonValueObject>(GJ));
	}

	// --- Assemble diff.json ---
	TSharedRef<FJsonObject> Diff = MakeShared<FJsonObject>();
	Diff->SetStringField(TEXT("schema_version"), TEXT("bp-diff-1.0"));
	Diff->SetStringField(TEXT("engine_version"), BPGenCompat::EngineFullVersion());
	Diff->SetStringField(TEXT("asset_old"), OldBP->GetPathName());
	Diff->SetStringField(TEXT("asset_new"), NewBP->GetPathName());
	TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetNumberField(TEXT("total"), Total);
	Summary->SetNumberField(TEXT("logical"), Logical);
	Summary->SetNumberField(TEXT("visual"), Visual);
	TSharedPtr<FJsonObject> ByTypeJson = MakeShared<FJsonObject>();
	for (const auto& KV : ByType) { ByTypeJson->SetNumberField(KV.Key, KV.Value); }
	Summary->SetObjectField(TEXT("by_type"), ByTypeJson);
	Diff->SetObjectField(TEXT("summary"), Summary);
	Diff->SetArrayField(TEXT("graphs"), GraphsJson);
	WriteJson(FPaths::Combine(OutDir, TEXT("diff.json")), Diff);

	// --- manifest.json ---
	TSharedRef<FJsonObject> Manifest = MakeShared<FJsonObject>();
	Manifest->SetStringField(TEXT("schema_version"), TEXT("1.0"));
	Manifest->SetStringField(TEXT("status"), TEXT("success"));
	Manifest->SetStringField(TEXT("engine_version"), BPGenCompat::EngineFullVersion());
	Manifest->SetStringField(TEXT("asset_old"), OldBP->GetPathName());
	Manifest->SetStringField(TEXT("asset_new"), NewBP->GetPathName());
	Manifest->SetNumberField(TEXT("total_changes"), Total);
	Manifest->SetStringField(TEXT("output_dir"), OutDir);
	Manifest->SetStringField(TEXT("diff_report"), FPaths::Combine(OutDir, TEXT("diff.json")));
	Manifest->SetStringField(TEXT("old_ir"), FPaths::Combine(OutDir, TEXT("old.ir.json")));
	Manifest->SetStringField(TEXT("new_ir"), FPaths::Combine(OutDir, TEXT("new.ir.json")));
	WriteJson(FPaths::Combine(OutDir, TEXT("manifest.json")), Manifest);

	Log(FString::Printf(TEXT("done: %d changes (logical=%d visual=%d) -> %s"), Total, Logical, Visual, *OutDir));
	return 0;
}
