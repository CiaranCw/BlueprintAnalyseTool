// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPPreflight.h"
#include "BPWidgetGen.h"
#include "BPGenIRDumper.h"
#include "BPParserTestGenModule.h"

#include "Engine/Blueprint.h"
#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/CanvasPanelSlot.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	FString JStr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FString& Def = FString())
	{
		FString V; return (O.IsValid() && O->TryGetStringField(Key, V)) ? V : Def;
	}
	bool JBool(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, bool Def)
	{
		bool V; return (O.IsValid() && O->TryGetBoolField(Key, V)) ? V : Def;
	}
	const TSharedPtr<FJsonObject>* JObj(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* P = nullptr;
		return (O.IsValid() && O->TryGetObjectField(Key, P)) ? P : nullptr;
	}
	const TArray<TSharedPtr<FJsonValue>>* JArr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		return (O.IsValid() && O->TryGetArrayField(Key, A)) ? A : nullptr;
	}

	void WriteJson(const FString& Path, const TSharedPtr<FJsonObject>& Root)
	{
		if (Path.IsEmpty() || !Root.IsValid()) { return; }
		const FString Dir = FPaths::GetPath(Path);
		if (!Dir.IsEmpty()) { IFileManager::Get().MakeDirectory(*Dir, true); }
		FString Out;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Root.ToSharedRef(), W);
		FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	TSharedPtr<FJsonObject> MatchToJson(const FBPPropertyMatchResult& M)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("input"), M.InputName);
		O->SetStringField(TEXT("resolved_to"), M.ResolvedName);
		O->SetStringField(TEXT("match_kind"), FBPWidgetGen::PropertyMatchKindToString(M.Kind));
		if (!M.MatchDetail.IsEmpty()) { O->SetStringField(TEXT("match_detail"), M.MatchDetail); }
		if (!M.DeclaringClass.IsEmpty()) { O->SetStringField(TEXT("declaring_class"), M.DeclaringClass); }
		if (!M.TypeCategory.IsEmpty()) { O->SetStringField(TEXT("type_category"), M.TypeCategory); }
		O->SetBoolField(TEXT("set_supported"), M.bSetSupported);
		if (!M.TypeCheckError.IsEmpty()) { O->SetStringField(TEXT("type_check_error"), M.TypeCheckError); }
		if (M.Suggestions.Num() > 0) { O->SetArrayField(TEXT("suggestions"), M.Suggestions); }
		return O;
	}

	bool IsOptionalProp(const TSharedPtr<FJsonObject>& Op, const FString& PropName)
	{
		if (const TArray<TSharedPtr<FJsonValue>>* Opt = JArr(Op, TEXT("optional_properties")))
		{
			for (const TSharedPtr<FJsonValue>& V : *Opt)
			{
				FString S; if (V.IsValid() && V->TryGetString(S) && S.Equals(PropName, ESearchCase::IgnoreCase)) { return true; }
			}
		}
		if (const TSharedPtr<FJsonObject>* Sem = JObj(Op, TEXT("property_semantics")))
		{
			FString SemVal;
			if ((*Sem)->TryGetStringField(PropName, SemVal) && SemVal.Equals(TEXT("optional"), ESearchCase::IgnoreCase)) { return true; }
		}
		return false;
	}

	UWidget* FindWidgetByName(UWidgetBlueprint* WBP, const FString& Name)
	{
		if (!WBP || !WBP->WidgetTree || Name.IsEmpty()) { return nullptr; }
		TArray<UWidget*> All;
		WBP->WidgetTree->GetAllWidgets(All);
		for (UWidget* W : All) { if (W && W->GetName() == Name) { return W; } }
		return nullptr;
	}

	void CollectClassCapability(UClass* Cls, UObject* Instance, const FString& Label, TArray<TSharedPtr<FJsonValue>>& ClassesOut)
	{
		if (!Cls) { return; }
		TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetStringField(TEXT("label"), Label);
		C->SetStringField(TEXT("class"), Cls->GetPathName());
		C->SetArrayField(TEXT("settable_properties"), FBPWidgetGen::ListSettableProperties(Cls, Instance));
		TArray<TSharedPtr<FJsonValue>> Events;
		for (FMulticastDelegateProperty* D : FBPWidgetGen::GetBindableDelegates(Cls))
		{
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("event_name"), D->GetName());
			E->SetArrayField(TEXT("parameters"), FBPWidgetGen::DescribeDelegateParams(D));
			Events.Add(MakeShared<FJsonValueObject>(E));
		}
		C->SetArrayField(TEXT("bindable_events"), Events);
		ClassesOut.Add(MakeShared<FJsonValueObject>(C));
	}

	void WalkHierarchyProps(const TSharedPtr<FJsonObject>& Node, UWidgetBlueprint* WBP,
		const TSharedPtr<FJsonObject>& OpCtx, TArray<TSharedPtr<FJsonValue>>& Checks, bool& bAnyFail, bool& bAnyWarn, bool bStrict)
	{
		if (!Node.IsValid()) { return; }
		const FString WType = JStr(Node, TEXT("type"));
		const FString WName = JStr(Node, TEXT("name"));
		FString RErr, RAsset, RGen; bool bCustom = false;
		UClass* WClass = FBPWidgetGen::ResolveWidgetClassEx(WType, RErr, bCustom, RAsset, RGen);
		if (!WClass)
		{
			TSharedPtr<FJsonObject> Ch = MakeShared<FJsonObject>();
			Ch->SetStringField(TEXT("context"), TEXT("create_widget"));
			Ch->SetStringField(TEXT("widget"), WName);
			Ch->SetStringField(TEXT("type"), WType);
			Ch->SetStringField(TEXT("status"), TEXT("class_unresolved"));
			Ch->SetStringField(TEXT("error"), RErr);
			Checks.Add(MakeShared<FJsonValueObject>(Ch));
			bAnyFail = true;
		}
		else if (const TSharedPtr<FJsonObject>* Props = JObj(Node, TEXT("properties")))
		{
			UObject* Target = WClass->GetDefaultObject();
			for (const auto& KV : (*Props)->Values)
			{
				const bool bOpt = IsOptionalProp(OpCtx, KV.Key);
				FBPPropertyMatchResult M = FBPWidgetGen::ResolvePropertyMatch(Target, KV.Key, KV.Value);
				TSharedPtr<FJsonObject> Ch = MatchToJson(M);
				Ch->SetStringField(TEXT("context"), TEXT("create_widget_property"));
				Ch->SetStringField(TEXT("widget"), WName);
				Ch->SetStringField(TEXT("widget_class"), WClass->GetPathName());
				Ch->SetBoolField(TEXT("required"), !bOpt);
				const bool bOk = (M.Kind == EBPPropertyMatchKind::ExactMatch || M.Kind == EBPPropertyMatchKind::AliasMatch
					|| M.Kind == EBPPropertyMatchKind::DisplayNameMatch);
				if (bOk) { Ch->SetStringField(TEXT("status"), TEXT("pass")); }
				else if (bOpt) { Ch->SetStringField(TEXT("status"), TEXT("skipped_optional")); bAnyWarn = true; }
				else { Ch->SetStringField(TEXT("status"), TEXT("fail")); if (bStrict) { bAnyFail = true; } else { bAnyWarn = true; } }
				Checks.Add(MakeShared<FJsonValueObject>(Ch));
			}
		}
		if (const TSharedPtr<FJsonObject>* SlotObj = JObj(Node, TEXT("slot")))
		{
			if (const TSharedPtr<FJsonObject>* SP = JObj(*SlotObj, TEXT("properties")))
			{
				// slot class unknown until parent panel type known — skip detailed slot preflight in create walk
			}
		}
		if (const TArray<TSharedPtr<FJsonValue>>* Kids = JArr(Node, TEXT("children")))
		{
			for (const TSharedPtr<FJsonValue>& V : *Kids) { if (auto O = V->AsObject()) WalkHierarchyProps(O, WBP, OpCtx, Checks, bAnyFail, bAnyWarn, bStrict); }
		}
	}
}

bool FBPPreflight::IsPreflightApplyAllowed(int32 PreflightExitCode)
{
	return PreflightExitCode == 0 || PreflightExitCode == 10;
}

FString FBPPreflight::ComputeIrHash(const TSharedPtr<FJsonObject>& Ir)
{
	if (!Ir.IsValid()) { return TEXT(""); }
	FString Serialized;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialized);
	FJsonSerializer::Serialize(Ir.ToSharedRef(), W);
	FSHAHash Hash;
	FSHA1::HashBuffer(TCHAR_TO_UTF8(*Serialized), FTCHARToUTF8(*Serialized).Length(), Hash.Hash);
	return Hash.ToString();
}

int32 FBPPreflight::RunPreflight(const FString& TaskType, const TSharedPtr<FJsonObject>& Request, const FOptions& Opt,
	TSharedPtr<FJsonObject>& OutReport, TSharedPtr<FJsonObject>& OutNormalized, TSharedPtr<FJsonObject>& OutCapability)
{
	OutReport = MakeShared<FJsonObject>();
	OutNormalized = Request.IsValid() ? Request : MakeShared<FJsonObject>();
	OutCapability = MakeShared<FJsonObject>();

	OutReport->SetStringField(TEXT("schema_version"), TEXT("1.0"));
	OutReport->SetStringField(TEXT("task_type"), TaskType);
	OutReport->SetStringField(TEXT("asset_path"), Opt.AssetPath);
	OutReport->SetStringField(TEXT("generated_at"), FDateTime::UtcNow().ToIso8601());

	TArray<TSharedPtr<FJsonValue>> Checks;
	bool bAnyFail = false;
	bool bAnyWarn = false;
	TArray<TSharedPtr<FJsonValue>> CapClasses;

	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Opt.LoadedBP);
	if (!WBP && !Opt.AssetPath.IsEmpty())
	{
		WBP = Cast<UWidgetBlueprint>(LoadObject<UBlueprint>(nullptr, *Opt.AssetPath));
	}

	if (WBP)
	{
		TSharedPtr<FJsonObject> Baseline = FBPGenIRDumper::DumpBlueprint(WBP);
		const FString Hash = ComputeIrHash(Baseline);
		OutReport->SetStringField(TEXT("baseline_ir_hash"), Hash);
		OutCapability->SetStringField(TEXT("baseline_ir_hash"), Hash);
		if (const TSharedPtr<FJsonObject>* WT = JObj(Baseline, TEXT("widget_tree")))
		{
			OutCapability->SetObjectField(TEXT("widget_tree"), *WT);
		}
	}

	const FString Task = TaskType.ToLower();
	if (Task == TEXT("edit"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Ops = nullptr;
		if (Request.IsValid()) { Request->TryGetArrayField(TEXT("operations"), Ops); }
		if (!Ops)
		{
			OutReport->SetStringField(TEXT("status"), TEXT("failed"));
			OutReport->SetStringField(TEXT("reason"), TEXT("no operations array"));
			return 30;
		}

		TSharedPtr<FJsonObject> NormOps = MakeShared<FJsonObject>();
		NormOps->SetStringField(TEXT("schema_version"), TEXT("1.0"));
		TArray<TSharedPtr<FJsonValue>> NormOpList;

		for (const TSharedPtr<FJsonValue>& OV : *Ops)
		{
			const TSharedPtr<FJsonObject> Op = OV->AsObject();
			if (!Op.IsValid()) { continue; }
			const FString OpName = JStr(Op, TEXT("operation"));
			TSharedPtr<FJsonObject> NormOp = MakeShared<FJsonObject>();
			for (const auto& KV : Op->Values) { NormOp->SetField(KV.Key, KV.Value); }

			if (OpName == TEXT("set_widget_property"))
			{
				const FString WidgetName = JStr(Op, TEXT("widget"));
				const FString Prop = JStr(Op, TEXT("property"));
				UWidget* W = WBP ? FindWidgetByName(WBP, WidgetName) : nullptr;
				const bool bOpt = IsOptionalProp(Op, Prop);
				FBPPropertyMatchResult M = W
					? FBPWidgetGen::ResolvePropertyMatch(W, Prop, Op->TryGetField(TEXT("value")))
					: FBPPropertyMatchResult{};
				if (!W) { M.Kind = EBPPropertyMatchKind::PropertyAbsent; M.InputName = Prop; M.TypeCheckError = TEXT("widget_not_found_in_target"); }

				TSharedPtr<FJsonObject> Ch = MatchToJson(M);
				Ch->SetStringField(TEXT("context"), TEXT("set_widget_property"));
				Ch->SetStringField(TEXT("op_id"), JStr(Op, TEXT("op_id")));
				Ch->SetStringField(TEXT("widget"), WidgetName);
				Ch->SetBoolField(TEXT("required"), !bOpt);
				const bool bOk = W && (M.Kind == EBPPropertyMatchKind::ExactMatch || M.Kind == EBPPropertyMatchKind::AliasMatch || M.Kind == EBPPropertyMatchKind::DisplayNameMatch);
				if (!W && !bOpt) { Ch->SetStringField(TEXT("status"), TEXT("fail")); bAnyFail = true; }
				else if (!bOk && bOpt) { Ch->SetStringField(TEXT("status"), TEXT("skipped_optional")); bAnyWarn = true; NormOp->SetBoolField(TEXT("_preflight_skip"), true); }
				else if (!bOk) { Ch->SetStringField(TEXT("status"), TEXT("fail")); bAnyFail = Opt.bStrictRequired; if (!Opt.bStrictRequired) bAnyWarn = true; }
				else
				{
					Ch->SetStringField(TEXT("status"), TEXT("pass"));
					if (!M.ResolvedName.IsEmpty() && !M.ResolvedName.Equals(Prop)) { NormOp->SetStringField(TEXT("property"), M.ResolvedName); }
				}
				Checks.Add(MakeShared<FJsonValueObject>(Ch));
			}
			else if (OpName == TEXT("set_slot_property"))
			{
				const FString WidgetName = JStr(Op, TEXT("widget"));
				const FString Prop = JStr(Op, TEXT("property"));
				UWidget* W = WBP ? FindWidgetByName(WBP, WidgetName) : nullptr;
				UPanelSlot* Slot = W ? W->Slot : nullptr;
				const bool bOpt = IsOptionalProp(Op, Prop);
				FBPPropertyMatchResult M = Slot
					? FBPWidgetGen::ResolvePropertyMatch(Slot, Prop, Op->TryGetField(TEXT("value")))
					: FBPPropertyMatchResult{};
				if (!Slot) { M.Kind = EBPPropertyMatchKind::PropertyAbsent; M.InputName = Prop; M.TypeCheckError = W ? TEXT("widget_has_no_slot") : TEXT("widget_not_found"); }

				TSharedPtr<FJsonObject> Ch = MatchToJson(M);
				Ch->SetStringField(TEXT("context"), TEXT("set_slot_property"));
				Ch->SetStringField(TEXT("widget"), WidgetName);
				if (Slot && Cast<UCanvasPanelSlot>(Slot))
				{
					if (TSharedPtr<FJsonObject> Geo = FBPWidgetGen::CanvasSlotGeometrySemantics(Slot))
					{
						Ch->SetObjectField(TEXT("geometry_semantics"), Geo);
					}
				}
				const bool bOk = Slot && (M.Kind == EBPPropertyMatchKind::ExactMatch || M.Kind == EBPPropertyMatchKind::AliasMatch || M.Kind == EBPPropertyMatchKind::DisplayNameMatch);
				Ch->SetBoolField(TEXT("required"), !bOpt);
				if (!bOk && bOpt) { Ch->SetStringField(TEXT("status"), TEXT("skipped_optional")); bAnyWarn = true; }
				else if (!bOk) { Ch->SetStringField(TEXT("status"), TEXT("fail")); bAnyFail = Opt.bStrictRequired; if (!Opt.bStrictRequired) bAnyWarn = true; }
				else { Ch->SetStringField(TEXT("status"), TEXT("pass")); if (!M.ResolvedName.IsEmpty()) NormOp->SetStringField(TEXT("property"), M.ResolvedName); }
				Checks.Add(MakeShared<FJsonValueObject>(Ch));
			}
			else if (OpName == TEXT("add_widget"))
			{
				const TSharedPtr<FJsonObject>* WObj = JObj(Op, TEXT("widget"));
				if (WObj)
				{
					WalkHierarchyProps(*WObj, WBP, Op, Checks, bAnyFail, bAnyWarn, Opt.bStrictRequired);
					const FString WType = JStr(*WObj, TEXT("type"));
					FString RErr, RAsset, RGen; bool bCustom = false;
					if (UClass* Cls = FBPWidgetGen::ResolveWidgetClassEx(WType, RErr, bCustom, RAsset, RGen))
					{
						CollectClassCapability(Cls, Cls->GetDefaultObject(), JStr(*WObj, TEXT("name")), CapClasses);
					}
				}
			}
			else if (OpName == TEXT("bind_widget_event"))
			{
				const FString WidgetName = JStr(Op, TEXT("widget"));
				const FString Ev = JStr(Op, TEXT("event"));
				UWidget* W = WBP ? FindWidgetByName(WBP, WidgetName) : nullptr;
				TSharedPtr<FJsonObject> Ch = MakeShared<FJsonObject>();
				Ch->SetStringField(TEXT("context"), TEXT("bind_widget_event"));
				Ch->SetStringField(TEXT("widget"), WidgetName);
				Ch->SetStringField(TEXT("event"), Ev);
				if (!W)
				{
					// widget will be created by prior add_widget op — check class from add_widget in same plan if present
					Ch->SetStringField(TEXT("status"), TEXT("pending_widget"));
					Ch->SetStringField(TEXT("note"), TEXT("widget not in tree yet; ensure add_widget precedes bind"));
					bAnyWarn = true;
				}
				else if (FMulticastDelegateProperty* D = FBPWidgetGen::FindBindableDelegate(W->GetClass(), Ev))
				{
					Ch->SetStringField(TEXT("status"), TEXT("pass"));
					Ch->SetStringField(TEXT("delegate_property"), D->GetName());
					Ch->SetArrayField(TEXT("parameters"), FBPWidgetGen::DescribeDelegateParams(D));
				}
				else
				{
					Ch->SetStringField(TEXT("status"), TEXT("fail"));
					Ch->SetStringField(TEXT("match_kind"), TEXT("property_absent"));
					bAnyFail = true;
				}
				Checks.Add(MakeShared<FJsonValueObject>(Ch));
			}

			NormOpList.Add(MakeShared<FJsonValueObject>(NormOp));
		}

		OutNormalized = MakeShared<FJsonObject>();
		for (const auto& KV : Request->Values)
		{
			if (KV.Key != TEXT("operations")) { OutNormalized->SetField(KV.Key, KV.Value); }
		}
		OutNormalized->SetArrayField(TEXT("operations"), NormOpList);
	}
	else if (Task == TEXT("create"))
	{
		if (const TSharedPtr<FJsonObject>* WObj = JObj(Request, TEXT("widget")))
		{
			if (const TSharedPtr<FJsonObject>* Hier = JObj(*WObj, TEXT("hierarchy")))
			{
				const TSharedPtr<FJsonObject>* RootWrap = JObj(*Hier, TEXT("root"));
				TSharedPtr<FJsonObject> RootNode = RootWrap ? *RootWrap : *Hier;
				TSharedPtr<FJsonObject> EmptyOp = MakeShared<FJsonObject>();
				WalkHierarchyProps(RootNode, nullptr, EmptyOp, Checks, bAnyFail, bAnyWarn, Opt.bStrictRequired);
			}
		}
		if (const TSharedPtr<FJsonObject>* Asset = JObj(Request, TEXT("asset")))
		{
			const FString ParentPath = JStr(*Asset, TEXT("parent_class"));
			if (!ParentPath.IsEmpty())
			{
				UClass* PC = LoadObject<UClass>(nullptr, *ParentPath);
				if (!PC) { PC = FindObject<UClass>(nullptr, *ParentPath); }
				if (PC) { CollectClassCapability(PC, PC->GetDefaultObject(), TEXT("parent_class"), CapClasses); }
			}
		}
		OutNormalized = Request;
	}
	else
	{
		OutReport->SetStringField(TEXT("status"), TEXT("failed"));
		OutReport->SetStringField(TEXT("reason"), FString::Printf(TEXT("unsupported task_type '%s'"), *TaskType));
		return 30;
	}

	OutCapability->SetStringField(TEXT("schema_version"), TEXT("1.0"));
	OutCapability->SetArrayField(TEXT("classes"), CapClasses);

	OutReport->SetArrayField(TEXT("checks"), Checks);
	FString Status = TEXT("pass");
	int32 Code = 0;
	if (bAnyFail) { Status = TEXT("fail"); Code = 20; }
	else if (bAnyWarn) { Status = TEXT("pass_with_warnings"); Code = 10; }
	OutReport->SetStringField(TEXT("status"), Status);
	OutReport->SetBoolField(TEXT("apply_allowed"), IsPreflightApplyAllowed(Code));

	if (!Opt.OutputDir.IsEmpty())
	{
		WriteJson(FPaths::Combine(Opt.OutputDir, TEXT("preflight_report.json")), OutReport);
		WriteJson(FPaths::Combine(Opt.OutputDir, TEXT("normalized_request.json")), OutNormalized);
		WriteJson(FPaths::Combine(Opt.OutputDir, TEXT("capability_snapshot.json")), OutCapability);
	}

	return Code;
}
