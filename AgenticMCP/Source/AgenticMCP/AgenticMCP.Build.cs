// AgenticMCP.Build.cs
// Module build rules for the AgenticMCP editor plugin.
// This plugin is Editor-only — it will NOT be included in packaged builds.

using UnrealBuildTool;

public class AgenticMCP : ModuleRules
{
	public AgenticMCP(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

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
			"AssetTools",         // Asset creation and rename
			"Kismet",             // Blueprint editor utilities
			"KismetCompiler",     // Blueprint compilation
			"EditorSubsystem",    // UEditorSubsystem base class
			"LevelEditor",        // Level management APIs
			"Landscape",          // Landscape actor support
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
			"OculusXRHMD",        // Meta XR HMD APIs
			"OculusXRInput"       // Meta XR Input/Hand tracking APIs
		});
	}
}
