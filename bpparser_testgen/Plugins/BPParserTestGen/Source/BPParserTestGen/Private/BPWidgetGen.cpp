// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPWidgetGen.h"
#include "BPParserTestGenModule.h"

#include "WidgetBlueprint.h"                 // UWidgetBlueprint (UMGEditor)
#include "WidgetBlueprintFactory.h"          // UWidgetBlueprintFactory (UMGEditor)
#include "Blueprint/UserWidget.h"            // UUserWidget (UMG)
#include "Blueprint/WidgetTree.h"            // UWidgetTree (UMG)
#include "Components/Widget.h"               // UWidget
#include "Components/PanelWidget.h"          // UPanelWidget
#include "Components/PanelSlot.h"            // UPanelSlot

#include "AssetRegistry/AssetRegistryModule.h"
#include "JsonObjectConverter.h"
#include "Dom/JsonValue.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Misc/PackageName.h"

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
	if (TypeSpec.IsEmpty()) { return nullptr; }

	// Full path (an engine class path like /Script/UMG.Image, or a custom generated class /Game/..._C).
	if (TypeSpec.Contains(TEXT("/")) || TypeSpec.Contains(TEXT(".")))
	{
		if (UClass* C = LoadObject<UClass>(nullptr, *TypeSpec)) { return C; }
	}
	// Builtin short name under /Script/UMG (CanvasPanel, TextBlock, Button, Image, ...).
	if (UClass* C = LoadObject<UClass>(nullptr, *(FString(TEXT("/Script/UMG.")) + TypeSpec))) { return C; }
	// Fallback: any loaded UWidget subclass whose name matches.
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UWidget::StaticClass()) && It->GetName() == TypeSpec) { return *It; }
	}
	return nullptr;
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
