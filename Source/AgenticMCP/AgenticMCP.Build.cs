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

		// UE 5.6: Disable warnings-as-errors for this module
		// C4459 (declaration hides global) triggers due to InterchangeCore headers
		bWarningsAsErrors = false;
		bEnableUndefinedIdentifierWarnings = false;
		UnsafeTypeCastWarningLevel = WarningLevel.Off;

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
			"UMGEditor",          // UMG Widget Blueprint editor support
			"SourceControl",      // Source control integration
			"AIModule",           // AI controllers, behavior tree runtime
			"GameplayTasks",      // Gameplay task system (used by AI)
			// "BehaviorTreeEditor",  // UE 5.6: Removed - AIGraph.h no longer available
			"ToolMenus",          // Editor tool menus and settings access
			"DeveloperSettings",  // UDeveloperSettings access
			"EngineSettings",     // UGameMapsSettings, UGeneralProjectSettings
			"Projects",           // IPluginManager for plugin info
			"MaterialEditor",     // Material editor APIs
			"MovieRenderPipelineCore", // Movie Render Pipeline (sequencer render)
			"PropertyEditor",     // Property editing for settings
			"EditorStyle",        // Editor style for widget inspection
			"OculusXRHMD",        // Meta XR HMD APIs
			"OculusXRInput",      // Meta XR Input/Hand tracking APIs
			"ControlRig",         // Control Rig editing APIs
			"RigVM",              // Rig VM for Control Rig execution
			"GeometryCollectionEngine", // Chaos Geometry Collection
			"ChaosSolverEngine",  // Chaos destruction solver
			"FieldSystemEngine",  // Chaos field system (radial/vector fields)
			"EnhancedInput",      // Enhanced Input system
			"LiveLinkInterface",  // Live Link base interfaces
			"LiveLink",           // Live Link runtime
			"MediaAssets",        // Media Player, Media Source, Media Texture
			"MovieRenderPipelineRenderPasses", // MRG render passes
			"ClothingSystemRuntimeInterface", // Cloth simulation interfaces
			"GameplayAbilities",  // Gameplay Ability System
			"GameplayTags",       // Gameplay Tags (used by GAS)
			"MassEntity",         // Mass Entity framework
			"InterchangeCore",    // Interchange pipeline core
			"InterchangeEngine",  // Interchange pipeline engine
			"VCamCore",           // Virtual Camera core
			"VariantManagerContent", // Variant Manager content types
			"Composure",          // Composure compositing
			"Water"               // Water system
		});

		// ---- Conditionally available modules ----
		// PCG may not be available in all engine builds
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("PCG");
		}
	}
}
