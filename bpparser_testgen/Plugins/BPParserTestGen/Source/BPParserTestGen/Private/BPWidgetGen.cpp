// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPWidgetGen.h"
#include "BPParserTestGenModule.h"

#include "Engine/Blueprint.h"                 // UBlueprint (custom widget generated-class fallback)
#include "WidgetBlueprint.h"                 // UWidgetBlueprint (UMGEditor)
#include "WidgetBlueprintFactory.h"          // UWidgetBlueprintFactory (UMGEditor)
#include "Blueprint/UserWidget.h"            // UUserWidget (UMG)
#include "Blueprint/WidgetTree.h"            // UWidgetTree (UMG)
#include "Components/Widget.h"               // UWidget
#include "Components/PanelWidget.h"          // UPanelWidget
#include "Components/PanelSlot.h"            // UPanelSlot

#include "BPGen.h"                           // FBPGen::GetEventGraph
#include "AssetRegistry/AssetRegistryModule.h"
#include "JsonObjectConverter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Misc/PackageName.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"

UWidgetBlueprint* FBPWidgetGen::CreateWidgetBlueprint(const FString& AssetPath, UClass* ParentClass)
{
	const FString AssetName = FPackageName::GetShortName(AssetPath);
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package) { UE_LOG(LogBPParserTestGen, Error, TEXT("BPWidgetGen: CreatePackage failed for %s"), *AssetPath); return nullptr; }

	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = (ParentClass && ParentClass->IsChildOf(UUserWidget::StaticClass())) ? ParentClass : UUserWidget::StaticClass();

	UObject* Created = Factory->FactoryCreateNew(
		UWidgetBlueprint::StaticClass(), Package, FName(*AssetName),
		RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn);

	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Created);
	if (!WBP) { UE_LOG(LogBPParserTestGen, Error, TEXT("BPWidgetGen: factory did not produce a UWidgetBlueprint for %s"), *AssetPath); return nullptr; }

	FAssetRegistryModule::AssetCreated(WBP);
	Package->MarkPackageDirty();
	return WBP;
}

UClass* FBPWidgetGen::ResolveWidgetClass(const FString& TypeSpec)
{
	FString Err, Asset, Gen; bool bCustom = false;
	return ResolveWidgetClassEx(TypeSpec, Err, bCustom, Asset, Gen);
}

UClass* FBPWidgetGen::ResolveWidgetClassEx(const FString& TypeSpec, FString& OutError, bool& bOutCustom,
	FString& OutAssetPath, FString& OutGeneratedClass)
{
	OutError.Reset(); bOutCustom = false; OutAssetPath.Reset(); OutGeneratedClass.Reset();
	if (TypeSpec.IsEmpty()) { OutError = TEXT("class_path_invalid"); return nullptr; }

	// Builtin short name (no path separators) -> /Script/UMG.<Name>.
	if (!TypeSpec.Contains(TEXT("/")))
	{
		if (UClass* C = LoadObject<UClass>(nullptr, *(FString(TEXT("/Script/UMG.")) + TypeSpec))) { return C; }
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->IsChildOf(UWidget::StaticClass()) && It->GetName() == TypeSpec) { return *It; }
		}
		OutError = TEXT("class_load_failed");
		return nullptr;
	}

	// Path form (engine /Script/... class OR custom /Game asset). Normalize to the generated class.
	bOutCustom = TypeSpec.StartsWith(TEXT("/Game/"));
	FString Pkg = TypeSpec; int32 Dot;
	if (TypeSpec.FindChar('.', Dot)) { Pkg = TypeSpec.Left(Dot); }            // strip object/_C suffix -> package path
	const FString Short = FPackageName::GetShortName(Pkg);
	OutAssetPath = Pkg;
	const FString GenObjPath = Pkg + TEXT(".") + Short + TEXT("_C");          // /Game/UI/WBP_X.WBP_X_C

	UClass* C = LoadObject<UClass>(nullptr, *TypeSpec);                        // as-given (handles _C or /Script.Class)
	if (!C) { C = LoadObject<UClass>(nullptr, *GenObjPath); }                  // derive _C generated class
	if (!C)
	{
		// last resort: load the Blueprint object and take its GeneratedClass
		if (UObject* Obj = LoadObject<UObject>(nullptr, *(Pkg + TEXT(".") + Short)))
		{
			if (UBlueprint* BP = Cast<UBlueprint>(Obj)) { C = BP->GeneratedClass; }
			else if (UClass* AsClass = Cast<UClass>(Obj)) { C = AsClass; }
		}
	}
	if (!C) { OutError = TEXT("class_load_failed"); return nullptr; }

	if (!C->IsChildOf(UWidget::StaticClass())) { OutError = TEXT("not_user_widget"); return nullptr; }
	if (C->HasAnyClassFlags(CLASS_Abstract))   { OutError = TEXT("not_user_widget"); return nullptr; }

	OutGeneratedClass = C->GetPathName();
	return C;
}

UWidget* FBPWidgetGen::ConstructWidget(UWidgetBlueprint* WBP, UClass* WidgetClass, const FName Name)
{
	if (!WBP || !WBP->WidgetTree || !WidgetClass || !WidgetClass->IsChildOf(UWidget::StaticClass())) { return nullptr; }
	UWidget* W = WBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass, Name);
	if (W) { W->bIsVariable = true; }   // named + accessible in the graph / Details
	return W;
}

void FBPWidgetGen::SetRoot(UWidgetBlueprint* WBP, UWidget* Root)
{
	if (WBP && WBP->WidgetTree) { WBP->WidgetTree->RootWidget = Root; }
}

UPanelSlot* FBPWidgetGen::AddChild(UWidget* Parent, UWidget* Child)
{
	UPanelWidget* Panel = Cast<UPanelWidget>(Parent);
	if (!Panel || !Child) { return nullptr; }
	return Panel->AddChild(Child);
}

namespace
{
	// Forward declarations (definitions live in the anonymous namespace further below).
	bool IsListableSettable(FProperty* P);
	TSharedPtr<FJsonObject> PropTypeObj(FProperty* P);
	FProperty* FindPropertyFuzzy(UStruct* Owner, const FString& Name, FString& OutMatched);
	TArray<TSharedPtr<FJsonValue>> SuggestProps(UStruct* Owner, const FString& Name);
}

TArray<TSharedPtr<FJsonValue>> FBPWidgetGen::ListSettableProperties(UStruct* Owner, UObject* Instance)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!Owner) { return Out; }
	for (TFieldIterator<FProperty> It(Owner); It; ++It)
	{
		FProperty* P = *It;
		if (!IsListableSettable(P)) { continue; }
		const bool bEditConst  = P->HasAnyPropertyFlags(CPF_EditConst);
		const bool bTransient  = P->HasAnyPropertyFlags(CPF_Transient);
		const bool bDeprecated = P->HasAnyPropertyFlags(CPF_Deprecated);

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), P->GetName());
		O->SetStringField(TEXT("display_name"), P->GetDisplayNameText().ToString());
		O->SetObjectField(TEXT("type"), PropTypeObj(P));
		O->SetStringField(TEXT("declaring_class"), P->GetOwnerStruct() ? P->GetOwnerStruct()->GetPathName() : TEXT(""));
		O->SetBoolField(TEXT("editable"), true);
		O->SetBoolField(TEXT("blueprint_visible"), P->HasAnyPropertyFlags(CPF_BlueprintVisible));
		O->SetBoolField(TEXT("blueprint_read_only"), P->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
		O->SetBoolField(TEXT("deprecated"), bDeprecated);
		if (Instance)
		{
			FString V; P->ExportTextItem_Direct(V, P->ContainerPtrToValuePtr<void>(Instance), nullptr, Instance, PPF_None);
			O->SetStringField(TEXT("current_value"), V);
		}
		TArray<TSharedPtr<FJsonValue>> Notes;
		bool bSetSupported = true;
		if (bEditConst)  { bSetSupported = false; Notes.Add(MakeShared<FJsonValueString>(TEXT("readonly_or_internal"))); }
		if (bTransient)  { bSetSupported = false; Notes.Add(MakeShared<FJsonValueString>(TEXT("transient"))); }
		if (bDeprecated) { bSetSupported = false; Notes.Add(MakeShared<FJsonValueString>(TEXT("deprecated"))); }
		O->SetBoolField(TEXT("set_supported"), bSetSupported);
		O->SetArrayField(TEXT("notes"), Notes);
		Out.Add(MakeShared<FJsonValueObject>(O));
	}
	return Out;
}

FString FBPWidgetGen::SetPropertyFromJson(UObject* Target, const FString& PropName, const TSharedPtr<FJsonValue>& Value,
	FString& OutResolvedName, TArray<TSharedPtr<FJsonValue>>& OutSuggestions)
{
	OutResolvedName.Reset(); OutSuggestions.Reset();
	if (!Target) { return TEXT("null target"); }
	if (!Value.IsValid()) { return TEXT("null value"); }

	// 1) Direct/fuzzy FProperty (exact -> case-insensitive -> bool `b` prefix -> DisplayName -> normalized).
	FString Matched;
	if (FProperty* Prop = FindPropertyFuzzy(Target->GetClass(), PropName, Matched))
	{
		void* Addr = Prop->ContainerPtrToValuePtr<void>(Target);
		if (!FJsonObjectConverter::JsonValueToUProperty(Value, Prop, Addr, 0, 0))
		{
			return FString::Printf(TEXT("type_mismatch: could not import value into property '%s' (%s)"), *Matched, *Prop->GetClass()->GetName());
		}
		OutResolvedName = Matched;
		return FString();
	}

	// 2) Fallback: a single-input setter UFUNCTION named Set<PropName> (e.g. CanvasPanelSlot Position/Size/...).
	if (UFunction* Fn = Target->FindFunction(FName(*(FString(TEXT("Set")) + PropName))))
	{
		FProperty* Parm = nullptr;
		for (TFieldIterator<FProperty> It(Fn); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			if (It->PropertyFlags & CPF_ReturnParm) { continue; }
			Parm = *It; break;
		}
		if (!Parm) { return FString::Printf(TEXT("setter Set%s has no input parameter"), *PropName); }

		void* Frame = FMemory_Alloca(FMath::Max<int32>(1, Fn->ParmsSize));
		FMemory::Memzero(Frame, Fn->ParmsSize);
		for (TFieldIterator<FProperty> It(Fn); It && (It->PropertyFlags & CPF_Parm); ++It) { It->InitializeValue_InContainer(Frame); }
		const bool bOk = FJsonObjectConverter::JsonValueToUProperty(Value, Parm, Parm->ContainerPtrToValuePtr<void>(Frame), 0, 0);
		if (bOk) { Target->ProcessEvent(Fn, Frame); }
		for (TFieldIterator<FProperty> It(Fn); It && (It->PropertyFlags & CPF_Parm); ++It) { It->DestroyValue_InContainer(Frame); }
		if (!bOk) { return FString::Printf(TEXT("type_mismatch: could not import value into setter Set%s parameter"), *PropName); }
		OutResolvedName = FString(TEXT("Set")) + PropName;
		return FString();
	}

	// 3) Not found -> classified error + candidate suggestions.
	OutSuggestions = SuggestProps(Target->GetClass(), PropName);
	return TEXT("property_not_found");
}

namespace
{
	// Map a delegate/function parameter FProperty to { name, type, sub_category_object? }.
	TSharedPtr<FJsonObject> ParamTypeJson(FProperty* P)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), P->GetName());
		FString Type = TEXT("unknown"); FString SubObj;
		if (CastField<FBoolProperty>(P))            { Type = TEXT("bool"); }
		else if (CastField<FIntProperty>(P))        { Type = TEXT("int"); }
		else if (CastField<FInt64Property>(P))      { Type = TEXT("int64"); }
		else if (CastField<FFloatProperty>(P))      { Type = TEXT("float"); }
		else if (CastField<FDoubleProperty>(P))     { Type = TEXT("double"); }
		else if (CastField<FStrProperty>(P))        { Type = TEXT("string"); }
		else if (CastField<FNameProperty>(P))       { Type = TEXT("name"); }
		else if (CastField<FTextProperty>(P))       { Type = TEXT("text"); }
		else if (const FEnumProperty* EP = CastField<FEnumProperty>(P)) { Type = TEXT("enum"); if (EP->GetEnum()) { SubObj = EP->GetEnum()->GetPathName(); } }
		else if (const FByteProperty* BP = CastField<FByteProperty>(P)) { if (BP->Enum) { Type = TEXT("enum"); SubObj = BP->Enum->GetPathName(); } else { Type = TEXT("byte"); } }
		else if (const FStructProperty* SP = CastField<FStructProperty>(P)) { Type = TEXT("struct"); if (SP->Struct) { SubObj = SP->Struct->GetPathName(); } }
		else if (const FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(P)) { Type = TEXT("object"); if (OP->PropertyClass) { SubObj = OP->PropertyClass->GetPathName(); } }
		O->SetStringField(TEXT("type"), Type);
		if (!SubObj.IsEmpty()) { O->SetStringField(TEXT("sub_category_object"), SubObj); }
		return O;
	}

	FString NormName(const FString& S){ FString O=S.ToLower(); O.ReplaceInline(TEXT(" "),TEXT("")); O.ReplaceInline(TEXT("_"),TEXT("")); return O; }

	// Short category + optional sub_category_object for a (non-container) property.
	FString PropCategory(FProperty* P, FString& OutSubObj)
	{
		OutSubObj.Reset();
		if (CastField<FBoolProperty>(P))   return TEXT("bool");
		if (CastField<FIntProperty>(P))    return TEXT("int");
		if (CastField<FInt64Property>(P))  return TEXT("int64");
		if (CastField<FFloatProperty>(P))  return TEXT("float");
		if (CastField<FDoubleProperty>(P)) return TEXT("double");
		if (CastField<FStrProperty>(P))    return TEXT("string");
		if (CastField<FNameProperty>(P))   return TEXT("name");
		if (CastField<FTextProperty>(P))   return TEXT("text");
		if (const FEnumProperty* EP=CastField<FEnumProperty>(P)) { if(EP->GetEnum()) OutSubObj=EP->GetEnum()->GetPathName(); return TEXT("enum"); }
		if (const FByteProperty* BP=CastField<FByteProperty>(P)) { if(BP->Enum){ OutSubObj=BP->Enum->GetPathName(); return TEXT("enum"); } return TEXT("byte"); }
		if (const FStructProperty* SP=CastField<FStructProperty>(P)) { if(SP->Struct) OutSubObj=SP->Struct->GetPathName(); return TEXT("struct"); }
		if (const FClassProperty* CP=CastField<FClassProperty>(P)) { if(CP->MetaClass) OutSubObj=CP->MetaClass->GetPathName(); return TEXT("class"); }
		if (const FSoftObjectProperty* SOP=CastField<FSoftObjectProperty>(P)) { if(SOP->PropertyClass) OutSubObj=SOP->PropertyClass->GetPathName(); return TEXT("soft_object"); }
		if (const FObjectPropertyBase* OP=CastField<FObjectPropertyBase>(P)) { if(OP->PropertyClass) OutSubObj=OP->PropertyClass->GetPathName(); return TEXT("object"); }
		return TEXT("unknown");
	}

	TSharedPtr<FJsonObject> PropTypeObj(FProperty* P)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		FString Container = TEXT("none"); FProperty* Inner = P;
		if (const FArrayProperty* A=CastField<FArrayProperty>(P)) { Container=TEXT("array"); Inner=A->Inner; }
		else if (const FSetProperty* S=CastField<FSetProperty>(P)) { Container=TEXT("set"); Inner=S->ElementProp; }
		else if (const FMapProperty* M=CastField<FMapProperty>(P)) { Container=TEXT("map"); Inner=M->ValueProp; }
		FString Sub; const FString Cat = PropCategory(Inner, Sub);
		O->SetStringField(TEXT("category"), Cat);
		O->SetStringField(TEXT("sub_category"), TEXT(""));
		O->SetStringField(TEXT("sub_category_object"), Sub);
		O->SetStringField(TEXT("container_type"), Container);
		return O;
	}

	// Editable, non-delegate, non-structural property = a candidate for settable_properties / fuzzy matching.
	bool IsListableSettable(FProperty* P)
	{
		if (!P) return false;
		if (CastField<FMulticastDelegateProperty>(P) || CastField<FDelegateProperty>(P)) return false;
		const FString N=P->GetName();
		if (N==TEXT("Slot") || N==TEXT("Slots")) return false;
		return P->HasAnyPropertyFlags(CPF_Edit);
	}

	// Resolve a property by fuzzy name: exact -> case-insensitive -> bool `b` prefix -> DisplayName -> normalized.
	FProperty* FindPropertyFuzzy(UStruct* Owner, const FString& Name, FString& OutMatched)
	{
		if (!Owner) return nullptr;
		if (FProperty* P=FindFProperty<FProperty>(Owner, FName(*Name))) { OutMatched=P->GetName(); return P; }
		const FString bName=FString(TEXT("b"))+Name;
		for (TFieldIterator<FProperty> It(Owner); It; ++It) { FProperty* P=*It; const FString pn=P->GetName();
			if (pn.Equals(Name,ESearchCase::IgnoreCase) || pn.Equals(bName,ESearchCase::IgnoreCase)) { OutMatched=pn; return P; } }
		const FString nm=NormName(Name);
		for (TFieldIterator<FProperty> It(Owner); It; ++It) { FProperty* P=*It; const FString pn=P->GetName();
			if (NormName(pn)==nm) { OutMatched=pn; return P; }
			const FString disp=P->GetDisplayNameText().ToString();
			if (disp.Equals(Name,ESearchCase::IgnoreCase) || NormName(disp)==nm) { OutMatched=pn; return P; } }
		return nullptr;
	}

	TArray<TSharedPtr<FJsonValue>> SuggestProps(UStruct* Owner, const FString& Name)
	{
		TArray<TSharedPtr<FJsonValue>> Out; if(!Owner) return Out;
		const FString nm=NormName(Name);
		auto AddP=[&](FProperty* P){ TSharedPtr<FJsonObject> O=MakeShared<FJsonObject>(); O->SetStringField(TEXT("name"),P->GetName()); O->SetStringField(TEXT("display_name"),P->GetDisplayNameText().ToString()); FString sub; O->SetStringField(TEXT("type"),PropCategory(P,sub)); Out.Add(MakeShared<FJsonValueObject>(O)); };
		for (TFieldIterator<FProperty> It(Owner); It && Out.Num()<8; ++It) { FProperty* P=*It; if(!IsListableSettable(P)) continue;
			if (NormName(P->GetName()).Contains(nm) || NormName(P->GetDisplayNameText().ToString()).Contains(nm)) { AddP(P); } }
		if (Out.Num()==0) { for (TFieldIterator<FProperty> It(Owner); It && Out.Num()<8; ++It) { FProperty* P=*It; if(IsListableSettable(P)) AddP(P); } }
		return Out;
	}
}

TArray<FMulticastDelegateProperty*> FBPWidgetGen::GetBindableDelegates(UClass* WidgetClass)
{
	TArray<FMulticastDelegateProperty*> Out;
	if (!WidgetClass) { return Out; }
	for (TFieldIterator<FMulticastDelegateProperty> It(WidgetClass); It; ++It)
	{
		FMulticastDelegateProperty* D = *It;
		if (D->HasAnyPropertyFlags(CPF_BlueprintAssignable)) { Out.Add(D); }   // = the "+ event" delegates
	}
	return Out;
}

FMulticastDelegateProperty* FBPWidgetGen::FindBindableDelegate(UClass* WidgetClass, const FString& EventName)
{
	if (!WidgetClass) { return nullptr; }
	// exact
	if (FMulticastDelegateProperty* D = FindFProperty<FMulticastDelegateProperty>(WidgetClass, FName(*EventName)))
	{
		if (D->HasAnyPropertyFlags(CPF_BlueprintAssignable)) { return D; }
	}
	// case-insensitive over the bindable set (handles display-vs-internal name drift without hardcoding)
	for (FMulticastDelegateProperty* D : GetBindableDelegates(WidgetClass))
	{
		if (D->GetName().Equals(EventName, ESearchCase::IgnoreCase)) { return D; }
	}
	return nullptr;
}

TArray<TSharedPtr<FJsonValue>> FBPWidgetGen::DescribeDelegateParams(const FMulticastDelegateProperty* Delegate)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!Delegate || !Delegate->SignatureFunction) { return Out; }
	for (TFieldIterator<FProperty> It(Delegate->SignatureFunction); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		if (It->PropertyFlags & CPF_ReturnParm) { continue; }
		Out.Add(MakeShared<FJsonValueObject>(ParamTypeJson(*It)));
	}
	return Out;
}

FString FBPWidgetGen::BindWidgetEvent(UWidgetBlueprint* WBP, const FString& WidgetName, const FString& EventName,
	TSharedPtr<FJsonObject>& OutResult, UK2Node_ComponentBoundEvent** OutNode)
{
	if (OutNode) { *OutNode = nullptr; }
	if (!OutResult.IsValid()) { OutResult = MakeShared<FJsonObject>(); }
	OutResult->SetStringField(TEXT("widget"), WidgetName);
	OutResult->SetStringField(TEXT("event"), EventName);
	auto Fail = [&](const FString& Status, const FString& Msg) -> FString { OutResult->SetStringField(TEXT("status"), Status); return Msg; };

	if (!WBP) { return Fail(TEXT("error"), TEXT("null WBP")); }
	UEdGraph* EventGraph = FBPGen::GetEventGraph(WBP);
	if (!EventGraph) { return Fail(TEXT("error"), TEXT("no EventGraph")); }

	UClass* OwnerClass = WBP->SkeletonGeneratedClass ? WBP->SkeletonGeneratedClass.Get()
		: (WBP->GeneratedClass ? WBP->GeneratedClass.Get() : nullptr);
	if (!OwnerClass) { return Fail(TEXT("property_missing"), TEXT("no generated class (compile the WBP first)")); }

	FObjectProperty* WidgetProp = FindFProperty<FObjectProperty>(OwnerClass, FName(*WidgetName));
	if (!WidgetProp)
	{
		// classify: not in tree, vs in tree but not a variable, vs property not yet generated
		UWidget* W = (WBP->WidgetTree) ? WBP->WidgetTree->FindWidget(FName(*WidgetName)) : nullptr;
		if (!W)                    { return Fail(TEXT("widget_not_found"), FString::Printf(TEXT("widget '%s' not found in WidgetTree"), *WidgetName)); }
		if (!W->bIsVariable)       { return Fail(TEXT("not_variable"), FString::Printf(TEXT("widget '%s' is not a variable; cannot bind events"), *WidgetName)); }
		return Fail(TEXT("property_missing"), FString::Printf(TEXT("widget variable '%s' not found on %s (compile first)"), *WidgetName, *OwnerClass->GetName()));
	}

	UClass* WidgetClass = WidgetProp->PropertyClass;
	OutResult->SetStringField(TEXT("widget_class"), WidgetClass ? WidgetClass->GetPathName() : TEXT(""));

	FMulticastDelegateProperty* DelProp = FindBindableDelegate(WidgetClass, EventName);
	if (!DelProp)
	{
		FString Avail;
		for (FMulticastDelegateProperty* D : GetBindableDelegates(WidgetClass)) { Avail += (Avail.IsEmpty() ? TEXT("") : TEXT(", ")) + D->GetName(); }
		return Fail(TEXT("delegate_not_found"), FString::Printf(TEXT("event '%s' is not a bindable multicast delegate on %s; available: [%s]"),
			*EventName, WidgetClass ? *WidgetClass->GetName() : TEXT("<null>"), *Avail));
	}
	OutResult->SetStringField(TEXT("delegate_property"), DelProp->GetName());
	OutResult->SetArrayField(TEXT("parameters"), DescribeDelegateParams(DelProp));

	// Idempotent: reuse an existing bound-event node for this widget+delegate.
	if (const UK2Node_ComponentBoundEvent* Existing = FKismetEditorUtilities::FindBoundEventForComponent(WBP, DelProp->GetFName(), WidgetProp->GetFName()))
	{
		if (OutNode) { *OutNode = const_cast<UK2Node_ComponentBoundEvent*>(Existing); }
		OutResult->SetStringField(TEXT("node_class"), TEXT("K2Node_ComponentBoundEvent"));
		OutResult->SetStringField(TEXT("node_title"), Existing->GetNodeTitle(ENodeTitleType::ListView).ToString());
		OutResult->SetStringField(TEXT("graph"), Existing->GetGraph() ? Existing->GetGraph()->GetName() : TEXT(""));
		OutResult->SetBoolField(TEXT("reused"), true);
		OutResult->SetStringField(TEXT("status"), TEXT("reused"));
		return FString();
	}

	UK2Node_ComponentBoundEvent* Node = NewObject<UK2Node_ComponentBoundEvent>(EventGraph);
	Node->InitializeComponentBoundEventParams(WidgetProp, DelProp);
	Node->CreateNewGuid();
	Node->PostPlacedNewNode();
	Node->AllocateDefaultPins();
	int32 MaxY = 0; for (UEdGraphNode* N : EventGraph->Nodes) { if (N) { MaxY = FMath::Max(MaxY, N->NodePosY + 160); } }
	Node->NodePosX = 0; Node->NodePosY = MaxY;
	EventGraph->AddNode(Node, /*bFromUI*/ false, /*bSelectNewNode*/ false);

	if (Node->Pins.Num() == 0)
	{
		return Fail(TEXT("pins_incomplete"), FString::Printf(TEXT("bound-event node for %s.%s created but has no pins"), *WidgetName, *EventName));
	}

	if (OutNode) { *OutNode = Node; }
	OutResult->SetStringField(TEXT("node_class"), TEXT("K2Node_ComponentBoundEvent"));
	OutResult->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
	OutResult->SetStringField(TEXT("graph"), EventGraph->GetName());
	OutResult->SetBoolField(TEXT("reused"), false);
	OutResult->SetStringField(TEXT("status"), TEXT("bound"));
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	return FString();
}

// ---- Phase 4 P2: handler exec/data wiring helpers ----
namespace
{
	// bound-event data OUTPUT pins = the delegate params (skip exec + delegate output).
	TArray<UEdGraphPin*> BoundEventParamPins(UEdGraphNode* Node)
	{
		TArray<UEdGraphPin*> Out;
		if (!Node) { return Out; }
		for (UEdGraphPin* P : Node->Pins)
		{
			if (!P || P->Direction != EGPD_Output) { continue; }
			if (P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) { continue; }
			if (P->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate) { continue; }
			if (P->PinName == TEXT("OutputDelegate")) { continue; }
			Out.Add(P);
		}
		return Out;
	}

	// call-node data INPUT pins that can accept event params (skip exec + self + hidden).
	TArray<UEdGraphPin*> CallInputDataPins(UEdGraphNode* Node)
	{
		TArray<UEdGraphPin*> Out;
		if (!Node) { return Out; }
		for (UEdGraphPin* P : Node->Pins)
		{
			if (!P || P->Direction != EGPD_Input) { continue; }
			if (P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) { continue; }
			if (P->bHidden) { continue; }
			if (P->PinName == UEdGraphSchema_K2::PN_Self) { continue; }
			Out.Add(P);
		}
		return Out;
	}

	bool PinsLinked(UEdGraphPin* A, UEdGraphPin* B) { return A && B && A->LinkedTo.Contains(B); }

	bool SameCoreType(const FEdGraphPinType& X, const FEdGraphPinType& Y)
	{
		return X.PinCategory == Y.PinCategory && X.PinSubCategory == Y.PinSubCategory
			&& X.PinSubCategoryObject == Y.PinSubCategoryObject && X.ContainerType == Y.ContainerType;
	}

	UK2Node_CustomEvent* FindCustomEventByName(UEdGraph* G, const FName Name)
	{
		if (!G) { return nullptr; }
		for (UEdGraphNode* N : G->Nodes)
		{
			if (UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(N)) { if (CE->CustomFunctionName == Name) { return CE; } }
		}
		return nullptr;
	}
}

FString FBPWidgetGen::EnsureEventHandlerEntry(UWidgetBlueprint* WBP, UK2Node_ComponentBoundEvent* BoundNode,
	const TSharedPtr<FJsonObject>& HandlerSpec, TSharedPtr<FJsonObject>& OutHandler)
{
	if (!OutHandler.IsValid()) { OutHandler = MakeShared<FJsonObject>(); }
	auto HStr = [&](const TCHAR* K, const FString& D) -> FString { FString v; return (HandlerSpec.IsValid() && HandlerSpec->TryGetStringField(K, v)) ? v : D; };
	auto HBool = [&](const TCHAR* K, bool D) -> bool { bool v; return (HandlerSpec.IsValid() && HandlerSpec->TryGetBoolField(K, v)) ? v : D; };

	const FString Type = HStr(TEXT("type"), TEXT("bound_event"));
	const FString Name = HStr(TEXT("name"), TEXT(""));
	const bool bCreateIfMissing = HBool(TEXT("create_if_missing"), true);
	OutHandler->SetStringField(TEXT("type"), Type);
	OutHandler->SetStringField(TEXT("name"), Name);
	OutHandler->SetBoolField(TEXT("created"), false);
	auto Fail = [&](const TCHAR* St, const FString& Msg) -> FString { OutHandler->SetStringField(TEXT("status"), St); return Msg; };

	if (Type == TEXT("bound_event")) { return FString(); }  // the bound-event node is itself the entry
	if (!WBP) { return Fail(TEXT("handler_create_failed"), TEXT("null WBP")); }
	if (!BoundNode) { return Fail(TEXT("bound_event_missing"), TEXT("no bound-event node")); }
	if (Name.IsEmpty()) { return Fail(TEXT("handler_not_found"), FString::Printf(TEXT("handler.name required for type '%s'"), *Type)); }
	UEdGraph* EG = BoundNode->GetGraph();
	if (!EG) { return Fail(TEXT("handler_create_failed"), TEXT("bound node has no graph")); }

	// signature mirrors the bound event's data-output pins (name + type)
	TArray<FBPGenParam> Params;
	for (UEdGraphPin* P : BoundEventParamPins(BoundNode)) { FBPGenParam Pr; Pr.Name = P->PinName; Pr.Type = P->PinType; Pr.bIsReturn = false; Params.Add(Pr); }

	if (Type == TEXT("custom_event"))
	{
		UK2Node_CustomEvent* CE = FindCustomEventByName(EG, FName(*Name));
		if (!CE)
		{
			if (!bCreateIfMissing) { return Fail(TEXT("handler_not_found"), FString::Printf(TEXT("custom event '%s' not found and create_if_missing=false"), *Name)); }
			int32 MaxY = 0; for (UEdGraphNode* N : EG->Nodes) { if (N) { MaxY = FMath::Max(MaxY, N->NodePosY + 180); } }
			CE = FBPGen::SpawnCustomEvent(EG, FName(*Name), Params, 480, MaxY);
			if (!CE) { return Fail(TEXT("handler_create_failed"), FString::Printf(TEXT("failed to create custom event '%s'"), *Name)); }
			OutHandler->SetBoolField(TEXT("created"), true);
		}
		OutHandler->SetStringField(TEXT("handler_kind"), TEXT("custom_event"));
		return FString();
	}
	if (Type == TEXT("function"))
	{
		UEdGraph* FG = nullptr;
		for (UEdGraph* G : WBP->FunctionGraphs) { if (G && G->GetFName() == FName(*Name)) { FG = G; break; } }
		if (!FG)
		{
			if (!bCreateIfMissing) { return Fail(TEXT("handler_not_found"), FString::Printf(TEXT("function '%s' not found and create_if_missing=false"), *Name)); }
			FG = FBPGen::AddFunctionGraph(WBP, FName(*Name), Params, /*bPure*/ false);
			if (!FG) { return Fail(TEXT("handler_create_failed"), FString::Printf(TEXT("failed to create function '%s'"), *Name)); }
			OutHandler->SetBoolField(TEXT("created"), true);
		}
		else if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(FBPGen::FindFunctionEntry(FG)))
		{
			if (Entry->GetFunctionFlags() & FUNC_BlueprintPure) { return Fail(TEXT("function_is_pure"), FString::Printf(TEXT("function '%s' is pure; cannot be an exec handler"), *Name)); }
		}
		OutHandler->SetStringField(TEXT("handler_kind"), TEXT("function"));
		return FString();
	}
	return Fail(TEXT("handler_not_found"), FString::Printf(TEXT("unknown handler type '%s' (use bound_event|custom_event|function)"), *Type));
}

FString FBPWidgetGen::WireEventHandlerCall(UWidgetBlueprint* WBP, UK2Node_ComponentBoundEvent* BoundNode,
	const TSharedPtr<FJsonObject>& HandlerSpec, TSharedPtr<FJsonObject>& OutHandler)
{
	if (!OutHandler.IsValid()) { OutHandler = MakeShared<FJsonObject>(); }
	auto HStr = [&](const TCHAR* K, const FString& D) -> FString { FString v; return (HandlerSpec.IsValid() && HandlerSpec->TryGetStringField(K, v)) ? v : D; };
	auto HBool = [&](const TCHAR* K, bool D) -> bool { bool v; return (HandlerSpec.IsValid() && HandlerSpec->TryGetBoolField(K, v)) ? v : D; };

	const FString Type = HStr(TEXT("type"), TEXT("bound_event"));
	const FString Name = HStr(TEXT("name"), TEXT(""));
	const bool bConnectExec = HBool(TEXT("connect_exec"), true);
	const bool bConnectParams = HBool(TEXT("connect_parameters"), true);
	OutHandler->SetStringField(TEXT("type"), Type);
	OutHandler->SetStringField(TEXT("name"), Name);
	auto Fail = [&](const TCHAR* St, const FString& Msg) -> FString { OutHandler->SetStringField(TEXT("status"), St); OutHandler->SetBoolField(TEXT("connected"), false); return Msg; };

	if (Type == TEXT("bound_event"))
	{
		OutHandler->SetBoolField(TEXT("exec_connected"), true);
		OutHandler->SetBoolField(TEXT("connected"), true);
		OutHandler->SetStringField(TEXT("status"), TEXT("connected"));
		return FString();
	}
	if (!BoundNode) { return Fail(TEXT("bound_event_missing"), TEXT("no bound-event node")); }
	UEdGraph* EG = BoundNode->GetGraph();

	// resolve the handler UFunction (custom event or function) - must exist post-compile
	UClass* FnOwner = WBP && WBP->GeneratedClass ? WBP->GeneratedClass.Get() : nullptr;
	UFunction* Fn = FnOwner ? FnOwner->FindFunctionByName(FName(*Name)) : nullptr;
	if (!Fn && WBP && WBP->SkeletonGeneratedClass) { FnOwner = WBP->SkeletonGeneratedClass.Get(); Fn = FnOwner ? FnOwner->FindFunctionByName(FName(*Name)) : nullptr; }
	if (!Fn) { return Fail(TEXT("handler_not_found"), FString::Printf(TEXT("handler function '%s' not found after compile"), *Name)); }

	// find/reuse a call node to <Name> already linked from this bound event, else create one
	UEdGraphPin* BoundThen = FBPGen::FindExecOut(BoundNode);
	UK2Node_CallFunction* Call = nullptr;
	for (UEdGraphNode* N : EG->Nodes)
	{
		UK2Node_CallFunction* CF = Cast<UK2Node_CallFunction>(N);
		if (!CF || CF->FunctionReference.GetMemberName() != FName(*Name)) { continue; }
		UEdGraphPin* Ci = FBPGen::FindExecIn(CF);
		if (BoundThen && Ci && Ci->LinkedTo.Contains(BoundThen)) { Call = CF; break; }
	}
	bool bCreatedCall = false;
	if (!Call)
	{
		Call = FBPGen::SpawnCallFunc(EG, FnOwner, FName(*Name), BoundNode->NodePosX + 340, BoundNode->NodePosY);
		if (!Call) { return Fail(TEXT("handler_create_failed"), FString::Printf(TEXT("could not spawn call node for '%s'"), *Name)); }
		bCreatedCall = true;
	}
	OutHandler->SetBoolField(TEXT("call_created"), bCreatedCall);

	// exec (idempotent)
	bool bExec = false;
	if (bConnectExec)
	{
		UEdGraphPin* CallExecIn = FBPGen::FindExecIn(Call);
		if (!BoundThen || !CallExecIn) { return Fail(TEXT("exec_pin_missing"), TEXT("exec pin missing on bound event or call node")); }
		if (PinsLinked(BoundThen, CallExecIn)) { bExec = true; }
		else { FBPGen::Connect(BoundThen, CallExecIn); bExec = PinsLinked(BoundThen, CallExecIn); }
		if (!bExec) { return Fail(TEXT("exec_connection_failed"), TEXT("could not connect bound-event exec to handler")); }
	}
	OutHandler->SetBoolField(TEXT("exec_connected"), bExec);

	// data params (name -> case-insensitive -> unique type), idempotent + classified
	TArray<TSharedPtr<FJsonValue>> ParamsOut;
	if (bConnectParams)
	{
		const TArray<UEdGraphPin*> Ins = CallInputDataPins(Call);
		for (UEdGraphPin* Out : BoundEventParamPins(BoundNode))
		{
			TSharedRef<FJsonObject> PR = MakeShared<FJsonObject>();
			PR->SetStringField(TEXT("from"), Out->PinName.ToString());
			UEdGraphPin* Target = nullptr; FString Status;
			for (UEdGraphPin* In : Ins) { if (In->PinName.ToString().Equals(Out->PinName.ToString(), ESearchCase::IgnoreCase)) { Target = In; break; } }
			if (!Target)
			{
				TArray<UEdGraphPin*> TypeMatches;
				for (UEdGraphPin* In : Ins) { if (SameCoreType(In->PinType, Out->PinType) && In->LinkedTo.Num() == 0) { TypeMatches.Add(In); } }
				if (TypeMatches.Num() == 1) { Target = TypeMatches[0]; }
				else if (TypeMatches.Num() > 1) { Status = TEXT("ambiguous_parameter_match"); }
				else { Status = TEXT("parameter_pin_missing"); }
			}
			if (Target)
			{
				PR->SetStringField(TEXT("to"), Target->PinName.ToString());
				if (PinsLinked(Out, Target)) { Status = TEXT("already_connected"); }
				else { FBPGen::Connect(Out, Target); Status = (Out->LinkedTo.Num() > 0 && Target->LinkedTo.Num() > 0) ? TEXT("connected") : TEXT("parameter_type_mismatch"); }
			}
			else { PR->SetStringField(TEXT("to"), TEXT("")); if (Status.IsEmpty()) { Status = TEXT("parameter_pin_missing"); } }
			PR->SetStringField(TEXT("status"), Status);
			ParamsOut.Add(MakeShared<FJsonValueObject>(PR));
		}
	}
	OutHandler->SetArrayField(TEXT("parameters_connected"), ParamsOut);
	OutHandler->SetBoolField(TEXT("connected"), bExec);
	OutHandler->SetStringField(TEXT("status"), bExec ? TEXT("connected") : TEXT("not_connected"));
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	return FString();
}
