// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPATEdit.h"
#include "BPParserTestGenModule.h"
#include "BPGen.h"
#include "BPGenIRDumper.h"
#include "BPGenUECompat.h"

#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphNode_Comment.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Knot.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/DateTime.h"
#include "UObject/UObjectGlobals.h"

// ============================================================================
// Local helpers
// ============================================================================
namespace
{
	const UEdGraphSchema_K2* K2() { return GetDefault<UEdGraphSchema_K2>(); }

	FString JStr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FString& Def = FString())
	{
		FString V; return (O.IsValid() && O->TryGetStringField(Key, V)) ? V : Def;
	}
	bool JBool(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, bool Def = false)
	{
		bool V; return (O.IsValid() && O->TryGetBoolField(Key, V)) ? V : Def;
	}
	double JNum(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Def = 0.0)
	{
		double V; return (O.IsValid() && O->TryGetNumberField(Key, V)) ? V : Def;
	}
	const TSharedPtr<FJsonObject>* JObj(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* P = nullptr;
		return (O.IsValid() && O->TryGetObjectField(Key, P)) ? P : nullptr;
	}

	FString SerializeJson(const TSharedPtr<FJsonObject>& Root)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Root.ToSharedRef(), W);
		return Out;
	}
	void WriteJson(const FString& Path, const TSharedPtr<FJsonObject>& Root)
	{
		FFileHelper::SaveStringToFile(SerializeJson(Root), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	void WriteText(const FString& Path, const FString& Text)
	{
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	FString Sanitize(const FString& In)
	{
		FString S = In;
		S.ReplaceInline(TEXT("/"), TEXT("_"));
		S.ReplaceInline(TEXT("\\"), TEXT("_"));
		S.ReplaceInline(TEXT("."), TEXT("_"));
		S.ReplaceInline(TEXT(":"), TEXT("_"));
		return S;
	}

	// ---- graph / node / pin resolution -------------------------------------
	UEdGraph* ResolveGraph(UBlueprint* BP, const FString& GraphName)
	{
		if (!BP) { return nullptr; }
		const bool bWantEvent = GraphName.IsEmpty() || GraphName.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase);
		auto Search = [&](const TArray<TObjectPtr<UEdGraph>>& List) -> UEdGraph*
		{
			for (UEdGraph* G : List) { if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) { return G; } }
			return nullptr;
		};
		if (UEdGraph* G = Search(BP->UbergraphPages)) { return G; }
		if (UEdGraph* G = Search(BP->FunctionGraphs)) { return G; }
		if (UEdGraph* G = Search(BP->MacroGraphs)) { return G; }
		if (bWantEvent && BP->UbergraphPages.Num() > 0) { return BP->UbergraphPages[0]; }
		return nullptr;
	}

	bool NodeMatches(UEdGraphNode* N, const TSharedPtr<FJsonObject>& Sel)
	{
		if (!N || !Sel.IsValid()) { return false; }
		FString S;
		if (Sel->TryGetStringField(TEXT("node_id"), S) && !S.IsEmpty())
		{
			if (!N->NodeGuid.ToString(EGuidFormats::Digits).Equals(S, ESearchCase::IgnoreCase)) { return false; }
		}
		if (Sel->TryGetStringField(TEXT("node_class"), S) && !S.IsEmpty())
		{
			if (!N->GetClass()->GetName().Equals(S, ESearchCase::IgnoreCase)) { return false; }
		}
		if (Sel->TryGetStringField(TEXT("node_title"), S) && !S.IsEmpty())
		{
			if (!N->GetNodeTitle(ENodeTitleType::ListView).ToString().Equals(S, ESearchCase::IgnoreCase)) { return false; }
		}
		if (Sel->TryGetStringField(TEXT("node_title_contains"), S) && !S.IsEmpty())
		{
			if (!N->GetNodeTitle(ENodeTitleType::ListView).ToString().Contains(S)) { return false; }
		}
		if (Sel->TryGetStringField(TEXT("function_name"), S) && !S.IsEmpty())
		{
			UK2Node_CallFunction* CF = Cast<UK2Node_CallFunction>(N);
			if (!CF || !CF->FunctionReference.GetMemberName().ToString().Equals(S, ESearchCase::IgnoreCase)) { return false; }
		}
		// Disambiguate ghost/placeholder events (UE auto-adds disabled Event stubs to Actor
		// EventGraphs) from the real, wired one: filter by whether an exec pin is connected.
		bool BoolCrit;
		if (Sel->TryGetBoolField(TEXT("exec_out_connected"), BoolCrit))
		{
			UEdGraphPin* P = FBPGen::FindExecOut(N);
			if ((P && P->LinkedTo.Num() > 0) != BoolCrit) { return false; }
		}
		if (Sel->TryGetBoolField(TEXT("exec_in_connected"), BoolCrit))
		{
			UEdGraphPin* P = FBPGen::FindExecIn(N);
			if ((P && P->LinkedTo.Num() > 0) != BoolCrit) { return false; }
		}
		return true;
	}

	// Resolve a unique node. Returns nullptr + sets Err on miss/ambiguity (unless match_index given).
	UEdGraphNode* ResolveNode(UEdGraph* G, const TSharedPtr<FJsonObject>& Sel, FString& Err)
	{
		if (!G) { Err = TEXT("graph not found"); return nullptr; }
		if (!Sel.IsValid()) { Err = TEXT("missing node selector"); return nullptr; }
		TArray<UEdGraphNode*> Hits;
		for (UEdGraphNode* N : G->Nodes) { if (NodeMatches(N, Sel)) { Hits.Add(N); } }
		if (Hits.Num() == 0) { Err = TEXT("no node matched selector"); return nullptr; }
		int32 Idx = 0;
		if (Sel->HasField(TEXT("match_index"))) { Idx = (int32)Sel->GetNumberField(TEXT("match_index")); }
		else if (Hits.Num() > 1) { Err = FString::Printf(TEXT("ambiguous selector (%d matches); add match_index or more criteria"), Hits.Num()); return nullptr; }
		if (!Hits.IsValidIndex(Idx)) { Err = TEXT("match_index out of range"); return nullptr; }
		return Hits[Idx];
	}

	// ---- pin type from JSON ------------------------------------------------
	FEdGraphPinType PinTypeFromSpec(const TSharedPtr<FJsonObject>& Spec, FString& Err)
	{
		const FString Cat = JStr(Spec, TEXT("category"), TEXT("int")).ToLower();
		const FString Sub = JStr(Spec, TEXT("sub_category"));
		const FString ObjPath = JStr(Spec, TEXT("object_type"));
		FEdGraphPinType T;
		if (Cat == TEXT("bool"))        { T = FBPGen::PinBool(); }
		else if (Cat == TEXT("byte"))   { T = FBPGen::PinByte(); }
		else if (Cat == TEXT("int"))    { T = FBPGen::PinInt(); }
		else if (Cat == TEXT("int64"))  { T = FBPGen::PinInt64(); }
		else if (Cat == TEXT("real"))   { T = (Sub == TEXT("double")) ? FBPGen::PinDouble() : FBPGen::PinFloat(); }
		else if (Cat == TEXT("float"))  { T = FBPGen::PinFloat(); }
		else if (Cat == TEXT("double")) { T = FBPGen::PinDouble(); }
		else if (Cat == TEXT("name"))   { T = FBPGen::PinName(); }
		else if (Cat == TEXT("string")) { T = FBPGen::PinString(); }
		else if (Cat == TEXT("text"))   { T = FBPGen::PinText(); }
		else if (Cat == TEXT("object")) { T = FBPGen::PinObject(ObjPath.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *ObjPath)); }
		else if (Cat == TEXT("class"))  { T = FBPGen::PinClass(ObjPath.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *ObjPath)); }
		else if (Cat == TEXT("struct")) { T = FBPGen::PinStruct(ObjPath.IsEmpty() ? nullptr : LoadObject<UScriptStruct>(nullptr, *ObjPath)); }
		else { Err = FString::Printf(TEXT("unsupported variable category '%s'"), *Cat); return T; }

		const FString Container = JStr(Spec, TEXT("container_type"), TEXT("none")).ToLower();
		if (Container == TEXT("array")) { T = FBPGen::AsArray(T); }
		else if (Container == TEXT("set")) { T = FBPGen::AsSet(T); }
		// map omitted (needs value type) — extend when required.
		return T;
	}

	// ---- node factory ------------------------------------------------------
	UEdGraphNode* MakeNode(UEdGraph* G, const TSharedPtr<FJsonObject>& Spec, FString& Err)
	{
		if (!G || !Spec.IsValid()) { Err = TEXT("missing new_node spec"); return nullptr; }
		const FString Cls = JStr(Spec, TEXT("node_class"));
		int32 X = 0, Y = 0;
		if (const TSharedPtr<FJsonObject>* Pos = JObj(Spec, TEXT("position")))
		{
			X = (int32)JNum(*Pos, TEXT("x"));
			Y = (int32)JNum(*Pos, TEXT("y"));
		}
		if (Cls == TEXT("K2Node_IfThenElse"))        { return FBPGen::SpawnBranch(G, X, Y); }
		if (Cls == TEXT("K2Node_ExecutionSequence")) { return FBPGen::SpawnSequence(G, FMath::Max(2, (int32)JNum(Spec, TEXT("num_outputs"), 2)), X, Y); }
		if (Cls == TEXT("K2Node_Knot"))              { return FBPGen::SpawnReroute(G, X, Y); }
		if (Cls == TEXT("K2Node_VariableGet"))       { return FBPGen::SpawnVarGet(G, *JStr(Spec, TEXT("variable_name")), X, Y); }
		if (Cls == TEXT("K2Node_VariableSet"))       { return FBPGen::SpawnVarSet(G, *JStr(Spec, TEXT("variable_name")), X, Y); }
		if (Cls == TEXT("UEdGraphNode_Comment") || Cls == TEXT("Comment"))
		{
			return FBPGen::AddComment(G, JStr(Spec, TEXT("comment"), TEXT("Comment")), X, Y,
				(int32)JNum(Spec, TEXT("w"), 300), (int32)JNum(Spec, TEXT("h"), 150));
		}
		if (Cls == TEXT("K2Node_CallFunction"))
		{
			const FString Fn = JStr(Spec, TEXT("function_name"));
			const FString Parent = JStr(Spec, TEXT("member_parent"), TEXT("/Script/Engine.KismetSystemLibrary"));
			UClass* Owner = LoadObject<UClass>(nullptr, *Parent);
			if (!Owner) { Err = FString::Printf(TEXT("cannot load member_parent class '%s'"), *Parent); return nullptr; }
			if (Fn.IsEmpty()) { Err = TEXT("K2Node_CallFunction requires function_name"); return nullptr; }
			return FBPGen::SpawnCallFunc(G, Owner, *Fn, X, Y);
		}
		Err = FString::Printf(TEXT("unsupported new_node.node_class '%s'"), *Cls);
		return nullptr;
	}

	// ---- IR diff -----------------------------------------------------------
	void CollectIR(const TSharedPtr<FJsonObject>& Root,
		TMap<FString, TSharedPtr<FJsonObject>>& OutNodes,
		TMap<FString, FString>& OutEdges)
	{
		if (!Root.IsValid()) { return; }
		const TArray<TSharedPtr<FJsonValue>>* Graphs;
		if (!Root->TryGetArrayField(TEXT("graphs"), Graphs)) { return; }
		for (const TSharedPtr<FJsonValue>& GV : *Graphs)
		{
			const TSharedPtr<FJsonObject> G = GV->AsObject();
			if (!G.IsValid()) { continue; }
			const TArray<TSharedPtr<FJsonValue>>* Nodes;
			if (G->TryGetArrayField(TEXT("nodes"), Nodes))
			{
				for (const TSharedPtr<FJsonValue>& NV : *Nodes)
				{
					const TSharedPtr<FJsonObject> N = NV->AsObject();
					if (N.IsValid()) { OutNodes.Add(N->GetStringField(TEXT("node_id")), N); }
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Edges;
			if (G->TryGetArrayField(TEXT("edges"), Edges))
			{
				for (const TSharedPtr<FJsonValue>& EV : *Edges)
				{
					const TSharedPtr<FJsonObject> E = EV->AsObject();
					if (!E.IsValid()) { continue; }
					const FString Key = FString::Printf(TEXT("%s|%s|%s|%s"),
						*E->GetStringField(TEXT("from_node")), *E->GetStringField(TEXT("from_pin")),
						*E->GetStringField(TEXT("to_node")), *E->GetStringField(TEXT("to_pin")));
					OutEdges.Add(Key, E->GetStringField(TEXT("edge_type")));
				}
			}
		}
	}

	TSharedPtr<FJsonObject> NodeRef(const TSharedPtr<FJsonObject>& N)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("node_id"), N->GetStringField(TEXT("node_id")));
		O->SetStringField(TEXT("node_class"), N->GetStringField(TEXT("node_class")));
		O->SetStringField(TEXT("node_title"), N->GetStringField(TEXT("node_title")));
		return O;
	}

	TSharedPtr<FJsonObject> BuildDiff(const FString& AssetPath,
		const TSharedPtr<FJsonObject>& Before, const TSharedPtr<FJsonObject>& After)
	{
		TMap<FString, TSharedPtr<FJsonObject>> BN, AN; TMap<FString, FString> BE, AE;
		CollectIR(Before, BN, BE);
		CollectIR(After, AN, AE);

		TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
		D->SetStringField(TEXT("schema_version"), TEXT("1.0"));
		D->SetStringField(TEXT("asset_path"), AssetPath);

		TArray<TSharedPtr<FJsonValue>> Added, Removed, Modified, AddedE, RemovedE;
		for (const auto& KV : AN)
		{
			if (!BN.Contains(KV.Key)) { Added.Add(MakeShared<FJsonValueObject>(NodeRef(KV.Value))); }
			else
			{
				const TSharedPtr<FJsonObject>& Bn = BN[KV.Key];
				const bool bTitle = Bn->GetStringField(TEXT("node_title")) != KV.Value->GetStringField(TEXT("node_title"));
				const bool bComment = Bn->GetStringField(TEXT("node_comment")) != KV.Value->GetStringField(TEXT("node_comment"));
				if (bTitle || bComment) { Modified.Add(MakeShared<FJsonValueObject>(NodeRef(KV.Value))); }
			}
		}
		for (const auto& KV : BN) { if (!AN.Contains(KV.Key)) { Removed.Add(MakeShared<FJsonValueObject>(NodeRef(KV.Value))); } }
		for (const auto& KV : AE) { if (!BE.Contains(KV.Key)) { TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("edge"), KV.Key); E->SetStringField(TEXT("edge_type"), KV.Value); AddedE.Add(MakeShared<FJsonValueObject>(E)); } }
		for (const auto& KV : BE) { if (!AE.Contains(KV.Key)) { TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("edge"), KV.Key); E->SetStringField(TEXT("edge_type"), KV.Value); RemovedE.Add(MakeShared<FJsonValueObject>(E)); } }

		D->SetArrayField(TEXT("added_nodes"), Added);
		D->SetArrayField(TEXT("removed_nodes"), Removed);
		D->SetArrayField(TEXT("modified_nodes"), Modified);
		D->SetArrayField(TEXT("added_edges"), AddedE);
		D->SetArrayField(TEXT("removed_edges"), RemovedE);
		D->SetArrayField(TEXT("modified_pins"), {});
		D->SetArrayField(TEXT("modified_variables"), {});
		D->SetArrayField(TEXT("modified_graphs"), {});
		D->SetArrayField(TEXT("unexpected_changes"), {});
		D->SetArrayField(TEXT("risk_notes"), {});
		return D;
	}

	FString IRToDot(const TSharedPtr<FJsonObject>& Root, const FString& Title)
	{
		FString S = FString::Printf(TEXT("digraph G {\n  rankdir=LR;\n  label=\"%s\";\n  node [shape=box,style=rounded];\n"), *Title);
		TMap<FString, TSharedPtr<FJsonObject>> N; TMap<FString, FString> E;
		CollectIR(Root, N, E);
		for (const auto& KV : N)
		{
			FString Lbl = KV.Value->GetStringField(TEXT("node_title"));
			Lbl.ReplaceInline(TEXT("\""), TEXT("'"));
			Lbl.ReplaceInline(TEXT("\n"), TEXT(" "));
			S += FString::Printf(TEXT("  n%s [label=\"%s\"];\n"), *KV.Key, *Lbl);
		}
		for (const auto& KV : E)
		{
			TArray<FString> P; KV.Key.ParseIntoArray(P, TEXT("|"));
			if (P.Num() == 4)
			{
				const TCHAR* Style = (KV.Value == TEXT("exec")) ? TEXT("solid") : TEXT("dashed");
				S += FString::Printf(TEXT("  n%s -> n%s [style=%s];\n"), *P[0], *P[2], Style);
			}
		}
		S += TEXT("}\n");
		return S;
	}
}

bool FBPATEdit::IsDestructiveOperation(const FString& Op)
{
	static const TSet<FString> Destructive = {
		TEXT("remove_node"), TEXT("disconnect_pins"), TEXT("insert_node_between"),
		TEXT("replace_edge"), TEXT("rewire_data_dependency"), TEXT("add_reroute_on_edge"),
		TEXT("remove_variable"), TEXT("change_variable_type"), TEXT("replace_node"),
		TEXT("disconnect_all_input_links"), TEXT("disconnect_all_output_links")
	};
	return Destructive.Contains(Op);
}

// ============================================================================
// Operation application
// ============================================================================
namespace
{
	struct FOpResult
	{
		FString OpId, Operation, Status = TEXT("pending");
		TArray<FString> Warnings, Errors;
		TSharedPtr<FJsonObject> Outputs = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> ToJson() const
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("op_id"), OpId);
			O->SetStringField(TEXT("operation"), Operation);
			O->SetStringField(TEXT("status"), Status);
			TArray<TSharedPtr<FJsonValue>> W, E;
			for (const FString& S : Warnings) { W.Add(MakeShared<FJsonValueString>(S)); }
			for (const FString& S : Errors)   { E.Add(MakeShared<FJsonValueString>(S)); }
			O->SetArrayField(TEXT("warnings"), W);
			O->SetArrayField(TEXT("errors"), E);
			O->SetObjectField(TEXT("outputs"), Outputs);
			return O;
		}
	};

	// Applies one operation. Returns true on success; fills R.
	bool ApplyOne(UBlueprint* BP, const TSharedPtr<FJsonObject>& Op, FOpResult& R)
	{
		const FString OpName = R.Operation;
		UEdGraph* G = ResolveGraph(BP, JStr(Op, TEXT("graph")));

		auto Fail = [&](const FString& Msg) { R.Status = TEXT("failed"); R.Errors.Add(Msg); return false; };

		if (OpName == TEXT("set_pin_default_value"))
		{
			FString Err; UEdGraphNode* N = ResolveNode(G, JObj(Op, TEXT("node")) ? *JObj(Op, TEXT("node")) : nullptr, Err);
			if (!N) { return Fail(Err); }
			const FString Pin = JStr(Op, TEXT("pin"));
			const FString Val = JStr(Op, TEXT("value"));
			UEdGraphPin* P = FBPGen::FindPin(N, Pin, EGPD_Input);
			if (!P) { return Fail(FString::Printf(TEXT("input pin '%s' not found"), *Pin)); }
			if (P->LinkedTo.Num() > 0) { R.Warnings.Add(TEXT("pin is connected; default may be ignored")); }
			K2()->TrySetDefaultValue(*P, Val);
			R.Outputs->SetStringField(TEXT("pin"), Pin);
			R.Outputs->SetStringField(TEXT("value"), Val);
			R.Status = TEXT("success"); return true;
		}
		if (OpName == TEXT("connect_pins"))
		{
			FString E1, E2;
			UEdGraphNode* A = ResolveNode(G, JObj(Op, TEXT("from_node")) ? *JObj(Op, TEXT("from_node")) : nullptr, E1);
			UEdGraphNode* B = ResolveNode(G, JObj(Op, TEXT("to_node")) ? *JObj(Op, TEXT("to_node")) : nullptr, E2);
			if (!A) { return Fail(TEXT("from_node: ") + E1); }
			if (!B) { return Fail(TEXT("to_node: ") + E2); }
			UEdGraphPin* PA = FBPGen::FindPin(A, JStr(Op, TEXT("from_pin")), EGPD_Output);
			UEdGraphPin* PB = FBPGen::FindPin(B, JStr(Op, TEXT("to_pin")), EGPD_Input);
			if (!PA) { return Fail(TEXT("from_pin not found")); }
			if (!PB) { return Fail(TEXT("to_pin not found")); }
			if (!FBPGen::Connect(PA, PB) || PA->LinkedTo.Num() == 0 || PB->LinkedTo.Num() == 0)
			{
				return Fail(TEXT("connection refused (incompatible pin types / no autocast)"));
			}
			R.Status = TEXT("success"); return true;
		}
		if (OpName == TEXT("disconnect_pins"))
		{
			FString E1, E2;
			UEdGraphNode* A = ResolveNode(G, JObj(Op, TEXT("from_node")) ? *JObj(Op, TEXT("from_node")) : nullptr, E1);
			UEdGraphNode* B = ResolveNode(G, JObj(Op, TEXT("to_node")) ? *JObj(Op, TEXT("to_node")) : nullptr, E2);
			if (!A || !B) { return Fail(TEXT("node selector failed: ") + E1 + E2); }
			UEdGraphPin* PA = FBPGen::FindPin(A, JStr(Op, TEXT("from_pin")), EGPD_Output);
			UEdGraphPin* PB = FBPGen::FindPin(B, JStr(Op, TEXT("to_pin")), EGPD_Input);
			if (!PA || !PB) { return Fail(TEXT("pin not found")); }
			if (!PA->LinkedTo.Contains(PB)) { return Fail(TEXT("edge does not exist")); }
			K2()->BreakSinglePinLink(PA, PB);
			R.Status = TEXT("success"); return true;
		}
		if (OpName == TEXT("add_node"))
		{
			FString Err; UEdGraphNode* N = MakeNode(G, JObj(Op, TEXT("new_node")) ? *JObj(Op, TEXT("new_node")) : nullptr, Err);
			if (!N) { return Fail(Err); }
			R.Outputs->SetStringField(TEXT("node_id"), N->NodeGuid.ToString(EGuidFormats::Digits));
			R.Status = TEXT("success"); return true;
		}
		if (OpName == TEXT("insert_node_between"))
		{
			FString E1, E2;
			UEdGraphNode* A = ResolveNode(G, JObj(Op, TEXT("from_node")) ? *JObj(Op, TEXT("from_node")) : nullptr, E1);
			UEdGraphNode* B = ResolveNode(G, JObj(Op, TEXT("to_node")) ? *JObj(Op, TEXT("to_node")) : nullptr, E2);
			if (!A) { return Fail(TEXT("from_node: ") + E1); }
			if (!B) { return Fail(TEXT("to_node: ") + E2); }
			const FString FromPinName = JStr(Op, TEXT("from_pin"));
			const FString ToPinName = JStr(Op, TEXT("to_pin"));
			UEdGraphPin* PA = FromPinName.IsEmpty() ? FBPGen::FindExecOut(A) : FBPGen::FindPin(A, FromPinName, EGPD_Output);
			UEdGraphPin* PB = ToPinName.IsEmpty() ? FBPGen::FindExecIn(B) : FBPGen::FindPin(B, ToPinName, EGPD_Input);
			if (!PA || !PB) { return Fail(TEXT("from/to exec pin not found")); }
			if (!PA->LinkedTo.Contains(PB)) { return Fail(TEXT("the A->B edge to splice does not exist")); }
			FString Err; UEdGraphNode* New = MakeNode(G, JObj(Op, TEXT("new_node")) ? *JObj(Op, TEXT("new_node")) : nullptr, Err);
			if (!New) { return Fail(Err); }
			UEdGraphPin* NewIn = FBPGen::FindExecIn(New);
			UEdGraphPin* NewOut = FBPGen::FindExecOut(New);
			if (!NewIn || !NewOut) { return Fail(TEXT("new node has no exec in/out (cannot splice into an exec chain)")); }
			K2()->BreakSinglePinLink(PA, PB);
			const bool bOk = FBPGen::Connect(PA, NewIn) && FBPGen::Connect(NewOut, PB);
			if (!bOk) { return Fail(TEXT("re-wire after splice failed")); }
			R.Outputs->SetStringField(TEXT("node_id"), New->NodeGuid.ToString(EGuidFormats::Digits));
			R.Status = TEXT("success"); return true;
		}
		if (OpName == TEXT("remove_node"))
		{
			FString Err; UEdGraphNode* N = ResolveNode(G, JObj(Op, TEXT("node")) ? *JObj(Op, TEXT("node")) : nullptr, Err);
			if (!N) { return Fail(Err); }
			const bool bPreserve = JBool(Op, TEXT("preserve_exec"), true);
			UEdGraphPin* Pred = nullptr; UEdGraphPin* Succ = nullptr;
			if (bPreserve)
			{
				if (UEdGraphPin* In = FBPGen::FindExecIn(N))   { if (In->LinkedTo.Num() > 0) { Pred = In->LinkedTo[0]; } }
				if (UEdGraphPin* Out = FBPGen::FindExecOut(N)) { if (Out->LinkedTo.Num() > 0) { Succ = Out->LinkedTo[0]; } }
			}
			N->BreakAllNodeLinks();
			G->RemoveNode(N);
			if (bPreserve && Pred && Succ)
			{
				if (!FBPGen::Connect(Pred, Succ)) { R.Warnings.Add(TEXT("could not reconnect predecessor->successor after removal")); }
				else { R.Outputs->SetBoolField(TEXT("exec_chain_preserved"), true); }
			}
			else if (bPreserve) { R.Warnings.Add(TEXT("preserve_exec requested but predecessor/successor not both present")); }
			R.Status = TEXT("success"); return true;
		}
		if (OpName == TEXT("add_reroute_on_edge"))
		{
			FString E1, E2;
			UEdGraphNode* A = ResolveNode(G, JObj(Op, TEXT("from_node")) ? *JObj(Op, TEXT("from_node")) : nullptr, E1);
			UEdGraphNode* B = ResolveNode(G, JObj(Op, TEXT("to_node")) ? *JObj(Op, TEXT("to_node")) : nullptr, E2);
			if (!A || !B) { return Fail(TEXT("node selector failed")); }
			UEdGraphPin* PA = FBPGen::FindPin(A, JStr(Op, TEXT("from_pin")), EGPD_Output);
			UEdGraphPin* PB = FBPGen::FindPin(B, JStr(Op, TEXT("to_pin")), EGPD_Input);
			if (!PA || !PB || !PA->LinkedTo.Contains(PB)) { return Fail(TEXT("edge does not exist")); }
			UK2Node_Knot* Knot = FBPGen::SpawnReroute(G, (A->NodePosX + B->NodePosX) / 2, A->NodePosY);
			if (!Knot) { return Fail(TEXT("reroute spawn failed")); }
			K2()->BreakSinglePinLink(PA, PB);
			const bool bOk = FBPGen::Connect(PA, Knot->GetInputPin()) && FBPGen::Connect(Knot->GetOutputPin(), PB);
			if (!bOk) { return Fail(TEXT("reroute rewire failed")); }
			R.Outputs->SetStringField(TEXT("node_id"), Knot->NodeGuid.ToString(EGuidFormats::Digits));
			R.Status = TEXT("success"); return true;
		}
		if (OpName == TEXT("add_variable"))
		{
			FString Err;
			FEdGraphPinType T = PinTypeFromSpec(JObj(Op, TEXT("var_type")) ? *JObj(Op, TEXT("var_type")) : nullptr, Err);
			if (!Err.IsEmpty()) { return Fail(Err); }
			const FName Var = *JStr(Op, TEXT("var_name"));
			if (Var.IsNone()) { return Fail(TEXT("var_name required")); }
			if (!FBPGen::AddVariable(BP, Var, T, JStr(Op, TEXT("default_value")), JStr(Op, TEXT("category"), TEXT("Default")), JBool(Op, TEXT("instance_editable"))))
			{
				return Fail(TEXT("AddVariable failed"));
			}
			R.Status = TEXT("success"); return true;
		}
		if (OpName == TEXT("set_variable_default"))
		{
			const FName Var = *JStr(Op, TEXT("var_name"));
			const FString Val = JStr(Op, TEXT("default_value"));
			bool bFound = false;
			for (FBPVariableDescription& V : BP->NewVariables)
			{
				if (V.VarName == Var) { V.DefaultValue = Val; bFound = true; break; }
			}
			if (!bFound) { return Fail(FString::Printf(TEXT("variable '%s' not found"), *Var.ToString())); }
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
			R.Status = TEXT("success"); return true;
		}

		return Fail(FString::Printf(TEXT("unsupported operation '%s'"), *OpName));
	}
}

// ============================================================================
// Pipeline
// ============================================================================
int32 FBPATEdit::Run(const FString& AssetPathIn, const TSharedPtr<FJsonObject>& Request, const FOptions& Opt)
{
	FString Log;
	auto L = [&](const FString& M) { Log += M + TEXT("\n"); UE_LOG(LogBPParserTestGen, Display, TEXT("BPATEdit: %s"), *M); };

	const FString AssetPath = AssetPathIn.IsEmpty() ? JStr(Request, TEXT("asset_path")) : AssetPathIn;
	if (AssetPath.IsEmpty()) { UE_LOG(LogBPParserTestGen, Error, TEXT("BPATEdit: no asset_path")); return 30; }

	const FString Mode = Opt.Mode;
	const bool bPlanOnly = Mode.Equals(TEXT("plan-only"), ESearchCase::IgnoreCase);
	const bool bDryRun   = Mode.Equals(TEXT("dry-run"), ESearchCase::IgnoreCase);
	const bool bApply    = Mode.Equals(TEXT("apply"), ESearchCase::IgnoreCase) || Mode.Equals(TEXT("apply-and-verify"), ESearchCase::IgnoreCase);
	const bool bVerify   = Mode.Equals(TEXT("apply-and-verify"), ESearchCase::IgnoreCase);

	// Output dir: <OutputDir>/<sanitized asset>/edits/<timestamp>/
	const FString Stamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ"));
	const FString Base = Opt.OutputDir.IsEmpty()
		? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BPParserAgentReports"))
		: Opt.OutputDir;
	const FString OutDir = FPaths::Combine(Base, Sanitize(AssetPath), TEXT("edits"), Stamp);
	const FString VizDir = FPaths::Combine(OutDir, TEXT("viz"));
	const FString LogDir = FPaths::Combine(OutDir, TEXT("logs"));
	IFileManager::Get().MakeDirectory(*VizDir, true);
	IFileManager::Get().MakeDirectory(*LogDir, true);

	// Persist the incoming request.
	WriteJson(FPaths::Combine(OutDir, TEXT("edit_request.json")), Request.IsValid() ? Request : TSharedPtr<FJsonObject>(MakeShared<FJsonObject>()));

	// Result skeleton.
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("schema_version"), TEXT("1.0"));
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetStringField(TEXT("engine_version"), BPGenCompat::EngineFullVersion());
	auto Finish = [&](const FString& Status, int32 Code) -> int32
	{
		Result->SetStringField(TEXT("status"), Status);
		TSharedPtr<FJsonObject> Art = MakeShared<FJsonObject>();
		Art->SetStringField(TEXT("baseline_ir"), FPaths::Combine(OutDir, TEXT("baseline_ir.json")));
		Art->SetStringField(TEXT("modified_ir"), FPaths::Combine(OutDir, TEXT("modified_ir.json")));
		Art->SetStringField(TEXT("diff_report"), FPaths::Combine(OutDir, TEXT("diff_report.json")));
		Art->SetStringField(TEXT("edit_plan"), FPaths::Combine(OutDir, TEXT("edit_plan.json")));
		Art->SetStringField(TEXT("summary"), FPaths::Combine(OutDir, TEXT("summary.md")));
		Result->SetObjectField(TEXT("artifacts"), Art);
		WriteJson(FPaths::Combine(OutDir, TEXT("edit_result.json")), Result);
		WriteText(FPaths::Combine(LogDir, TEXT("edit_log.txt")), Log);
		WriteText(FPaths::Combine(OutDir, TEXT("summary.md")),
			FString::Printf(TEXT("# Edit summary\n\n- asset: %s\n- mode: %s\n- status: **%s**\n- output: %s\n"),
				*AssetPath, *Mode, *Status, *OutDir));
		UE_LOG(LogBPParserTestGen, Display, TEXT("BPATEdit: status=%s dir=%s"), *Status, *OutDir);
		return Code;
	};

	// --- Load source ---
	UBlueprint* SourceBP = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!SourceBP) { L(TEXT("asset not found")); return Finish(TEXT("failed"), 30); }

	// --- Optionally edit a COPY instead of the source ---
	UBlueprint* BP = SourceBP;
	FAssetToolsModule& ATM = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	if (!Opt.WorkOnCopy.IsEmpty())
	{
		const FString PkgPath = FPackageName::GetLongPackagePath(Opt.WorkOnCopy);
		const FString Name = FPackageName::GetShortName(Opt.WorkOnCopy);
		if (UObject* Dup = ATM.Get().DuplicateAsset(Name, PkgPath, SourceBP))
		{
			BP = Cast<UBlueprint>(Dup);
			FBPGen::SaveAsset(BP);
			L(FString::Printf(TEXT("editing COPY %s"), *Opt.WorkOnCopy));
		}
		else { L(TEXT("WorkOnCopy duplicate failed")); return Finish(TEXT("failed"), 20); }
	}
	const FString EditedPath = BP->GetPathName();

	// --- Baseline IR ---
	TSharedPtr<FJsonObject> Baseline = FBPGenIRDumper::DumpBlueprint(BP);
	WriteJson(FPaths::Combine(OutDir, TEXT("baseline_ir.json")), Baseline);
	WriteText(FPaths::Combine(VizDir, TEXT("before.dot")), IRToDot(Baseline, TEXT("before")));

	// --- Parse operations + classify ---
	const TArray<TSharedPtr<FJsonValue>>* Ops = nullptr;
	Request->TryGetArrayField(TEXT("operations"), Ops);
	const bool bRequestDestructive = JBool(Request, TEXT("allow_destructive_edit")) || Opt.bAllowDestructive;
	bool bAnyDestructive = false;
	if (Ops) { for (const TSharedPtr<FJsonValue>& OV : *Ops) { if (OV->AsObject().IsValid() && IsDestructiveOperation(JStr(OV->AsObject(), TEXT("operation")))) { bAnyDestructive = true; } } }

	// --- Build edit plan (preconditions + atomic ops) ---
	TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
	Plan->SetStringField(TEXT("schema_version"), TEXT("1.0"));
	Plan->SetStringField(TEXT("asset_path"), EditedPath);
	Plan->SetStringField(TEXT("mode"), Mode);
	{
		TArray<TSharedPtr<FJsonValue>> Pre;
		auto AddPre = [&](const FString& Check, const FString& Status, const FString& Detail)
		{
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("check"), Check); P->SetStringField(TEXT("status"), Status);
			if (!Detail.IsEmpty()) { P->SetStringField(TEXT("detail"), Detail); }
			Pre.Add(MakeShared<FJsonValueObject>(P));
		};
		AddPre(TEXT("asset_exists"), TEXT("pass"), EditedPath);
		AddPre(TEXT("allow_destructive_edit"), (!bAnyDestructive || bRequestDestructive) ? TEXT("pass") : TEXT("fail"),
			bAnyDestructive ? (bRequestDestructive ? TEXT("granted") : TEXT("destructive ops present but not allowed")) : TEXT("no destructive ops"));
		AddPre(TEXT("backup_planned"), (Opt.bCreateBackup ? TEXT("pass") : TEXT("skip")), TEXT(""));
		Plan->SetArrayField(TEXT("preconditions"), Pre);

		TArray<TSharedPtr<FJsonValue>> AtomicOps;
		if (Ops) { for (const TSharedPtr<FJsonValue>& OV : *Ops) { if (OV->AsObject().IsValid()) { AtomicOps.Add(MakeShared<FJsonValueObject>(OV->AsObject())); } } }
		Plan->SetArrayField(TEXT("atomic_operations"), AtomicOps);
	}
	WriteJson(FPaths::Combine(OutDir, TEXT("edit_plan.json")), Plan);
	Result->SetObjectField(TEXT("plan"), Plan);

	// plan-only / dry-run: ALWAYS succeed and emit the plan (this is how an agent previews an
	// edit — including a destructive one — safely). These modes never mutate, so the destructive
	// gate below does not apply to them.
	if (bPlanOnly || bDryRun)
	{
		TSharedPtr<FJsonObject> Diff = BuildDiff(EditedPath, Baseline, Baseline);
		WriteJson(FPaths::Combine(OutDir, TEXT("diff_report.json")), Diff);
		Result->SetObjectField(TEXT("diff"), Diff);
		Result->SetBoolField(TEXT("contains_destructive_ops"), bAnyDestructive);
		Result->SetBoolField(TEXT("would_require_allow_destructive"), bAnyDestructive && !bRequestDestructive);
		L(FString::Printf(TEXT("%s complete; no changes applied."), *Mode));
		return Finish(TEXT("success"), 0);
	}

	// Precondition gate (apply modes only): refuse destructive edits unless explicitly allowed.
	if (bAnyDestructive && !bRequestDestructive)
	{
		L(TEXT("destructive operations present but AllowDestructiveEdit=false; refusing to apply (use plan-only/dry-run, or pass AllowDestructiveEdit)."));
		TSharedPtr<FJsonObject> Diff = BuildDiff(EditedPath, Baseline, Baseline);
		WriteJson(FPaths::Combine(OutDir, TEXT("diff_report.json")), Diff);
		Result->SetObjectField(TEXT("diff"), Diff);
		return Finish(TEXT("partial"), 10);
	}

	// --- Backup (duplicate the edited asset) ---
	TSharedPtr<FJsonObject> BackupJson = MakeShared<FJsonObject>();
	if (Opt.bCreateBackup)
	{
		const FString BkName = BP->GetName() + TEXT("_") + Stamp;
		if (UObject* Bk = ATM.Get().DuplicateAsset(BkName, TEXT("/Game/BPParserBackups"), BP))
		{
			FBPGen::SaveAsset(Bk);
			BackupJson->SetBoolField(TEXT("created"), true);
			BackupJson->SetStringField(TEXT("path"), Bk->GetPathName());
			L(FString::Printf(TEXT("backup -> %s"), *Bk->GetPathName()));
		}
		else { BackupJson->SetBoolField(TEXT("created"), false); L(TEXT("backup duplicate failed (continuing; on-disk source unchanged until save)")); }
	}
	else { BackupJson->SetBoolField(TEXT("created"), false); }
	Result->SetObjectField(TEXT("backup"), BackupJson);

	// --- Apply ops in order ---
	TArray<TSharedPtr<FJsonValue>> OpResults;
	bool bAllOk = true;
	if (Ops)
	{
		for (const TSharedPtr<FJsonValue>& OV : *Ops)
		{
			const TSharedPtr<FJsonObject> Op = OV->AsObject();
			if (!Op.IsValid()) { continue; }
			FOpResult R; R.OpId = JStr(Op, TEXT("op_id"), TEXT("op")); R.Operation = JStr(Op, TEXT("operation"));
			const bool bOk = ApplyOne(BP, Op, R);
			OpResults.Add(MakeShared<FJsonValueObject>(R.ToJson()));
			L(FString::Printf(TEXT("op %s [%s] -> %s"), *R.OpId, *R.Operation, *R.Status));
			if (!bOk) { bAllOk = false; break; }   // on_failure: abort/rollback
		}
	}
	Result->SetArrayField(TEXT("operations"), OpResults);

	// --- Verify (compile) ---
	TSharedPtr<FJsonObject> Validation = MakeShared<FJsonObject>();
	FString CompileStatus = TEXT("skipped");
	if (bAllOk)
	{
		CompileStatus = FBPGen::CompileBlueprint(BP);
		L(FString::Printf(TEXT("compile -> %s"), *CompileStatus));
	}
	const bool bCompileBad = CompileStatus.Contains(TEXT("error")) || CompileStatus.Contains(TEXT("fail"));
	Validation->SetStringField(TEXT("compile_status"), CompileStatus);

	// --- Decide save vs rollback ---
	const bool bShouldRollback = !bAllOk || (bVerify && bCompileBad);
	FString SaveStatus = TEXT("skipped");
	if (bShouldRollback)
	{
		// Rollback = discard in-memory edits without saving; the on-disk asset is unchanged
		// (we never saved). The backup duplicate remains as an extra snapshot.
		L(TEXT("rollback: not saving; on-disk asset unchanged."));
		Validation->SetStringField(TEXT("save_status"), TEXT("skipped"));
		Validation->SetBoolField(TEXT("rolled_back"), true);

		TSharedPtr<FJsonObject> Modified = FBPGenIRDumper::DumpBlueprint(BP);
		WriteJson(FPaths::Combine(OutDir, TEXT("modified_ir.json")), Modified);   // in-memory (not persisted)
		TSharedPtr<FJsonObject> Diff = BuildDiff(EditedPath, Baseline, Modified);
		WriteJson(FPaths::Combine(OutDir, TEXT("diff_report.json")), Diff);
		Result->SetObjectField(TEXT("diff"), Diff);
		Result->SetObjectField(TEXT("validation"), Validation);
		return Finish(TEXT("rolled_back"), 40);
	}

	// Save (success path).
	const bool bSaved = FBPGen::SaveAsset(BP);
	SaveStatus = bSaved ? TEXT("success") : TEXT("failed");
	Validation->SetStringField(TEXT("save_status"), SaveStatus);
	Validation->SetBoolField(TEXT("rolled_back"), false);
	L(FString::Printf(TEXT("save -> %s"), *SaveStatus));

	// --- Re-dump + diff ---
	TSharedPtr<FJsonObject> Modified = FBPGenIRDumper::DumpBlueprint(BP);
	WriteJson(FPaths::Combine(OutDir, TEXT("modified_ir.json")), Modified);
	WriteText(FPaths::Combine(VizDir, TEXT("after.dot")), IRToDot(Modified, TEXT("after")));
	Validation->SetStringField(TEXT("ir_redump_status"), TEXT("success"));

	TSharedPtr<FJsonObject> Diff = BuildDiff(EditedPath, Baseline, Modified);
	WriteJson(FPaths::Combine(OutDir, TEXT("diff_report.json")), Diff);
	Result->SetObjectField(TEXT("diff"), Diff);

	// diff.mmd (compact)
	{
		const TArray<TSharedPtr<FJsonValue>>* AN; const TArray<TSharedPtr<FJsonValue>>* RN;
		FString M = TEXT("flowchart TB\n");
		if (Diff->TryGetArrayField(TEXT("added_nodes"), AN)) { for (auto& V : *AN) { M += FString::Printf(TEXT("  add[\"+ %s\"]\n"), *V->AsObject()->GetStringField(TEXT("node_title"))); } }
		if (Diff->TryGetArrayField(TEXT("removed_nodes"), RN)) { for (auto& V : *RN) { M += FString::Printf(TEXT("  rem[\"- %s\"]\n"), *V->AsObject()->GetStringField(TEXT("node_title"))); } }
		WriteText(FPaths::Combine(VizDir, TEXT("diff.mmd")), M);
	}

	Result->SetObjectField(TEXT("validation"), Validation);

	const bool bSaveOk = (SaveStatus == TEXT("success"));
	if (bAllOk && bSaveOk) { return Finish(TEXT("success"), 0); }
	return Finish(TEXT("partial"), 10);
}
