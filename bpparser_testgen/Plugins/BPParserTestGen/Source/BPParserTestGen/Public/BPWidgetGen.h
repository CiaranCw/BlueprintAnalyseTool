// Copyright BlueprintAnalyseTool. All Rights Reserved.
//
// FBPWidgetGen: UMG-aware helpers for creating Widget Blueprints (UMG) from a structured spec.
// Mirrors FBPGen but for the widget layer: create the WBP asset, construct widgets into its WidgetTree,
// parent them (producing slots), and set widget/slot Details properties by reflection. Kept minimal and
// reusable; higher-level orchestration (hierarchy recursion, compile/save/dump) lives in FBPCreate.
//
// Scope (Phase 1-3): asset creation, hierarchy, slots, common Details via reflection.
// NOT in scope this phase: event binding, custom-widget event graphs, UMG animation, pixel-accurate render.
#pragma once

#include "CoreMinimal.h"

class UWidgetBlueprint;
class UWidget;
class UPanelSlot;
class UClass;
class FJsonValue;

class FBPWidgetGen
{
public:
	/** Create a Widget Blueprint asset at /Game/... with the given UserWidget-derived parent (default UUserWidget).
	 *  Returns the new UWidgetBlueprint (as UObject-owning), or nullptr on failure. Does NOT compile/save. */
	static UWidgetBlueprint* CreateWidgetBlueprint(const FString& AssetPath, UClass* ParentClass);

	/** Resolve a widget class from a spec "type": a builtin short name (e.g. "CanvasPanel","TextBlock","Button")
	 *  or a full class path (e.g. "/Script/UMG.Image" or a custom "/Game/UI/WBP_X.WBP_X_C"). nullptr if unresolved. */
	static UClass* ResolveWidgetClass(const FString& TypeSpec);

	/** Construct a widget of WidgetClass into WBP's WidgetTree (unparented). Marks it as a variable (named). */
	static UWidget* ConstructWidget(UWidgetBlueprint* WBP, UClass* WidgetClass, const FName Name);

	/** Set WBP->WidgetTree->RootWidget. */
	static void SetRoot(UWidgetBlueprint* WBP, UWidget* Root);

	/** Parent Child under Parent (Parent must be a UPanelWidget). Returns the created slot, or nullptr. */
	static UPanelSlot* AddChild(UWidget* Parent, UWidget* Child);

	/** Set a property (on a widget or a slot) from a JSON value via reflection. Returns "" on success or an error. */
	static FString SetPropertyFromJson(UObject* Target, const FString& PropName, const TSharedPtr<FJsonValue>& Value);

	/** Phase 4: create a bound-event node in the WBP EventGraph for <WidgetName>.<EventName>
	 *  (e.g. PlayButton.OnClicked), like the UMG Details "+ event" button. The WBP must be compiled once
	 *  first so the widget variable exists as a property. Returns "" on success or an error string. */
	static FString BindWidgetEvent(UWidgetBlueprint* WBP, const FString& WidgetName, const FString& EventName);
};
