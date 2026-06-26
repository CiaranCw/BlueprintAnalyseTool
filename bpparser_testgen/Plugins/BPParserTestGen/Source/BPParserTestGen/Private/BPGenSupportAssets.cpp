// Copyright BlueprintAnalyseTool. All Rights Reserved.
#include "BPGenSupportAssets.h"
#include "BPGen.h"
#include "BPGenOrchestrator.h"
#include "BPParserTestGenModule.h"

#include "Engine/UserDefinedEnum.h"
#include "Engine/UserDefinedStruct.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"

#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"  // full FStructVariableDescription definition

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/EnumFactory.h"
#include "Misc/PackageName.h"

namespace
{
	void EnsureCleanAssetPath(const FString& FullAssetPath)
	{
		const FString PackageName = FullAssetPath;
		const FString AssetName = FPackageName::GetShortName(FullAssetPath);
		if (UPackage* Existing = FindPackage(nullptr, *PackageName))
		{
			if (UObject* Obj = StaticFindObject(nullptr, Existing, *AssetName))
			{
				Obj->Rename(nullptr, GetTransientPackage(),
					REN_DontCreateRedirectors | REN_DoNotDirty | REN_ForceNoResetLoaders);
				Obj->MarkAsGarbage();
			}
		}
	}
}

// ----------------------------------------------------------------------------
FBPGenAssetResult FBPGenSupportAssets::BuildEnum()
{
	FBPGenAssetResult R;
	R.AssetPath = PathEnum();
	R.AssetType = TEXT("Enum");

	EnsureCleanAssetPath(PathEnum());

	// IAssetTools::CreateAsset can return null under a commandlet (unattended); call the
	// factory directly instead.
	UPackage* Pkg = CreatePackage(PathEnum());
	UEnumFactory* Factory = NewObject<UEnumFactory>();
	UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(Factory->FactoryCreateNew(
		UUserDefinedEnum::StaticClass(), Pkg, FName("E_BPParserTestState"),
		RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));

	if (!Enum)
	{
		R.Notes.Add(TEXT("UEnumFactory::FactoryCreateNew returned null."));
		return R;
	}
	R.bCreated = true;
	FAssetRegistryModule::AssetCreated(Enum);
	Pkg->MarkPackageDirty();

	const TArray<FString> Names = { TEXT("Idle"), TEXT("Moving"), TEXT("Attacking"), TEXT("Dead") };
	// A freshly created UserDefinedEnum has 1 enumerator (+ hidden _MAX). Grow to 4.
	while (Enum->NumEnums() < Names.Num() + 1)
	{
		FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
	}
	for (int32 i = 0; i < Names.Num(); ++i)
	{
		FEnumEditorUtils::SetEnumeratorDisplayName(Enum, i, FText::FromString(Names[i]));
	}

	R.CompileStatus = TEXT("n/a");
	R.Notes.Add(TEXT("Enumerators: Idle, Moving, Attacking, Dead (display names)."));
	return R;
}

// ----------------------------------------------------------------------------
FBPGenAssetResult FBPGenSupportAssets::BuildStruct()
{
	FBPGenAssetResult R;
	R.AssetPath = PathStruct();
	R.AssetType = TEXT("Struct");

	EnsureCleanAssetPath(PathStruct());

	UPackage* Pkg = CreatePackage(PathStruct());
	UUserDefinedStruct* Struct = FStructureEditorUtils::CreateUserDefinedStruct(
		Pkg, FName("ST_BPParserTestData"), RF_Public | RF_Standalone | RF_Transactional);

	if (!Struct)
	{
		R.Notes.Add(TEXT("FStructureEditorUtils::CreateUserDefinedStruct returned null. Verify API signature in UE 5.4."));
		return R;
	}
	R.bCreated = true;
	FAssetRegistryModule::AssetCreated(Struct);

	UEnum* StateEnum = FBPGen::LoadEnum(FString(PathEnum()) + TEXT(".E_BPParserTestState"));

	// Snapshot the default member so we can drop it after adding our own fields.
	FGuid DefaultGuid;
	{
		TArray<FStructVariableDescription>& Desc = FStructureEditorUtils::GetVarDesc(Struct);
		if (Desc.Num() > 0) { DefaultGuid = Desc[0].VarGuid; }
	}

	auto AddField = [&](const FString& FieldName, const FEdGraphPinType& Type)
	{
		if (!FStructureEditorUtils::AddVariable(Struct, Type)) { R.Notes.Add(TEXT("AddVariable failed: ") + FieldName); return; }
		TArray<FStructVariableDescription>& Desc = FStructureEditorUtils::GetVarDesc(Struct);
		if (Desc.Num() > 0)
		{
			FStructureEditorUtils::RenameVariable(Struct, Desc.Last().VarGuid, FieldName);
		}
	};

	AddField(TEXT("ID"),             FBPGen::PinInt());
	AddField(TEXT("DisplayName"),    FBPGen::PinText());
	AddField(TEXT("Score"),          FBPGen::PinFloat());
	AddField(TEXT("Location"),       FBPGen::PinVector());
	AddField(TEXT("Rotation"),       FBPGen::PinRotator());
	AddField(TEXT("TransformValue"), FBPGen::PinTransform());
	AddField(TEXT("Tags"),           FBPGen::AsArray(FBPGen::PinName()));
	if (StateEnum) { AddField(TEXT("State"), FBPGen::PinByteEnum(StateEnum)); }
	else           { R.Notes.Add(TEXT("State field skipped: E_BPParserTestState not loaded.")); }
	AddField(TEXT("TargetActor"),    FBPGen::PinObject(AActor::StaticClass()));
	AddField(TEXT("SoftMesh"),       FBPGen::PinSoftObject(UStaticMesh::StaticClass()));

	if (DefaultGuid.IsValid())
	{
		FStructureEditorUtils::RemoveVariable(Struct, DefaultGuid);
	}

	R.CompileStatus = TEXT("n/a");
	R.Notes.Add(TEXT("Fields: ID(int), DisplayName(text), Score(float), Location(vector), Rotation(rotator), TransformValue(transform), Tags(array<name>), State(enum), TargetActor(actor ref), SoftMesh(soft object)."));
	return R;
}

// ----------------------------------------------------------------------------
FBPGenAssetResult FBPGenSupportAssets::BuildInterface()
{
	FBPGenAssetResult R;
	R.AssetPath = PathInterface();
	R.AssetType = TEXT("Interface");

	UBlueprint* BP = FBPGen::CreateInterfaceBlueprint(PathInterface());
	if (!BP) { R.Notes.Add(TEXT("CreateInterfaceBlueprint failed.")); return R; }
	R.bCreated = true;

	UScriptStruct* DataStruct = FBPGen::LoadStruct(FString(PathStruct()) + TEXT(".ST_BPParserTestData"));
	UEnum* StateEnum = FBPGen::LoadEnum(FString(PathEnum()) + TEXT(".E_BPParserTestState"));

	// GetParserTestName() -> String
	{
		TArray<FBPGenParam> P;
		P.Add({ FName("Name"), FBPGen::PinString(), /*bIsReturn*/ true });
		FBPGen::AddInterfaceFunction(BP, FName("GetParserTestName"), P);
	}
	// ReceiveParserTestData(Data : ST_BPParserTestData)
	if (DataStruct)
	{
		TArray<FBPGenParam> P;
		P.Add({ FName("Data"), FBPGen::PinStruct(DataStruct), false });
		FBPGen::AddInterfaceFunction(BP, FName("ReceiveParserTestData"), P);
	}
	else { R.Notes.Add(TEXT("ReceiveParserTestData skipped: struct not loaded.")); }

	// CanAcceptState(State : E_BPParserTestState) -> Bool
	if (StateEnum)
	{
		TArray<FBPGenParam> P;
		P.Add({ FName("State"), FBPGen::PinByteEnum(StateEnum), false });
		P.Add({ FName("bCanAccept"), FBPGen::PinBool(), true });
		FBPGen::AddInterfaceFunction(BP, FName("CanAcceptState"), P);
	}
	else { R.Notes.Add(TEXT("CanAcceptState skipped: enum not loaded.")); }

	R.CompileStatus = FBPGen::CompileBlueprint(BP);
	R.Notes.Add(TEXT("Functions: GetParserTestName()->String, ReceiveParserTestData(Data), CanAcceptState(State)->Bool."));
	return R;
}

// ----------------------------------------------------------------------------
FBPGenAssetResult FBPGenSupportAssets::BuildTargetActor()
{
	FBPGenAssetResult R;
	R.AssetPath = PathTargetActor();
	R.AssetType = TEXT("Actor");

	UBlueprint* BP = FBPGen::CreateActorBlueprint(PathTargetActor(), AActor::StaticClass());
	if (!BP) { R.Notes.Add(TEXT("CreateActorBlueprint failed.")); return R; }
	R.bCreated = true;

	// SCS components for Component-Reference tests.
	FBPGen::AddComponent(BP, USceneComponent::StaticClass(), FName("TestRoot"));
	FBPGen::AddComponent(BP, UStaticMeshComponent::StaticClass(), FName("TestMesh"));

	// A descriptive variable used by Cast / interface tests.
	FBPGen::AddVariable(BP, FName("DisplayLabel"), FBPGen::PinString(), TEXT("Target"), TEXT("Default"), /*instance editable*/ true);

	// Implement BPI_BPParserTest.
	if (!FBPGen::ImplementInterface(BP, FString(PathInterface()) + TEXT(".BPI_BPParserTest")))
	{
		R.Notes.Add(TEXT("ImplementInterface failed or interface unavailable; actor still valid."));
	}

	R.CompileStatus = FBPGen::CompileBlueprint(BP);
	R.Notes.Add(TEXT("Implements BPI_BPParserTest. Components: TestRoot(Scene), TestMesh(StaticMesh). Interface function bodies are default unless edited in UE."));
	return R;
}

// ----------------------------------------------------------------------------
FBPGenAssetResult FBPGenSupportAssets::BuildTestComponent()
{
	FBPGenAssetResult R;
	R.AssetPath = PathComponent();
	R.AssetType = TEXT("ActorComponent");

	UBlueprint* BP = FBPGen::CreateComponentBlueprint(PathComponent(), UActorComponent::StaticClass());
	if (!BP) { R.Notes.Add(TEXT("CreateComponentBlueprint failed.")); return R; }
	R.bCreated = true;

	FBPGen::AddVariable(BP, FName("ComponentTag"), FBPGen::PinName(), TEXT("ParserTestComp"), TEXT("Default"), true);
	FBPGen::AddVariable(BP, FName("ActivationCount"), FBPGen::PinInt(), TEXT("0"), TEXT("Default"), true);

	// A simple impure function: IncrementActivation() -> Int
	{
		TArray<FBPGenParam> P;
		P.Add({ FName("NewCount"), FBPGen::PinInt(), true });
		UEdGraph* Fn = FBPGen::AddFunctionGraph(BP, FName("IncrementActivation"), P, /*pure*/ false);
		if (Fn)
		{
			UEdGraphNode* Entry = FBPGen::FindFunctionEntry(Fn);
			UEdGraphNode* Result = FBPGen::FindFunctionResult(Fn);
			if (Entry && Result) { FBPGen::ConnectExec(Entry, Result); }
		}
	}

	R.CompileStatus = FBPGen::CompileBlueprint(BP);
	R.Notes.Add(TEXT("Vars: ComponentTag(name), ActivationCount(int). Function: IncrementActivation()->Int."));
	return R;
}
