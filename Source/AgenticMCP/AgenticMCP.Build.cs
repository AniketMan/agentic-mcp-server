// AgenticMCP.Build.cs
// Module build rules for the AgenticMCP editor plugin.
// This plugin is Editor-only -- it will NOT be included in packaged builds.

using UnrealBuildTool;

public class AgenticMCP : ModuleRules
{
	public AgenticMCP(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Allow missing modules at link time -- optional subsystems (Niagara,
		// OculusXR, PCG, etc.) may not be present in every project.
		bPrecompile = false;

		// ---- Public dependencies (headers exposed to other modules) ----
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",           // Editor APIs, FBlueprintEditorUtils
			"BlueprintGraph",     // K2Node types, UEdGraphSchema_K2
			"Json",               // TJsonWriter, FJsonObject
			"JsonUtilities",      // FJsonObjectConverter
			"HTTPServer",         // FHttpServerModule, IHttpRouter
			"Sockets",            // Socket subsystem for port check
			"Networking"          // Networking primitives
		});

		// ---- Private dependencies (implementation only) ----
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",      // Asset scanning and lookup
			"AssetTools",         // Asset creation, rename, duplicate, import
			"Kismet",             // Blueprint editor utilities
			"KismetCompiler",     // Blueprint compilation
			"EditorSubsystem",    // UEditorSubsystem base class
			"LevelEditor",        // Level management APIs
			"Landscape", "NavigationSystem",          // Landscape actor support
			"Foliage",            // Foliage editing APIs
			"RHI",                // Render hardware interface (for nullrhi commandlet)
			"ImageWrapper",       // PNG/JPEG compression for screenshots
			"GraphEditor",        // Graph editor for K2Node types
			"RenderCore",         // FlushRenderingCommands
			"LevelSequence",      // Level Sequence reading/editing
			"MovieScene",         // MovieScene tracks and sections
			"MovieSceneTracks",   // Audio, Animation tracks
			"Niagara",            // Niagara particle system APIs
			"InputCore",          // EKeys, FKey for input simulation
			"Slate",              // FSlateApplication for input
			"SlateCore",          // Slate core types
			"PhysicsCore",        // Physics body instance, constraints
			"AnimGraph",          // Animation Blueprint graph nodes
			"AnimGraphRuntime",   // Animation state machine runtime
			"UMG",                // UMG Widget Blueprint support
			"SourceControl",      // Source control integration
			"AIModule",           // AI controllers, behavior tree runtime
			"GameplayTasks",      // Gameplay task system (used by AI)
			"BehaviorTreeEditor",  // Behavior Tree editor APIs (node graph editing)
			"ToolMenus",          // Editor tool menus and settings access
			"DeveloperSettings",  // UDeveloperSettings access
			"MaterialEditor",     // Material editor APIs
			"MovieRenderPipelineCore", // Movie Render Pipeline (sequencer render)
			"PropertyEditor",     // Property editing for settings
			"EditorStyle",        // Editor style for widget inspection
			"OculusXRHMD",        // Meta XR HMD APIs
			"OculusXRInput"       // Meta XR Input/Hand tracking APIs
		});

		// ---- Conditionally available modules ----
		// PCG may not be available in all engine builds
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("PCG");
		}
	}
}
