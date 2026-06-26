// Copyright BlueprintAnalyseTool. All Rights Reserved.
//
// BPGen: a thin, centralized wrapper around the gnarly UE 5.4 editor graph APIs.
// Per-blueprint builders call these helpers so all API-correctness lives in one place.
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"

class UEdGraph;
class UEdGraphNode;
class UK2Node_Event;
class UK2Node_CustomEvent;
class UK2Node_CallFunction;
class UK2Node_VariableGet;
class UK2Node_VariableSet;
class UK2Node_IfThenElse;
class UK2Node_ExecutionSequence;
class UK2Node_SwitchInteger;
class UK2Node_SwitchString;
class UK2Node_SwitchEnum;
class UK2Node_DynamicCast;
class UK2Node_MakeStruct;
class UK2Node_BreakStruct;
class UK2Node_MakeArray;
class UK2Node_MakeMap;
class UK2Node_MakeSet;
class UK2Node_Self;
class UK2Node_Knot;
class UK2Node_MacroInstance;
class UK2Node_AddDelegate;
class UK2Node_CallDelegate;
class UK2Node_ClearDelegate;
class UK2Node_CreateDelegate;
class UK2Node_Message;
class UK2Node_SpawnActorFromClass;
class UEdGraphNode_Comment;
class UScriptStruct;
class UEnum;

/** A simple parameter descriptor used for function / dispatcher signatures. */
struct FBPGenParam
{
	FName Name;
	FEdGraphPinType Type;
	bool bIsReturn = false;
};

/**
 * Static helper API. All functions are defensive: on failure they log a warning
 * (LogBPParserTestGen) and return nullptr/false instead of crashing, so a single
 * bad node never aborts the whole generation run.
 */
class FBPGen
{
public:
	// ---- Asset creation -------------------------------------------------
	static UBlueprint* CreateActorBlueprint(const FString& AssetPath, UClass* ParentClass);
	static UBlueprint* CreateComponentBlueprint(const FString& AssetPath, UClass* ParentClass);
	static UBlueprint* CreateInterfaceBlueprint(const FString& AssetPath);

	static UEdGraph* GetEventGraph(UBlueprint* BP);

	// ---- Members --------------------------------------------------------
	static bool AddVariable(UBlueprint* BP, FName VarName, const FEdGraphPinType& Type,
	                        const FString& DefaultValue = FString(), const FString& Category = TEXT("Default"),
	                        bool bInstanceEditable = false);
	static UEdGraph* AddFunctionGraph(UBlueprint* BP, FName FuncName, const TArray<FBPGenParam>& Params,
	                                  bool bPure = false);
	static UEdGraph* AddMacroGraph(UBlueprint* BP, FName MacroName, const TArray<FBPGenParam>& Inputs,
	                               const TArray<FBPGenParam>& Outputs);
	static bool AddLocalVariable(UEdGraph* FunctionGraph, FName VarName, const FEdGraphPinType& Type,
	                             const FString& DefaultValue = FString());
	static bool ImplementInterface(UBlueprint* BP, const FString& InterfaceAssetPath);
	static bool AddInterfaceFunction(UBlueprint* InterfaceBP, FName FuncName, const TArray<FBPGenParam>& Params);

	/** Adds a multicast-delegate Event Dispatcher (member var + signature graph). */
	static bool AddEventDispatcher(UBlueprint* BP, FName DispatcherName, const TArray<FBPGenParam>& Params);

	/** Adds an SCS component (returns the component variable name for later VariableGet). */
	static FName AddComponent(UBlueprint* BP, UClass* ComponentClass, FName DesiredName);

	// ---- Node spawning --------------------------------------------------
	static UK2Node_Event*             SpawnEvent(UEdGraph* G, FName EventName, UClass* EventClass, int32 X, int32 Y);
	static UK2Node_CustomEvent*       SpawnCustomEvent(UEdGraph* G, FName Name, const TArray<FBPGenParam>& Params, int32 X, int32 Y);
	static UK2Node_CallFunction*      SpawnCallFunc(UEdGraph* G, UClass* OwnerClass, FName FuncName, int32 X, int32 Y);
	static UK2Node_VariableGet*       SpawnVarGet(UEdGraph* G, FName VarName, int32 X, int32 Y);
	static UK2Node_VariableSet*       SpawnVarSet(UEdGraph* G, FName VarName, int32 X, int32 Y);
	static UK2Node_IfThenElse*        SpawnBranch(UEdGraph* G, int32 X, int32 Y);
	static UK2Node_ExecutionSequence* SpawnSequence(UEdGraph* G, int32 NumOutputs, int32 X, int32 Y);
	static UK2Node_SwitchInteger*     SpawnSwitchInt(UEdGraph* G, int32 X, int32 Y);
	static UK2Node_SwitchString*      SpawnSwitchString(UEdGraph* G, const TArray<FString>& Pins, int32 X, int32 Y);
	static UK2Node_SwitchEnum*        SpawnSwitchEnum(UEdGraph* G, UEnum* Enum, int32 X, int32 Y);
	static UK2Node_DynamicCast*       SpawnCast(UEdGraph* G, UClass* TargetType, int32 X, int32 Y, bool bPure = false);
	static UK2Node_MakeStruct*        SpawnMakeStruct(UEdGraph* G, UScriptStruct* Struct, int32 X, int32 Y);
	static UK2Node_BreakStruct*       SpawnBreakStruct(UEdGraph* G, UScriptStruct* Struct, int32 X, int32 Y);
	static UK2Node_MakeArray*         SpawnMakeArray(UEdGraph* G, int32 NumInputs, int32 X, int32 Y);
	static UK2Node_MakeMap*           SpawnMakeMap(UEdGraph* G, int32 NumPairs, int32 X, int32 Y);
	static UK2Node_MakeSet*           SpawnMakeSet(UEdGraph* G, int32 NumInputs, int32 X, int32 Y);
	static UK2Node_Self*              SpawnSelf(UEdGraph* G, int32 X, int32 Y);
	static UK2Node_Knot*              SpawnReroute(UEdGraph* G, int32 X, int32 Y);
	static UK2Node_MacroInstance*     SpawnStdMacro(UEdGraph* G, const FString& MacroName, int32 X, int32 Y);
	static UK2Node_SpawnActorFromClass* SpawnActorNode(UEdGraph* G, UClass* ActorClass, int32 X, int32 Y);
	static UK2Node_Message*           SpawnInterfaceMessage(UEdGraph* G, UClass* InterfaceClass, FName FuncName, int32 X, int32 Y);

	// Delegate family. DelegateName is a self multicast-delegate property (an Event Dispatcher).
	static UK2Node_AddDelegate*    SpawnBindDelegate(UEdGraph* G, FName DelegateName, int32 X, int32 Y);
	static UK2Node_CallDelegate*   SpawnCallDelegate(UEdGraph* G, FName DelegateName, int32 X, int32 Y);
	static UK2Node_ClearDelegate*  SpawnClearDelegate(UEdGraph* G, FName DelegateName, int32 X, int32 Y);
	static UK2Node_CreateDelegate* SpawnCreateDelegate(UEdGraph* G, FName FunctionName, int32 X, int32 Y);

	// Function-graph terminals (for editing custom function/macro graphs).
	static UEdGraphNode* FindFunctionEntry(UEdGraph* FunctionGraph);
	static UEdGraphNode* FindFunctionResult(UEdGraph* FunctionGraph);

	static UEdGraphNode_Comment* AddComment(UEdGraph* G, const FString& Text, int32 X, int32 Y, int32 W, int32 H);

	// ---- Wiring & pins --------------------------------------------------
	static UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Dir);
	static UEdGraphPin* FindExecOut(UEdGraphNode* Node);     // first output exec
	static UEdGraphPin* FindExecIn(UEdGraphNode* Node);      // first input exec
	static TArray<UEdGraphPin*> GetExecOutPins(UEdGraphNode* Node);  // all output exec pins (e.g. Sequence Then 0..N)
	static bool Connect(UEdGraphPin* A, UEdGraphPin* B);
	static bool ConnectByName(UEdGraphNode* From, const FString& FromPin, UEdGraphNode* To, const FString& ToPin);
	/** Connects From's first output-exec to To's first input-exec. */
	static bool ConnectExec(UEdGraphNode* From, UEdGraphNode* To);
	static bool SetPinDefault(UEdGraphNode* Node, const FString& PinName, const FString& Value);
	static bool SetPinDefaultObject(UEdGraphNode* Node, const FString& PinName, UObject* Obj);

	// ---- Pin-type factory -----------------------------------------------
	static FEdGraphPinType PinExec();
	static FEdGraphPinType PinBool();
	static FEdGraphPinType PinByte();
	static FEdGraphPinType PinByteEnum(UEnum* Enum);
	static FEdGraphPinType PinInt();
	static FEdGraphPinType PinInt64();
	static FEdGraphPinType PinFloat();   // real/float
	static FEdGraphPinType PinDouble();  // real/double
	static FEdGraphPinType PinName();
	static FEdGraphPinType PinString();
	static FEdGraphPinType PinText();
	static FEdGraphPinType PinStruct(UScriptStruct* Struct);
	static FEdGraphPinType PinVector();
	static FEdGraphPinType PinVector2D();
	static FEdGraphPinType PinVector4();
	static FEdGraphPinType PinRotator();
	static FEdGraphPinType PinTransform();
	static FEdGraphPinType PinLinearColor();
	static FEdGraphPinType PinColor();
	static FEdGraphPinType PinObject(UClass* Class);
	static FEdGraphPinType PinClass(UClass* Class);
	static FEdGraphPinType PinSoftObject(UClass* Class);
	static FEdGraphPinType PinSoftClass(UClass* Class);
	static FEdGraphPinType PinInterface(UClass* Class);
	static FEdGraphPinType AsArray(FEdGraphPinType T);
	static FEdGraphPinType AsSet(FEdGraphPinType T);
	static FEdGraphPinType AsMap(FEdGraphPinType KeyT, const FEdGraphPinType& ValueT);

	// ---- Compile & save -------------------------------------------------
	static FString CompileBlueprint(UBlueprint* BP);   // returns CompileStatus string
	static bool    SaveAsset(UObject* Asset);

	// ---- Misc -----------------------------------------------------------
	static UEnum*         LoadEnum(const FString& AssetPath);
	static UScriptStruct* LoadStruct(const FString& AssetPath);
	static UClass*        LoadBPClass(const FString& BlueprintAssetPath);  // returns generated class
};
