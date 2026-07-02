// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPGenIRDumper.h"
#include "BPParserTestGenModule.h"
#include "BPGenUECompat.h"

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphNode_Comment.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	FString ContainerStr(EPinContainerType C)
	{
		switch (C)
		{
			case EPinContainerType::Array: return TEXT("array");
			case EPinContainerType::Set:   return TEXT("set");
			case EPinContainerType::Map:   return TEXT("map");
			default:                       return TEXT("none");
		}
	}

	FString GuidStr(const FGuid& G) { return G.ToString(EGuidFormats::Digits); }

	TSharedPtr<FJsonObject> PinToJson(UEdGraphPin* Pin)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("pin_id"), GuidStr(Pin->PinId));
		O->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
		O->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));

		const FEdGraphPinType& T = Pin->PinType;
		O->SetStringField(TEXT("category"), T.PinCategory.ToString());
		O->SetStringField(TEXT("sub_category"), T.PinSubCategory.ToString());
		if (T.PinSubCategoryObject.IsValid())
		{
			O->SetStringField(TEXT("object_type"), T.PinSubCategoryObject->GetPathName());
		}
		else
		{
			O->SetField(TEXT("object_type"), MakeShared<FJsonValueNull>());
		}
		O->SetStringField(TEXT("container_type"), ContainerStr(T.ContainerType));
		O->SetBoolField(TEXT("is_exec"), T.PinCategory == UEdGraphSchema_K2::PC_Exec);

		if (!Pin->DefaultValue.IsEmpty()) { O->SetStringField(TEXT("default_value"), Pin->DefaultValue); }
		else { O->SetField(TEXT("default_value"), MakeShared<FJsonValueNull>()); }
		if (Pin->DefaultObject) { O->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName()); }

		O->SetBoolField(TEXT("is_connected"), Pin->LinkedTo.Num() > 0);

		TArray<TSharedPtr<FJsonValue>> Linked;
		for (UEdGraphPin* L : Pin->LinkedTo)
		{
			if (!L || !L->GetOwningNodeUnchecked()) { continue; }
			TSharedPtr<FJsonObject> LO = MakeShared<FJsonObject>();
			LO->SetStringField(TEXT("node_id"), GuidStr(L->GetOwningNode()->NodeGuid));
			LO->SetStringField(TEXT("pin_id"), GuidStr(L->PinId));
			Linked.Add(MakeShared<FJsonValueObject>(LO));
		}
		O->SetArrayField(TEXT("linked_to"), Linked);
		return O;
	}

	TSharedPtr<FJsonObject> NodeToJson(UEdGraphNode* Node)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("node_id"), GuidStr(Node->NodeGuid));
		O->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		O->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		O->SetStringField(TEXT("node_comment"), Node->NodeComment);

		TSharedPtr<FJsonObject> Pos = MakeShared<FJsonObject>();
		Pos->SetNumberField(TEXT("x"), Node->NodePosX);
		Pos->SetNumberField(TEXT("y"), Node->NodePosY);
		O->SetObjectField(TEXT("position"), Pos);

		TArray<TSharedPtr<FJsonValue>> Pins;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin) { Pins.Add(MakeShared<FJsonValueObject>(PinToJson(Pin))); }
		}
		O->SetArrayField(TEXT("pins"), Pins);
		return O;
	}

	FString EdgeKindForPin(UEdGraphPin* Pin)
	{
		const FName Cat = Pin->PinType.PinCategory;
		if (Cat == UEdGraphSchema_K2::PC_Exec) { return TEXT("exec"); }
		if (Cat == UEdGraphSchema_K2::PC_Delegate || Cat == UEdGraphSchema_K2::PC_MCDelegate) { return TEXT("delegate"); }
		return TEXT("data");
	}

	TSharedPtr<FJsonObject> GraphToJson(UEdGraph* G, const FString& GraphType)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("graph_name"), G->GetName());
		O->SetStringField(TEXT("graph_type"), GraphType);

		TArray<TSharedPtr<FJsonValue>> Nodes;
		TArray<TSharedPtr<FJsonValue>> Edges;
		TArray<TSharedPtr<FJsonValue>> Comments;
		int32 EdgeIndex = 0;

		for (UEdGraphNode* Node : G->Nodes)
		{
			if (!Node) { continue; }

			if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
			{
				TSharedPtr<FJsonObject> CO = MakeShared<FJsonObject>();
				CO->SetStringField(TEXT("comment_id"), GuidStr(Comment->NodeGuid));
				CO->SetStringField(TEXT("text"), Comment->NodeComment);
				TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
				B->SetNumberField(TEXT("x"), Comment->NodePosX);
				B->SetNumberField(TEXT("y"), Comment->NodePosY);
				B->SetNumberField(TEXT("w"), Comment->NodeWidth);
				B->SetNumberField(TEXT("h"), Comment->NodeHeight);
				CO->SetObjectField(TEXT("bounds"), B);
				Comments.Add(MakeShared<FJsonValueObject>(CO));
				continue;
			}

			Nodes.Add(MakeShared<FJsonValueObject>(NodeToJson(Node)));

			// Edges from this node's output pins.
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output) { continue; }
				for (UEdGraphPin* L : Pin->LinkedTo)
				{
					if (!L || !L->GetOwningNodeUnchecked()) { continue; }
					TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
					E->SetStringField(TEXT("edge_id"), FString::Printf(TEXT("e%d"), ++EdgeIndex));
					E->SetStringField(TEXT("from_node"), GuidStr(Node->NodeGuid));
					E->SetStringField(TEXT("from_pin"), GuidStr(Pin->PinId));
					E->SetStringField(TEXT("to_node"), GuidStr(L->GetOwningNode()->NodeGuid));
					E->SetStringField(TEXT("to_pin"), GuidStr(L->PinId));
					E->SetStringField(TEXT("edge_type"), EdgeKindForPin(Pin));
					Edges.Add(MakeShared<FJsonValueObject>(E));
				}
			}
		}

		O->SetArrayField(TEXT("nodes"), Nodes);
		O->SetArrayField(TEXT("edges"), Edges);
		O->SetArrayField(TEXT("comments"), Comments);
		return O;
	}
}

TSharedPtr<FJsonObject> FBPGenIRDumper::DumpBlueprint(UBlueprint* BP)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	if (!BP) { return Root; }

	Root->SetStringField(TEXT("schema_version"), TEXT("bpat-ir-dump-1.0"));
	Root->SetStringField(TEXT("dumper_engine_version"), BPGenCompat::EngineFullVersion());
	Root->SetStringField(TEXT("asset_name"), BP->GetName());
	Root->SetStringField(TEXT("asset_path"), BP->GetPathName());
	Root->SetStringField(TEXT("blueprint_class"), BP->GetClass()->GetName());
	Root->SetStringField(TEXT("parent_class"), BP->ParentClass ? BP->ParentClass->GetPathName() : TEXT(""));
	Root->SetStringField(TEXT("generated_class"),
		BP->GeneratedClass ? BP->GeneratedClass->GetPathName()
		: (BP->SkeletonGeneratedClass ? BP->SkeletonGeneratedClass->GetPathName() : TEXT("")));

	// Graphs.
	TArray<TSharedPtr<FJsonValue>> Graphs;
	auto AddGraphs = [&Graphs](const TArray<TObjectPtr<UEdGraph>>& List, const TCHAR* Type)
	{
		for (UEdGraph* G : List)
		{
			if (G) { Graphs.Add(MakeShared<FJsonValueObject>(GraphToJson(G, Type))); }
		}
	};
	AddGraphs(BP->UbergraphPages, TEXT("ubergraph"));
	AddGraphs(BP->FunctionGraphs, TEXT("function"));
	AddGraphs(BP->MacroGraphs, TEXT("macro"));
	AddGraphs(BP->DelegateSignatureGraphs, TEXT("delegate"));
	Root->SetArrayField(TEXT("graphs"), Graphs);

	// Variables.
	TArray<TSharedPtr<FJsonValue>> Vars;
	for (const FBPVariableDescription& V : BP->NewVariables)
	{
		TSharedPtr<FJsonObject> VO = MakeShared<FJsonObject>();
		VO->SetStringField(TEXT("name"), V.VarName.ToString());
		VO->SetStringField(TEXT("category"), V.VarType.PinCategory.ToString());
		VO->SetStringField(TEXT("sub_category"), V.VarType.PinSubCategory.ToString());
		VO->SetStringField(TEXT("container_type"), ContainerStr(V.VarType.ContainerType));
		if (V.VarType.PinSubCategoryObject.IsValid())
		{
			VO->SetStringField(TEXT("object_type"), V.VarType.PinSubCategoryObject->GetPathName());
		}
		VO->SetStringField(TEXT("default_value"), V.DefaultValue);
		Vars.Add(MakeShared<FJsonValueObject>(VO));
	}
	Root->SetArrayField(TEXT("variables"), Vars);

	// Functions / macros / dispatchers. Emit OBJECT arrays [{ "name": ... }] so their shape is
	// symmetric with variables[] (a consumer can always read entry.name without type-checking).
	auto ObjNamesArray = [](const TArray<TObjectPtr<UEdGraph>>& List)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (UEdGraph* G : List)
		{
			if (G) { TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("name"), G->GetName()); Arr.Add(MakeShared<FJsonValueObject>(O)); }
		}
		return Arr;
	};
	Root->SetArrayField(TEXT("functions"), ObjNamesArray(BP->FunctionGraphs));
	Root->SetArrayField(TEXT("macros"), ObjNamesArray(BP->MacroGraphs));
	Root->SetArrayField(TEXT("event_dispatchers"), ObjNamesArray(BP->DelegateSignatureGraphs));

	// Interfaces.
	TArray<TSharedPtr<FJsonValue>> Ifaces;
	for (const FBPInterfaceDescription& I : BP->ImplementedInterfaces)
	{
		if (I.Interface) { Ifaces.Add(MakeShared<FJsonValueString>(I.Interface->GetPathName())); }
	}
	Root->SetArrayField(TEXT("interfaces"), Ifaces);

	return Root;
}

FString FBPGenIRDumper::DumpAssetToFile(const FString& AssetPath, const FString& OutDir)
{
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!BP)
	{
		UE_LOG(LogBPParserTestGen, Error, TEXT("DumpAssetToFile: cannot load %s"), *AssetPath);
		return FString();
	}

	TSharedPtr<FJsonObject> Root = DumpBlueprint(BP);

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	IFileManager::Get().MakeDirectory(*OutDir, /*Tree*/ true);
	const FString ShortName = FPackageName::GetShortName(AssetPath);
	const FString File = FPaths::Combine(OutDir, ShortName + TEXT(".ir.json"));
	if (!FFileHelper::SaveStringToFile(Out, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogBPParserTestGen, Error, TEXT("DumpAssetToFile: failed to write %s"), *File);
		return FString();
	}
	return File;
}
