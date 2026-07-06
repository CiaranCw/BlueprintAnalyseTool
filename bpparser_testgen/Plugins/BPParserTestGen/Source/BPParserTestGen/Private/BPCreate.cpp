// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPCreate.h"
#include "BPParserTestGenModule.h"
#include "BPGen.h"
#include "BPWidgetGen.h"
#include "BPGenIRDumper.h"
#include "BPGenUECompat.h"

#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Components/PanelSlot.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "EdGraphNode_Comment.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/DateTime.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	FString JStr(const TSharedPtr<FJsonObject>& O,const TCHAR* K,const FString& D=FString()){ FString v; return (O.IsValid()&&O->TryGetStringField(K,v))?v:D; }
	bool    JBool(const TSharedPtr<FJsonObject>& O,const TCHAR* K,bool D=false){ bool v; return (O.IsValid()&&O->TryGetBoolField(K,v))?v:D; }
	double  JNum(const TSharedPtr<FJsonObject>& O,const TCHAR* K,double D=0){ double v; return (O.IsValid()&&O->TryGetNumberField(K,v))?v:D; }
	const TSharedPtr<FJsonObject>* JObj(const TSharedPtr<FJsonObject>& O,const TCHAR* K){ const TSharedPtr<FJsonObject>* p=nullptr; return (O.IsValid()&&O->TryGetObjectField(K,p))?p:nullptr; }
	const TArray<TSharedPtr<FJsonValue>>* JArr(const TSharedPtr<FJsonObject>& O,const TCHAR* K){ const TArray<TSharedPtr<FJsonValue>>* p=nullptr; return (O.IsValid()&&O->TryGetArrayField(K,p))?p:nullptr; }

	void WriteJson(const FString& Path,const TSharedRef<FJsonObject>& Root){ FString s; auto w=TJsonWriterFactory<>::Create(&s); FJsonSerializer::Serialize(Root,w); FFileHelper::SaveStringToFile(s,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); }

	UClass* ResolveClass(const FString& Path){ if(Path.IsEmpty()) return nullptr; return LoadObject<UClass>(nullptr,*Path); }

	// Emit a widget-hierarchy graph (DOT + Mermaid) from the created_ir widget_tree node (recursive).
	void EmitWidgetDot(const TSharedPtr<FJsonObject>& Node, FString& Dot, FString& Mmd, int32& Counter, const FString& ParentId)
	{
		if(!Node.IsValid()) return;
		const FString Name = JStr(Node,TEXT("name"),TEXT("?"));
		FString Cls = JStr(Node,TEXT("class")); int32 d; if(Cls.FindLastChar('.',d)) Cls=Cls.Mid(d+1); Cls.RemoveFromEnd(TEXT("_C"));
		const FString Id = FString::Printf(TEXT("w%d"), Counter++);
		FString Label = Name + TEXT("\\n") + Cls; Label.ReplaceInline(TEXT("\""),TEXT("'"));
		Dot += FString::Printf(TEXT("  %s [label=\"%s\"];\n"), *Id, *Label);
		Mmd += FString::Printf(TEXT("  %s[\"%s : %s\"]\n"), *Id, *Name, *Cls);
		if(!ParentId.IsEmpty()){ Dot += FString::Printf(TEXT("  %s -> %s;\n"),*ParentId,*Id); Mmd += FString::Printf(TEXT("  %s --> %s\n"),*ParentId,*Id); }
		const TArray<TSharedPtr<FJsonValue>>* Kids=nullptr;
		if(Node->TryGetArrayField(TEXT("children"),Kids)) for(const auto& k:*Kids){ if(auto ko=k->AsObject()) EmitWidgetDot(ko,Dot,Mmd,Counter,Id); }
	}

	FEdGraphPinType PinTypeFromSpec(const TSharedPtr<FJsonObject>& S, FString& Err)
	{
		const FString Cat=JStr(S,TEXT("category"),TEXT("int")).ToLower();
		const FString Sub=JStr(S,TEXT("sub_category"));
		const FString Obj=JStr(S,TEXT("sub_category_object"));
		FEdGraphPinType T;
		if(Cat==TEXT("bool")) T=FBPGen::PinBool();
		else if(Cat==TEXT("byte")) T=FBPGen::PinByte();
		else if(Cat==TEXT("int")) T=FBPGen::PinInt();
		else if(Cat==TEXT("int64")) T=FBPGen::PinInt64();
		else if(Cat==TEXT("float")||(Cat==TEXT("real")&&Sub!=TEXT("double"))) T=FBPGen::PinFloat();
		else if(Cat==TEXT("double")||(Cat==TEXT("real")&&Sub==TEXT("double"))) T=FBPGen::PinDouble();
		else if(Cat==TEXT("name")) T=FBPGen::PinName();
		else if(Cat==TEXT("string")) T=FBPGen::PinString();
		else if(Cat==TEXT("text")) T=FBPGen::PinText();
		else if(Cat==TEXT("object")) T=FBPGen::PinObject(Obj.IsEmpty()?nullptr:ResolveClass(Obj));
		else if(Cat==TEXT("class")) T=FBPGen::PinClass(Obj.IsEmpty()?nullptr:ResolveClass(Obj));
		else if(Cat==TEXT("struct")) T=FBPGen::PinStruct(Obj.IsEmpty()?nullptr:LoadObject<UScriptStruct>(nullptr,*Obj));
		else { Err=FString::Printf(TEXT("unsupported variable category '%s'"),*Cat); return T; }
		const FString C=JStr(S,TEXT("container_type"),TEXT("none")).ToLower();
		if(C==TEXT("array")) T=FBPGen::AsArray(T); else if(C==TEXT("set")) T=FBPGen::AsSet(T);
		return T;
	}
}

int32 FBPCreate::Run(const FString& SpecFile, const FString& OutputDirIn)
{
	auto Log=[](const FString& m){ UE_LOG(LogBPParserTestGen,Display,TEXT("BPCreate: %s"),*m); };
	TArray<FString> Warn, Err, Manual;
	bool bWidgetAsset=false;
	TArray<TSharedPtr<FJsonValue>> EventBindings;   // widget event-binding results (Phase 4)

	FString Text;
	if(!FFileHelper::LoadFileToString(Text,*SpecFile)){ Log(TEXT("cannot read spec file")); return 30; }
	TSharedPtr<FJsonObject> Root; auto R=TJsonReaderFactory<>::Create(Text);
	if(!FJsonSerializer::Deserialize(R,Root)||!Root.IsValid()){ Log(TEXT("invalid spec JSON")); return 30; }
	// accept either the full request (with .request) or the bare create request
	TSharedPtr<FJsonObject> Req = Root;
	if(const TSharedPtr<FJsonObject>* rq=JObj(Root,TEXT("request"))) Req=*rq;

	const TSharedPtr<FJsonObject>* AssetObj=JObj(Req,TEXT("asset"));
	if(!AssetObj){ Log(TEXT("missing 'asset'")); return 30; }
	FString AssetPath=JStr(*AssetObj,TEXT("asset_path"));
	const FString BpType=JStr(*AssetObj,TEXT("blueprint_type"),TEXT("Actor"));
	const FString ParentPath=JStr(*AssetObj,TEXT("parent_class"));
	const FString Overwrite=JStr(*AssetObj,TEXT("overwrite_policy"),TEXT("fail_if_exists"));
	if(AssetPath.IsEmpty()){ Log(TEXT("missing asset_path")); return 30; }

	FString OutDir=OutputDirIn;
	if(OutDir.IsEmpty()) OutDir=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("BPParserAgentReports"),TEXT("create"),
		FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")));
	IFileManager::Get().MakeDirectory(*OutDir,true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(OutDir,TEXT("logs")),true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(OutDir,TEXT("viz")),true);

	auto Finish=[&](const FString& Status,int32 Code,UBlueprint* BP)->int32{
		TSharedRef<FJsonObject> M=MakeShared<FJsonObject>();
		M->SetStringField(TEXT("schema_version"),TEXT("1.0"));
		M->SetStringField(TEXT("status"),Status);
		M->SetStringField(TEXT("task_type"),TEXT("create"));
		M->SetStringField(TEXT("mode"),TEXT("native_full"));
		M->SetStringField(TEXT("engine_version"),BPGenCompat::EngineFullVersion());
		M->SetStringField(TEXT("asset_path"),AssetPath);
		TArray<TSharedPtr<FJsonValue>> ap; ap.Add(MakeShared<FJsonValueString>(AssetPath)); M->SetArrayField(TEXT("asset_paths"),ap);
		TArray<TSharedPtr<FJsonValue>> W,E,MC;
		for(auto&s:Warn)W.Add(MakeShared<FJsonValueString>(s)); for(auto&s:Err)E.Add(MakeShared<FJsonValueString>(s)); for(auto&s:Manual)MC.Add(MakeShared<FJsonValueString>(s));
		M->SetArrayField(TEXT("warnings"),W); M->SetArrayField(TEXT("errors"),E); M->SetArrayField(TEXT("manual_check_required"),MC);
		TSharedRef<FJsonObject> Out=MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("created_ir"),TEXT("created_ir.json"));
		Out->SetStringField(TEXT("create_result"),TEXT("create_result.json"));
		Out->SetStringField(TEXT("summary"),TEXT("summary.md"));
		Out->SetStringField(TEXT("dot"), bWidgetAsset ? TEXT("viz/hierarchy.dot") : TEXT("viz/created.dot"));
		if(bWidgetAsset){ Out->SetStringField(TEXT("hierarchy_dot"),TEXT("viz/hierarchy.dot")); Out->SetStringField(TEXT("hierarchy_mmd"),TEXT("viz/hierarchy.mmd")); }
		M->SetObjectField(TEXT("outputs"),Out);
		if(BP){ M->SetStringField(TEXT("created_asset"),BP->GetPathName()); }
		if(bWidgetAsset){ M->SetArrayField(TEXT("widget_event_bindings"),EventBindings); }
		WriteJson(FPaths::Combine(OutDir,TEXT("manifest.json")),M);

		TSharedRef<FJsonObject> Res=MakeShared<FJsonObject>();
		Res->SetStringField(TEXT("status"),Status); Res->SetStringField(TEXT("asset_path"),AssetPath);
		Res->SetStringField(TEXT("overwrite_policy"),Overwrite);
		Res->SetArrayField(TEXT("warnings"),W); Res->SetArrayField(TEXT("errors"),E);
		if(bWidgetAsset){ Res->SetArrayField(TEXT("widget_event_bindings"),EventBindings); }
		WriteJson(FPaths::Combine(OutDir,TEXT("create_result.json")),Res);
		Log(FString::Printf(TEXT("status=%s -> %s"),*Status,*OutDir));
		return Code;
	};

	// --- overwrite policy ---
	if(FPackageName::DoesPackageExist(AssetPath))
	{
		if(Overwrite==TEXT("fail_if_exists")){ Err.Add(FString::Printf(TEXT("asset exists: %s (overwrite_policy=fail_if_exists)"),*AssetPath)); return Finish(TEXT("failed"),41,nullptr); }
		else if(Overwrite==TEXT("create_unique_name")){ int32 i=1; FString base=AssetPath; while(FPackageName::DoesPackageExist(AssetPath)){ AssetPath=FString::Printf(TEXT("%s_%d"),*base,i++); } Warn.Add(FString::Printf(TEXT("asset existed; using unique name %s"),*AssetPath)); }
		else { Warn.Add(TEXT("overwrite_if_allowed: proceeding over existing asset")); }
	}

	// --- resolve parent + create asset ---
	UClass* Parent = ParentPath.IsEmpty()? nullptr : ResolveClass(ParentPath);
	UBlueprint* BP=nullptr;
	const FString T=BpType.ToLower();
	if(T==TEXT("interface")) BP=FBPGen::CreateInterfaceBlueprint(AssetPath);
	else if(T.Contains(TEXT("component"))|| (Parent&&Parent->IsChildOf(UActorComponent::StaticClass())))
		BP=FBPGen::CreateComponentBlueprint(AssetPath, Parent?Parent:USceneComponent::StaticClass());
	else if(T==TEXT("widget")||T==TEXT("widgetblueprint")||T==TEXT("wbp"))
	{
		UClass* WParent = (Parent && Parent->IsChildOf(UUserWidget::StaticClass())) ? Parent : UUserWidget::StaticClass();
		UWidgetBlueprint* WBP = FBPWidgetGen::CreateWidgetBlueprint(AssetPath, WParent);
		if(!WBP){ Err.Add(TEXT("Widget Blueprint creation failed (UWidgetBlueprintFactory)")); return Finish(TEXT("failed"),20,nullptr); }
		BP = WBP; bWidgetAsset = true;

		// Build the visual hierarchy from widget.hierarchy (either the root node directly or a { "root": <node> } wrapper).
		if(const TSharedPtr<FJsonObject>* WObj = JObj(Req,TEXT("widget")))
		{
			if(JArr(*WObj,TEXT("bindings")))   Manual.Add(TEXT("widget.bindings deferred (property binding is a later phase; not applied)."));
			if(JArr(*WObj,TEXT("animations"))) Manual.Add(TEXT("widget.animations deferred to a later phase."));
			if(const TSharedPtr<FJsonObject>* Hier = JObj(*WObj,TEXT("hierarchy")))
			{
				const TSharedPtr<FJsonObject>* RootWrap = JObj(*Hier,TEXT("root"));
				TSharedPtr<FJsonObject> RootNode = RootWrap ? *RootWrap : *Hier;
				TFunction<UWidget*(const TSharedPtr<FJsonObject>&, UWidget*)> Build;
				Build = [&](const TSharedPtr<FJsonObject>& Node, UWidget* ParentW) -> UWidget*
				{
					const FString WType = JStr(Node,TEXT("type"));
					const FString WName = JStr(Node,TEXT("name"));
					UClass* WClass = FBPWidgetGen::ResolveWidgetClass(WType);
					if(!WClass){ Warn.Add(FString::Printf(TEXT("widget type unresolved: '%s' (name=%s)"),*WType,*WName)); return (UWidget*)nullptr; }
					UWidget* W = FBPWidgetGen::ConstructWidget(WBP, WClass, WName.IsEmpty()?NAME_None:FName(*WName));
					if(!W){ Warn.Add(FString::Printf(TEXT("construct failed for type '%s'"),*WType)); return (UWidget*)nullptr; }
					if(!ParentW) { FBPWidgetGen::SetRoot(WBP, W); }
					else
					{
						UPanelSlot* Slot = FBPWidgetGen::AddChild(ParentW, W);
						if(!Slot){ Warn.Add(FString::Printf(TEXT("AddChild failed (parent '%s' is not a panel?) for '%s'"),*ParentW->GetName(),*WName)); }
						else if(const TSharedPtr<FJsonObject>* SlotObj = JObj(Node,TEXT("slot")))
							if(const TSharedPtr<FJsonObject>* SP = JObj(*SlotObj,TEXT("properties")))
								for(const auto& kv : (*SP)->Values){ const FString e=FBPWidgetGen::SetPropertyFromJson(Slot,kv.Key,kv.Value); if(!e.IsEmpty()) Warn.Add(TEXT("slot prop '")+kv.Key+TEXT("' on ")+WName+TEXT(": ")+e); }
					}
					if(const TSharedPtr<FJsonObject>* Props = JObj(Node,TEXT("properties")))
						for(const auto& kv : (*Props)->Values){ const FString e=FBPWidgetGen::SetPropertyFromJson(W,kv.Key,kv.Value); if(!e.IsEmpty()) Warn.Add(TEXT("prop '")+kv.Key+TEXT("' on ")+WName+TEXT(": ")+e); }
					if(const TArray<TSharedPtr<FJsonValue>>* Kids = JArr(Node,TEXT("children")))
						for(const auto& kv : *Kids){ if(auto ko=kv->AsObject()) Build(ko, W); }
					return W;
				};
				Build(RootNode, (UWidget*)nullptr);
			}
			else { Manual.Add(TEXT("widget: no hierarchy provided; created an empty WidgetTree.")); }

			// Phase 4 (generalized): bind widget events by reflection. Needs widget variables -> compile once.
			if(const TArray<TSharedPtr<FJsonValue>>* Events = JArr(*WObj,TEXT("events")))
			{
				FBPGen::CompileBlueprint(WBP);
				for(const auto& ev : *Events)
				{
					const TSharedPtr<FJsonObject> eo=ev->AsObject(); if(!eo) continue;
					const FString wn=JStr(eo,TEXT("widget")); const FString en=JStr(eo,TEXT("event"),TEXT("OnClicked"));
					TSharedPtr<FJsonObject> res;
					const FString e=FBPWidgetGen::BindWidgetEvent(WBP, wn, en, res);
					// record requested handler (exec-wiring to custom_event/function is P2 / deferred)
					if(const TSharedPtr<FJsonObject>* h=JObj(eo,TEXT("handler")))
					{
						const FString ht=JStr(*h,TEXT("type"),TEXT("bound_event")); const FString hn=JStr(*h,TEXT("name"));
						res->SetStringField(TEXT("handler_type"),ht); res->SetStringField(TEXT("handler_name"),hn);
						if(e.IsEmpty() && ht!=TEXT("bound_event")) { Manual.Add(FString::Printf(TEXT("event %s.%s: bound-event node created; exec-wiring to %s '%s' is deferred (P2)."),*wn,*en,*ht,*hn)); }
					}
					EventBindings.Add(MakeShared<FJsonValueObject>(res));
					if(!e.IsEmpty())
					{
						const FString st=JStr(res,TEXT("status"),TEXT("error"));
						Warn.Add(FString::Printf(TEXT("event bind %s.%s [%s]: %s"),*wn,*en,*st,*e));
						Manual.Add(FString::Printf(TEXT("event %s.%s not bound (%s): %s"),*wn,*en,*st,*e));
					}
				}
			}
		}
	}
	else BP=FBPGen::CreateActorBlueprint(AssetPath, Parent?Parent:AActor::StaticClass());
	if(!BP){ Err.Add(TEXT("asset creation failed")); return Finish(TEXT("failed"),20,nullptr); }
	Log(FString::Printf(TEXT("created %s (type=%s parent=%s)"),*AssetPath,*BpType,Parent?*Parent->GetName():TEXT("<default>")));

	// --- variables ---
	if(const TArray<TSharedPtr<FJsonValue>>* Vars=JArr(Req,TEXT("variables")))
		for(auto& v:*Vars){ auto o=v->AsObject(); if(!o)continue; FString e; FEdGraphPinType ty=PinTypeFromSpec(JObj(o,TEXT("type"))?*JObj(o,TEXT("type")):nullptr,e);
			if(!e.IsEmpty()){ Warn.Add("variable '"+JStr(o,TEXT("name"))+"': "+e); continue; }
			FBPGen::AddVariable(BP,*JStr(o,TEXT("name")),ty,JStr(o,TEXT("default_value")),JStr(o,TEXT("category"),TEXT("Default")),JBool(o,TEXT("editable")));
			if(JBool(o,TEXT("expose_on_spawn"))) Manual.Add("variable '"+JStr(o,TEXT("name"))+"': expose_on_spawn requested; verify in editor."); }

	// --- components ---
	if(const TArray<TSharedPtr<FJsonValue>>* Comps=JArr(Req,TEXT("components")))
		for(auto& c:*Comps){ auto o=c->AsObject(); if(!o)continue; UClass* cc=ResolveClass(JStr(o,TEXT("class")));
			if(!cc){ Warn.Add("component '"+JStr(o,TEXT("name"))+"': class not found "+JStr(o,TEXT("class"))); continue; }
			FBPGen::AddComponent(BP,cc,*JStr(o,TEXT("name")));
			if(!JStr(o,TEXT("attach_to")).IsEmpty()) Manual.Add("component '"+JStr(o,TEXT("name"))+"': attach_to='"+JStr(o,TEXT("attach_to"))+"' requested; verify SCS attachment in editor."); }

	// --- functions (signatures) ---
	if(const TArray<TSharedPtr<FJsonValue>>* Fns=JArr(Req,TEXT("functions")))
		for(auto& f:*Fns){ auto o=f->AsObject(); if(!o)continue; TArray<FBPGenParam> P;
			if(auto ins=JArr(o,TEXT("inputs"))) for(auto& i:*ins){ auto io=i->AsObject(); FString e; P.Add({*JStr(io,TEXT("name")),PinTypeFromSpec(JObj(io,TEXT("type"))?*JObj(io,TEXT("type")):nullptr,e),false}); }
			if(auto outs=JArr(o,TEXT("outputs"))) for(auto& ot:*outs){ auto oo=ot->AsObject(); FString e; P.Add({*JStr(oo,TEXT("name")),PinTypeFromSpec(JObj(oo,TEXT("type"))?*JObj(oo,TEXT("type")):nullptr,e),true}); }
			if(!FBPGen::AddFunctionGraph(BP,*JStr(o,TEXT("name")),P,JBool(o,TEXT("pure")))) Warn.Add("function '"+JStr(o,TEXT("name"))+"' creation failed");
			if(JArr(JObj(o,TEXT("body"))?*JObj(o,TEXT("body")):nullptr,TEXT("nodes"))) Manual.Add("function '"+JStr(o,TEXT("name"))+"': body nodes not auto-wired (signature only)."); }

	// --- event dispatchers ---
	if(const TArray<TSharedPtr<FJsonValue>>* Ds=JArr(Req,TEXT("event_dispatchers")))
		for(auto& d:*Ds){ auto o=d->AsObject(); if(!o)continue; TArray<FBPGenParam> P;
			if(auto ps=JArr(o,TEXT("parameters"))) for(auto& p:*ps){ auto po=p->AsObject(); FString e; P.Add({*JStr(po,TEXT("name")),PinTypeFromSpec(JObj(po,TEXT("type"))?*JObj(po,TEXT("type")):nullptr,e),false}); }
			if(!FBPGen::AddEventDispatcher(BP,*JStr(o,TEXT("name")),P)) Warn.Add("dispatcher '"+JStr(o,TEXT("name"))+"' creation failed"); }

	// --- graphs (EventGraph nodes + edges) ---
	if(const TArray<TSharedPtr<FJsonValue>>* Graphs=JArr(Req,TEXT("graphs")))
	{
		for(auto& gv:*Graphs){ auto g=gv->AsObject(); if(!g)continue;
			const FString gname=JStr(g,TEXT("graph_name"),TEXT("EventGraph"));
			UEdGraph* G = (gname.Equals(TEXT("EventGraph"),ESearchCase::IgnoreCase)) ? FBPGen::GetEventGraph(BP) : FBPGen::GetEventGraph(BP);
			if(!G){ Warn.Add("graph '"+gname+"' not resolvable (only EventGraph node authoring supported)"); continue; }
			TMap<FString,UEdGraphNode*> Local;
			if(auto nodes=JArr(g,TEXT("nodes"))) for(auto& nv:*nodes){ auto n=nv->AsObject(); if(!n)continue;
				const FString lid=JStr(n,TEXT("local_id")); const FString nt=JStr(n,TEXT("node_type")).ToLower();
				int32 x=(int32)JNum(JObj(n,TEXT("position"))?*JObj(n,TEXT("position")):nullptr,TEXT("x"));
				int32 y=(int32)JNum(JObj(n,TEXT("position"))?*JObj(n,TEXT("position")):nullptr,TEXT("y"));
				UEdGraphNode* node=nullptr;
				if(nt==TEXT("event")) node=FBPGen::SpawnEvent(G,*JStr(n,TEXT("event_name"),TEXT("ReceiveBeginPlay")),Parent?Parent:AActor::StaticClass(),x,y);
				else if(nt==TEXT("call_function")){ FString fp=JStr(n,TEXT("function")); int32 dot; if(fp.FindLastChar('.',dot)){ FString owner=fp.Left(dot); FString fn=fp.Mid(dot+1); UClass* oc=ResolveClass(owner); if(oc) node=FBPGen::SpawnCallFunc(G,oc,*fn,x,y); else Warn.Add("call_function owner class not found: "+owner); } else Warn.Add("call_function 'function' must be /Script/Pkg.Class.Func"); }
				else if(nt==TEXT("branch")) node=FBPGen::SpawnBranch(G,x,y);
				else if(nt==TEXT("sequence")) node=FBPGen::SpawnSequence(G,FMath::Max(2,(int32)JNum(n,TEXT("num_outputs"),2)),x,y);
				else if(nt==TEXT("variable_get")) node=FBPGen::SpawnVarGet(G,*JStr(n,TEXT("variable")),x,y);
				else if(nt==TEXT("variable_set")) node=FBPGen::SpawnVarSet(G,*JStr(n,TEXT("variable")),x,y);
				else if(nt==TEXT("comment")) node=FBPGen::AddComment(G,JStr(n,TEXT("text"),TEXT("Comment")),x,y,300,150);
				else { Warn.Add("unsupported node_type '"+nt+"' (local_id="+lid+"); skipped"); Manual.Add("node_type '"+nt+"' unsupported by create worker"); }
				if(node&&!lid.IsEmpty()) Local.Add(lid,node);
				// defaults
				if(node){ if(auto defs=JObj(n,TEXT("defaults"))){ for(auto& kv:(*defs)->Values){ FString sv; if(kv.Value->TryGetString(sv)) FBPGen::SetPinDefault(node,kv.Key,sv); else FBPGen::SetPinDefault(node,kv.Key,kv.Value->AsNumber()!=0?FString::SanitizeFloat(kv.Value->AsNumber()):TEXT("")); } } }
			}
			// comments (bounds)
			if(auto cs=JArr(g,TEXT("comments"))) for(auto& cv:*cs){ auto c=cv->AsObject(); if(!c)continue; auto b=JObj(c,TEXT("bounds")); FBPGen::AddComment(G,JStr(c,TEXT("text"),TEXT("Comment")),(int32)JNum(b?*b:nullptr,TEXT("x")),(int32)JNum(b?*b:nullptr,TEXT("y")),(int32)JNum(b?*b:nullptr,TEXT("w"),300),(int32)JNum(b?*b:nullptr,TEXT("h"),150)); }
			// edges
			if(auto edges=JArr(g,TEXT("edges"))) for(auto& ev:*edges){ auto e=ev->AsObject(); if(!e)continue;
				FString from=JStr(e,TEXT("from")), to=JStr(e,TEXT("to")); int32 fd,td;
				if(!from.FindLastChar('.',fd)||!to.FindLastChar('.',td)){ Warn.Add("edge endpoints must be 'local_id.Pin'"); continue; }
				UEdGraphNode* fn=Local.FindRef(from.Left(fd)); UEdGraphNode* tn=Local.FindRef(to.Left(td));
				if(!fn||!tn){ Warn.Add("edge references unknown local_id: "+from+" -> "+to); continue; }
				UEdGraphPin* fp=FBPGen::FindPin(fn,from.Mid(fd+1),EGPD_Output); UEdGraphPin* tp=FBPGen::FindPin(tn,to.Mid(td+1),EGPD_Input);
				if(!fp||!tp){ Warn.Add("edge pin not found: "+from+" -> "+to); continue; }
				if(!FBPGen::Connect(fp,tp)) Warn.Add("edge connect refused: "+from+" -> "+to); }
		}
	}

	// --- compile + save ---
	const FString Compile=FBPGen::CompileBlueprint(BP);
	const bool bSaved=FBPGen::SaveAsset(BP);
	if(!bSaved) Warn.Add("SaveAsset returned false");
	Log(FString::Printf(TEXT("compile=%s saved=%d"),*Compile,bSaved));

	// --- created IR + summary + dot ---
	TSharedRef<FJsonObject> IR=FBPGenIRDumper::DumpBlueprint(BP).ToSharedRef();
	WriteJson(FPaths::Combine(OutDir,TEXT("created_ir.json")),IR);
	{
		int32 gc=0,nc=0; const TArray<TSharedPtr<FJsonValue>>* gs; if(IR->TryGetArrayField(TEXT("graphs"),gs)){ gc=gs->Num(); for(auto&g:*gs){ const TArray<TSharedPtr<FJsonValue>>* ns; if(g->AsObject()->TryGetArrayField(TEXT("nodes"),ns)) nc+=ns->Num(); } }
		FString sm=FString::Printf(TEXT("# Create Summary\n\n- asset: `%s`\n- type: %s  parent: %s\n- compile: %s  saved: %d\n- graphs: %d  nodes: %d\n- warnings: %d  manual: %d\n"),
			*AssetPath,*BpType,ParentPath.IsEmpty()?TEXT("<default>"):*ParentPath,*Compile,bSaved,gc,nc,Warn.Num(),Manual.Num());
		FFileHelper::SaveStringToFile(sm,*FPaths::Combine(OutDir,TEXT("summary.md")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("digraph Created { label=\"%s\"; node[shape=box,style=rounded]; }\n"),*AssetPath),*FPaths::Combine(OutDir,TEXT("viz\\created.dot")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	// Widget hierarchy preview (DOT + Mermaid) from the created_ir widget_tree.
	if(bWidgetAsset)
	{
		TSharedPtr<FJsonObject> RootNode; const TSharedPtr<FJsonObject>* WT=nullptr;
		if(IR->TryGetObjectField(TEXT("widget_tree"),WT) && WT){ if(const TSharedPtr<FJsonObject>* rn=JObj(*WT,TEXT("root"))) RootNode=*rn; }
		FString Dot=TEXT("digraph WidgetTree {\n  rankdir=TB;\n  node[shape=box,style=rounded];\n");
		FString Mmd=TEXT("flowchart TB\n");
		int32 counter=0;
		if(RootNode.IsValid()) EmitWidgetDot(RootNode,Dot,Mmd,counter,FString());
		Dot+=TEXT("}\n");
		FFileHelper::SaveStringToFile(Dot,*FPaths::Combine(OutDir,TEXT("viz\\hierarchy.dot")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		FFileHelper::SaveStringToFile(Mmd,*FPaths::Combine(OutDir,TEXT("viz\\hierarchy.mmd")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	const bool bBad = Compile.Contains(TEXT("error"))||Compile.Contains(TEXT("fail"));
	return Finish(bBad?TEXT("partial"):TEXT("success"), bBad?10:0, BP);
}
