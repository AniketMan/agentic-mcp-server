// ManusMCP.Build.cs
// Module build rules for the ManusMCP editor plugin.
// This plugin is Editor-only — it will NOT be included in packaged builds.

using UnrealBuildTool;

public class ManusMCP : ModuleRules
{
	public ManusMCP(ReadOnlyTargetRules Target) : base(Target)
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
			"RHI"                 // Render hardware interface (for nullrhi commandlet)
		});
	}
}
