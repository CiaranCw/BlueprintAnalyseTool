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

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",            // FKismetEditorUtilities, FBlueprintEditorUtils, factories
			"BlueprintGraph",      // UK2Node_* node classes
			"KismetCompiler",      // compilation (explicit, defensive)
			"Kismet",              // editor kismet helpers
			"AssetTools",
			"AssetRegistry",
			"Slate",
			"SlateCore",
			"ToolMenus",           // editor menu button
			"EditorStyle",
			"EditorSubsystem",
			"Projects",
			"UMG",                 // FBPVariableDescription / LinearColor structs are engine; kept for safety
			"DeveloperSettings",
		});
	}
}
