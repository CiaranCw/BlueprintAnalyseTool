// Copyright BlueprintAnalyseTool. All Rights Reserved.
//
// Builder for BP_11_SupplementalCoverage: folds in coverage items the generator
// supports but that did not fit naturally into BP_01..BP_10 — container
// Make/Get/Find/Remove/Values, Vector4/Color/DateTime/Timespan pins, a
// by-reference function parameter, and the DoN / WhileLoop / ForEachLoopWithBreak
// macros. Same defensive style: every node spawn is guarded.
#include "BPGenTestBlueprints.h"
#include "BPGen.h"
#include "BPGenOrchestrator.h"
#include "BPParserTestGenModule.h"

#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"

#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraph.h"

#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet/BlueprintSetLibrary.h"
#include "Kismet/BlueprintMapLibrary.h"

#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeSet.h"
#include "K2Node_MakeMap.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"

namespace
{
	UEdGraphPin* OutPin(UEdGraphNode* N, const FString& Name) { return FBPGen::FindPin(N, Name, EGPD_Output); }
	UEdGraphPin* InPin(UEdGraphNode* N, const FString& Name)  { return FBPGen::FindPin(N, Name, EGPD_Input); }

	// First output data pin (avoids depending on container make-node output pin names).
	UEdGraphPin* FirstDataOut(UEdGraphNode* N)
	{
		if (!N) { return nullptr; }
		for (UEdGraphPin* P : N->Pins)
		{
			if (P && P->Direction == EGPD_Output && P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) { return P; }
		}
		return nullptr;
	}
}

FBPGenAssetResult FBPGenTestBlueprints::Build_BP11_SupplementalCoverage()
{
	FBPGenAssetResult R; R.AssetPath = Path11(); R.AssetType = TEXT("Actor");

	UBlueprint* BP = FBPGen::CreateActorBlueprint(Path11(), AActor::StaticClass());
	if (!BP) { R.Notes.Add(TEXT("CreateActorBlueprint failed.")); return R; }
	R.bCreated = true;

	// ---- Variables (extra primitive/struct pin types + containers) ----------
	FBPGen::AddVariable(BP, "SupVector4", FBPGen::PinVector4(), FString(), TEXT("11_Types"));
	FBPGen::AddVariable(BP, "SupColor",   FBPGen::PinColor(),   FString(), TEXT("11_Types"));

	UScriptStruct* DateTimeStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/CoreUObject.DateTime"));
	UScriptStruct* TimespanStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/CoreUObject.Timespan"));
	if (DateTimeStruct) { FBPGen::AddVariable(BP, "SupDateTime", FBPGen::PinStruct(DateTimeStruct), FString(), TEXT("11_Types")); }
	else                { R.Notes.Add(TEXT("DateTime struct not resolved; SupDateTime skipped (mark needs-confirm).")); }
	if (TimespanStruct) { FBPGen::AddVariable(BP, "SupTimespan", FBPGen::PinStruct(TimespanStruct), FString(), TEXT("11_Types")); }
	else                { R.Notes.Add(TEXT("Timespan struct not resolved; SupTimespan skipped (mark needs-confirm).")); }

	FBPGen::AddVariable(BP, "SupArray", FBPGen::AsArray(FBPGen::PinInt()), FString(), TEXT("11_Containers"));
	FBPGen::AddVariable(BP, "SupSet",   FBPGen::AsSet(FBPGen::PinInt()),   FString(), TEXT("11_Containers"));
	FBPGen::AddVariable(BP, "SupMap",   FBPGen::AsMap(FBPGen::PinName(), FBPGen::PinInt()), FString(), TEXT("11_Containers"));

	// ---- Function with a by-reference parameter -----------------------------
	{
		FEdGraphPinType ByRef = FBPGen::PinInt();
		ByRef.bIsReference = true;
		TArray<FBPGenParam> P;
		P.Add({ "InOutValue", ByRef, false });
		UEdGraph* Fn = FBPGen::AddFunctionGraph(BP, "AccumulateByRef", P, /*pure*/ false);
		if (Fn)
		{
			UEdGraphNode* Entry = FBPGen::FindFunctionEntry(Fn);
			UEdGraphNode* Result = FBPGen::FindFunctionResult(Fn);
			if (Entry && Result) { FBPGen::ConnectExec(Entry, Result); }
			R.Notes.Add(TEXT("AccumulateByRef has a by-reference int parameter (InOutValue)."));
		}
	}

	UEdGraph* G = FBPGen::GetEventGraph(BP);
	if (!G) { R.CompileStatus = FBPGen::CompileBlueprint(BP); return R; }

	FBPGen::AddComment(G, TEXT("Make Containers (Array/Set/Map) -> store into variables"), -80, -200, 1100, 420);
	FBPGen::AddComment(G, TEXT("Container Get/Find/Remove/Values"), 1060, -200, 1100, 620);
	FBPGen::AddComment(G, TEXT("Boundary loops: DoN / WhileLoop / ForEachLoopWithBreak"), -80, 460, 1500, 480);
	FBPGen::AddComment(G, TEXT("Extra type pins: Vector4 / Color / DateTime / Timespan"), 1560, 460, 760, 480);

	UK2Node_Event* Begin = FBPGen::SpawnEvent(G, "ReceiveBeginPlay", AActor::StaticClass(), -40, 0);

	// --- Make containers and store ---
	UK2Node_MakeArray* MakeArr = FBPGen::SpawnMakeArray(G, 3, 80, 0);
	UK2Node_VariableSet* SetArr = FBPGen::SpawnVarSet(G, "SupArray", 360, 0);
	if (MakeArr && SetArr)
	{
		FBPGen::Connect(FirstDataOut(MakeArr), InPin(SetArr, "SupArray"));
		FBPGen::ConnectExec(Begin, SetArr);
		FBPGen::SetPinDefault(MakeArr, TEXT("[0]"), TEXT("10"));
		FBPGen::SetPinDefault(MakeArr, TEXT("[1]"), TEXT("20"));
		FBPGen::SetPinDefault(MakeArr, TEXT("[2]"), TEXT("30"));
	}
	UK2Node_MakeSet* MakeSet = FBPGen::SpawnMakeSet(G, 2, 80, 160);
	UK2Node_VariableSet* SetSet = FBPGen::SpawnVarSet(G, "SupSet", 360, 160);
	if (MakeSet && SetSet) { FBPGen::Connect(FirstDataOut(MakeSet), InPin(SetSet, "SupSet")); FBPGen::ConnectExec(SetArr, SetSet); }
	UK2Node_MakeMap* MakeMap = FBPGen::SpawnMakeMap(G, 2, 80, 320);
	UK2Node_VariableSet* SetMap = FBPGen::SpawnVarSet(G, "SupMap", 360, 320);
	if (MakeMap && SetMap) { FBPGen::Connect(FirstDataOut(MakeMap), InPin(SetMap, "SupMap")); FBPGen::ConnectExec(SetSet, SetMap); }

	UEdGraphNode* Tail = SetMap ? (UEdGraphNode*)SetMap : (UEdGraphNode*)Begin;

	// --- Container Get / Find / Remove / Values ---
	UK2Node_VariableGet* GetArr = FBPGen::SpawnVarGet(G, "SupArray", 1080, -120);
	UK2Node_CallFunction* ArrGet = FBPGen::SpawnCallArrayFunc(G, UKismetArrayLibrary::StaticClass(), "Array_Get", 1320, -160);
	if (ArrGet && GetArr) { FBPGen::Connect(OutPin(GetArr, "SupArray"), InPin(ArrGet, "TargetArray")); FBPGen::SetPinDefault(ArrGet, TEXT("Index"), TEXT("0")); }

	UK2Node_CallFunction* ArrRemove = FBPGen::SpawnCallArrayFunc(G, UKismetArrayLibrary::StaticClass(), "Array_RemoveItem", 1320, 0);
	if (ArrRemove)
	{
		if (GetArr) FBPGen::Connect(OutPin(GetArr, "SupArray"), InPin(ArrRemove, "TargetArray"));
		FBPGen::SetPinDefault(ArrRemove, TEXT("Item"), TEXT("20"));
		FBPGen::ConnectExec(Tail, ArrRemove); Tail = ArrRemove;
	}

	UK2Node_VariableGet* GetSet = FBPGen::SpawnVarGet(G, "SupSet", 1080, 140);
	UK2Node_CallFunction* SetRemove = FBPGen::SpawnCallFunc(G, UBlueprintSetLibrary::StaticClass(), "Set_Remove", 1320, 160);
	if (SetRemove)
	{
		if (GetSet) FBPGen::Connect(OutPin(GetSet, "SupSet"), InPin(SetRemove, "TargetSet"));
		FBPGen::SetPinDefault(SetRemove, TEXT("Item"), TEXT("1"));
		FBPGen::ConnectExec(Tail, SetRemove); Tail = SetRemove;
	}

	UK2Node_VariableGet* GetMap = FBPGen::SpawnVarGet(G, "SupMap", 1080, 320);
	UK2Node_CallFunction* MapFind = FBPGen::SpawnCallFunc(G, UBlueprintMapLibrary::StaticClass(), "Map_Find", 1320, 300);
	if (MapFind && GetMap) { FBPGen::Connect(OutPin(GetMap, "SupMap"), InPin(MapFind, "TargetMap")); FBPGen::SetPinDefault(MapFind, TEXT("Key"), TEXT("a")); }
	UK2Node_CallFunction* MapValues = FBPGen::SpawnCallFunc(G, UBlueprintMapLibrary::StaticClass(), "Map_Values", 1320, 400);
	if (MapValues && GetMap) { FBPGen::Connect(OutPin(GetMap, "SupMap"), InPin(MapValues, "TargetMap")); }
	UK2Node_CallFunction* MapRemove = FBPGen::SpawnCallFunc(G, UBlueprintMapLibrary::StaticClass(), "Map_Remove", 1320, 500);
	if (MapRemove)
	{
		if (GetMap) FBPGen::Connect(OutPin(GetMap, "SupMap"), InPin(MapRemove, "TargetMap"));
		FBPGen::SetPinDefault(MapRemove, TEXT("Key"), TEXT("a"));
		FBPGen::ConnectExec(Tail, MapRemove); Tail = MapRemove;
	}

	UK2Node_CallFunction* Print = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 1700, 0);
	if (Print) { FBPGen::SetPinDefault(Print, TEXT("InString"), TEXT("BP_11 containers done")); FBPGen::ConnectExec(Tail, Print); Tail = Print; }

	// --- Boundary loops ---
	UK2Node_ExecutionSequence* Seq = FBPGen::SpawnSequence(G, 3, 80, 560);
	if (Seq && Print) FBPGen::ConnectExec(Print, Seq);
	TArray<UEdGraphPin*> Then = FBPGen::GetExecOutPins(Seq);
	auto SeqThen = [&](int32 i) -> UEdGraphPin* { return Then.IsValidIndex(i) ? Then[i] : nullptr; };

	if (UK2Node_MacroInstance* DoN = FBPGen::SpawnStdMacro(G, TEXT("DoN"), 360, 540))
	{
		if (SeqThen(0)) FBPGen::Connect(SeqThen(0), FBPGen::FindExecIn(DoN));
		FBPGen::SetPinDefault(DoN, TEXT("N"), TEXT("3"));
		UK2Node_CallFunction* P = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 700, 540);
		if (P && OutPin(DoN, TEXT("Exit"))) { FBPGen::SetPinDefault(P, TEXT("InString"), TEXT("DoN")); FBPGen::Connect(OutPin(DoN, TEXT("Exit")), FBPGen::FindExecIn(P)); }
	}
	else R.Notes.Add(TEXT("DoN macro not found."));

	if (UK2Node_MacroInstance* While = FBPGen::SpawnStdMacro(G, TEXT("WhileLoop"), 360, 660))
	{
		if (SeqThen(1)) FBPGen::Connect(SeqThen(1), FBPGen::FindExecIn(While));
		// Condition left at default false -> completes immediately (no infinite loop).
		UK2Node_CallFunction* P = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 700, 680);
		if (P && OutPin(While, TEXT("Completed"))) { FBPGen::SetPinDefault(P, TEXT("InString"), TEXT("While done")); FBPGen::Connect(OutPin(While, TEXT("Completed")), FBPGen::FindExecIn(P)); }
	}
	else R.Notes.Add(TEXT("WhileLoop macro not found."));

	if (UK2Node_MacroInstance* ForEachBreak = FBPGen::SpawnStdMacro(G, TEXT("ForEachLoopWithBreak"), 360, 800))
	{
		if (SeqThen(2)) FBPGen::Connect(SeqThen(2), FBPGen::FindExecIn(ForEachBreak));
		UK2Node_VariableGet* GetArr2 = FBPGen::SpawnVarGet(G, "SupArray", 360, 900);
		if (GetArr2) FBPGen::Connect(OutPin(GetArr2, "SupArray"), InPin(ForEachBreak, TEXT("Array")));
		UK2Node_CallFunction* P = FBPGen::SpawnCallFunc(G, UKismetSystemLibrary::StaticClass(), "PrintString", 700, 800);
		if (P && OutPin(ForEachBreak, TEXT("Loop Body"))) { FBPGen::SetPinDefault(P, TEXT("InString"), TEXT("elem")); FBPGen::Connect(OutPin(ForEachBreak, TEXT("Loop Body")), FBPGen::FindExecIn(P)); }
	}
	else R.Notes.Add(TEXT("ForEachLoopWithBreak macro not found."));

	// --- Extra type-pin coverage (data-only Gets) ---
	FBPGen::SpawnVarGet(G, "SupVector4", 1600, 540);
	FBPGen::SpawnVarGet(G, "SupColor",   1600, 640);
	if (DateTimeStruct) FBPGen::SpawnVarGet(G, "SupDateTime", 1600, 740);
	if (TimespanStruct) FBPGen::SpawnVarGet(G, "SupTimespan", 1600, 840);

	R.CompileStatus = FBPGen::CompileBlueprint(BP);
	R.Notes.Add(TEXT("Supplemental coverage: MakeArray/Set/Map, Array Get/RemoveItem, Set Remove, Map Find/Values/Remove, Vector4/Color (+DateTime/Timespan if resolved), by-ref function param, DoN/WhileLoop/ForEachLoopWithBreak macros. Macros + DateTime/Timespan need UE confirmation."));
	return R;
}
