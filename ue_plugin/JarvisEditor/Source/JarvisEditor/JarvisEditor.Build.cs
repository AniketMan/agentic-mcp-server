// Copyright Aniket Bhatt. All Rights Reserved.
// JarvisEditor.Build.cs - Module build rules for JARVIS Editor plugin
// This plugin exposes Blueprint graph manipulation to Python scripting,
// enabling AI-controlled level logic editing in UE 5.6.

using UnrealBuildTool;

public class JarvisEditor : ModuleRules
{
	public JarvisEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Editor modules for Blueprint graph manipulation
			"UnrealEd",
			"BlueprintGraph",
			"KismetCompiler",
			"Kismet",                    // SGraphEditor, SKismetInspector
			"GraphEditor",               // Graph editor widgets
			"EditorFramework",
			"ToolMenus",
			"Slate",
			"SlateCore",
			"EditorStyle",
			"PropertyEditor",

			// Subsystems and gameplay
			"GameplayMessageRuntime",

			// Python interop
			"PythonScriptPlugin",

			// Sequencer (for level sequence manipulation)
			"MovieScene",
			"MovieSceneTracks",
			"LevelSequence",
			"Sequencer",
		});
	}
}
