// Copyright BlueprintAnalyseTool. All Rights Reserved.
//
// Read-only by design. Do NOT add KismetCompiler / BlueprintCompilationManager
// dependencies here. See docs/readonly_safety.md.

using UnrealBuildTool;

public class BlueprintAgentToolsEditor : ModuleRules
{
	public BlueprintAgentToolsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicIncludePaths.AddRange(new string[]
		{
		});

		PrivateIncludePaths.AddRange(new string[]
		{
		});

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
			"AssetRegistry",
			"AssetTools",
			"BlueprintGraph",
			"EditorSubsystem",
			"Kismet",
			"UMG",
			"UMGEditor",
			"AnimGraph",
			"AnimGraphRuntime",
			"UnrealEd",
			"Slate",
			"SlateCore",
		});

		// Strict read-only enforcement: forbid linking with compilation modules.
		// If anyone adds the modules below, code review must reject.
		// PrivateDependencyModuleNames.Add("KismetCompiler");                 // FORBIDDEN
		// PrivateDependencyModuleNames.Add("BlueprintCompilationManager");    // FORBIDDEN
	}
}
