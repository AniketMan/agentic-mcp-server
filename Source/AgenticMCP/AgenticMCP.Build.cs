// AgenticMCP.Build.cs
// Module build rules for the AgenticMCP editor plugin.
// This plugin is Editor-only -- it will NOT be included in packaged builds.
//
// ARCHITECTURE:
// Optional plugin modules (Niagara, OculusXR, ControlRig, Composure, etc.)
// are conditionally added as dependencies only if they exist in the engine.
// This prevents DLL load failures when optional plugins are not enabled.
//
// Modules with direct header includes (Niagara, OculusXR) use preprocessor
// defines (WITH_NIAGARA, WITH_OCULUSXR) so their handler code compiles to
// stubs when the plugin is absent.
//
// All other optional modules use only FModuleManager::IsModuleLoaded() and
// FindFirstObject() at runtime, so they have no compile-time type dependency.

using UnrealBuildTool;

public class AgenticMCP : ModuleRules
{
	public AgenticMCP(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		bPrecompile = false;

		// UE 5.6: Disable warnings-as-errors for this module
		bWarningsAsErrors = false;
		bEnableUndefinedIdentifierWarnings = false;
		UnsafeTypeCastWarningLevel = WarningLevel.Off;

		// ---- Public dependencies (always available in editor builds) ----
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"BlueprintGraph",
			"Json",
			"JsonUtilities",
			"HTTPServer",
			"Sockets",
			"Networking"
		});

		// ---- Core private dependencies (always available in editor builds) ----
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"AssetTools",
			"Kismet",
			"KismetCompiler",
			"EditorSubsystem",
			"LevelEditor",
			"Landscape",
			"NavigationSystem",
			"Foliage",
			"RHI",
			"ImageWrapper",
			"GraphEditor",
			"RenderCore",
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
			"InputCore",
			"Slate",
			"SlateCore",
			"PhysicsCore",
			"AnimGraph",
			"AnimGraphRuntime",
			"UMG",
			"SourceControl",
			"AIModule",
			"GameplayTasks",
			"GameplayTags",
			"ToolMenus",
			"DeveloperSettings",
			"EngineSettings",
			"Projects",
			"MaterialEditor",
			"PropertyEditor"
		});

		// ---- Niagara: direct type usage (UNiagaraComponent, etc.) ----
		// Requires preprocessor guard WITH_NIAGARA in Handlers_Niagara.cpp
		AddOptionalModule("Niagara", "WITH_NIAGARA");

		// ---- OculusXR / Meta XR: direct type usage (UOculusXRFunctionLibrary) ----
		// Requires preprocessor guard WITH_OCULUSXR in Handlers_MetaXR*.cpp
		if (IsModuleAvailable("OculusXRHMD"))
		{
			PrivateDependencyModuleNames.Add("OculusXRHMD");
			AddOptionalModule("OculusXRInput", null);
			PublicDefinitions.Add("WITH_OCULUSXR=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_OCULUSXR=0");
		}

		// ---- Optional modules: no direct includes, runtime-only usage ----
		// These handlers use only FModuleManager::IsModuleLoaded() and
		// FindFirstObject<UClass>() -- zero compile-time type dependency.
		// If the module doesn't exist, the handler gracefully returns an error.
		string[] OptionalModules = new string[]
		{
			"ControlRig",
			"RigVM",
			"Composure",
			"Water",
			"VCamCore",
			"LiveLink",
			"LiveLinkInterface",
			"GameplayAbilities",
			"MassEntity",
			"InterchangeCore",
			"InterchangeEngine",
			"MovieRenderPipelineCore",
			"MovieRenderPipelineRenderPasses",
			"VariantManagerContent",
			"GeometryCollectionEngine",
			"ChaosSolverEngine",
			"FieldSystemEngine",
			"EnhancedInput",
			"MediaAssets",
			"ClothingSystemRuntimeInterface",
			"PCG"
		};

		foreach (string Mod in OptionalModules)
		{
			if (IsModuleAvailable(Mod))
			{
				PrivateDependencyModuleNames.Add(Mod);
			}
		}
	}

	/// <summary>
	/// Check if a module exists in the engine/project by looking up its directory.
	/// Returns true if the module's Build.cs can be found.
	/// </summary>
	private bool IsModuleAvailable(string ModuleName)
	{
		string Dir = GetModuleDirectory(ModuleName);
		return !string.IsNullOrEmpty(Dir);
	}

	/// <summary>
	/// Add an optional module dependency with a preprocessor define.
	/// If the module exists, adds it as a dependency and defines WITH_X=1.
	/// If not, defines WITH_X=0 so handler code compiles to stubs.
	/// </summary>
	private void AddOptionalModule(string ModuleName, string DefineName)
	{
		if (IsModuleAvailable(ModuleName))
		{
			PrivateDependencyModuleNames.Add(ModuleName);
			if (DefineName != null)
			{
				PublicDefinitions.Add(DefineName + "=1");
			}
		}
		else
		{
			if (DefineName != null)
			{
				PublicDefinitions.Add(DefineName + "=0");
			}
		}
	}
}
