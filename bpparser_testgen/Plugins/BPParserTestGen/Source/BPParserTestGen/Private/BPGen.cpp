// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPGen.h"
#include "BPParserTestGenModule.h"

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/UserDefinedStruct.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"

#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchString.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeMap.h"
#include "K2Node_MakeSet.h"
#include "K2Node_Self.h"
#include "K2Node_Knot.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_Message.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Tunnel.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Misc/PackageName.h"

// ============================================================================
// File-local helpers
// ============================================================================
namespace
{
	const UEdGraphSchema_K2* K2() { return GetDefault<UEdGraphSchema_K2>(); }

	void PrepCleanName(UPackage* Package, const FString& AssetName)
	{
		// Free the object name if a previous run left an asset here, so re-runs work.
		if (UObject* Existing = StaticFindObject(nullptr, Package, *AssetName))
		{
			Existing->Rename(nullptr, GetTransientPackage(),
				REN_DontCreateRedirectors | REN_DoNotDirty | REN_ForceNoResetLoaders);
			Existing->MarkAsGarbage();
		}
	}

	template<typename TNode>
	TNode* BeginNode(UEdGraph* G)
	{
		if (!G) { return nullptr; }
		TNode* N = NewObject<TNode>(G);
		N->SetFlags(RF_Transactional);
		G->AddNode(N, /*bUserAction*/ false, /*bSelectNewNode*/ false);
		N->CreateNewGuid();
		return N;
	}

	void FinalizeNode(UEdGraphNode* N, int32 X, int32 Y)
	{
		if (!N) { return; }
		N->NodePosX = X;
		N->NodePosY = Y;
		N->PostPlacedNewNode();
		N->AllocateDefaultPins();
	}

	FString Normalize(const FString& In)
	{
		return In.Replace(TEXT(" "), TEXT("")).Replace(TEXT("_"), TEXT("")).ToLower();
	}
}

// ============================================================================
// Asset creation
// ============================================================================
UBlueprint* FBPGen::CreateActorBlueprint(const FString& AssetPath, UClass* ParentClass)
{
	const FString AssetName = FPackageName::GetShortName(AssetPath);
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package) { UE_LOG(LogBPParserTestGen, Warning, TEXT("CreatePackage failed: %s"), *AssetPath); return nullptr; }
	PrepCleanName(Package, AssetName);

	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
		ParentClass ? ParentClass : AActor::StaticClass(),
		Package, FName(*AssetName), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName("BPParserTestGen"));

	if (BP)
	{
		FAssetRegistryModule::AssetCreated(BP);
		Package->MarkPackageDirty();
	}
	return BP;
}

UBlueprint* FBPGen::CreateComponentBlueprint(const FString& AssetPath, UClass* ParentClass)
{
	return CreateActorBlueprint(AssetPath, ParentClass ? ParentClass : UActorComponent::StaticClass());
}

UBlueprint* FBPGen::CreateInterfaceBlueprint(const FString& AssetPath)
{
	const FString AssetName = FPackageName::GetShortName(AssetPath);
	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package) { return nullptr; }
	PrepCleanName(Package, AssetName);

	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
		UInterface::StaticClass(),
		Package, FName(*AssetName), BPTYPE_Interface,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName("BPParserTestGen"));

	if (BP)
	{
		FAssetRegistryModule::AssetCreated(BP);
		Package->MarkPackageDirty();
	}
	return BP;
}

UEdGraph* FBPGen::GetEventGraph(UBlueprint* BP)
{
	if (!BP) { return nullptr; }
	if (UEdGraph* Found = FBlueprintEditorUtils::FindEventGraph(BP))
	{
		return Found;
	}
	return BP->UbergraphPages.Num() > 0 ? BP->UbergraphPages[0] : nullptr;
}

// ============================================================================
// Members
// ============================================================================
bool FBPGen::AddVariable(UBlueprint* BP, FName VarName, const FEdGraphPinType& Type,
                         const FString& DefaultValue, const FString& Category, bool bInstanceEditable)
{
	if (!BP) { return false; }
	const bool bOk = FBlueprintEditorUtils::AddMemberVariable(BP, VarName, Type, DefaultValue);
	if (!bOk)
	{
		UE_LOG(LogBPParserTestGen, Warning, TEXT("AddMemberVariable failed: %s"), *VarName.ToString());
		return false;
	}
	if (!Category.IsEmpty())
	{
		FBlueprintEditorUtils::SetBlueprintVariableCategory(BP, VarName, nullptr, FText::FromString(Category));
	}
	if (bInstanceEditable)
	{
		for (FBPVariableDescription& V : BP->NewVariables)
		{
			if (V.VarName == VarName)
			{
				V.PropertyFlags |= (CPF_Edit | CPF_BlueprintVisible);
				V.PropertyFlags &= ~CPF_DisableEditOnInstance;
			}
		}
	}
	return true;
}

UEdGraph* FBPGen::AddFunctionGraph(UBlueprint* BP, FName FuncName, const TArray<FBPGenParam>& Params, bool bPure)
{
	if (!BP) { return nullptr; }

	UEdGraph* FuncGraph = FBlueprintEditorUtils::CreateNewGraph(
		BP, FuncName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, FuncGraph, /*bIsUserCreated*/ true, (UClass*)nullptr);

	UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(FindFunctionEntry(FuncGraph));
	UK2Node_FunctionResult* Result = nullptr;

	// inputs (function inputs are OUTPUT pins on the entry node)
	for (const FBPGenParam& P : Params)
	{
		if (P.bIsReturn) { continue; }
		if (Entry) { Entry->CreateUserDefinedPin(P.Name, P.Type, EGPD_Output); }
	}

	// returns (function outputs are INPUT pins on the result node)
	const bool bHasReturn = Params.ContainsByPredicate([](const FBPGenParam& P){ return P.bIsReturn; });
	if (bHasReturn)
	{
		Result = Cast<UK2Node_FunctionResult>(FindFunctionResult(FuncGraph));
		if (!Result)
		{
			Result = BeginNode<UK2Node_FunctionResult>(FuncGraph);
			FinalizeNode(Result, 700, 0);
		}
		for (const FBPGenParam& P : Params)
		{
			if (P.bIsReturn && Result) { Result->CreateUserDefinedPin(P.Name, P.Type, EGPD_Input); }
		}
	}

	if (bPure && Entry)
	{
		Entry->AddExtraFlags(FUNC_BlueprintPure);
		Entry->ReconstructNode();
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	return FuncGraph;
}

UEdGraph* FBPGen::AddMacroGraph(UBlueprint* BP, FName MacroName, const TArray<FBPGenParam>& Inputs,
                                const TArray<FBPGenParam>& Outputs)
{
	if (!BP) { return nullptr; }

	UEdGraph* MacroGraph = FBlueprintEditorUtils::CreateNewGraph(
		BP, MacroName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	BP->MacroGraphs.Add(MacroGraph);
	K2()->CreateMacroGraphTerminators(*MacroGraph, (UClass*)nullptr);

	// Locate the input/output tunnels.
	UK2Node_Tunnel* InputTunnel = nullptr;   // provides macro inputs as OUTPUT pins into the macro body
	UK2Node_Tunnel* OutputTunnel = nullptr;  // collects macro outputs as INPUT pins
	for (UEdGraphNode* N : MacroGraph->Nodes)
	{
		if (UK2Node_Tunnel* T = Cast<UK2Node_Tunnel>(N))
		{
			if (T->bCanHaveOutputs) { InputTunnel = T; }
			else if (T->bCanHaveInputs) { OutputTunnel = T; }
		}
	}

	for (const FBPGenParam& P : Inputs)
	{
		if (InputTunnel) { InputTunnel->CreateUserDefinedPin(P.Name, P.Type, EGPD_Output); }
	}
	for (const FBPGenParam& P : Outputs)
	{
		if (OutputTunnel) { OutputTunnel->CreateUserDefinedPin(P.Name, P.Type, EGPD_Input); }
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	return MacroGraph;
}

bool FBPGen::AddLocalVariable(UEdGraph* FunctionGraph, FName VarName, const FEdGraphPinType& Type, const FString& DefaultValue)
{
	if (!FunctionGraph) { return false; }
	UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForGraph(FunctionGraph);
	if (!BP) { return false; }
	return FBlueprintEditorUtils::AddLocalVariable(BP, FunctionGraph, VarName, Type, DefaultValue);
}

bool FBPGen::ImplementInterface(UBlueprint* BP, const FString& InterfaceAssetPath)
{
	if (!BP) { return false; }
	UClass* IfaceClass = LoadBPClass(InterfaceAssetPath);
	if (!IfaceClass)
	{
		UE_LOG(LogBPParserTestGen, Warning, TEXT("ImplementInterface: cannot load %s"), *InterfaceAssetPath);
		return false;
	}
	const FTopLevelAssetPath Path(IfaceClass->GetPackage()->GetFName(), IfaceClass->GetFName());
	FBlueprintEditorUtils::ImplementNewInterface(BP, Path);
	return true;
}

bool FBPGen::AddInterfaceFunction(UBlueprint* InterfaceBP, FName FuncName, const TArray<FBPGenParam>& Params)
{
	// Interface functions are plain function graphs on the interface blueprint.
	return AddFunctionGraph(InterfaceBP, FuncName, Params, /*bPure*/ false) != nullptr;
}

bool FBPGen::AddEventDispatcher(UBlueprint* BP, FName DispatcherName, const TArray<FBPGenParam>& Params)
{
	if (!BP) { return false; }

	// 1) multicast-delegate member variable
	FEdGraphPinType DelegateType;
	DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
	if (!FBlueprintEditorUtils::AddMemberVariable(BP, DispatcherName, DelegateType))
	{
		UE_LOG(LogBPParserTestGen, Warning, TEXT("AddEventDispatcher: AddMemberVariable failed for %s"), *DispatcherName.ToString());
		return false;
	}

	// 2) signature graph
	UEdGraph* SigGraph = FBlueprintEditorUtils::CreateNewGraph(
		BP, DispatcherName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	SigGraph->bEditable = false;
	K2()->CreateDefaultNodesForGraph(*SigGraph);
	K2()->CreateFunctionGraphTerminators(*SigGraph, (UClass*)nullptr);
	K2()->AddExtraFunctionFlags(SigGraph, (FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public));
	K2()->MarkFunctionEntryAsEditable(SigGraph, true);
	BP->DelegateSignatureGraphs.Add(SigGraph);

	// 3) dispatcher parameters added to the signature's function entry as OUTPUT pins
	if (Params.Num() > 0)
	{
		if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(FindFunctionEntry(SigGraph)))
		{
			for (const FBPGenParam& P : Params)
			{
				Entry->CreateUserDefinedPin(P.Name, P.Type, EGPD_Output);
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	return true;
}

FName FBPGen::AddComponent(UBlueprint* BP, UClass* ComponentClass, FName DesiredName)
{
	if (!BP || !ComponentClass) { return NAME_None; }
	if (!BP->SimpleConstructionScript)
	{
		UE_LOG(LogBPParserTestGen, Warning, TEXT("AddComponent: blueprint has no SCS (%s)"), *BP->GetName());
		return NAME_None;
	}
	USCS_Node* Node = BP->SimpleConstructionScript->CreateNode(ComponentClass, DesiredName);
	if (!Node) { return NAME_None; }
	BP->SimpleConstructionScript->AddNode(Node);
	return Node->GetVariableName();
}

// ============================================================================
// Node spawning
// ============================================================================
UK2Node_Event* FBPGen::SpawnEvent(UEdGraph* G, FName EventName, UClass* EventClass, int32 X, int32 Y)
{
	UK2Node_Event* N = BeginNode<UK2Node_Event>(G);
	if (!N) { return nullptr; }
	N->EventReference.SetExternalMember(EventName, EventClass);
	N->bOverrideFunction = true;
	FinalizeNode(N, X, Y);
	return N;
}

UK2Node_CustomEvent* FBPGen::SpawnCustomEvent(UEdGraph* G, FName Name, const TArray<FBPGenParam>& Params, int32 X, int32 Y)
{
	UK2Node_CustomEvent* N = BeginNode<UK2Node_CustomEvent>(G);
	if (!N) { return nullptr; }
	N->CustomFunctionName = Name;
	FinalizeNode(N, X, Y);
	for (const FBPGenParam& P : Params)
	{
		N->CreateUserDefinedPin(P.Name, P.Type, EGPD_Output);
	}
	N->ReconstructNode();
	return N;
}

UK2Node_CallFunction* FBPGen::SpawnCallFunc(UEdGraph* G, UClass* OwnerClass, FName FuncName, int32 X, int32 Y)
{
	if (!OwnerClass) { UE_LOG(LogBPParserTestGen, Warning, TEXT("SpawnCallFunc: null owner for %s"), *FuncName.ToString()); return nullptr; }
	UFunction* Fn = OwnerClass->FindFunctionByName(FuncName);
	if (!Fn) { UE_LOG(LogBPParserTestGen, Warning, TEXT("SpawnCallFunc: function %s not found on %s"), *FuncName.ToString(), *OwnerClass->GetName()); return nullptr; }
	UK2Node_CallFunction* N = BeginNode<UK2Node_CallFunction>(G);
	if (!N) { return nullptr; }
	N->SetFromFunction(Fn);
	FinalizeNode(N, X, Y);
	return N;
}

UK2Node_VariableGet* FBPGen::SpawnVarGet(UEdGraph* G, FName VarName, int32 X, int32 Y)
{
	UK2Node_VariableGet* N = BeginNode<UK2Node_VariableGet>(G);
	if (!N) { return nullptr; }
	N->VariableReference.SetSelfMember(VarName);
	FinalizeNode(N, X, Y);
	return N;
}

UK2Node_VariableSet* FBPGen::SpawnVarSet(UEdGraph* G, FName VarName, int32 X, int32 Y)
{
	UK2Node_VariableSet* N = BeginNode<UK2Node_VariableSet>(G);
	if (!N) { return nullptr; }
	N->VariableReference.SetSelfMember(VarName);
	FinalizeNode(N, X, Y);
	return N;
}

UK2Node_IfThenElse* FBPGen::SpawnBranch(UEdGraph* G, int32 X, int32 Y)
{
	UK2Node_IfThenElse* N = BeginNode<UK2Node_IfThenElse>(G);
	FinalizeNode(N, X, Y);
	return N;
}

UK2Node_ExecutionSequence* FBPGen::SpawnSequence(UEdGraph* G, int32 NumOutputs, int32 X, int32 Y)
{
	UK2Node_ExecutionSequence* N = BeginNode<UK2Node_ExecutionSequence>(G);
	if (!N) { return nullptr; }
	FinalizeNode(N, X, Y);
	while (GetExecOutPins(N).Num() < FMath::Max(2, NumOutputs))
	{
		N->AddInputPin();   // IK2Node_AddPinInterface: adds another "Then" output
	}
	return N;
}

UK2Node_SwitchInteger* FBPGen::SpawnSwitchInt(UEdGraph* G, int32 X, int32 Y)
{
	UK2Node_SwitchInteger* N = BeginNode<UK2Node_SwitchInteger>(G);
	if (!N) { return nullptr; }
	FinalizeNode(N, X, Y);
	N->AddPinToSwitchNode();
	N->AddPinToSwitchNode();
	N->AddPinToSwitchNode();
	return N;
}

UK2Node_SwitchString* FBPGen::SpawnSwitchString(UEdGraph* G, const TArray<FString>& Pins, int32 X, int32 Y)
{
	UK2Node_SwitchString* N = BeginNode<UK2Node_SwitchString>(G);
	if (!N) { return nullptr; }
	FinalizeNode(N, X, Y);
	const int32 Count = FMath::Max(2, Pins.Num());
	for (int32 i = 0; i < Count; ++i)
	{
		N->AddPinToSwitchNode();
	}
	return N;
}

UK2Node_SwitchEnum* FBPGen::SpawnSwitchEnum(UEdGraph* G, UEnum* Enum, int32 X, int32 Y)
{
	if (!Enum) { UE_LOG(LogBPParserTestGen, Warning, TEXT("SpawnSwitchEnum: null enum")); return nullptr; }
	UK2Node_SwitchEnum* N = BeginNode<UK2Node_SwitchEnum>(G);
	if (!N) { return nullptr; }
	// SetEnum() is not exported (UCLASS MinimalAPI). Set the public Enum field directly;
	// AllocateDefaultPins -> CreateCasePins() calls SetEnum(Enum) internally to build case pins.
	N->Enum = Enum;
	FinalizeNode(N, X, Y);
	return N;
}

UK2Node_DynamicCast* FBPGen::SpawnCast(UEdGraph* G, UClass* TargetType, int32 X, int32 Y, bool bPure)
{
	if (!TargetType) { return nullptr; }
	UK2Node_DynamicCast* N = BeginNode<UK2Node_DynamicCast>(G);
	if (!N) { return nullptr; }
	N->TargetType = TargetType;
	N->SetPurity(bPure);
	FinalizeNode(N, X, Y);
	return N;
}

UK2Node_MakeStruct* FBPGen::SpawnMakeStruct(UEdGraph* G, UScriptStruct* Struct, int32 X, int32 Y)
{
	if (!Struct) { return nullptr; }
	UK2Node_MakeStruct* N = BeginNode<UK2Node_MakeStruct>(G);
	if (!N) { return nullptr; }
	N->StructType = Struct;
	FinalizeNode(N, X, Y);
	return N;
}

UK2Node_BreakStruct* FBPGen::SpawnBreakStruct(UEdGraph* G, UScriptStruct* Struct, int32 X, int32 Y)
{
	if (!Struct) { return nullptr; }
	UK2Node_BreakStruct* N = BeginNode<UK2Node_BreakStruct>(G);
	if (!N) { return nullptr; }
	N->StructType = Struct;
	FinalizeNode(N, X, Y);
	return N;
}

UK2Node_MakeArray* FBPGen::SpawnMakeArray(UEdGraph* G, int32 NumInputs, int32 X, int32 Y)
{
	UK2Node_MakeArray* N = BeginNode<UK2Node_MakeArray>(G);
	if (!N) { return nullptr; }
	FinalizeNode(N, X, Y);
	for (int32 i = 1; i < FMath::Max(1, NumInputs); ++i) { N->AddInputPin(); }
	return N;
}

UK2Node_MakeMap* FBPGen::SpawnMakeMap(UEdGraph* G, int32 NumPairs, int32 X, int32 Y)
{
	UK2Node_MakeMap* N = BeginNode<UK2Node_MakeMap>(G);
	if (!N) { return nullptr; }
	FinalizeNode(N, X, Y);
	for (int32 i = 1; i < FMath::Max(1, NumPairs); ++i) { N->AddInputPin(); }
	return N;
}

UK2Node_MakeSet* FBPGen::SpawnMakeSet(UEdGraph* G, int32 NumInputs, int32 X, int32 Y)
{
	UK2Node_MakeSet* N = BeginNode<UK2Node_MakeSet>(G);
	if (!N) { return nullptr; }
	FinalizeNode(N, X, Y);
	for (int32 i = 1; i < FMath::Max(1, NumInputs); ++i) { N->AddInputPin(); }
	return N;
}

UK2Node_Self* FBPGen::SpawnSelf(UEdGraph* G, int32 X, int32 Y)
{
	UK2Node_Self* N = BeginNode<UK2Node_Self>(G);
	FinalizeNode(N, X, Y);
	return N;
}

UK2Node_Knot* FBPGen::SpawnReroute(UEdGraph* G, int32 X, int32 Y)
{
	UK2Node_Knot* N = BeginNode<UK2Node_Knot>(G);
	FinalizeNode(N, X, Y);
	return N;
}

UK2Node_MacroInstance* FBPGen::SpawnStdMacro(UEdGraph* G, const FString& MacroName, int32 X, int32 Y)
{
	UBlueprint* StdLib = LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
	if (!StdLib)
	{
		UE_LOG(LogBPParserTestGen, Warning, TEXT("SpawnStdMacro: cannot load StandardMacros library"));
		return nullptr;
	}
	UEdGraph* Target = nullptr;
	const FString Want = Normalize(MacroName);
	for (UEdGraph* M : StdLib->MacroGraphs)
	{
		if (M && Normalize(M->GetName()) == Want) { Target = M; break; }
	}
	if (!Target)
	{
		UE_LOG(LogBPParserTestGen, Warning, TEXT("SpawnStdMacro: macro '%s' not found in StandardMacros"), *MacroName);
		return nullptr;
	}
	UK2Node_MacroInstance* N = BeginNode<UK2Node_MacroInstance>(G);
	if (!N) { return nullptr; }
	N->SetMacroGraph(Target);
	FinalizeNode(N, X, Y);
	return N;
}

UK2Node_SpawnActorFromClass* FBPGen::SpawnActorNode(UEdGraph* G, UClass* ActorClass, int32 X, int32 Y)
{
	UK2Node_SpawnActorFromClass* N = BeginNode<UK2Node_SpawnActorFromClass>(G);
	if (!N) { return nullptr; }
	FinalizeNode(N, X, Y);
	if (ActorClass)
	{
		if (UEdGraphPin* ClassPin = FindPin(N, TEXT("Class"), EGPD_Input))
		{
			K2()->TrySetDefaultObject(*ClassPin, ActorClass);
			N->ReconstructNode();
		}
	}
	return N;
}

UK2Node_Message* FBPGen::SpawnInterfaceMessage(UEdGraph* G, UClass* InterfaceClass, FName FuncName, int32 X, int32 Y)
{
	if (!InterfaceClass) { return nullptr; }
	UK2Node_Message* N = BeginNode<UK2Node_Message>(G);
	if (!N) { return nullptr; }
	N->FunctionReference.SetExternalMember(FuncName, InterfaceClass);
	FinalizeNode(N, X, Y);
	return N;
}

UK2Node_AddDelegate* FBPGen::SpawnBindDelegate(UEdGraph* G, FName DelegateName, int32 X, int32 Y)
{
	UK2Node_AddDelegate* N = BeginNode<UK2Node_AddDelegate>(G);
	if (!N) { return nullptr; }
	N->DelegateReference.SetSelfMember(DelegateName);
	FinalizeNode(N, X, Y);
	return N;
}

UK2Node_CallDelegate* FBPGen::SpawnCallDelegate(UEdGraph* G, FName DelegateName, int32 X, int32 Y)
{
	UK2Node_CallDelegate* N = BeginNode<UK2Node_CallDelegate>(G);
	if (!N) { return nullptr; }
	N->DelegateReference.SetSelfMember(DelegateName);
	FinalizeNode(N, X, Y);
	return N;
}

UK2Node_ClearDelegate* FBPGen::SpawnClearDelegate(UEdGraph* G, FName DelegateName, int32 X, int32 Y)
{
	UK2Node_ClearDelegate* N = BeginNode<UK2Node_ClearDelegate>(G);
	if (!N) { return nullptr; }
	N->DelegateReference.SetSelfMember(DelegateName);
	FinalizeNode(N, X, Y);
	return N;
}

UK2Node_CreateDelegate* FBPGen::SpawnCreateDelegate(UEdGraph* G, FName FunctionName, int32 X, int32 Y)
{
	UK2Node_CreateDelegate* N = BeginNode<UK2Node_CreateDelegate>(G);
	if (!N) { return nullptr; }
	FinalizeNode(N, X, Y);
	N->SetFunction(FunctionName);
	N->HandleAnyChange();
	return N;
}

UEdGraphNode* FBPGen::FindFunctionEntry(UEdGraph* FunctionGraph)
{
	if (!FunctionGraph) { return nullptr; }
	for (UEdGraphNode* N : FunctionGraph->Nodes)
	{
		if (N && N->IsA<UK2Node_FunctionEntry>()) { return N; }
	}
	return nullptr;
}

UEdGraphNode* FBPGen::FindFunctionResult(UEdGraph* FunctionGraph)
{
	if (!FunctionGraph) { return nullptr; }
	for (UEdGraphNode* N : FunctionGraph->Nodes)
	{
		if (N && N->IsA<UK2Node_FunctionResult>()) { return N; }
	}
	return nullptr;
}

UEdGraphNode_Comment* FBPGen::AddComment(UEdGraph* G, const FString& Text, int32 X, int32 Y, int32 W, int32 H)
{
	if (!G) { return nullptr; }
	UEdGraphNode_Comment* C = NewObject<UEdGraphNode_Comment>(G);
	C->SetFlags(RF_Transactional);
	G->AddNode(C, false, false);
	C->CreateNewGuid();
	C->NodePosX = X;
	C->NodePosY = Y;
	C->NodeWidth = W;
	C->NodeHeight = H;
	C->NodeComment = Text;
	C->PostPlacedNewNode();
	return C;
}

// ============================================================================
// Wiring & pins
// ============================================================================
UEdGraphPin* FBPGen::FindPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Dir)
{
	if (!Node) { return nullptr; }
	for (UEdGraphPin* P : Node->Pins)
	{
		if (P && P->Direction == Dir && P->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
		{
			return P;
		}
	}
	return nullptr;
}

UEdGraphPin* FBPGen::FindExecOut(UEdGraphNode* Node)
{
	if (!Node) { return nullptr; }
	for (UEdGraphPin* P : Node->Pins)
	{
		if (P && P->Direction == EGPD_Output && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) { return P; }
	}
	return nullptr;
}

UEdGraphPin* FBPGen::FindExecIn(UEdGraphNode* Node)
{
	if (!Node) { return nullptr; }
	for (UEdGraphPin* P : Node->Pins)
	{
		if (P && P->Direction == EGPD_Input && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) { return P; }
	}
	return nullptr;
}

TArray<UEdGraphPin*> FBPGen::GetExecOutPins(UEdGraphNode* Node)
{
	TArray<UEdGraphPin*> Out;
	if (!Node) { return Out; }
	for (UEdGraphPin* P : Node->Pins)
	{
		if (P && P->Direction == EGPD_Output && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) { Out.Add(P); }
	}
	return Out;
}

bool FBPGen::Connect(UEdGraphPin* A, UEdGraphPin* B)
{
	if (!A || !B) { return false; }
	return K2()->TryCreateConnection(A, B);
}

bool FBPGen::ConnectByName(UEdGraphNode* From, const FString& FromPin, UEdGraphNode* To, const FString& ToPin)
{
	UEdGraphPin* A = FindPin(From, FromPin, EGPD_Output);
	UEdGraphPin* B = FindPin(To, ToPin, EGPD_Input);
	if (!A || !B)
	{
		UE_LOG(LogBPParserTestGen, Warning, TEXT("ConnectByName failed: %s.%s -> %s.%s"),
			From ? *From->GetName() : TEXT("?"), *FromPin, To ? *To->GetName() : TEXT("?"), *ToPin);
		return false;
	}
	return K2()->TryCreateConnection(A, B);
}

bool FBPGen::ConnectExec(UEdGraphNode* From, UEdGraphNode* To)
{
	return Connect(FindExecOut(From), FindExecIn(To));
}

bool FBPGen::SetPinDefault(UEdGraphNode* Node, const FString& PinName, const FString& Value)
{
	UEdGraphPin* Pin = FindPin(Node, PinName, EGPD_Input);
	if (!Pin) { return false; }
	K2()->TrySetDefaultValue(*Pin, Value);
	return true;
}

bool FBPGen::SetPinDefaultObject(UEdGraphNode* Node, const FString& PinName, UObject* Obj)
{
	UEdGraphPin* Pin = FindPin(Node, PinName, EGPD_Input);
	if (!Pin) { return false; }
	K2()->TrySetDefaultObject(*Pin, Obj);
	return true;
}

// ============================================================================
// Pin-type factory
// ============================================================================
static FEdGraphPinType MakeType(FName Cat, FName Sub = NAME_None, UObject* Obj = nullptr)
{
	FEdGraphPinType T;
	T.PinCategory = Cat;
	T.PinSubCategory = Sub;
	T.PinSubCategoryObject = Obj;
	T.ContainerType = EPinContainerType::None;
	return T;
}

FEdGraphPinType FBPGen::PinExec()   { return MakeType(UEdGraphSchema_K2::PC_Exec); }
FEdGraphPinType FBPGen::PinBool()   { return MakeType(UEdGraphSchema_K2::PC_Boolean); }
FEdGraphPinType FBPGen::PinByte()   { return MakeType(UEdGraphSchema_K2::PC_Byte); }
FEdGraphPinType FBPGen::PinByteEnum(UEnum* Enum) { return MakeType(UEdGraphSchema_K2::PC_Byte, NAME_None, Enum); }
FEdGraphPinType FBPGen::PinInt()    { return MakeType(UEdGraphSchema_K2::PC_Int); }
FEdGraphPinType FBPGen::PinInt64()  { return MakeType(UEdGraphSchema_K2::PC_Int64); }
FEdGraphPinType FBPGen::PinFloat()  { return MakeType(UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Float); }
FEdGraphPinType FBPGen::PinDouble() { return MakeType(UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Double); }
FEdGraphPinType FBPGen::PinName()   { return MakeType(UEdGraphSchema_K2::PC_Name); }
FEdGraphPinType FBPGen::PinString() { return MakeType(UEdGraphSchema_K2::PC_String); }
FEdGraphPinType FBPGen::PinText()   { return MakeType(UEdGraphSchema_K2::PC_Text); }
FEdGraphPinType FBPGen::PinStruct(UScriptStruct* Struct) { return MakeType(UEdGraphSchema_K2::PC_Struct, NAME_None, Struct); }
FEdGraphPinType FBPGen::PinVector()      { return PinStruct(TBaseStructure<FVector>::Get()); }
FEdGraphPinType FBPGen::PinVector2D()    { return PinStruct(TBaseStructure<FVector2D>::Get()); }
FEdGraphPinType FBPGen::PinVector4()     { return PinStruct(TBaseStructure<FVector4>::Get()); }
FEdGraphPinType FBPGen::PinRotator()     { return PinStruct(TBaseStructure<FRotator>::Get()); }
FEdGraphPinType FBPGen::PinTransform()   { return PinStruct(TBaseStructure<FTransform>::Get()); }
FEdGraphPinType FBPGen::PinLinearColor() { return PinStruct(TBaseStructure<FLinearColor>::Get()); }
FEdGraphPinType FBPGen::PinColor()       { return PinStruct(TBaseStructure<FColor>::Get()); }
FEdGraphPinType FBPGen::PinObject(UClass* Class) { return MakeType(UEdGraphSchema_K2::PC_Object, NAME_None, Class ? Class : UObject::StaticClass()); }
FEdGraphPinType FBPGen::PinClass(UClass* Class)  { return MakeType(UEdGraphSchema_K2::PC_Class, NAME_None, Class ? Class : UObject::StaticClass()); }
FEdGraphPinType FBPGen::PinSoftObject(UClass* Class) { return MakeType(UEdGraphSchema_K2::PC_SoftObject, NAME_None, Class ? Class : UObject::StaticClass()); }
FEdGraphPinType FBPGen::PinSoftClass(UClass* Class)  { return MakeType(UEdGraphSchema_K2::PC_SoftClass, NAME_None, Class ? Class : UObject::StaticClass()); }
FEdGraphPinType FBPGen::PinInterface(UClass* Class)  { return MakeType(UEdGraphSchema_K2::PC_Interface, NAME_None, Class); }

FEdGraphPinType FBPGen::AsArray(FEdGraphPinType T) { T.ContainerType = EPinContainerType::Array; return T; }
FEdGraphPinType FBPGen::AsSet(FEdGraphPinType T)   { T.ContainerType = EPinContainerType::Set;   return T; }
FEdGraphPinType FBPGen::AsMap(FEdGraphPinType KeyT, const FEdGraphPinType& ValueT)
{
	KeyT.ContainerType = EPinContainerType::Map;
	KeyT.PinValueType = FEdGraphTerminalType::FromPinType(ValueT);
	return KeyT;
}

// ============================================================================
// Compile & save
// ============================================================================
FString FBPGen::CompileBlueprint(UBlueprint* BP)
{
	if (!BP) { return TEXT("not_compiled"); }
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None);
	switch (BP->Status)
	{
		case BS_UpToDate:             return TEXT("up_to_date");
		case BS_UpToDateWithWarnings: return TEXT("warnings");
		case BS_Error:                return TEXT("error");
		case BS_Dirty:                return TEXT("dirty");
		default:                      return TEXT("unknown");
	}
}

bool FBPGen::SaveAsset(UObject* Asset)
{
	if (!Asset) { return false; }
	UPackage* Package = Asset->GetOutermost();
	if (!Package) { return false; }
	Package->SetDirtyFlag(true);

	const FString PackageName = Package->GetName();
	const FString FileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_NoError;
	Args.Error = GWarn;

	const bool bSaved = UPackage::SavePackage(Package, Asset, *FileName, Args);
	if (!bSaved)
	{
		UE_LOG(LogBPParserTestGen, Warning, TEXT("SaveAsset failed: %s"), *PackageName);
	}
	return bSaved;
}

// ============================================================================
// Misc loaders
// ============================================================================
UEnum* FBPGen::LoadEnum(const FString& AssetPath)
{
	return LoadObject<UEnum>(nullptr, *AssetPath);
}

UScriptStruct* FBPGen::LoadStruct(const FString& AssetPath)
{
	return LoadObject<UScriptStruct>(nullptr, *AssetPath);
}

UClass* FBPGen::LoadBPClass(const FString& BlueprintAssetPath)
{
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BlueprintAssetPath);
	return BP ? BP->GeneratedClass : nullptr;
}
