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
#include "K2Node_ComponentBoundEvent.h"
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

FString FBPWidgetGen::SetPropertyFromJson(UObject* Target, const FString& PropName, const TSharedPtr<FJsonValue>& Value)
{
	if (!Target) { return TEXT("null target"); }
	if (!Value.IsValid()) { return TEXT("null value"); }

	// 1) Direct FProperty by name (works for box slots' Padding/Size, widget Details, ZOrder, LayoutData, ...).
	if (FProperty* Prop = Target->GetClass()->FindPropertyByName(FName(*PropName)))
	{
		void* Addr = Prop->ContainerPtrToValuePtr<void>(Target);
		if (!FJsonObjectConverter::JsonValueToUProperty(Value, Prop, Addr, 0, 0))
		{
			return FString::Printf(TEXT("could not import value into property '%s' (%s)"), *PropName, *Prop->GetClass()->GetName());
		}
		return FString();
	}

	// 2) Fallback: a single-input setter UFUNCTION named Set<PropName>. This makes convenience keys like
	// CanvasPanelSlot Position/Size/Anchors/Alignment work (they are setters over LayoutData, not properties).
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
		if (!bOk) { return FString::Printf(TEXT("could not import value into setter Set%s parameter"), *PropName); }
		return FString();
	}

	return FString::Printf(TEXT("no property or 'Set%s' setter on %s"), *PropName, *Target->GetClass()->GetName());
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
	TSharedPtr<FJsonObject>& OutResult)
{
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

	OutResult->SetStringField(TEXT("node_class"), TEXT("K2Node_ComponentBoundEvent"));
	OutResult->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
	OutResult->SetStringField(TEXT("graph"), EventGraph->GetName());
	OutResult->SetBoolField(TEXT("reused"), false);
	OutResult->SetStringField(TEXT("status"), TEXT("bound"));
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	return FString();
}
