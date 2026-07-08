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
class FJsonObject;
class UK2Node_ComponentBoundEvent;

class FBPWidgetGen
{
public:
	/** Create a Widget Blueprint asset at /Game/... with the given UserWidget-derived parent (default UUserWidget).
	 *  Returns the new UWidgetBlueprint (as UObject-owning), or nullptr on failure. Does NOT compile/save. */
	static UWidgetBlueprint* CreateWidgetBlueprint(const FString& AssetPath, UClass* ParentClass);

	/** Resolve a widget class from a spec "type": a builtin short name (e.g. "CanvasPanel","TextBlock","Button")
	 *  or a full class path (e.g. "/Script/UMG.Image" or a custom "/Game/UI/WBP_X.WBP_X_C"). nullptr if unresolved. */
	static UClass* ResolveWidgetClass(const FString& TypeSpec);

	/** Rich resolver (Phase 5): also accepts a custom UserWidget by package path (`/Game/UI/WBP_X`), object path
	 *  (`/Game/UI/WBP_X.WBP_X`) or generated-class path (`/Game/UI/WBP_X.WBP_X_C`) - all normalized to the `_C`
	 *  generated class. On failure returns nullptr and sets OutError to a category:
	 *  class_path_invalid | class_load_failed | not_user_widget. On success OutError is empty, bOutCustom is true
	 *  for `/Game/...` widgets, and OutAssetPath / OutGeneratedClass are filled (for dependency recording). */
	static UClass* ResolveWidgetClassEx(const FString& TypeSpec, FString& OutError, bool& bOutCustom,
		FString& OutAssetPath, FString& OutGeneratedClass);

	/** Construct a widget of WidgetClass into WBP's WidgetTree (unparented). Marks it as a variable (named). */
	static UWidget* ConstructWidget(UWidgetBlueprint* WBP, UClass* WidgetClass, const FName Name);

	/** Set WBP->WidgetTree->RootWidget. */
	static void SetRoot(UWidgetBlueprint* WBP, UWidget* Root);

	/** Parent Child under Parent (Parent must be a UPanelWidget). Returns the created slot, or nullptr. */
	static UPanelSlot* AddChild(UWidget* Parent, UWidget* Child);

	/** Set a property (on a widget or a slot) from a JSON value via reflection with fuzzy name resolution
	 *  (exact -> case-insensitive -> bool `b` prefix -> DisplayName -> strip space/underscore) then a `Set<Name>`
	 *  setter fallback. Returns "" on success or "property_not_found". OutResolvedName = the actual property/setter
	 *  used (differs from PropName => alias matched; caller should warn). On failure OutSuggestions is filled with
	 *  candidate { name, display_name, type } objects. */
	static FString SetPropertyFromJson(UObject* Target, const FString& PropName, const TSharedPtr<FJsonValue>& Value,
		FString& OutResolvedName, TArray<TSharedPtr<FJsonValue>>& OutSuggestions);

	/** Enumerate the editable (CPF_Edit) properties of a widget or slot class (incl. inherited) for discovery:
	 *  [{ name, display_name, type{category,sub_category,sub_category_object,container_type}, declaring_class,
	 *     editable, blueprint_visible, blueprint_read_only, deprecated, current_value, set_supported, notes }].
	 *  Delegates (see bindable_events), functions, and structural backrefs (Slot/Slots) are excluded. */
	static TArray<TSharedPtr<FJsonValue>> ListSettableProperties(UStruct* Owner, UObject* Instance);

	// ---- Phase 4 (generalized, reflection-based widget event binding) ----

	/** All BlueprintAssignable multicast-delegate properties on a widget class (incl. inherited) = the events
	 *  the UMG Details "+ event" button can bind. Generic (no per-widget special-casing). */
	static TArray<FMulticastDelegateProperty*> GetBindableDelegates(UClass* WidgetClass);

	/** Resolve an event by name to its bindable delegate: exact match, then case-insensitive over the
	 *  bindable set. Returns nullptr if the class has no such bindable multicast delegate. */
	static FMulticastDelegateProperty* FindBindableDelegate(UClass* WidgetClass, const FString& EventName);

	/** Describe a delegate's parameters as a JSON array [{ name, type, sub_category_object? }]. */
	static TArray<TSharedPtr<FJsonValue>> DescribeDelegateParams(const FMulticastDelegateProperty* Delegate);

	/** Create (or reuse) a bound-event node in the WBP EventGraph for <WidgetName>.<EventName>, via reflection
	 *  (works for ANY widget/custom UserWidget with a BlueprintAssignable multicast delegate). The WBP must be
	 *  compiled once first (so the widget variable exists). Fills OutResult (widget/widget_class/event/
	 *  delegate_property/node_title/node_class/graph/parameters/status/reused). Returns "" on success or a
	 *  classified error string; OutResult.status is one of bound|reused|widget_not_found|not_variable|
	 *  property_missing|delegate_not_found|pins_incomplete|error. OutNode (optional) receives the bound-event node. */
	static FString BindWidgetEvent(UWidgetBlueprint* WBP, const FString& WidgetName, const FString& EventName,
		TSharedPtr<FJsonObject>& OutResult, UK2Node_ComponentBoundEvent** OutNode = nullptr);

	// ---- Phase 4 P2 (handler exec/data wiring) ----

	/** ENTRY step: ensure the handler node/graph exists (idempotent). HandlerSpec: { type, name, create_if_missing }.
	 *  For `custom_event` -> a UK2Node_CustomEvent named `name` (params mirror the bound event); for `function` ->
	 *  a function graph `name` (params mirror the bound event). Signatures mirror BoundNode's data-output pins.
	 *  Does NOT compile or create the call node (call `WireEventHandlerCall` after a compile). Fills OutHandler
	 *  (type/name/created). Returns "" or a classified error: handler_not_found | handler_create_failed |
	 *  function_is_pure | handler_signature_mismatch. `bound_event` returns "" with nothing to create. */
	static FString EnsureEventHandlerEntry(UWidgetBlueprint* WBP, UK2Node_ComponentBoundEvent* BoundNode,
		const TSharedPtr<FJsonObject>& HandlerSpec, TSharedPtr<FJsonObject>& OutHandler);

	/** WIRE step (run AFTER a compile so the handler UFunction exists): create/reuse a Call node to the handler
	 *  and connect BoundNode's exec (then connect_parameters data pins by name then type). Idempotent (reuses an
	 *  existing call linked from the bound event; never double-links). Fills OutHandler.connected/exec_connected/
	 *  parameters_connected[{from,to,status}]. Returns "" or a classified error: bound_event_missing |
	 *  handler_not_found | exec_pin_missing | exec_connection_failed. For `bound_event` type it is a no-op success. */
	static FString WireEventHandlerCall(UWidgetBlueprint* WBP, UK2Node_ComponentBoundEvent* BoundNode,
		const TSharedPtr<FJsonObject>& HandlerSpec, TSharedPtr<FJsonObject>& OutHandler);

	/** Add a simple handler-body logic template inside the handler entry (custom_event / function / bound_event).
	 *  Run AFTER WireEventHandlerCall (widget variables exist). HandlerSpec.body is an array of ops (MVP):
	 *   { "op":"print_string", "text"|"from_param" }
	 *   { "op":"set_text", "target":"<WidgetName>", "text"|"from_param" }
	 *  Each op's exec chains off the entry node; `from_param` wires the entry's matching data-out pin. Fills
	 *  OutHandler.body_ops[{op,status,detail}] + body_applied. Idempotent (skips if the entry already drives a
	 *  chain). Returns "" or a classified error (handler_body_entry_missing). */
	static FString AddHandlerBody(UWidgetBlueprint* WBP, UK2Node_ComponentBoundEvent* BoundNode,
		const TSharedPtr<FJsonObject>& HandlerSpec, TSharedPtr<FJsonObject>& OutHandler);
};
