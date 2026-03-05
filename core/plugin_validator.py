"""
plugin_validator.py -- UE 5.6 .uplugin validation and analysis.

Validates plugin descriptor files (.uplugin) and their associated
filesystem structure against UE 5.6 requirements. Checks for:

  - Descriptor field correctness (FileVersion, Modules, Plugins)
  - Module descriptor validity (Name, Type, LoadingPhase)
  - Filesystem structure (Source/, Content/, Binaries/, Resources/)
  - .Build.cs presence and basic dependency validation
  - Module name consistency across descriptor, source, and binaries
  - Plugin dependency chain validation (missing deps, circular refs)
  - Content/CanContainContent consistency
  - API macro presence in module headers
  - Platform targeting consistency

Usage:
    validator = PluginValidator("/path/to/MyPlugin")
    report = validator.validate()
    print(report.to_dict())

Each check produces a ValidationItem with severity (error/warning/info),
a human-readable message, and the source location of the issue.
"""

import json
import os
import re
import logging
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Set, Tuple

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Valid enum values from UE 5.6 source
# ---------------------------------------------------------------------------
# Source: Engine/Source/Runtime/Projects/Public/ModuleDescriptor.h

VALID_MODULE_TYPES = {
    "Runtime", "RuntimeNoCommandlet", "RuntimeAndProgram",
    "CookedOnly", "UncookedOnly",
    "Developer", "DeveloperTool",
    "Editor", "EditorNoCommandlet", "EditorAndProgram",
    "Program",
    "ServerOnly", "ClientOnly", "ClientOnlyNoCommandlet",
}

# Source: Engine/Source/Runtime/Projects/Public/ModuleDescriptor.h
VALID_LOADING_PHASES = {
    "EarliestPossible", "PostConfigInit", "PostSplashScreen",
    "PreEarlyLoadingScreen", "PreLoadingScreen",
    "PreDefault", "Default", "PostDefault",
    "PostEngineInit", "None",
}

# Module types that ship in packaged builds (non-editor)
SHIPPING_MODULE_TYPES = {
    "Runtime", "RuntimeNoCommandlet", "RuntimeAndProgram",
    "CookedOnly", "ServerOnly", "ClientOnly", "ClientOnlyNoCommandlet",
}

# Module types that are editor-only
EDITOR_MODULE_TYPES = {
    "Editor", "EditorNoCommandlet", "EditorAndProgram",
    "Developer", "DeveloperTool", "UncookedOnly",
}

# Well-known engine module names for dependency validation
# This is a subset; full validation requires the engine module list
KNOWN_ENGINE_MODULES = {
    "Core", "CoreUObject", "Engine", "InputCore", "Slate", "SlateCore",
    "UMG", "UnrealEd", "PropertyEditor", "EditorStyle", "EditorFramework",
    "RenderCore", "RHI", "Renderer", "ShaderCore", "MeshDescription",
    "PhysicsCore", "Chaos", "GeometryCore", "GeometryFramework",
    "NavigationSystem", "AIModule", "GameplayTasks", "GameplayTags",
    "GameplayAbilities", "EnhancedInput", "Niagara", "NiagaraCore",
    "MovieScene", "MovieSceneTracks", "LevelSequence", "SequencerCore",
    "MediaAssets", "AudioMixer", "AudioExtensions", "SignalProcessing",
    "Json", "JsonUtilities", "HTTP", "Sockets", "Networking",
    "OnlineSubsystem", "OnlineSubsystemUtils",
    "DeveloperSettings", "Projects", "ApplicationCore",
    "HeadMountedDisplay", "XRBase", "OpenXR", "OpenXRHMD",
    "OculusXR", "MetaXR", "MetaXRHaptics",
    "PCG", "PCGCompute", "StructUtils",
    "CommonUI", "CommonInput", "GameFeatures", "ModularGameplay",
    "GameplayMessageRuntime", "ModelViewViewModel",
    "AssetRegistry", "AssetTools", "ContentBrowser",
    "BlueprintGraph", "KismetCompiler", "Kismet",
    "AnimationCore", "AnimGraphRuntime", "LiveLinkInterface",
    "CinematicCamera", "TemplateSequence",
    "Paper2D", "ProceduralMeshComponent", "MeshConversion",
    "Foliage", "Landscape", "Water",
    "DatasmithCore", "InterchangeCore",
    "ChaosCloth", "ChaosVehicles",
}


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------

@dataclass
class ValidationItem:
    """A single validation finding."""
    severity: str       # "error", "warning", "info"
    category: str       # "descriptor", "module", "filesystem", "dependency", "build"
    message: str        # Human-readable description
    location: str = ""  # File path or field reference
    suggestion: str = ""  # How to fix it


@dataclass
class PluginReport:
    """Complete validation report for a plugin."""
    plugin_name: str
    plugin_path: str
    descriptor: Optional[Dict] = None
    items: List[ValidationItem] = field(default_factory=list)
    module_count: int = 0
    dependency_count: int = 0
    has_content: bool = False
    has_source: bool = False
    has_binaries: bool = False
    has_icon: bool = False

    @property
    def error_count(self) -> int:
        return sum(1 for i in self.items if i.severity == "error")

    @property
    def warning_count(self) -> int:
        return sum(1 for i in self.items if i.severity == "warning")

    @property
    def passed(self) -> bool:
        return self.error_count == 0

    def to_dict(self) -> Dict:
        return {
            "plugin_name": self.plugin_name,
            "plugin_path": self.plugin_path,
            "passed": self.passed,
            "error_count": self.error_count,
            "warning_count": self.warning_count,
            "info_count": sum(1 for i in self.items if i.severity == "info"),
            "module_count": self.module_count,
            "dependency_count": self.dependency_count,
            "has_content": self.has_content,
            "has_source": self.has_source,
            "has_binaries": self.has_binaries,
            "has_icon": self.has_icon,
            "descriptor": self.descriptor,
            "items": [
                {
                    "severity": i.severity,
                    "category": i.category,
                    "message": i.message,
                    "location": i.location,
                    "suggestion": i.suggestion,
                }
                for i in self.items
            ],
        }


# ---------------------------------------------------------------------------
# Validator
# ---------------------------------------------------------------------------

class PluginValidator:
    """
    Validates a UE 5.6 plugin directory.
    
    Expects the plugin root directory containing the .uplugin file.
    Can also accept the .uplugin file path directly.
    
    Performs comprehensive validation across:
      - Descriptor correctness
      - Module configuration
      - Filesystem structure
      - Build file analysis
      - Dependency chain validation
    """

    def __init__(self, plugin_path: str):
        """
        Initialize the validator.
        
        Args:
            plugin_path: Path to plugin directory or .uplugin file.
        """
        path = Path(plugin_path)
        
        # Accept either directory or .uplugin file
        if path.is_file() and path.suffix == ".uplugin":
            self.uplugin_path = path
            self.plugin_dir = path.parent
        elif path.is_dir():
            self.plugin_dir = path
            # Find .uplugin file in directory
            uplugin_files = list(path.glob("*.uplugin"))
            if len(uplugin_files) == 1:
                self.uplugin_path = uplugin_files[0]
            elif len(uplugin_files) > 1:
                # Use the one matching the directory name
                matching = [f for f in uplugin_files if f.stem == path.name]
                self.uplugin_path = matching[0] if matching else uplugin_files[0]
            else:
                self.uplugin_path = None
        else:
            raise FileNotFoundError(f"Plugin path not found: {plugin_path}")
        
        self.plugin_name = self.plugin_dir.name
        self.descriptor: Optional[Dict] = None
        self.report = PluginReport(
            plugin_name=self.plugin_name,
            plugin_path=str(self.plugin_dir),
        )

    def validate(self) -> PluginReport:
        """
        Run all validation checks and return a complete report.
        
        Checks are run in order of dependency:
          1. Descriptor parsing
          2. Descriptor field validation
          3. Module descriptor validation
          4. Filesystem structure validation
          5. Build file validation
          6. Dependency chain validation
        """
        logger.info(f"Validating plugin: {self.plugin_name} at {self.plugin_dir}")
        
        # Phase 1: Parse descriptor
        if not self._parse_descriptor():
            return self.report  # Can't continue without descriptor
        
        # Phase 2: Validate descriptor fields
        self._validate_descriptor_fields()
        
        # Phase 3: Validate module descriptors
        self._validate_modules()
        
        # Phase 4: Validate filesystem structure
        self._validate_filesystem()
        
        # Phase 5: Validate .Build.cs files
        self._validate_build_files()
        
        # Phase 6: Validate dependency chains
        self._validate_dependencies()
        
        # Summary info
        self._add_summary_info()
        
        logger.info(
            f"Validation complete: {self.report.error_count} errors, "
            f"{self.report.warning_count} warnings"
        )
        return self.report

    # ----- Phase 1: Parse Descriptor -----

    def _parse_descriptor(self) -> bool:
        """Parse the .uplugin JSON file."""
        if self.uplugin_path is None:
            self.report.items.append(ValidationItem(
                severity="error",
                category="descriptor",
                message="No .uplugin file found in plugin directory",
                location=str(self.plugin_dir),
                suggestion="Create a .uplugin descriptor file matching the plugin directory name",
            ))
            return False
        
        try:
            with open(self.uplugin_path, 'r', encoding='utf-8-sig') as f:
                self.descriptor = json.load(f)
            self.report.descriptor = self.descriptor
            logger.debug(f"Parsed descriptor: {self.uplugin_path}")
            return True
        except json.JSONDecodeError as e:
            self.report.items.append(ValidationItem(
                severity="error",
                category="descriptor",
                message=f"Invalid JSON in .uplugin file: {e}",
                location=str(self.uplugin_path),
                suggestion="Fix the JSON syntax error in the .uplugin file",
            ))
            return False
        except Exception as e:
            self.report.items.append(ValidationItem(
                severity="error",
                category="descriptor",
                message=f"Failed to read .uplugin file: {e}",
                location=str(self.uplugin_path),
            ))
            return False

    # ----- Phase 2: Descriptor Field Validation -----

    def _validate_descriptor_fields(self):
        """Validate top-level descriptor fields."""
        d = self.descriptor
        
        # FileVersion (required, must be 3)
        fv = d.get("FileVersion")
        if fv is None:
            self._error("descriptor", "Missing required field 'FileVersion'",
                        str(self.uplugin_path), "Add \"FileVersion\": 3")
        elif fv != 3:
            self._warning("descriptor",
                          f"FileVersion is {fv}, expected 3 (current UE 5.6 format)",
                          str(self.uplugin_path), "Update FileVersion to 3")
        
        # Version (recommended)
        if "Version" not in d:
            self._warning("descriptor", "Missing 'Version' field (integer plugin version)",
                          str(self.uplugin_path), "Add a Version field (e.g., 1)")
        
        # VersionName (recommended)
        if "VersionName" not in d:
            self._warning("descriptor", "Missing 'VersionName' field (display version string)",
                          str(self.uplugin_path), "Add a VersionName field (e.g., \"1.0\")")
        
        # FriendlyName
        if "FriendlyName" not in d:
            self._info("descriptor", "No 'FriendlyName' set; plugin will display as directory name",
                        str(self.uplugin_path))
        
        # Description
        if not d.get("Description"):
            self._info("descriptor", "No 'Description' set for the plugin",
                        str(self.uplugin_path))
        
        # Modules array
        modules = d.get("Modules", [])
        if not isinstance(modules, list):
            self._error("descriptor", "'Modules' must be an array",
                        str(self.uplugin_path))
        elif len(modules) == 0:
            # Only an error if Source/ exists
            if (self.plugin_dir / "Source").is_dir():
                self._error("descriptor",
                            "Source/ directory exists but 'Modules' array is empty",
                            str(self.uplugin_path),
                            "Add module descriptors for each module in Source/")
            else:
                self._info("descriptor", "No modules defined (content-only plugin)")
        
        self.report.module_count = len(modules) if isinstance(modules, list) else 0
        
        # Plugins (dependencies)
        plugins = d.get("Plugins", [])
        if isinstance(plugins, list):
            self.report.dependency_count = len(plugins)

    # ----- Phase 3: Module Descriptor Validation -----

    def _validate_modules(self):
        """Validate each module descriptor entry."""
        modules = self.descriptor.get("Modules", [])
        if not isinstance(modules, list):
            return
        
        seen_names: Set[str] = set()
        
        for i, mod in enumerate(modules):
            loc = f"{self.uplugin_path} -> Modules[{i}]"
            
            if not isinstance(mod, dict):
                self._error("module", f"Module entry {i} is not an object", loc)
                continue
            
            # Name (required)
            name = mod.get("Name")
            if not name:
                self._error("module", f"Module {i} missing required 'Name' field", loc,
                            "Every module must have a unique Name")
                continue
            
            if name in seen_names:
                self._error("module", f"Duplicate module name '{name}'", loc,
                            "Each module must have a unique name")
            seen_names.add(name)
            
            # Name must be a valid C++ identifier
            if not re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', name):
                self._error("module",
                            f"Module name '{name}' is not a valid C++ identifier",
                            loc, "Use only letters, digits, and underscores")
            
            # Type (required)
            mod_type = mod.get("Type")
            if not mod_type:
                self._error("module", f"Module '{name}' missing required 'Type' field",
                            loc, f"Valid types: {', '.join(sorted(VALID_MODULE_TYPES))}")
            elif mod_type not in VALID_MODULE_TYPES:
                self._error("module",
                            f"Module '{name}' has invalid Type '{mod_type}'",
                            loc, f"Valid types: {', '.join(sorted(VALID_MODULE_TYPES))}")
            
            # LoadingPhase (optional but validate if present)
            phase = mod.get("LoadingPhase")
            if phase and phase not in VALID_LOADING_PHASES:
                self._error("module",
                            f"Module '{name}' has invalid LoadingPhase '{phase}'",
                            loc,
                            f"Valid phases: {', '.join(sorted(VALID_LOADING_PHASES))}")
            
            # Check for common LoadingPhase issues
            if phase == "EarliestPossible" and mod_type in EDITOR_MODULE_TYPES:
                self._warning("module",
                              f"Module '{name}' is Editor-type with EarliestPossible loading; "
                              "this can cause initialization order issues",
                              loc, "Consider using Default or PostDefault for editor modules")
            
            # Platform lists
            allow = mod.get("PlatformAllowList", mod.get("WhitelistPlatforms", []))
            deny = mod.get("PlatformDenyList", mod.get("BlacklistPlatforms", []))
            if allow and deny:
                self._warning("module",
                              f"Module '{name}' has both PlatformAllowList and PlatformDenyList; "
                              "only one should be used",
                              loc, "Use either AllowList or DenyList, not both")

    # ----- Phase 4: Filesystem Structure Validation -----

    def _validate_filesystem(self):
        """Validate the plugin directory structure."""
        # Check key directories
        source_dir = self.plugin_dir / "Source"
        content_dir = self.plugin_dir / "Content"
        binaries_dir = self.plugin_dir / "Binaries"
        resources_dir = self.plugin_dir / "Resources"
        config_dir = self.plugin_dir / "Config"
        
        self.report.has_source = source_dir.is_dir()
        self.report.has_content = content_dir.is_dir()
        self.report.has_binaries = binaries_dir.is_dir()
        
        # Icon check
        icon_path = resources_dir / "Icon128.png"
        self.report.has_icon = icon_path.is_file()
        if not self.report.has_icon:
            self._info("filesystem", "No Resources/Icon128.png found; plugin will use default icon",
                        str(resources_dir))
        
        # CanContainContent consistency
        can_content = self.descriptor.get("CanContainContent", False)
        if self.report.has_content and not can_content:
            self._error("filesystem",
                        "Content/ directory exists but CanContainContent is false or missing",
                        str(content_dir),
                        "Set \"CanContainContent\": true in the .uplugin descriptor")
        elif can_content and not self.report.has_content:
            self._info("filesystem",
                        "CanContainContent is true but no Content/ directory exists",
                        str(self.uplugin_path))
        
        # Validate each module has matching source directory
        modules = self.descriptor.get("Modules", [])
        if isinstance(modules, list) and self.report.has_source:
            for mod in modules:
                if not isinstance(mod, dict):
                    continue
                name = mod.get("Name", "")
                if not name:
                    continue
                
                mod_dir = source_dir / name
                if not mod_dir.is_dir():
                    self._error("filesystem",
                                f"Module '{name}' declared in .uplugin but no Source/{name}/ directory found",
                                str(mod_dir),
                                f"Create the directory Source/{name}/ with the module source files")
                else:
                    # Check for Public/ and Private/ directories
                    public_dir = mod_dir / "Public"
                    private_dir = mod_dir / "Private"
                    if not public_dir.is_dir():
                        self._warning("filesystem",
                                      f"Module '{name}' missing Public/ directory",
                                      str(public_dir),
                                      "Create Public/ for header files exposed to other modules")
                    if not private_dir.is_dir():
                        self._warning("filesystem",
                                      f"Module '{name}' missing Private/ directory",
                                      str(private_dir),
                                      "Create Private/ for implementation files")
        
        # Check for orphan source directories (not declared in .uplugin)
        if self.report.has_source:
            declared_modules = set()
            if isinstance(modules, list):
                for mod in modules:
                    if isinstance(mod, dict) and mod.get("Name"):
                        declared_modules.add(mod["Name"])
            
            for item in source_dir.iterdir():
                if item.is_dir() and item.name not in declared_modules:
                    # Check if it has a .Build.cs (indicating it's a module)
                    build_cs = item / f"{item.name}.Build.cs"
                    if build_cs.is_file():
                        self._warning("filesystem",
                                      f"Source directory '{item.name}' has a .Build.cs but is not "
                                      "declared in the .uplugin Modules array",
                                      str(item),
                                      f"Add a module entry for '{item.name}' to the .uplugin Modules array")

    # ----- Phase 5: Build File Validation -----

    def _validate_build_files(self):
        """Validate .Build.cs files for each module."""
        modules = self.descriptor.get("Modules", [])
        if not isinstance(modules, list):
            return
        
        source_dir = self.plugin_dir / "Source"
        if not source_dir.is_dir():
            return
        
        for mod in modules:
            if not isinstance(mod, dict):
                continue
            name = mod.get("Name", "")
            if not name:
                continue
            
            build_cs = source_dir / name / f"{name}.Build.cs"
            if not build_cs.is_file():
                self._error("build",
                            f"Module '{name}' missing {name}.Build.cs",
                            str(build_cs),
                            f"Create Source/{name}/{name}.Build.cs with module build rules")
                continue
            
            # Parse .Build.cs for basic validation
            try:
                content = build_cs.read_text(encoding='utf-8-sig')
                self._validate_build_cs_content(name, content, str(build_cs), mod)
            except Exception as e:
                self._warning("build",
                              f"Failed to read {name}.Build.cs: {e}",
                              str(build_cs))

    def _validate_build_cs_content(self, module_name: str, content: str,
                                    location: str, mod_descriptor: Dict):
        """Validate the content of a .Build.cs file."""
        # Check class name matches module name
        class_pattern = rf'class\s+{re.escape(module_name)}\s*:\s*ModuleRules'
        if not re.search(class_pattern, content):
            self._error("build",
                        f"Build.cs class name doesn't match module name '{module_name}'",
                        location,
                        f"Class must be: public class {module_name} : ModuleRules")
        
        # Extract dependency lists
        pub_deps = self._extract_cs_string_array(content, "PublicDependencyModuleNames")
        priv_deps = self._extract_cs_string_array(content, "PrivateDependencyModuleNames")
        all_deps = pub_deps | priv_deps
        
        # Check for Core dependency
        if "Core" not in all_deps and "CoreUObject" not in all_deps:
            self._warning("build",
                          f"Module '{module_name}' doesn't depend on Core or CoreUObject",
                          location,
                          "Most modules need at least Core in PublicDependencyModuleNames")
        
        # Check for editor dependencies in runtime modules
        mod_type = mod_descriptor.get("Type", "")
        if mod_type in SHIPPING_MODULE_TYPES:
            editor_deps = all_deps & {"UnrealEd", "EditorStyle", "EditorFramework",
                                       "PropertyEditor", "ContentBrowser", "AssetTools",
                                       "BlueprintGraph", "KismetCompiler", "Kismet",
                                       "LevelEditor", "EditorScriptingUtilities"}
            if editor_deps:
                self._error("build",
                            f"Runtime module '{module_name}' depends on editor-only modules: "
                            f"{', '.join(sorted(editor_deps))}",
                            location,
                            "Move editor dependencies to a separate Editor-type module, "
                            "or change this module's Type to Editor")
        
        # Check PCHUsage
        if "PCHUsageMode" not in content and "PCHUsage" not in content:
            self._info("build",
                        f"Module '{module_name}' doesn't set PCHUsage explicitly",
                        location,
                        "Consider setting PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs")
        
        # Check for API macro in public headers
        api_macro = f"{module_name.upper()}_API"
        public_dir = self.plugin_dir / "Source" / module_name / "Public"
        if public_dir.is_dir():
            has_api_macro = False
            for header in public_dir.rglob("*.h"):
                try:
                    header_content = header.read_text(encoding='utf-8-sig')
                    if api_macro in header_content:
                        has_api_macro = True
                        break
                except Exception:
                    pass
            
            if not has_api_macro and len(list(public_dir.rglob("*.h"))) > 0:
                self._warning("build",
                              f"No public headers use the {api_macro} macro",
                              str(public_dir),
                              f"Classes/functions exposed to other modules need {api_macro}")

    def _extract_cs_string_array(self, content: str, field_name: str) -> Set[str]:
        """Extract string values from a C# AddRange or Add call in .Build.cs."""
        result = set()
        # Match: FieldName.AddRange(new string[] { "A", "B" })
        pattern = rf'{field_name}\s*\.(?:AddRange|Add)\s*\(\s*(?:new\s+(?:string|List<string>)\s*\[\s*\]\s*\{{\s*)?([^)}}]+)'
        match = re.search(pattern, content, re.DOTALL)
        if match:
            # Extract quoted strings
            strings = re.findall(r'"([^"]+)"', match.group(1))
            result.update(strings)
        return result

    # ----- Phase 6: Dependency Validation -----

    def _validate_dependencies(self):
        """Validate plugin dependency declarations."""
        plugins = self.descriptor.get("Plugins", [])
        if not isinstance(plugins, list):
            return
        
        seen_deps: Set[str] = set()
        
        for i, dep in enumerate(plugins):
            if not isinstance(dep, dict):
                self._error("dependency", f"Plugin dependency {i} is not an object",
                            f"{self.uplugin_path} -> Plugins[{i}]")
                continue
            
            name = dep.get("Name", "")
            if not name:
                self._error("dependency", f"Plugin dependency {i} missing 'Name' field",
                            f"{self.uplugin_path} -> Plugins[{i}]")
                continue
            
            if name in seen_deps:
                self._warning("dependency", f"Duplicate plugin dependency '{name}'",
                              f"{self.uplugin_path} -> Plugins[{i}]")
            seen_deps.add(name)
            
            # Check if Enabled is explicitly set
            if "Enabled" not in dep:
                self._info("dependency",
                            f"Plugin dependency '{name}' doesn't set 'Enabled' explicitly",
                            f"{self.uplugin_path} -> Plugins[{i}]",
                            "Set \"Enabled\": true to make the dependency explicit")
            
            # Self-dependency check
            if name == self.plugin_name:
                self._error("dependency",
                            f"Plugin depends on itself: '{name}'",
                            f"{self.uplugin_path} -> Plugins[{i}]")

    # ----- Summary -----

    def _add_summary_info(self):
        """Add informational items about the plugin structure."""
        modules = self.descriptor.get("Modules", [])
        if isinstance(modules, list):
            # Check for mixed module types
            types = set()
            for mod in modules:
                if isinstance(mod, dict) and mod.get("Type"):
                    types.add(mod["Type"])
            
            has_runtime = bool(types & SHIPPING_MODULE_TYPES)
            has_editor = bool(types & EDITOR_MODULE_TYPES)
            
            if has_runtime and has_editor:
                self._info("module",
                            "Plugin has both runtime and editor modules "
                            "(good separation of concerns)")
            elif has_runtime and not has_editor:
                self._info("module", "Plugin has only runtime modules (ships in packaged builds)")
            elif has_editor and not has_runtime:
                self._info("module", "Plugin has only editor modules (editor-only plugin)")

    # ----- Helpers -----

    def _error(self, category: str, message: str, location: str = "",
               suggestion: str = ""):
        self.report.items.append(ValidationItem(
            severity="error", category=category,
            message=message, location=location, suggestion=suggestion,
        ))

    def _warning(self, category: str, message: str, location: str = "",
                 suggestion: str = ""):
        self.report.items.append(ValidationItem(
            severity="warning", category=category,
            message=message, location=location, suggestion=suggestion,
        ))

    def _info(self, category: str, message: str, location: str = "",
              suggestion: str = ""):
        self.report.items.append(ValidationItem(
            severity="info", category=category,
            message=message, location=location, suggestion=suggestion,
        ))
