// Copyright BlueprintAnalyseTool. All Rights Reserved.
//
// Builders for BP_06 .. BP_10 and BP_99. Same defensive style as BPGenBuilders_A.
#include "BPGenTestBlueprints.h"
#include "BPGen.h"
#include "BPGenOrchestrator.h"
#include "BPGenSupportAssets.h"
#include "BPParserTestGenModule.h"

#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraph.h"

#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetArrayLibrary.h"

#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_Message.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_Knot.h"
#include "K2Node_Self.h"

namespace
{
	UEdGraphPin* OutPin(UEdGraphNode* N, const FString& Name) { return FBPGen::FindPin(N, Name, EGPD_Output); }
	UEdGraphPin* InPin(UEdGraphNode* N, const FString& Name)  { return FBPGen::FindPin(N, Name, EGPD_Input); }

	UEdGraphPin* DelegatePin(UEdGraphNode* N, EEdGraphPinDirection Dir)
	{
		if (!N) { return nullptr; }
		for (UEdGraphPin* P : N->Pins)
		{
			if (P && P->Direction == Dir &&
				(P->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate ||
				 P->PinType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate))
			{
				return P;
			}
		}
		return nullptr;
	}
}

// ============================================================================
// BP_06_Delegates_EventDispatchers
// ============================================================================
FBPGenAssetResult FBPGenTestBlueprints::Build_BP06_DelegatesDispatchers()
{
	FBPGenAssetResult R; R.AssetPath = Path06(); R.AssetType = TEXT("Actor");

	UBlueprint* BP = FBPGen::CreateActorBlueprint(Path06(), AActor::StaticClass());
	if (!BP) { R.Notes.Add(TEXT("CreateActorBlueprint failed.")); return R; }
	R.bCreated = true;

	TArray<FBPGenParam> DispParams;
	DispParams.Add({ "Message", FBPGen::PinString(), false });
	const bool bDisp = FBPGen::AddEventDispatcher(BP, "OnParserTestTriggered", DispParams);
	if (!bDisp) R.Notes.Add(TEXT("AddEventDispatcher failed."));

	UEdGraph* G = FBPGen::GetEventGraph(BP);
	if (!G) { R.CompileStatus = FBPGen::CompileBlueprint(BP); return R; }

	FBPGen::AddComment(G, TEXT("Delegate Test: Bind -> Call -> Unbind"), -80, -160, 1600, 360);
	FBPGen::AddComment(G, TEXT("Custom Event handler"), -80, 240, 760, 280);

	// Handler custom event
	TArray<FBPGenParam> HandlerParams;
	HandlerParams.Add({ "Message", FBPGen::PinString(), false });
	UK2Node_CustomEvent* Handler = FBPGen::SpawnCustomEvent(G, "HandleParserTestTriggered", HandlerParams, 0, 320);
	if (Handler)
	{
		UK2Node_CallFunction* Print = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 320, 320);
		if (Print)
		{
			FBPGen::ConnectExec(Handler, Print);
			if (UEdGraphPin* Msg = OutPin(Handler, TEXT("Message"))) FBPGen::Connect(Msg, InPin(Print, "InString"));
		}
	}

	// BeginPlay -> Bind (CreateDelegate -> AddDelegate) -> Call -> Clear
	UK2Node_Event* Begin = FBPGen::SpawnEvent(G, "ReceiveBeginPlay", AActor::StaticClass(), 0, 0);

	UK2Node_CreateDelegate* Create = FBPGen::SpawnCreateDelegate(G, "HandleParserTestTriggered", 240, 120);
	UK2Node_AddDelegate* Bind = FBPGen::SpawnBindDelegate(G, "OnParserTestTriggered", 560, 0);
	if (Bind && Create)
	{
		UEdGraphPin* CreateOut = DelegatePin(Create, EGPD_Output);
		UEdGraphPin* BindIn = InPin(Bind, TEXT("Delegate"));
		if (!BindIn) { BindIn = DelegatePin(Bind, EGPD_Input); }
		if (CreateOut && BindIn) { FBPGen::Connect(CreateOut, BindIn); }
		// Bind the handler AFTER wiring the delegate pin (signature now known).
		Create->SetFunction(FName("HandleParserTestTriggered"));
		Create->HandleAnyChange(true);
		FBPGen::ConnectExec(Begin, Bind);
	}

	UK2Node_CallDelegate* Call = FBPGen::SpawnCallDelegate(G, "OnParserTestTriggered", 880, 0);
	if (Call)
	{
		FBPGen::SetPinDefault(Call, TEXT("Message"), TEXT("Triggered!"));
		if (Bind) FBPGen::ConnectExec(Bind, Call);
	}

	UK2Node_ClearDelegate* Clear = FBPGen::SpawnClearDelegate(G, "OnParserTestTriggered", 1200, 0);
	if (Clear && Call) FBPGen::ConnectExec(Call, Clear);

	R.CompileStatus = FBPGen::CompileBlueprint(BP);
	R.Notes.Add(TEXT("Covers Event Dispatcher (multicast delegate var + signature graph), Custom Event, Create Delegate -> Bind (AddDelegate), Call Dispatcher, Clear/Unbind All, Delegate Pin wiring. Verify the Create->Bind delegate pin resolved correctly in UE."));
	return R;
}

// ============================================================================
// BP_07_Latent_Timeline_Async  (Timer-based; Timeline/Async documented as manual)
// ============================================================================
FBPGenAssetResult FBPGenTestBlueprints::Build_BP07_LatentTimerAsync()
{
	FBPGenAssetResult R; R.AssetPath = Path07(); R.AssetType = TEXT("Actor");

	UBlueprint* BP = FBPGen::CreateActorBlueprint(Path07(), AActor::StaticClass());
	if (!BP) { R.Notes.Add(TEXT("CreateActorBlueprint failed.")); return R; }
	R.bCreated = true;

	UEdGraph* G = FBPGen::GetEventGraph(BP);
	if (!G) { R.CompileStatus = FBPGen::CompileBlueprint(BP); return R; }

	FBPGen::AddComment(G, TEXT("Latent: Delay -> SetTimerByFunctionName -> ClearTimer"), -80, -160, 1700, 420);
	FBPGen::AddComment(G, TEXT("Timer callback (Custom Event)"), -80, 300, 700, 260);

	// Timer callback
	UK2Node_CustomEvent* Tick = FBPGen::SpawnCustomEvent(G, "OnTimerTick", {}, 0, 380);
	if (Tick)
	{
		UK2Node_CallFunction* P = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 320, 380);
		if (P) { FBPGen::SetPinDefault(P, TEXT("InString"), TEXT("Timer tick")); FBPGen::ConnectExec(Tick, P); }
	}

	UK2Node_Event* Begin = FBPGen::SpawnEvent(G, "ReceiveBeginPlay", AActor::StaticClass(), 0, 0);

	UK2Node_CallFunction* Delay = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "Delay", 260, 0);
	if (Delay) { FBPGen::SetPinDefault(Delay, TEXT("Duration"), TEXT("1.0")); FBPGen::ConnectExec(Begin, Delay); }

	UK2Node_Self* Self1 = FBPGen::SpawnSelf(G, 540, 160);
	UK2Node_CallFunction* SetTimer = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "K2_SetTimer", 560, 0);
	if (SetTimer)
	{
		FBPGen::ConnectExec(Delay, SetTimer);
		if (Self1) FBPGen::Connect(OutPin(Self1, TEXT("self")), InPin(SetTimer, "Object"));
		FBPGen::SetPinDefault(SetTimer, TEXT("FunctionName"), TEXT("OnTimerTick"));
		FBPGen::SetPinDefault(SetTimer, TEXT("Time"), TEXT("0.5"));
		FBPGen::SetPinDefault(SetTimer, TEXT("bLooping"), TEXT("true"));
	}
	else R.Notes.Add(TEXT("K2_SetTimer node not spawned; verify function name in UE 5.4."));

	UK2Node_CallFunction* Print = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 900, 0);
	if (Print) { FBPGen::SetPinDefault(Print, TEXT("InString"), TEXT("Timer scheduled")); FBPGen::ConnectExec(SetTimer, Print); }

	UK2Node_Self* Self2 = FBPGen::SpawnSelf(G, 1180, 160);
	UK2Node_CallFunction* ClearTimer = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "K2_ClearTimer", 1200, 0);
	if (ClearTimer)
	{
		FBPGen::ConnectExec(Print, ClearTimer);
		if (Self2) FBPGen::Connect(OutPin(Self2, TEXT("self")), InPin(ClearTimer, "Object"));
		FBPGen::SetPinDefault(ClearTimer, TEXT("FunctionName"), TEXT("OnTimerTick"));
	}

	R.CompileStatus = FBPGen::CompileBlueprint(BP);
	R.Notes.Add(TEXT("Covers Latent Delay + Set/Clear Timer by Function Name + Custom Event callback. Timeline and Async-Action nodes are NOT auto-generated (high API risk / no guaranteed engine-native async node) -> see manual steps; status = cannot-auto-cover."));
	return R;
}

// ============================================================================
// BP_08_ComplexGameplayLikeGraph
// ============================================================================
FBPGenAssetResult FBPGenTestBlueprints::Build_BP08_ComplexGameplay()
{
	FBPGenAssetResult R; R.AssetPath = Path08(); R.AssetType = TEXT("Actor");

	UBlueprint* BP = FBPGen::CreateActorBlueprint(Path08(), AActor::StaticClass());
	if (!BP) { R.Notes.Add(TEXT("CreateActorBlueprint failed.")); return R; }
	R.bCreated = true;

	UScriptStruct* DataStruct = FBPGen::LoadStruct(FString(FBPGenSupportAssets::PathStruct()) + TEXT(".ST_BPParserTestData"));
	UEnum* StateEnum = FBPGen::LoadEnum(FString(FBPGenSupportAssets::PathEnum()) + TEXT(".E_BPParserTestState"));
	UClass* TargetClass = FBPGen::LoadBPClass(FBPGenSupportAssets::PathTargetActor());
	UClass* IfaceClass  = FBPGen::LoadBPClass(FBPGenSupportAssets::PathInterface());

	// Members
	if (DataStruct) FBPGen::AddVariable(BP, "DataList", FBPGen::AsArray(FBPGen::PinStruct(DataStruct)), FString(), TEXT("08_State"));
	FBPGen::AddVariable(BP, "ResultMap", FBPGen::AsMap(FBPGen::PinName(), FBPGen::PinInt()), FString(), TEXT("08_State"));
	FBPGen::AddVariable(BP, "TargetRef", FBPGen::PinObject(AActor::StaticClass()), FString(), TEXT("08_State"));
	if (StateEnum) FBPGen::AddVariable(BP, "CurrentState", FBPGen::PinByteEnum(StateEnum), FString(), TEXT("08_State"), true);

	TArray<FBPGenParam> DispParams;
	DispParams.Add({ "Summary", FBPGen::PinString(), false });
	FBPGen::AddEventDispatcher(BP, "OnSummaryReady", DispParams);

	UEdGraph* G = FBPGen::GetEventGraph(BP);
	if (!G) { R.CompileStatus = FBPGen::CompileBlueprint(BP); return R; }

	// Region comments
	FBPGen::AddComment(G, TEXT("Init Test Data"),   -80, -200, 420, 460);
	FBPGen::AddComment(G, TEXT("Spawn Target"),      360, -200, 420, 460);
	FBPGen::AddComment(G, TEXT("Validate Target"),   800, -200, 420, 460);
	FBPGen::AddComment(G, TEXT("Process State"),    1240, -200, 520, 620);
	FBPGen::AddComment(G, TEXT("Dispatch Result"),  1780, -200, 420, 320);
	FBPGen::AddComment(G, TEXT("Log Summary"),      2200, -200, 420, 320);

	UK2Node_Event* Begin = FBPGen::SpawnEvent(G, "ReceiveBeginPlay", AActor::StaticClass(), -40, 0);
	UK2Node_ExecutionSequence* Seq = FBPGen::SpawnSequence(G, 4, 80, 0);
	FBPGen::ConnectExec(Begin, Seq);
	TArray<UEdGraphPin*> Then = FBPGen::GetExecOutPins(Seq);
	auto SeqThen = [&](int32 i) -> UEdGraphPin* { return Then.IsValidIndex(i) ? Then[i] : nullptr; };

	// Init Test Data: MakeStruct -> Array Add to DataList
	if (DataStruct)
	{
		UK2Node_MakeStruct* Make = FBPGen::SpawnMakeStruct(G, DataStruct, -60, 60);
		if (Make) FBPGen::SetPinDefault(Make, TEXT("ID"), TEXT("100"));
		UK2Node_VariableGet* GetList = FBPGen::SpawnVarGet(G, "DataList", -60, 260);
		UK2Node_CallFunction* ArrAdd = FBPGen::SpawnCallArrayFunc(G, UKismetArrayLibrary::StaticClass(), "Array_Add", 140, 60);
		if (ArrAdd)
		{
			if (GetList) FBPGen::Connect(OutPin(GetList, "DataList"), InPin(ArrAdd, "TargetArray"));
			if (Make) FBPGen::Connect(OutPin(Make, DataStruct->GetName()), InPin(ArrAdd, "NewItem"));
			if (SeqThen(0)) FBPGen::Connect(SeqThen(0), FBPGen::FindExecIn(ArrAdd));
		}
	}

	// Spawn Target
	UK2Node_SpawnActorFromClass* Spawn = FBPGen::SpawnActorNode(G, TargetClass, 420, 60);
	UK2Node_VariableSet* SetTarget = FBPGen::SpawnVarSet(G, "TargetRef", 620, 60);
	if (Spawn && SetTarget)
	{
		if (SeqThen(1)) FBPGen::Connect(SeqThen(1), FBPGen::FindExecIn(Spawn));
		FBPGen::ConnectExec(Spawn, SetTarget);
		FBPGen::Connect(OutPin(Spawn, TEXT("ReturnValue")), InPin(SetTarget, "TargetRef"));
	}

	// Validate Target: IsValid -> Branch
	UK2Node_VariableGet* GetTarget = FBPGen::SpawnVarGet(G, "TargetRef", 840, 200);
	UK2Node_CallFunction* IsValid = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "IsValid", 1000, 60);
	UK2Node_IfThenElse* Branch = FBPGen::SpawnBranch(G, 1180, 60);
	if (IsValid && Branch)
	{
		if (GetTarget) FBPGen::Connect(OutPin(GetTarget, "TargetRef"), InPin(IsValid, "Object"));
		FBPGen::Connect(OutPin(IsValid, "ReturnValue"), Branch->GetConditionPin());
		if (SeqThen(2)) FBPGen::Connect(SeqThen(2), FBPGen::FindExecIn(Branch));
	}

	// Process State: Cast + Interface call on True; SwitchEnum
	UK2Node_DynamicCast* Cast = nullptr;
	if (TargetClass && Branch)
	{
		Cast = FBPGen::SpawnCast(G, TargetClass, 1300, 40);
		if (Cast)
		{
			FBPGen::Connect(Branch->GetThenPin(), FBPGen::FindExecIn(Cast));
			if (GetTarget) FBPGen::Connect(OutPin(GetTarget, "TargetRef"), Cast->GetCastSourcePin());
		}
	}
	if (IfaceClass && Cast)
	{
		UK2Node_Message* Msg = FBPGen::SpawnInterfaceMessage(G, IfaceClass, "GetParserTestName", 1500, 40);
		if (Msg)
		{
			FBPGen::Connect(Cast->GetCastResultPin(), InPin(Msg, TEXT("self")));
			FBPGen::Connect(Cast->GetValidCastPin(), FBPGen::FindExecIn(Msg));
		}
	}
	if (StateEnum)
	{
		UK2Node_VariableGet* GetState = FBPGen::SpawnVarGet(G, "CurrentState", 1300, 320);
		UK2Node_SwitchEnum* Sw = FBPGen::SpawnSwitchEnum(G, StateEnum, 1500, 320);
		if (Sw && GetState) { if (UEdGraphPin* Sel = Sw->GetSelectionPin()) FBPGen::Connect(OutPin(GetState, "CurrentState"), Sel); }
	}

	// Dispatch Result via reroute -> Call dispatcher
	UK2Node_Knot* Reroute = FBPGen::SpawnReroute(G, 1820, 80);
	UK2Node_CallDelegate* CallDisp = FBPGen::SpawnCallDelegate(G, "OnSummaryReady", 1980, 40);
	if (CallDisp)
	{
		FBPGen::SetPinDefault(CallDisp, TEXT("Summary"), TEXT("Processed"));
		if (Branch && Reroute)
		{
			FBPGen::Connect(Branch->GetElsePin(), Reroute->GetInputPin());
			FBPGen::Connect(Reroute->GetOutputPin(), FBPGen::FindExecIn(CallDisp));
		}
	}

	// Log Summary
	UK2Node_CallFunction* LogPrint = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 2240, 40);
	if (LogPrint)
	{
		FBPGen::SetPinDefault(LogPrint, TEXT("InString"), TEXT("BP_08 summary done"));
		if (CallDisp) FBPGen::ConnectExec(CallDisp, LogPrint);
	}

	R.CompileStatus = FBPGen::CompileBlueprint(BP);
	R.Notes.Add(TEXT("Complex graph: 6 comment regions, Sequence, MakeStruct+Array Add, SpawnActor, IsValid+Branch, Cast, Interface Message, SwitchEnum, Reroute, Call Dispatcher, PrintString. Some branches converge; verify wiring in UE."));
	if (!DataStruct || !StateEnum || !TargetClass || !IfaceClass) R.Notes.Add(TEXT("Some regions degraded because support assets were unavailable at build time."));
	return R;
}

// ============================================================================
// BP_09_NodeFormatting_Comments_Reroutes
// ============================================================================
FBPGenAssetResult FBPGenTestBlueprints::Build_BP09_FormattingCommentsReroutes()
{
	FBPGenAssetResult R; R.AssetPath = Path09(); R.AssetType = TEXT("Actor");

	UBlueprint* BP = FBPGen::CreateActorBlueprint(Path09(), AActor::StaticClass());
	if (!BP) { R.Notes.Add(TEXT("CreateActorBlueprint failed.")); return R; }
	R.bCreated = true;

	FBPGen::AddVariable(BP, "LayoutInt", FBPGen::PinInt(), TEXT("5"), TEXT("09_Layout"), true);
	FBPGen::AddVariable(BP, "LayoutString", FBPGen::PinString(), TEXT("Layout"), TEXT("09_Layout"), true);
	FBPGen::AddVariable(BP, "UnusedFlag", FBPGen::PinBool(), TEXT("false"), TEXT("09_Layout"), true);

	UEdGraph* G = FBPGen::GetEventGraph(BP);
	if (!G) { R.CompileStatus = FBPGen::CompileBlueprint(BP); return R; }

	FBPGen::AddComment(G, TEXT("Region A: Connected exec with long reroute chain"), -80, -160, 1700, 360);
	FBPGen::AddComment(G, TEXT("Region B: Unconnected nodes + default-only pins (far away)"), -80, 700, 900, 400);
	FBPGen::AddComment(G, FString(), 1100, 700, 360, 200);   // empty comment text (edge case)

	UK2Node_Event* Begin = FBPGen::SpawnEvent(G, "ReceiveBeginPlay", AActor::StaticClass(), 0, 0);

	// Long wire via two reroutes -> PrintString
	UK2Node_Knot* R1 = FBPGen::SpawnReroute(G, 400, 40);
	UK2Node_Knot* R2 = FBPGen::SpawnReroute(G, 900, 40);
	UK2Node_CallFunction* Print = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 1300, 0);
	if (Print)
	{
		Print->NodeComment = TEXT("This node carries a NodeComment bubble");
		FBPGen::SetPinDefault(Print, TEXT("InString"), TEXT("Rerouted exec"));
		if (R1 && R2)
		{
			FBPGen::Connect(FBPGen::FindExecOut(Begin), R1->GetInputPin());
			FBPGen::Connect(R1->GetOutputPin(), R2->GetInputPin());
			FBPGen::Connect(R2->GetOutputPin(), FBPGen::FindExecIn(Print));
		}
	}

	// Unconnected VariableGet (dangling output) far away
	FBPGen::SpawnVarGet(G, "UnusedFlag", -40, 760);
	// PrintString with no exec connections, defaults only (unconnected node)
	UK2Node_CallFunction* Orphan = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 300, 760);
	if (Orphan) FBPGen::SetPinDefault(Orphan, TEXT("InString"), TEXT("Orphan default-only"));
	// A pure math node with no consumer (dangling data output)
	UK2Node_CallFunction* PureAdd = FBPGen::SpawnCallFunc(G, UKismetMathLibrary::StaticClass(), "Add_IntInt", 600, 900);
	if (PureAdd) { FBPGen::SetPinDefault(PureAdd, TEXT("A"), TEXT("2")); FBPGen::SetPinDefault(PureAdd, TEXT("B"), TEXT("3")); }

	R.CompileStatus = FBPGen::CompileBlueprint(BP);
	R.Notes.Add(TEXT("Layout/formatting test: multiple comment boxes (incl. empty), long reroute chain, NodeComment bubble, far-apart coordinates, unconnected node, default-only pins, dangling data output, unused variable. Expected to compile with warnings."));
	return R;
}

// ============================================================================
// BP_10_ParserRoundTrip_Master
// ============================================================================
FBPGenAssetResult FBPGenTestBlueprints::Build_BP10_RoundTripMaster()
{
	FBPGenAssetResult R; R.AssetPath = Path10(); R.AssetType = TEXT("Actor");

	UBlueprint* BP = FBPGen::CreateActorBlueprint(Path10(), AActor::StaticClass());
	if (!BP) { R.Notes.Add(TEXT("CreateActorBlueprint failed.")); return R; }
	R.bCreated = true;

	UScriptStruct* DataStruct = FBPGen::LoadStruct(FString(FBPGenSupportAssets::PathStruct()) + TEXT(".ST_BPParserTestData"));
	UEnum* StateEnum = FBPGen::LoadEnum(FString(FBPGenSupportAssets::PathEnum()) + TEXT(".E_BPParserTestState"));
	UClass* TargetClass = FBPGen::LoadBPClass(FBPGenSupportAssets::PathTargetActor());
	UClass* IfaceClass  = FBPGen::LoadBPClass(FBPGenSupportAssets::PathInterface());

	if (StateEnum) FBPGen::AddVariable(BP, "MasterState", FBPGen::PinByteEnum(StateEnum), FString(), TEXT("10_Master"), true);
	FBPGen::AddVariable(BP, "TargetRef", FBPGen::PinObject(AActor::StaticClass()), FString(), TEXT("10_Master"));

	TArray<FBPGenParam> DispParams;
	DispParams.Add({ "Result", FBPGen::PinString(), false });
	FBPGen::AddEventDispatcher(BP, "OnMasterDone", DispParams);

	UEdGraph* G = FBPGen::GetEventGraph(BP);
	if (!G) { R.CompileStatus = FBPGen::CompileBlueprint(BP); return R; }

	FBPGen::AddComment(G, TEXT("Master regression entry: Spawn -> Cast -> Interface -> Enum branch -> Dispatch -> Log"), -80, -160, 2200, 620);

	UK2Node_Event* Begin = FBPGen::SpawnEvent(G, "ReceiveBeginPlay", AActor::StaticClass(), 0, 0);

	UK2Node_SpawnActorFromClass* Spawn = FBPGen::SpawnActorNode(G, TargetClass, 240, 0);
	FBPGen::ConnectExec(Begin, Spawn);
	UK2Node_VariableSet* SetTarget = FBPGen::SpawnVarSet(G, "TargetRef", 520, 0);
	if (Spawn && SetTarget)
	{
		FBPGen::ConnectExec(Spawn, SetTarget);
		FBPGen::Connect(OutPin(Spawn, TEXT("ReturnValue")), InPin(SetTarget, "TargetRef"));
	}

	UK2Node_DynamicCast* Cast = nullptr;
	if (TargetClass)
	{
		Cast = FBPGen::SpawnCast(G, TargetClass, 800, 0);
		if (Cast && SetTarget)
		{
			FBPGen::ConnectExec(SetTarget, Cast);
			// Cast source from the spawn return (VariableSet passthrough pin is unreliable for wildcard cast input).
			if (Spawn) { FBPGen::Connect(OutPin(Spawn, TEXT("ReturnValue")), Cast->GetCastSourcePin()); }
		}
	}

	if (IfaceClass && Cast)
	{
		UK2Node_Message* Msg = FBPGen::SpawnInterfaceMessage(G, IfaceClass, "GetParserTestName", 1080, 0);
		if (Msg)
		{
			FBPGen::Connect(Cast->GetCastResultPin(), InPin(Msg, TEXT("self")));
			FBPGen::Connect(Cast->GetValidCastPin(), FBPGen::FindExecIn(Msg));

			UK2Node_CallDelegate* CallDisp = FBPGen::SpawnCallDelegate(G, "OnMasterDone", 1360, 0);
			if (CallDisp)
			{
				FBPGen::ConnectExec(Msg, CallDisp);
				if (UEdGraphPin* NameOut = OutPin(Msg, TEXT("Name"))) FBPGen::Connect(NameOut, InPin(CallDisp, "Result"));
			}
			UK2Node_CallFunction* Print = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 1640, 0);
			if (Print) { FBPGen::SetPinDefault(Print, TEXT("InString"), TEXT("Master done")); if (CallDisp) FBPGen::ConnectExec(CallDisp, Print); }
		}
	}

	// Construct struct + enum branch (data-side coverage)
	if (DataStruct)
	{
		UK2Node_MakeStruct* Make = FBPGen::SpawnMakeStruct(G, DataStruct, 240, 320);
		if (Make) FBPGen::SetPinDefault(Make, TEXT("ID"), TEXT("999"));
	}
	if (StateEnum)
	{
		UK2Node_VariableGet* GetState = FBPGen::SpawnVarGet(G, "MasterState", 240, 460);
		UK2Node_SwitchEnum* Sw = FBPGen::SpawnSwitchEnum(G, StateEnum, 440, 460);
		if (Sw && GetState) { if (UEdGraphPin* Sel = Sw->GetSelectionPin()) FBPGen::Connect(OutPin(GetState, "MasterState"), Sel); }
	}

	R.CompileStatus = FBPGen::CompileBlueprint(BP);
	R.Notes.Add(TEXT("Master entry references support assets: SpawnActor(BP_BPParserTargetActor), Cast, Interface Message, Call Dispatcher, MakeStruct, SwitchEnum, PrintString. Get-Class-Defaults node omitted (use Spawn+Cast instead); noted as manual-optional."));
	return R;
}

// ============================================================================
// BP_99_NegativeOrEdgeCases  (intentionally messy, but compiles with warnings)
// ============================================================================
FBPGenAssetResult FBPGenTestBlueprints::Build_BP99_NegativeEdgeCases()
{
	FBPGenAssetResult R; R.AssetPath = Path99(); R.AssetType = TEXT("Actor");

	UBlueprint* BP = FBPGen::CreateActorBlueprint(Path99(), AActor::StaticClass());
	if (!BP) { R.Notes.Add(TEXT("CreateActorBlueprint failed.")); return R; }
	R.bCreated = true;

	FBPGen::AddVariable(BP, "NeverUsed", FBPGen::PinInt(), TEXT("0"), TEXT("99_Negative"), true);

	UEdGraph* G = FBPGen::GetEventGraph(BP);
	if (!G) { R.CompileStatus = FBPGen::CompileBlueprint(BP); return R; }

	FBPGen::AddComment(G, TEXT("INTENTIONAL EDGE CASES (see report): dangling exec, orphan nodes, reroute-to-nowhere, empty pins"), -80, -160, 1400, 560);

	UK2Node_Event* Begin = FBPGen::SpawnEvent(G, "ReceiveBeginPlay", AActor::StaticClass(), 0, 0);
	// BeginPlay -> PrintString (this part is valid so the BP still compiles)
	UK2Node_CallFunction* Print = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 280, 0);
	if (Print) { FBPGen::SetPinDefault(Print, TEXT("InString"), TEXT("BP_99 valid path")); FBPGen::ConnectExec(Begin, Print); }

	// Reroute to nowhere (input connected, output dangling)
	UK2Node_Knot* Knot = FBPGen::SpawnReroute(G, 280, 200);
	if (Knot && Print) FBPGen::Connect(FBPGen::FindExecOut(Print), Knot->GetInputPin());

	// Orphan branch with no incoming exec (dangling), condition default only
	FBPGen::SpawnBranch(G, 600, 200);

	// Orphan VariableGet (dangling output)
	FBPGen::SpawnVarGet(G, "NeverUsed", 600, 380);

	R.CompileStatus = FBPGen::CompileBlueprint(BP);
	R.Notes.Add(TEXT("INTENTIONAL: dangling reroute output, orphan Branch (no exec in), orphan VariableGet, unused variable. Valid BeginPlay->PrintString path keeps it compilable (expect warnings). These are deliberate negative cases, NOT bugs."));
	return R;
}
