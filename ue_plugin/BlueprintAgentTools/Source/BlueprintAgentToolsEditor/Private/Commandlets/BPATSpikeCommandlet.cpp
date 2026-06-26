// Copyright BlueprintAnalyseTool. All Rights Reserved.
//
// Spike implementation for docs/architecture.md §7 / docs/spike_ue54.md.
// Read-only by construction.

#include "Commandlets/BPATSpikeCommandlet.h"
#include "BPATErrorCodes.h"
#include "BPATGlobals.h"
#include "BPATLog.h"
#include "Util/BPATOutputDirManager.h"
#include "Util/BPATPathPolicy.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "Animation/AnimBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "WidgetBlueprint.h"

#if WITH_EDITOR
#include "AnimGraphNode_Base.h"
#endif

namespace
{
	FString JoinPath(const FString& Dir, const FString& File)
	{
		return FPaths::Combine(Dir, File);
	}

	void WriteJsonObject(const FString& AbsPath, const TSharedRef<FJsonObject>& Obj)
	{
		FBPATPathPolicy::AssertWritable(AbsPath);

		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		FFileHelper::SaveStringToFile(Out, *AbsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	TSharedPtr<FJsonObject> NewObj() { return MakeShared<FJsonObject>(); }

	TSharedPtr<FJsonValue> StrV(const FString& S) { return MakeShared<FJsonValueString>(S); }
	TSharedPtr<FJsonValue> IntV(int64 V) { return MakeShared<FJsonValueNumber>((double)V); }
	TSharedPtr<FJsonValue> BoolV(bool B) { return MakeShared<FJsonValueBoolean>(B); }
}

UBPATSpikeCommandlet::UBPATSpikeCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UBPATSpikeCommandlet::Main(const FString& Params)
{
	UE_LOG(LogBPAT, Log, TEXT("BPATSpike starting. Schema=%s"), BPATGlobals::SchemaVersion);

	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamsMap;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	const FString OutputDir = ParamsMap.FindRef(TEXT("OutputDir"));
	if (OutputDir.IsEmpty())
	{
		UE_LOG(LogBPAT, Error, TEXT("missing -OutputDir"));
		return BPATExitCodes::InvalidArguments;
	}

	const FBPATOutputLayout Layout = FBPATOutputDirManager::Prepare(
		OutputDir, FPaths::GetProjectFilePath(),
		BPATOverwritePolicy::Overwrite, /*bDryRun=*/false);

	const FString SpikeDir = FPaths::Combine(Layout.ProjectOutputDirAbs, TEXT("spike"));
	IFileManager::Get().MakeDirectory(*SpikeDir, /*Tree=*/true);

	const FString AssetPath       = ParamsMap.FindRef(TEXT("AssetPath"));
	const FString WidgetAssetPath = ParamsMap.FindRef(TEXT("WidgetAssetPath"));
	const FString AnimAssetPath   = ParamsMap.FindRef(TEXT("AnimAssetPath"));
	const FString LevelMaps       = ParamsMap.FindRef(TEXT("LevelMaps"));

	TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
	auto MarkV = [&](const TCHAR* Key, bool bOk, const TCHAR* Note = nullptr)
	{
		TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetBoolField(TEXT("ok"), bOk);
		if (Note) S->SetStringField(TEXT("note"), Note);
		Summary->SetObjectField(Key, S);
	};

	// =====================================================================
	// V1 — Save / Dirty hooks
	// =====================================================================
	{
		TArray<FString> DirtyHits;
		TArray<FString> SavedHits;

		FDelegateHandle DirtyHandle = UPackage::PackageMarkedDirtyEvent.AddLambda(
			[&](UPackage* Pkg, bool /*bWasDirty*/)
			{
				if (Pkg) DirtyHits.Add(Pkg->GetName());
			});

		FDelegateHandle SavedHandle = UPackage::PackageSavedWithContextEvent.AddLambda(
			[&](const FString& /*FileName*/, UPackage* Pkg, FObjectPostSaveContext)
			{
				if (Pkg) SavedHits.Add(Pkg->GetName());
			});

		// 不主动写任何 /Game/ 包；就只是注册 + 立刻取消，确认编译通过。
		UPackage::PackageMarkedDirtyEvent.Remove(DirtyHandle);
		UPackage::PackageSavedWithContextEvent.Remove(SavedHandle);

		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Empty;
		O->SetArrayField(TEXT("dirty_hits"), Empty);
		O->SetArrayField(TEXT("saved_hits"), Empty);
		O->SetBoolField(TEXT("hooks_compiled"), true);
		WriteJsonObject(JoinPath(SpikeDir, TEXT("v1_hooks.json")), O);
		MarkV(TEXT("V1"), true, TEXT("hooks compiled and registerable"));
	}

	// =====================================================================
	// V2 — AssetRegistry FTopLevelAssetPath
	// =====================================================================
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
			TEXT("AssetRegistry"));
		IAssetRegistry& AR = ARM.Get();

		FARFilter Filter;
		Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")));
		Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/UMG"),    TEXT("WidgetBlueprint")));
		Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("AnimBlueprint")));
		Filter.PackagePaths.Add(FName(TEXT("/Game")));
		Filter.bRecursivePaths = true;

		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);

		TArray<TSharedPtr<FJsonValue>> Items;
		for (const FAssetData& A : Assets)
		{
			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("asset_path"),       A.GetObjectPathString());
			J->SetStringField(TEXT("asset_class_path"), A.AssetClassPath.ToString());
			Items.Add(MakeShared<FJsonValueObject>(J));
		}
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("count"), Items.Num());
		O->SetArrayField(TEXT("assets"), Items);
		WriteJsonObject(JoinPath(SpikeDir, TEXT("v2_assets.json")), O);
		MarkV(TEXT("V2"), Assets.Num() > 0, TEXT("FTopLevelAssetPath filter works"));
	}

	// =====================================================================
	// V8 — LOAD_DisableCompileOnLoad behavior + V3 — Latent metadata classification
	//      Both run on the same fixture BP if -AssetPath provided.
	// =====================================================================
	UBlueprint* SpikeBP = nullptr;
	if (!AssetPath.IsEmpty())
	{
		const uint32 LoadFlags = LOAD_NoWarn | LOAD_DisableCompileOnLoad;
		SpikeBP = LoadObject<UBlueprint>(nullptr, *AssetPath, nullptr, LoadFlags);

		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("asset_path"), AssetPath);
		O->SetBoolField(TEXT("load_ok"), SpikeBP != nullptr);
		O->SetStringField(TEXT("load_flags"),
			TEXT("LOAD_NoWarn | LOAD_DisableCompileOnLoad"));
		if (SpikeBP)
		{
			O->SetBoolField(TEXT("package_dirty_after_load"),
				SpikeBP->GetOutermost()->IsDirty());
			O->SetStringField(TEXT("parent_class"),
				SpikeBP->ParentClass ? SpikeBP->ParentClass->GetPathName() : TEXT(""));
		}
		WriteJsonObject(JoinPath(SpikeDir, TEXT("v8_load_disable_compile.json")), O);
		MarkV(TEXT("V8"), SpikeBP != nullptr, TEXT("load with LOAD_DisableCompileOnLoad"));

		// V3 — latent classification on EventGraphs
		TArray<TSharedPtr<FJsonValue>> Items;
		if (SpikeBP)
		{
			TArray<UEdGraph*> AllGraphs;
			AllGraphs.Append(SpikeBP->UbergraphPages);
			AllGraphs.Append(SpikeBP->FunctionGraphs);
			for (UEdGraph* G : AllGraphs)
			{
				if (!G) continue;
				for (UEdGraphNode* N : G->Nodes)
				{
					if (auto* Call = Cast<UK2Node_CallFunction>(N))
					{
						UFunction* F = Call->GetTargetFunction();
						const bool bIsLatent = F && F->HasMetaData(TEXT("Latent"));
						TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
						J->SetStringField(TEXT("graph"),       G->GetName());
						J->SetStringField(TEXT("node_class"),  N->GetClass()->GetName());
						J->SetStringField(TEXT("function"),    F ? F->GetName() : TEXT(""));
						J->SetBoolField(TEXT("is_latent"),     bIsLatent);
						Items.Add(MakeShared<FJsonValueObject>(J));
					}
				}
			}
		}
		TSharedRef<FJsonObject> O3 = MakeShared<FJsonObject>();
		O3->SetArrayField(TEXT("call_function_nodes"), Items);
		WriteJsonObject(JoinPath(SpikeDir, TEXT("v3_latent_classification.json")), O3);
		MarkV(TEXT("V3"), true, TEXT("HasMetaData('Latent') iterated"));
	}
	else
	{
		MarkV(TEXT("V8"), false, TEXT("skipped: no -AssetPath"));
		MarkV(TEXT("V3"), false, TEXT("skipped: no -AssetPath"));
	}

	// =====================================================================
	// V5 — WidgetTree iteration
	// =====================================================================
	if (!WidgetAssetPath.IsEmpty())
	{
		const uint32 LoadFlags = LOAD_NoWarn | LOAD_DisableCompileOnLoad;
		UWidgetBlueprint* WBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetAssetPath, nullptr, LoadFlags);

		TArray<TSharedPtr<FJsonValue>> Items;
		bool bHasTree = false;
		if (WBP && WBP->WidgetTree)
		{
			bHasTree = true;
			WBP->WidgetTree->ForEachWidgetAndDescendants([&Items](UWidget* W)
			{
				if (!W) return;
				TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
				J->SetStringField(TEXT("name"),  W->GetName());
				J->SetStringField(TEXT("class"), W->GetClass()->GetPathName());
				Items.Add(MakeShared<FJsonValueObject>(J));
			});
		}
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("asset_path"), WidgetAssetPath);
		O->SetBoolField(TEXT("has_widget_tree"), bHasTree);
		O->SetNumberField(TEXT("widget_count"), Items.Num());
		O->SetArrayField(TEXT("widgets"), Items);
		WriteJsonObject(JoinPath(SpikeDir, TEXT("v5_widget_tree.json")), O);
		MarkV(TEXT("V5"), bHasTree, TEXT("WidgetTree::ForEachWidgetAndDescendants"));
	}
	else
	{
		MarkV(TEXT("V5"), false, TEXT("skipped: no -WidgetAssetPath"));
	}

	// =====================================================================
	// V6 — AnimBlueprint AnimGraph identification
	// =====================================================================
	if (!AnimAssetPath.IsEmpty())
	{
#if WITH_EDITOR
		const uint32 LoadFlags = LOAD_NoWarn | LOAD_DisableCompileOnLoad;
		UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimAssetPath, nullptr, LoadFlags);

		TArray<TSharedPtr<FJsonValue>> Graphs;
		if (AnimBP)
		{
			for (UEdGraph* G : AnimBP->FunctionGraphs)
			{
				if (!G) continue;
				int32 AnimNodeCount = 0;
				for (UEdGraphNode* N : G->Nodes)
				{
					if (Cast<UAnimGraphNode_Base>(N)) ++AnimNodeCount;
				}
				TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
				J->SetStringField(TEXT("graph_name"),     G->GetName());
				J->SetNumberField(TEXT("node_count"),     G->Nodes.Num());
				J->SetNumberField(TEXT("anim_node_count"),AnimNodeCount);
				Graphs.Add(MakeShared<FJsonValueObject>(J));
			}
		}
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("asset_path"), AnimAssetPath);
		O->SetBoolField(TEXT("load_ok"), AnimBP != nullptr);
		O->SetArrayField(TEXT("graphs"), Graphs);
		WriteJsonObject(JoinPath(SpikeDir, TEXT("v6_anim_graph.json")), O);
		MarkV(TEXT("V6"), AnimBP != nullptr, TEXT("UAnimGraphNode_Base cast classification"));
#else
		MarkV(TEXT("V6"), false, TEXT("skipped: WITH_EDITOR=0"));
#endif
	}
	else
	{
		MarkV(TEXT("V6"), false, TEXT("skipped: no -AnimAssetPath"));
	}

	// =====================================================================
	// V7 — Level Blueprint (EXPLICIT only, default skip)
	// =====================================================================
	if (LevelMaps.IsEmpty())
	{
		MarkV(TEXT("V7"), false, TEXT("skipped: no -LevelMaps (intentional default)"));
	}
	else
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("note"),
			TEXT("Level Blueprint loading is heavy; this spike only records the request. "
			     "Implement actual LoadMap in Authoring phase or behind explicit opt-in."));
		O->SetStringField(TEXT("requested_maps"), LevelMaps);
		WriteJsonObject(JoinPath(SpikeDir, TEXT("v7_level_blueprint.json")), O);
		MarkV(TEXT("V7"), true, TEXT("requested but not loaded; opt-in only"));
	}

	// =====================================================================
	// V4 — Serial loading sanity (degenerate: just confirm GC stable)
	// =====================================================================
	{
		const bool bGCInProgress = IsGarbageCollecting();
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("gc_in_progress"), bGCInProgress);
		O->SetStringField(TEXT("policy"), TEXT("serial-only; no parallel load attempted"));
		WriteJsonObject(JoinPath(SpikeDir, TEXT("v4_load_stats.json")), O);
		MarkV(TEXT("V4"), !bGCInProgress, TEXT("serial-only policy"));
	}

	// =====================================================================
	// Summary
	// =====================================================================
	WriteJsonObject(JoinPath(SpikeDir, TEXT("summary.json")), Summary);

	UE_LOG(LogBPAT, Log, TEXT("BPATSpike finished. See %s"), *SpikeDir);
	return BPATExitCodes::AllSuccess;
}
