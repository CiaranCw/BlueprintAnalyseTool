// Copyright BlueprintAnalyseTool. All Rights Reserved.
//
// Generator plugin. Unlike the read-only BlueprintAgentTools plugin, this module
// is ALLOWED to create/compile/save assets, but ONLY under /Game/BPParserTest.

using UnrealBuildTool;

public class BPParserTestGen : ModuleRules
{
	public BPParserTestGen(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"JsonUtilities",
		});

		// Only modules actually referenced by the source are listed, to minimize
		// link/compile surface in UE 5.4. (Removed: Kismet, EditorStyle [deprecated],
		// EditorSubsystem, Projects, UMG, DeveloperSettings — none were used.)
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",            // FKismetEditorUtilities, FBlueprintEditorUtils, FStructureEditorUtils,
			                       // FEnumEditorUtils, UEnumFactory, UEdGraphNode_Comment, save utils, DiffUtils::LoadPackageForDiff
			"GraphEditor",         // FGraphDiffControl::DiffGraphs (headless structural blueprint diff)
			"BlueprintGraph",      // UK2Node_* node classes, UEdGraphSchema_K2
			"KismetCompiler",      // defensive (CompileBlueprint path)
			"AssetTools",          // IAssetTools / FAssetToolsModule (enum asset creation)
			"AssetRegistry",       // FAssetRegistryModule::AssetCreated
			"ToolMenus",           // editor Tools menu entry
			"Slate",               // FUIAction / FExecuteAction
			"SlateCore",           // FSlateIcon
			"EditorSubsystem",     // GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() (editor_live open-editor check)
			"UMG",                 // UWidget/UWidgetTree/UPanelWidget/UPanelSlot/UserWidget (Widget Blueprint creation + IR)
			"UMGEditor",           // UWidgetBlueprint / UWidgetBlueprintFactory (create WBP asset)
		});
	}
}
