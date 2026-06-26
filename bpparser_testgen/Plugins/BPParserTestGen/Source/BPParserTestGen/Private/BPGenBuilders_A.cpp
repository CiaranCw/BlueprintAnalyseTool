// Copyright BlueprintAnalyseTool. All Rights Reserved.
//
// Builders for BP_01 .. BP_05. Every helper call is guarded; a failed node
// spawn logs a warning and the build continues, so partial failures degrade
// gracefully instead of aborting the run.
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
#include "Kismet/BlueprintSetLibrary.h"
#include "Kismet/BlueprintMapLibrary.h"

#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchString.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Message.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_Knot.h"
#include "K2Node_Self.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"

namespace
{
	UEdGraphPin* OutPin(UEdGraphNode* N, const FString& Name) { return FBPGen::FindPin(N, Name, EGPD_Output); }
	UEdGraphPin* InPin(UEdGraphNode* N, const FString& Name)  { return FBPGen::FindPin(N, Name, EGPD_Input); }

	FString StructPath()    { return FString(FBPGenSupportAssets::PathStruct())    + TEXT(".ST_BPParserTestData"); }
	FString EnumPath()      { return FString(FBPGenSupportAssets::PathEnum())      + TEXT(".E_BPParserTestState"); }
}

// ============================================================================
// BP_01_PrimitivePins_Basic
// ============================================================================
FBPGenAssetResult FBPGenTestBlueprints::Build_BP01_PrimitivePins()
{
	FBPGenAssetResult R; R.AssetPath = Path01(); R.AssetType = TEXT("Actor");

	UBlueprint* BP = FBPGen::CreateActorBlueprint(Path01(), AActor::StaticClass());
	if (!BP) { R.Notes.Add(TEXT("CreateActorBlueprint failed.")); return R; }
	R.bCreated = true;

	// ---- Variables: one per primitive Pin category --------------------------
	FBPGen::AddVariable(BP, "TestBool",        FBPGen::PinBool(),        TEXT("true"),        TEXT("01_Primitives"), true);
	FBPGen::AddVariable(BP, "TestByte",        FBPGen::PinByte(),        TEXT("7"),           TEXT("01_Primitives"));
	FBPGen::AddVariable(BP, "TestInt",         FBPGen::PinInt(),         TEXT("42"),          TEXT("01_Primitives"));
	FBPGen::AddVariable(BP, "TestInt64",       FBPGen::PinInt64(),       TEXT("9000000000"),  TEXT("01_Primitives"));
	FBPGen::AddVariable(BP, "TestFloat",       FBPGen::PinFloat(),       TEXT("3.14"),        TEXT("01_Primitives"));
	FBPGen::AddVariable(BP, "TestDouble",      FBPGen::PinDouble(),      TEXT("2.7182818"),   TEXT("01_Primitives"));
	FBPGen::AddVariable(BP, "TestName",        FBPGen::PinName(),        TEXT("ParserName"),  TEXT("01_Primitives"));
	FBPGen::AddVariable(BP, "TestString",      FBPGen::PinString(),      TEXT("Hello"),       TEXT("01_Primitives"));
	FBPGen::AddVariable(BP, "TestText",        FBPGen::PinText(),        FString(),           TEXT("01_Primitives"));
	FBPGen::AddVariable(BP, "TestVector",      FBPGen::PinVector(),      FString(),           TEXT("01_Primitives"));
	FBPGen::AddVariable(BP, "TestVector2D",    FBPGen::PinVector2D(),    FString(),           TEXT("01_Primitives"));
	FBPGen::AddVariable(BP, "TestRotator",     FBPGen::PinRotator(),     FString(),           TEXT("01_Primitives"));
	FBPGen::AddVariable(BP, "TestTransform",   FBPGen::PinTransform(),   FString(),           TEXT("01_Primitives"));
	FBPGen::AddVariable(BP, "TestLinearColor", FBPGen::PinLinearColor(), FString(),           TEXT("01_Primitives"));

	UEdGraph* G = FBPGen::GetEventGraph(BP);
	if (!G) { R.Notes.Add(TEXT("No EventGraph.")); R.CompileStatus = FBPGen::CompileBlueprint(BP); return R; }

	FBPGen::AddComment(G, TEXT("Primitive Pins: Get/Set + Math (pure) + Make/Break Transform + PrintString (impure)"), -80, -160, 1700, 720);

	UK2Node_Event* Begin = FBPGen::SpawnEvent(G, "ReceiveBeginPlay", AActor::StaticClass(), 0, 0);

	// Pure math: TestInt + 8 -> Set TestInt
	UK2Node_VariableGet* GetInt = FBPGen::SpawnVarGet(G, "TestInt", 0, 200);
	UK2Node_CallFunction* Add = FBPGen::SpawnCallFunc(G, UKismetMathLibrary::StaticClass(), "Add_IntInt", 260, 200);
	if (Add)
	{
		if (GetInt) FBPGen::Connect(OutPin(GetInt, "TestInt"), InPin(Add, "A"));
		FBPGen::SetPinDefault(Add, TEXT("B"), TEXT("8"));   // node default value (unconnected pin)
	}
	UK2Node_VariableSet* SetInt = FBPGen::SpawnVarSet(G, "TestInt", 520, 40);
	if (Add && SetInt) FBPGen::Connect(OutPin(Add, "ReturnValue"), InPin(SetInt, "TestInt"));

	// Pure double multiply (separate, demonstrates real/double pin)
	UK2Node_VariableGet* GetDbl = FBPGen::SpawnVarGet(G, "TestDouble", 0, 360);
	UK2Node_CallFunction* Mul = FBPGen::SpawnCallFunc(G, UKismetMathLibrary::StaticClass(), "Multiply_DoubleDouble", 260, 360);
	if (Mul)
	{
		if (GetDbl) FBPGen::Connect(OutPin(GetDbl, "TestDouble"), InPin(Mul, "A"));
		FBPGen::SetPinDefault(Mul, TEXT("B"), TEXT("2.0"));
	}

	// Make/Break Transform (pure)
	UK2Node_VariableGet* GetVec = FBPGen::SpawnVarGet(G, "TestVector", 0, 520);
	UK2Node_VariableGet* GetRot = FBPGen::SpawnVarGet(G, "TestRotator", 0, 620);
	UK2Node_CallFunction* MakeXf = FBPGen::SpawnCallFunc(G, UKismetMathLibrary::StaticClass(), "MakeTransform", 260, 520);
	if (MakeXf)
	{
		if (GetVec) FBPGen::Connect(OutPin(GetVec, "TestVector"), InPin(MakeXf, "Location"));
		if (GetRot) FBPGen::Connect(OutPin(GetRot, "TestRotator"), InPin(MakeXf, "Rotation"));
	}
	UK2Node_VariableSet* SetXf = FBPGen::SpawnVarSet(G, "TestTransform", 520, 360);
	if (MakeXf && SetXf) FBPGen::Connect(OutPin(MakeXf, "ReturnValue"), InPin(SetXf, "TestTransform"));

	UK2Node_CallFunction* BreakXf = FBPGen::SpawnCallFunc(G, UKismetMathLibrary::StaticClass(), "BreakTransform", 780, 520);
	if (BreakXf)
	{
		UK2Node_VariableGet* GetXf = FBPGen::SpawnVarGet(G, "TestTransform", 540, 640);
		if (GetXf) FBPGen::Connect(OutPin(GetXf, "TestTransform"), InPin(BreakXf, "InTransform"));
	}

	// String build + PrintString (impure)
	UK2Node_CallFunction* IntToStr = FBPGen::SpawnCallFunc(G, UKismetStringLibrary::StaticClass(), "Conv_IntToString", 780, 200);
	UK2Node_VariableGet* GetInt2 = FBPGen::SpawnVarGet(G, "TestInt", 540, 280);
	if (IntToStr && GetInt2) FBPGen::Connect(OutPin(GetInt2, "TestInt"), InPin(IntToStr, "InInt"));

	UK2Node_CallFunction* Concat = FBPGen::SpawnCallFunc(G, UKismetStringLibrary::StaticClass(), "Concat_StrStr", 1040, 200);
	if (Concat)
	{
		FBPGen::SetPinDefault(Concat, TEXT("A"), TEXT("TestInt="));
		if (IntToStr) FBPGen::Connect(OutPin(IntToStr, "ReturnValue"), InPin(Concat, "B"));
	}

	UK2Node_CallFunction* Print = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 1320, 40);
	if (Print && Concat) FBPGen::Connect(OutPin(Concat, "ReturnValue"), InPin(Print, "InString"));

	// Exec chain: BeginPlay -> SetInt -> SetXf -> Print
	FBPGen::ConnectExec(Begin, SetInt);
	FBPGen::ConnectExec(SetInt, SetXf);
	FBPGen::ConnectExec(SetXf, Print);

	R.CompileStatus = FBPGen::CompileBlueprint(BP);
	R.Notes.Add(TEXT("Covers primitive Pin categories via variables; pure math + Make/Break Transform; impure PrintString; node default on Add.B and Concat.A; unconnected defaults on PrintString Duration/Color."));
	return R;
}

// ============================================================================
// BP_02_StructEnumContainers
// ============================================================================
FBPGenAssetResult FBPGenTestBlueprints::Build_BP02_StructEnumContainers()
{
	FBPGenAssetResult R; R.AssetPath = Path02(); R.AssetType = TEXT("Actor");

	UBlueprint* BP = FBPGen::CreateActorBlueprint(Path02(), AActor::StaticClass());
	if (!BP) { R.Notes.Add(TEXT("CreateActorBlueprint failed.")); return R; }
	R.bCreated = true;

	UScriptStruct* DataStruct = FBPGen::LoadStruct(StructPath());
	UEnum* StateEnum = FBPGen::LoadEnum(EnumPath());
	if (!DataStruct) R.Notes.Add(TEXT("ST_BPParserTestData not loaded (build support assets first)."));
	if (!StateEnum)  R.Notes.Add(TEXT("E_BPParserTestState not loaded (build support assets first)."));

	if (DataStruct) FBPGen::AddVariable(BP, "DataItem", FBPGen::PinStruct(DataStruct), FString(), TEXT("02_Data"));
	if (StateEnum)  FBPGen::AddVariable(BP, "StateValue", FBPGen::PinByteEnum(StateEnum), FString(), TEXT("02_Data"), true);
	FBPGen::AddVariable(BP, "IntArray",  FBPGen::AsArray(FBPGen::PinInt()),  FString(), TEXT("02_Containers"));
	FBPGen::AddVariable(BP, "NameSet",   FBPGen::AsSet(FBPGen::PinName()),   FString(), TEXT("02_Containers"));
	FBPGen::AddVariable(BP, "ScoreMap",  FBPGen::AsMap(FBPGen::PinName(), FBPGen::PinFloat()), FString(), TEXT("02_Containers"));

	UEdGraph* G = FBPGen::GetEventGraph(BP);
	if (!G) { R.CompileStatus = FBPGen::CompileBlueprint(BP); return R; }

	FBPGen::AddComment(G, TEXT("Make/Break Struct"),            -80, -160, 760, 520);
	FBPGen::AddComment(G, TEXT("Switch on Enum"),               760, -160, 620, 620);
	FBPGen::AddComment(G, TEXT("Array Set Map + ForEachLoop"),  -80,  420, 1460, 640);

	UK2Node_Event* Begin = FBPGen::SpawnEvent(G, "ReceiveBeginPlay", AActor::StaticClass(), 0, 0);

	UEdGraphNode* Tail = Begin;

	// --- Struct ---
	if (DataStruct)
	{
		UK2Node_MakeStruct* Make = FBPGen::SpawnMakeStruct(G, DataStruct, 0, 80);
		if (Make)
		{
			FBPGen::SetPinDefault(Make, TEXT("ID"), TEXT("1"));
			FBPGen::SetPinDefault(Make, TEXT("Score"), TEXT("10.0"));
			// DisplayName pin intentionally left at default (unconnected text default).
			UK2Node_VariableSet* SetData = FBPGen::SpawnVarSet(G, "DataItem", 320, 0);
			if (SetData)
			{
				FBPGen::Connect(OutPin(Make, DataStruct->GetName()), InPin(SetData, "DataItem"));
				FBPGen::ConnectExec(Tail, SetData);
				Tail = SetData;
			}
			// Break for read-side coverage (pure, off the exec chain)
			UK2Node_VariableGet* GetData = FBPGen::SpawnVarGet(G, "DataItem", 0, 320);
			UK2Node_BreakStruct* Break = FBPGen::SpawnBreakStruct(G, DataStruct, 320, 320);
			if (GetData && Break) FBPGen::Connect(OutPin(GetData, "DataItem"), InPin(Break, DataStruct->GetName()));
		}
	}

	// --- Switch on Enum ---
	if (StateEnum)
	{
		UK2Node_VariableGet* GetState = FBPGen::SpawnVarGet(G, "StateValue", 760, 0);
		UK2Node_SwitchEnum* Sw = FBPGen::SpawnSwitchEnum(G, StateEnum, 1000, 0);
		if (Sw)
		{
			FBPGen::ConnectExec(Tail, Sw);
			Tail = Sw;
			if (GetState)
			{
				UEdGraphPin* Sel = Sw->GetSelectionPin();
				if (Sel) FBPGen::Connect(OutPin(GetState, "StateValue"), Sel);
			}
			// Print for the "Moving" case if present.
			UK2Node_CallFunction* PrintMoving = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 1240, 0);
			if (PrintMoving)
			{
				FBPGen::SetPinDefault(PrintMoving, TEXT("InString"), TEXT("State=Moving"));
				if (UEdGraphPin* Case = OutPin(Sw, TEXT("Moving"))) FBPGen::Connect(Case, FBPGen::FindExecIn(PrintMoving));
			}
		}
	}

	// --- Containers ---
	UK2Node_VariableGet* GetArr = FBPGen::SpawnVarGet(G, "IntArray", -80, 520);
	UK2Node_CallFunction* ArrAdd = FBPGen::SpawnCallFunc(G, UKismetArrayLibrary::StaticClass(), "Array_Add", 160, 480);
	if (ArrAdd)
	{
		if (GetArr) FBPGen::Connect(OutPin(GetArr, "IntArray"), InPin(ArrAdd, "TargetArray"));
		FBPGen::SetPinDefault(ArrAdd, TEXT("NewItem"), TEXT("4"));
		FBPGen::ConnectExec(Tail, ArrAdd);
		Tail = ArrAdd;
	}
	UK2Node_CallFunction* ArrLen = FBPGen::SpawnCallFunc(G, UKismetArrayLibrary::StaticClass(), "Array_Length", 160, 600);
	if (ArrLen && GetArr) FBPGen::Connect(OutPin(GetArr, "IntArray"), InPin(ArrLen, "TargetArray"));

	// ForEachLoop over IntArray
	UK2Node_MacroInstance* ForEach = FBPGen::SpawnStdMacro(G, TEXT("ForEachLoop"), 420, 480);
	if (ForEach)
	{
		FBPGen::ConnectExec(Tail, ForEach);
		Tail = ForEach;
		if (GetArr) FBPGen::Connect(OutPin(GetArr, "IntArray"), InPin(ForEach, TEXT("Array")));
		UK2Node_CallFunction* PrintElem = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 760, 460);
		if (PrintElem && OutPin(ForEach, TEXT("Loop Body")))
		{
			FBPGen::Connect(OutPin(ForEach, TEXT("Loop Body")), FBPGen::FindExecIn(PrintElem));
		}
	}
	else { R.Notes.Add(TEXT("ForEachLoop macro not found; container iteration uses Array_Length only.")); }

	// Set Add / Contains
	UK2Node_VariableGet* GetSet = FBPGen::SpawnVarGet(G, "NameSet", -80, 760);
	UK2Node_CallFunction* SetAdd = FBPGen::SpawnCallFunc(G, UBlueprintSetLibrary::StaticClass(), "Set_Add", 160, 740);
	if (SetAdd)
	{
		if (GetSet) FBPGen::Connect(OutPin(GetSet, "NameSet"), InPin(SetAdd, "TargetSet"));
		FBPGen::SetPinDefault(SetAdd, TEXT("NewItem"), TEXT("Alpha"));
		FBPGen::ConnectExec(Tail, SetAdd);
		Tail = SetAdd;
	}
	UK2Node_CallFunction* SetContains = FBPGen::SpawnCallFunc(G, UBlueprintSetLibrary::StaticClass(), "Set_Contains", 160, 860);
	if (SetContains && GetSet)
	{
		FBPGen::Connect(OutPin(GetSet, "NameSet"), InPin(SetContains, "TargetSet"));
		FBPGen::SetPinDefault(SetContains, TEXT("ItemToFind"), TEXT("Alpha"));
	}

	// Map Add / Keys
	UK2Node_VariableGet* GetMap = FBPGen::SpawnVarGet(G, "ScoreMap", -80, 980);
	UK2Node_CallFunction* MapAdd = FBPGen::SpawnCallFunc(G, UBlueprintMapLibrary::StaticClass(), "Map_Add", 160, 980);
	if (MapAdd)
	{
		if (GetMap) FBPGen::Connect(OutPin(GetMap, "ScoreMap"), InPin(MapAdd, "TargetMap"));
		FBPGen::SetPinDefault(MapAdd, TEXT("Key"), TEXT("Alpha"));
		FBPGen::SetPinDefault(MapAdd, TEXT("Value"), TEXT("1.0"));
		FBPGen::ConnectExec(Tail, MapAdd);
		Tail = MapAdd;
	}
	UK2Node_CallFunction* MapKeys = FBPGen::SpawnCallFunc(G, UBlueprintMapLibrary::StaticClass(), "Map_Keys", 420, 980);
	if (MapKeys && GetMap) FBPGen::Connect(OutPin(GetMap, "ScoreMap"), InPin(MapKeys, "TargetMap"));

	R.CompileStatus = FBPGen::CompileBlueprint(BP);
	R.Notes.Add(TEXT("Covers Make/Break Struct, Switch on Enum, Array Add/Length, Set Add/Contains, Map Add/Keys, ForEachLoop, nested array<name> in struct field. Wildcard container Pins resolved by connecting the container variable."));
	return R;
}

// ============================================================================
// BP_03_ObjectReference_Cast_Interface
// ============================================================================
FBPGenAssetResult FBPGenTestBlueprints::Build_BP03_ObjectRefCastInterface()
{
	FBPGenAssetResult R; R.AssetPath = Path03(); R.AssetType = TEXT("Actor");

	UBlueprint* BP = FBPGen::CreateActorBlueprint(Path03(), AActor::StaticClass());
	if (!BP) { R.Notes.Add(TEXT("CreateActorBlueprint failed.")); return R; }
	R.bCreated = true;

	UClass* TargetClass = FBPGen::LoadBPClass(FBPGenSupportAssets::PathTargetActor());
	UClass* IfaceClass  = FBPGen::LoadBPClass(FBPGenSupportAssets::PathInterface());
	if (!TargetClass) R.Notes.Add(TEXT("BP_BPParserTargetActor class not loaded."));
	if (!IfaceClass)  R.Notes.Add(TEXT("BPI_BPParserTest class not loaded."));

	FBPGen::AddVariable(BP, "TargetActorRef", FBPGen::PinObject(AActor::StaticClass()), FString(), TEXT("03_Refs"), true);
	FBPGen::AddVariable(BP, "TargetClassRef", FBPGen::PinClass(AActor::StaticClass()),  FString(), TEXT("03_Refs"), true);
	FBPGen::AddVariable(BP, "SoftActorRef",   FBPGen::PinSoftObject(AActor::StaticClass()), FString(), TEXT("03_Refs"), true);
	FBPGen::AddVariable(BP, "SoftClassRef",   FBPGen::PinSoftClass(AActor::StaticClass()),  FString(), TEXT("03_Refs"), true);
	FBPGen::AddVariable(BP, "SceneCompRef",   FBPGen::PinObject(USceneComponent::StaticClass()), FString(), TEXT("03_Refs"));

	UEdGraph* G = FBPGen::GetEventGraph(BP);
	if (!G) { R.CompileStatus = FBPGen::CompileBlueprint(BP); return R; }

	FBPGen::AddComment(G, TEXT("Spawn + Cast (cast object input)"), -80, -160, 1100, 560);
	FBPGen::AddComment(G, TEXT("Interface Message + Self + Target Pin + IsValid"), 1060, -160, 980, 560);

	UK2Node_Event* Begin = FBPGen::SpawnEvent(G, "ReceiveBeginPlay", AActor::StaticClass(), 0, 0);

	UK2Node_SpawnActorFromClass* Spawn = FBPGen::SpawnActorNode(G, TargetClass, 260, 0);
	FBPGen::ConnectExec(Begin, Spawn);

	UK2Node_DynamicCast* Cast = nullptr;
	if (TargetClass)
	{
		Cast = FBPGen::SpawnCast(G, TargetClass, 640, 0, /*pure*/ false);
		if (Cast && Spawn)
		{
			FBPGen::Connect(OutPin(Spawn, TEXT("ReturnValue")), Cast->GetCastSourcePin());
			FBPGen::ConnectExec(Spawn, Cast);
		}
	}

	// Interface message call (interface message) on cast result (target pin)
	if (IfaceClass && Cast)
	{
		UK2Node_Message* Msg = FBPGen::SpawnInterfaceMessage(G, IfaceClass, "GetParserTestName", 1080, 0);
		if (Msg)
		{
			FBPGen::Connect(Cast->GetCastResultPin(), InPin(Msg, TEXT("self")));
			FBPGen::Connect(Cast->GetValidCastPin(), FBPGen::FindExecIn(Msg));

			UK2Node_CallFunction* Print = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 1440, 0);
			if (Print)
			{
				FBPGen::ConnectExec(Msg, Print);
				// Connect returned Name (output pin named "Name") to PrintString.InString.
				if (UEdGraphPin* NameOut = OutPin(Msg, TEXT("Name"))) FBPGen::Connect(NameOut, InPin(Print, "InString"));
				else if (UEdGraphPin* RetOut = OutPin(Msg, TEXT("ReturnValue"))) FBPGen::Connect(RetOut, InPin(Print, "InString"));
			}
		}
	}

	// Self + IsValid + GetComponentByClass (component reference)
	FBPGen::SpawnSelf(G, 260, 360);
	UK2Node_VariableGet* GetRef = FBPGen::SpawnVarGet(G, "TargetActorRef", 260, 460);
	UK2Node_CallFunction* IsValid = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "IsValid", 520, 460);
	if (IsValid && GetRef) FBPGen::Connect(OutPin(GetRef, "TargetActorRef"), InPin(IsValid, "Object"));

	UK2Node_CallFunction* GetComp = FBPGen::SpawnCallFunc(G, AActor::StaticClass(), "GetComponentByClass", 520, 560);
	if (GetComp) FBPGen::SetPinDefaultObject(GetComp, TEXT("ComponentClass"), USceneComponent::StaticClass());

	R.CompileStatus = FBPGen::CompileBlueprint(BP);
	R.Notes.Add(TEXT("Covers Object/Actor/Class/SoftObject/SoftClass/Component refs, Self, SpawnActorFromClass, Cast (object input/valid/invalid), Interface Message, IsValid, Target Pin. Soft ref default objects left empty (needs manual confirm)."));
	if (!TargetClass || !IfaceClass) R.Notes.Add(TEXT("Some nodes skipped because support classes were unavailable at build time."));
	return R;
}

// ============================================================================
// BP_04_ExecFlow_Control
// ============================================================================
FBPGenAssetResult FBPGenTestBlueprints::Build_BP04_ExecFlowControl()
{
	FBPGenAssetResult R; R.AssetPath = Path04(); R.AssetType = TEXT("Actor");

	UBlueprint* BP = FBPGen::CreateActorBlueprint(Path04(), AActor::StaticClass());
	if (!BP) { R.Notes.Add(TEXT("CreateActorBlueprint failed.")); return R; }
	R.bCreated = true;

	FBPGen::AddVariable(BP, "FlowBool", FBPGen::PinBool(), TEXT("true"), TEXT("04_Flow"), true);
	FBPGen::AddVariable(BP, "FlowArray", FBPGen::AsArray(FBPGen::PinInt()), FString(), TEXT("04_Flow"));

	UEdGraph* G = FBPGen::GetEventGraph(BP);
	if (!G) { R.CompileStatus = FBPGen::CompileBlueprint(BP); return R; }

	FBPGen::AddComment(G, TEXT("Sequence fan-out"),        -80, -200, 360, 1100);
	FBPGen::AddComment(G, TEXT("Branch + converge (Reroute)"), 320, -200, 760, 320);
	FBPGen::AddComment(G, TEXT("DoOnce / FlipFlop / Gate"), 320, 160, 760, 360);
	FBPGen::AddComment(G, TEXT("ForLoop / ForLoopWithBreak / ForEachLoop"), 320, 540, 900, 360);
	FBPGen::AddComment(G, TEXT("Switch Int / Switch String / Delay (latent)"), 320, 920, 900, 360);

	UK2Node_Event* Begin = FBPGen::SpawnEvent(G, "ReceiveBeginPlay", AActor::StaticClass(), -40, 0);
	UK2Node_ExecutionSequence* Seq = FBPGen::SpawnSequence(G, 5, 80, 0);
	FBPGen::ConnectExec(Begin, Seq);
	TArray<UEdGraphPin*> Then = FBPGen::GetExecOutPins(Seq);

	auto SeqThen = [&](int32 i) -> UEdGraphPin* { return Then.IsValidIndex(i) ? Then[i] : nullptr; };

	// Then0: Branch -> converge through reroute -> PrintString
	{
		UK2Node_IfThenElse* Branch = FBPGen::SpawnBranch(G, 380, -120);
		UK2Node_VariableGet* GetB = FBPGen::SpawnVarGet(G, "FlowBool", 380, -220);
		if (Branch)
		{
			if (SeqThen(0)) FBPGen::Connect(SeqThen(0), FBPGen::FindExecIn(Branch));
			if (GetB) FBPGen::Connect(OutPin(GetB, "FlowBool"), Branch->GetConditionPin());
			UK2Node_Knot* Reroute = FBPGen::SpawnReroute(G, 760, -60);
			UK2Node_CallFunction* Print = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 900, -120);
			if (Print) FBPGen::SetPinDefault(Print, TEXT("InString"), TEXT("Branch converge"));
			if (Reroute && Print)
			{
				FBPGen::Connect(Branch->GetThenPin(), Reroute->GetInputPin());
				FBPGen::Connect(Branch->GetElsePin(), Reroute->GetInputPin());   // two execs converge
				FBPGen::Connect(Reroute->GetOutputPin(), FBPGen::FindExecIn(Print));
			}
		}
	}

	// Then1: DoOnce -> Print
	if (UK2Node_MacroInstance* DoOnce = FBPGen::SpawnStdMacro(G, TEXT("DoOnce"), 380, 220))
	{
		if (SeqThen(1)) FBPGen::Connect(SeqThen(1), FBPGen::FindExecIn(DoOnce));
		UK2Node_CallFunction* P = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 720, 220);
		if (P) { FBPGen::SetPinDefault(P, TEXT("InString"), TEXT("DoOnce")); if (OutPin(DoOnce, TEXT("Completed"))) FBPGen::Connect(OutPin(DoOnce, TEXT("Completed")), FBPGen::FindExecIn(P)); }
	}
	else R.Notes.Add(TEXT("DoOnce macro not found."));

	// Then2: FlipFlop -> A/B prints
	if (UK2Node_MacroInstance* Flip = FBPGen::SpawnStdMacro(G, TEXT("FlipFlop"), 380, 340))
	{
		if (SeqThen(2)) FBPGen::Connect(SeqThen(2), FBPGen::FindExecIn(Flip));
		UK2Node_CallFunction* PA = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 720, 320);
		UK2Node_CallFunction* PB = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 720, 400);
		if (PA) { FBPGen::SetPinDefault(PA, TEXT("InString"), TEXT("Flip A")); if (OutPin(Flip, TEXT("A"))) FBPGen::Connect(OutPin(Flip, TEXT("A")), FBPGen::FindExecIn(PA)); }
		if (PB) { FBPGen::SetPinDefault(PB, TEXT("InString"), TEXT("Flip B")); if (OutPin(Flip, TEXT("B"))) FBPGen::Connect(OutPin(Flip, TEXT("B")), FBPGen::FindExecIn(PB)); }
	}
	else R.Notes.Add(TEXT("FlipFlop macro not found."));

	// Then3: Gate -> ForLoop -> Print(index)
	UEdGraphNode* Gate = FBPGen::SpawnStdMacro(G, TEXT("Gate"), 380, 580);
	if (Gate)
	{
		if (SeqThen(3)) FBPGen::Connect(SeqThen(3), FBPGen::FindExecIn(Gate));
	}
	else R.Notes.Add(TEXT("Gate macro not found."));

	if (UK2Node_MacroInstance* ForLoop = FBPGen::SpawnStdMacro(G, TEXT("ForLoop"), 700, 580))
	{
		if (Gate && OutPin(Gate, TEXT("Exit"))) FBPGen::Connect(OutPin(Gate, TEXT("Exit")), FBPGen::FindExecIn(ForLoop));
		FBPGen::SetPinDefault(ForLoop, TEXT("First Index"), TEXT("0"));
		FBPGen::SetPinDefault(ForLoop, TEXT("Last Index"),  TEXT("3"));
		UK2Node_CallFunction* P = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 1020, 560);
		if (P && OutPin(ForLoop, TEXT("Loop Body"))) FBPGen::Connect(OutPin(ForLoop, TEXT("Loop Body")), FBPGen::FindExecIn(P));
		// ForLoopWithBreak after completed, then ForEachLoop over FlowArray
		if (UK2Node_MacroInstance* ForBreak = FBPGen::SpawnStdMacro(G, TEXT("ForLoopWithBreak"), 700, 700))
		{
			if (OutPin(ForLoop, TEXT("Completed"))) FBPGen::Connect(OutPin(ForLoop, TEXT("Completed")), FBPGen::FindExecIn(ForBreak));

			if (UK2Node_MacroInstance* ForEach = FBPGen::SpawnStdMacro(G, TEXT("ForEachLoop"), 700, 820))
			{
				if (OutPin(ForBreak, TEXT("Completed"))) FBPGen::Connect(OutPin(ForBreak, TEXT("Completed")), FBPGen::FindExecIn(ForEach));
				UK2Node_VariableGet* GetArr = FBPGen::SpawnVarGet(G, "FlowArray", 700, 940);
				if (GetArr) FBPGen::Connect(OutPin(GetArr, "FlowArray"), InPin(ForEach, TEXT("Array")));
				UK2Node_CallFunction* Pe = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 1020, 820);
				if (Pe && OutPin(ForEach, TEXT("Loop Body"))) FBPGen::Connect(OutPin(ForEach, TEXT("Loop Body")), FBPGen::FindExecIn(Pe));
			}
			else R.Notes.Add(TEXT("ForEachLoop macro not found."));
		}
		else R.Notes.Add(TEXT("ForLoopWithBreak macro not found."));
	}
	else R.Notes.Add(TEXT("ForLoop macro not found."));

	// Then4: Switch Int -> Switch String -> Delay (latent)
	{
		UK2Node_SwitchInteger* SwInt = FBPGen::SpawnSwitchInt(G, 380, 980);
		if (SwInt && SeqThen(4)) FBPGen::Connect(SeqThen(4), FBPGen::FindExecIn(SwInt));

		UK2Node_SwitchString* SwStr = FBPGen::SpawnSwitchString(G, { TEXT("Alpha"), TEXT("Beta") }, 680, 980);
		if (SwInt && SwStr) { if (UEdGraphPin* Def = SwInt->GetDefaultPin()) FBPGen::Connect(Def, FBPGen::FindExecIn(SwStr)); }

		UK2Node_CallFunction* Delay = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "Delay", 980, 980);
		if (Delay) FBPGen::SetPinDefault(Delay, TEXT("Duration"), TEXT("0.2"));
		if (SwStr && Delay) { if (UEdGraphPin* Def = SwStr->GetDefaultPin()) FBPGen::Connect(Def, FBPGen::FindExecIn(Delay)); }
		else if (SwInt && Delay) { if (UEdGraphPin* Def = SwInt->GetDefaultPin()) FBPGen::Connect(Def, FBPGen::FindExecIn(Delay)); }

		UK2Node_CallFunction* PostDelay = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 1280, 980);
		if (PostDelay) { FBPGen::SetPinDefault(PostDelay, TEXT("InString"), TEXT("after delay")); FBPGen::ConnectExec(Delay, PostDelay); }
	}

	R.CompileStatus = FBPGen::CompileBlueprint(BP);
	R.Notes.Add(TEXT("Covers Sequence, Branch (+converge via Reroute), DoOnce, FlipFlop, Gate, ForLoop, ForLoopWithBreak, ForEachLoop, Switch Int, Switch String, Delay (latent). Control-flow macros come from /Engine StandardMacros; verify each resolved in UE. Switch-String case literals are engine-default names."));
	return R;
}

// ============================================================================
// BP_05_Functions_Macros_LocalVariables
// ============================================================================
FBPGenAssetResult FBPGenTestBlueprints::Build_BP05_FunctionsMacrosLocals()
{
	FBPGenAssetResult R; R.AssetPath = Path05(); R.AssetType = TEXT("Actor");

	UBlueprint* BP = FBPGen::CreateActorBlueprint(Path05(), AActor::StaticClass());
	if (!BP) { R.Notes.Add(TEXT("CreateActorBlueprint failed.")); return R; }
	R.bCreated = true;

	// --- Function: ComputeScore(Base:int, Bonus:int) -> Total:int (impure, local var) ---
	{
		TArray<FBPGenParam> P;
		P.Add({ "Base",  FBPGen::PinInt(), false });
		P.Add({ "Bonus", FBPGen::PinInt(), false });
		P.Add({ "Total", FBPGen::PinInt(), true });
		UEdGraph* Fn = FBPGen::AddFunctionGraph(BP, "ComputeScore", P, /*pure*/ false);
		if (Fn)
		{
			FBPGen::AddLocalVariable(Fn, "TempSum", FBPGen::PinInt(), TEXT("0"));
			UEdGraphNode* Entry = FBPGen::FindFunctionEntry(Fn);
			UEdGraphNode* Result = FBPGen::FindFunctionResult(Fn);
			UK2Node_CallFunction* Add = FBPGen::SpawnCallFunc(Fn, UKismetMathLibrary::StaticClass(), "Add_IntInt", 340, 40);
			if (Entry && Add)
			{
				FBPGen::Connect(OutPin(Entry, TEXT("Base")),  InPin(Add, "A"));
				FBPGen::Connect(OutPin(Entry, TEXT("Bonus")), InPin(Add, "B"));
			}
			if (Add && Result) FBPGen::Connect(OutPin(Add, "ReturnValue"), InPin(Result, TEXT("Total")));
			if (Entry && Result) FBPGen::ConnectExec(Entry, Result);
		}
	}

	// --- Pure function: NormalizeScore(Raw:float) -> Norm:float ---
	{
		TArray<FBPGenParam> P;
		P.Add({ "Raw",  FBPGen::PinFloat(), false });
		P.Add({ "Norm", FBPGen::PinFloat(), true });
		UEdGraph* Fn = FBPGen::AddFunctionGraph(BP, "NormalizeScore", P, /*pure*/ true);
		if (Fn)
		{
			UEdGraphNode* Entry = FBPGen::FindFunctionEntry(Fn);
			UEdGraphNode* Result = FBPGen::FindFunctionResult(Fn);
			UK2Node_CallFunction* Div = FBPGen::SpawnCallFunc(Fn, UKismetMathLibrary::StaticClass(), "Divide_DoubleDouble", 340, 40);
			if (Entry && Div) FBPGen::Connect(OutPin(Entry, TEXT("Raw")), InPin(Div, "A"));
			if (Div) FBPGen::SetPinDefault(Div, TEXT("B"), TEXT("100.0"));
			if (Div && Result) FBPGen::Connect(OutPin(Div, "ReturnValue"), InPin(Result, TEXT("Norm")));
		}
	}

	// --- Multi in/out function: ComputeStats(A:int,B:int) -> (Sum:int, Product:int) ---
	{
		TArray<FBPGenParam> P;
		P.Add({ "A", FBPGen::PinInt(), false });
		P.Add({ "B", FBPGen::PinInt(), false });
		P.Add({ "Sum",     FBPGen::PinInt(), true });
		P.Add({ "Product", FBPGen::PinInt(), true });
		UEdGraph* Fn = FBPGen::AddFunctionGraph(BP, "ComputeStats", P, false);
		if (Fn)
		{
			UEdGraphNode* Entry = FBPGen::FindFunctionEntry(Fn);
			UEdGraphNode* Result = FBPGen::FindFunctionResult(Fn);
			UK2Node_CallFunction* Add = FBPGen::SpawnCallFunc(Fn, UKismetMathLibrary::StaticClass(), "Add_IntInt", 340, 0);
			UK2Node_CallFunction* Mul = FBPGen::SpawnCallFunc(Fn, UKismetMathLibrary::StaticClass(), "Multiply_IntInt", 340, 160);
			if (Entry && Add && Mul)
			{
				FBPGen::Connect(OutPin(Entry, TEXT("A")), InPin(Add, "A"));
				FBPGen::Connect(OutPin(Entry, TEXT("B")), InPin(Add, "B"));
				FBPGen::Connect(OutPin(Entry, TEXT("A")), InPin(Mul, "A"));
				FBPGen::Connect(OutPin(Entry, TEXT("B")), InPin(Mul, "B"));
			}
			if (Result)
			{
				if (Add) FBPGen::Connect(OutPin(Add, "ReturnValue"), InPin(Result, TEXT("Sum")));
				if (Mul) FBPGen::Connect(OutPin(Mul, "ReturnValue"), InPin(Result, TEXT("Product")));
			}
			if (Entry && Result) FBPGen::ConnectExec(Entry, Result);
		}
	}

	// --- Macro: Macro_LogWithPrefix(exec, Prefix:string, Message:string) -> exec ---
	{
		TArray<FBPGenParam> In;
		In.Add({ "Exec",    FBPGen::PinExec(),   false });
		In.Add({ "Prefix",  FBPGen::PinString(), false });
		In.Add({ "Message", FBPGen::PinString(), false });
		TArray<FBPGenParam> Out;
		Out.Add({ "Exec", FBPGen::PinExec(), false });
		UEdGraph* MG = FBPGen::AddMacroGraph(BP, "Macro_LogWithPrefix", In, Out);
		if (MG) R.Notes.Add(TEXT("Macro_LogWithPrefix created with exec + Prefix + Message inputs; body left for manual wiring of Concat+PrintString (tunnel pin wiring is fragile to automate)."));
		else    R.Notes.Add(TEXT("Macro_LogWithPrefix creation failed."));
	}

	// --- EventGraph: call function + macro ---
	UEdGraph* G = FBPGen::GetEventGraph(BP);
	if (G)
	{
		FBPGen::AddComment(G, TEXT("Call functions (ComputeScore / NormalizeScore / ComputeStats)"), -80, -160, 1500, 520);
		UK2Node_Event* Begin = FBPGen::SpawnEvent(G, "ReceiveBeginPlay", AActor::StaticClass(), 0, 0);
		// Skeleton class carries member-function signatures before a full compile.
		UClass* SelfClass = BP->SkeletonGeneratedClass ? BP->SkeletonGeneratedClass : BP->GeneratedClass;
		UK2Node_CallFunction* CallScore = FBPGen::SpawnCallFunc(G, SelfClass, "ComputeScore", 260, 0);
		if (CallScore)
		{
			FBPGen::SetPinDefault(CallScore, TEXT("Base"), TEXT("10"));
			FBPGen::SetPinDefault(CallScore, TEXT("Bonus"), TEXT("5"));
			FBPGen::ConnectExec(Begin, CallScore);
			UK2Node_CallFunction* IntToStr = FBPGen::SpawnCallFunc(G, UKismetStringLibrary::StaticClass(), "Conv_IntToString", 560, 120);
			if (IntToStr) FBPGen::Connect(OutPin(CallScore, TEXT("Total")), InPin(IntToStr, "InInt"));
			UK2Node_CallFunction* Print = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 820, 0);
			if (Print)
			{
				FBPGen::ConnectExec(CallScore, Print);
				if (IntToStr) FBPGen::Connect(OutPin(IntToStr, "ReturnValue"), InPin(Print, "InString"));
			}
		}
		else R.Notes.Add(TEXT("ComputeScore call node not spawned (function not yet on generated class at EventGraph build time; re-open in UE to confirm)."));
	}

	R.CompileStatus = FBPGen::CompileBlueprint(BP);
	R.Notes.Add(TEXT("Graphs: EventGraph + 3 Function graphs (ComputeScore impure+local var, NormalizeScore pure, ComputeStats multi-out) + 1 Macro graph. Macro body wiring is a manual step (see report)."));
	return R;
}
