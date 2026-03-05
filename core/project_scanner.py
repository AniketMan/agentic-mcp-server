"""
project_scanner.py - Full UE5.6 project scanning and cross-asset dependency resolution.

When given a full UE project directory, this module:
1. Parses the .uproject file for engine version, plugins, modules, target platforms
2. Scans all .uplugin files and validates them
3. Builds a complete asset registry from Content/ directory
4. Resolves cross-asset dependencies (which maps reference which Blueprints, etc.)
5. Maps level streaming relationships (persistent level -> sub-levels)
6. Identifies orphaned assets (not referenced by anything)

This is the "full project mode" that lights up when you have the complete project
instead of just individual .umap files.

Architecture:
    ProjectScanner
        -> parse_uproject()        -> ProjectInfo
        -> scan_plugins()          -> list[PluginInfo]
        -> scan_assets()           -> AssetRegistry
        -> resolve_dependencies()  -> DependencyGraph
        -> scan_levels()           -> list[LevelInfo] with streaming relationships
"""

import json
import os
import re
import logging
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional

logger = logging.getLogger(__name__)


# =============================================================================
# DATA CLASSES
# =============================================================================

@dataclass
class ModuleInfo:
    """Represents a C++ module in the project."""
    name: str
    host_type: str = ""           # Runtime, Editor, DeveloperTool, etc.
    loading_phase: str = ""       # Default, PreDefault, PostConfigInit, etc.
    has_build_cs: bool = False
    source_file_count: int = 0
    header_file_count: int = 0
    dependencies: list = field(default_factory=list)


@dataclass
class PluginRef:
    """A plugin reference in .uproject or .uplugin."""
    name: str
    enabled: bool = True
    optional: bool = False
    platform_allow_list: list = field(default_factory=list)


@dataclass
class ProjectInfo:
    """Parsed .uproject file data."""
    project_name: str = ""
    project_path: str = ""
    engine_version: str = ""
    engine_association: str = ""
    description: str = ""
    category: str = ""
    modules: list = field(default_factory=list)       # list[ModuleInfo]
    plugins: list = field(default_factory=list)        # list[PluginRef]
    target_platforms: list = field(default_factory=list)
    raw_json: dict = field(default_factory=dict)


@dataclass
class AssetInfo:
    """Represents a single asset in the Content directory."""
    name: str
    path: str                     # Relative path from project root
    asset_type: str = ""          # .uasset, .umap, .uexp, etc.
    size_bytes: int = 0
    has_uexp: bool = False        # Whether a companion .uexp file exists
    references: list = field(default_factory=list)     # Assets this one references
    referenced_by: list = field(default_factory=list)  # Assets that reference this one


@dataclass
class LevelInfo:
    """Represents a level/map in the project."""
    name: str
    path: str
    size_bytes: int = 0
    has_logic: bool = False       # Whether it has a Level Script Blueprint
    is_persistent: bool = False   # Whether it's a persistent level
    sub_levels: list = field(default_factory=list)     # Streaming sub-levels
    referenced_blueprints: list = field(default_factory=list)
    referenced_assets: list = field(default_factory=list)
    actor_count: int = 0
    function_count: int = 0


@dataclass
class AssetRegistry:
    """Complete asset registry for the project."""
    total_assets: int = 0
    total_maps: int = 0
    total_blueprints: int = 0
    total_materials: int = 0
    total_textures: int = 0
    total_meshes: int = 0
    total_sounds: int = 0
    total_other: int = 0
    total_size_bytes: int = 0
    assets: dict = field(default_factory=dict)  # path -> AssetInfo
    assets_by_type: dict = field(default_factory=dict)  # extension -> list[AssetInfo]


# =============================================================================
# PROJECT SCANNER
# =============================================================================

class ProjectScanner:
    """
    Scans a full UE5.6 project directory and builds a comprehensive
    understanding of the project structure, assets, and dependencies.
    """

    def __init__(self, project_root: str):
        """
        Initialize the scanner with the project root directory.

        Args:
            project_root: Absolute path to the UE project root
                         (the directory containing the .uproject file)
        """
        self.project_root = Path(project_root)
        self.project_info: Optional[ProjectInfo] = None
        self.asset_registry: Optional[AssetRegistry] = None
        self.levels: list = []
        self.plugins: list = []

        logger.info(f"ProjectScanner initialized: {self.project_root}")

        # Validate the project root
        if not self.project_root.exists():
            raise FileNotFoundError(f"Project root not found: {self.project_root}")

        # Find the .uproject file
        uproject_files = list(self.project_root.glob("*.uproject"))
        if not uproject_files:
            raise FileNotFoundError(f"No .uproject file found in: {self.project_root}")

        self.uproject_path = uproject_files[0]
        logger.info(f"Found .uproject: {self.uproject_path.name}")

    # =========================================================================
    # UPROJECT PARSING
    # =========================================================================

    def parse_uproject(self) -> ProjectInfo:
        """
        Parse the .uproject file and extract all project metadata.

        Returns:
            ProjectInfo with all parsed data

        Raises:
            json.JSONDecodeError: If the .uproject file is not valid JSON
        """
        logger.info(f"Parsing .uproject: {self.uproject_path}")

        with open(self.uproject_path, 'r', encoding='utf-8-sig') as f:
            data = json.load(f)

        info = ProjectInfo()
        info.project_name = self.uproject_path.stem
        info.project_path = str(self.project_root)
        info.raw_json = data

        # Engine version / association
        info.engine_association = data.get("EngineAssociation", "")
        info.description = data.get("Description", "")
        info.category = data.get("Category", "")

        # Determine engine version from association
        # Format: "5.6" or a GUID for source builds
        ea = info.engine_association
        if re.match(r'^\d+\.\d+', ea):
            info.engine_version = ea
        elif re.match(r'^[{]?[0-9a-fA-F-]+[}]?$', ea):
            info.engine_version = "Source Build"
        else:
            info.engine_version = ea or "Unknown"

        # Modules
        for mod_data in data.get("Modules", []):
            mod = ModuleInfo(
                name=mod_data.get("Name", ""),
                host_type=mod_data.get("Type", ""),
                loading_phase=mod_data.get("LoadingPhase", "Default"),
            )
            # Check if the module source exists
            mod_source_dir = self.project_root / "Source" / mod.name
            if mod_source_dir.exists():
                mod.has_build_cs = (mod_source_dir / f"{mod.name}.Build.cs").exists()
                mod.source_file_count = len(list(mod_source_dir.rglob("*.cpp")))
                mod.header_file_count = len(list(mod_source_dir.rglob("*.h")))

                # Parse Build.cs for dependencies if it exists
                build_cs = mod_source_dir / f"{mod.name}.Build.cs"
                if build_cs.exists():
                    mod.dependencies = self._parse_build_cs_dependencies(build_cs)

            info.modules.append(mod)
            logger.info(f"  Module: {mod.name} ({mod.host_type}, {mod.loading_phase})")

        # Plugins
        for plug_data in data.get("Plugins", []):
            plug = PluginRef(
                name=plug_data.get("Name", ""),
                enabled=plug_data.get("Enabled", True),
                optional=plug_data.get("Optional", False),
                platform_allow_list=plug_data.get("PlatformAllowList", []),
            )
            info.plugins.append(plug)

        # Target platforms
        info.target_platforms = data.get("TargetPlatforms", [])

        self.project_info = info
        logger.info(f"Project: {info.project_name}, Engine: {info.engine_version}, "
                     f"Modules: {len(info.modules)}, Plugins: {len(info.plugins)}")

        return info

    def _parse_build_cs_dependencies(self, build_cs_path: Path) -> list:
        """
        Extract module dependencies from a .Build.cs file.
        Looks for AddRange calls on PublicDependencyModuleNames and
        PrivateDependencyModuleNames.

        Args:
            build_cs_path: Path to the .Build.cs file

        Returns:
            List of dependency module names
        """
        deps = []
        try:
            content = build_cs_path.read_text(encoding='utf-8-sig')
            # Match patterns like: "Core", "CoreUObject", "Engine"
            matches = re.findall(r'"([A-Za-z0-9_]+)"', content)
            # Filter to likely module names (exclude common non-module strings)
            exclude = {'true', 'false', 'None', 'null', 'PCHUsage', 'UseExplicitOrSharedPCHs',
                       'Win64', 'Linux', 'Mac', 'Editor', 'Runtime', 'Default'}
            deps = [m for m in matches if m not in exclude and len(m) > 2]
        except Exception as e:
            logger.warning(f"Could not parse Build.cs: {build_cs_path}: {e}")

        return deps

    # =========================================================================
    # PLUGIN SCANNING
    # =========================================================================

    def scan_plugins(self) -> list:
        """
        Scan the Plugins/ directory for all .uplugin files.
        Uses the plugin_validator module for detailed validation.

        Returns:
            List of plugin scan results (dicts with name, path, validation)
        """
        logger.info("Scanning plugins directory...")

        plugins_dir = self.project_root / "Plugins"
        if not plugins_dir.exists():
            logger.info("No Plugins/ directory found")
            return []

        results = []
        uplugin_files = list(plugins_dir.rglob("*.uplugin"))

        for uplugin_path in uplugin_files:
            try:
                with open(uplugin_path, 'r', encoding='utf-8-sig') as f:
                    plugin_data = json.load(f)

                result = {
                    "name": plugin_data.get("FriendlyName", uplugin_path.stem),
                    "internal_name": uplugin_path.stem,
                    "path": str(uplugin_path.relative_to(self.project_root)),
                    "version": plugin_data.get("VersionName", ""),
                    "description": plugin_data.get("Description", ""),
                    "category": plugin_data.get("Category", ""),
                    "created_by": plugin_data.get("CreatedBy", ""),
                    "is_beta": plugin_data.get("IsBetaVersion", False),
                    "is_experimental": plugin_data.get("IsExperimentalVersion", False),
                    "can_contain_content": plugin_data.get("CanContainContent", False),
                    "modules": [],
                    "plugin_dependencies": [],
                }

                # Modules
                for mod in plugin_data.get("Modules", []):
                    result["modules"].append({
                        "name": mod.get("Name", ""),
                        "type": mod.get("Type", ""),
                        "loading_phase": mod.get("LoadingPhase", "Default"),
                    })

                # Plugin dependencies
                for dep in plugin_data.get("Plugins", []):
                    result["plugin_dependencies"].append({
                        "name": dep.get("Name", ""),
                        "enabled": dep.get("Enabled", True),
                    })

                results.append(result)
                logger.info(f"  Plugin: {result['name']} v{result['version']} "
                            f"({len(result['modules'])} modules)")

            except Exception as e:
                logger.error(f"Error scanning plugin {uplugin_path}: {e}")
                results.append({
                    "name": uplugin_path.stem,
                    "path": str(uplugin_path.relative_to(self.project_root)),
                    "error": str(e),
                })

        self.plugins = results
        logger.info(f"Found {len(results)} plugins")
        return results

    # =========================================================================
    # ASSET SCANNING
    # =========================================================================

    def scan_assets(self) -> AssetRegistry:
        """
        Scan the Content/ directory and build a complete asset registry.
        Categorizes assets by type and calculates sizes.

        Returns:
            AssetRegistry with all discovered assets
        """
        logger.info("Scanning Content/ directory...")

        content_dir = self.project_root / "Content"
        if not content_dir.exists():
            logger.warning("No Content/ directory found")
            return AssetRegistry()

        registry = AssetRegistry()

        # Asset type classification based on directory conventions and extensions
        type_dirs = {
            "Blueprints": "blueprint",
            "Materials": "material",
            "Textures": "texture",
            "Meshes": "mesh",
            "StaticMeshes": "mesh",
            "SkeletalMeshes": "mesh",
            "Audio": "sound",
            "Sounds": "sound",
            "Maps": "map",
            "Levels": "map",
            "Animations": "animation",
            "Sequences": "sequence",
            "Particles": "particle",
            "Niagara": "particle",
            "UI": "ui",
            "Widgets": "ui",
        }

        # Scan all .uasset and .umap files
        for ext in ["*.uasset", "*.umap"]:
            for asset_path in content_dir.rglob(ext):
                rel_path = str(asset_path.relative_to(self.project_root))
                size = asset_path.stat().st_size

                # Check for companion .uexp
                uexp_path = asset_path.with_suffix('.uexp')
                has_uexp = uexp_path.exists()
                if has_uexp:
                    size += uexp_path.stat().st_size

                # Determine asset type from directory or extension
                asset_type = "other"
                if asset_path.suffix == '.umap':
                    asset_type = "map"
                else:
                    # Check parent directories for type hints
                    parts = asset_path.relative_to(content_dir).parts
                    for part in parts:
                        for dir_name, atype in type_dirs.items():
                            if dir_name.lower() in part.lower():
                                asset_type = atype
                                break

                asset_info = AssetInfo(
                    name=asset_path.stem,
                    path=rel_path,
                    asset_type=asset_type,
                    size_bytes=size,
                    has_uexp=has_uexp,
                )

                registry.assets[rel_path] = asset_info

                # Group by type
                if asset_type not in registry.assets_by_type:
                    registry.assets_by_type[asset_type] = []
                registry.assets_by_type[asset_type].append(asset_info)

                registry.total_size_bytes += size

        # Count totals
        registry.total_assets = len(registry.assets)
        registry.total_maps = len(registry.assets_by_type.get("map", []))
        registry.total_blueprints = len(registry.assets_by_type.get("blueprint", []))
        registry.total_materials = len(registry.assets_by_type.get("material", []))
        registry.total_textures = len(registry.assets_by_type.get("texture", []))
        registry.total_meshes = len(registry.assets_by_type.get("mesh", []))
        registry.total_sounds = len(registry.assets_by_type.get("sound", []))
        registry.total_other = (registry.total_assets - registry.total_maps -
                                registry.total_blueprints - registry.total_materials -
                                registry.total_textures - registry.total_meshes -
                                registry.total_sounds)

        self.asset_registry = registry
        logger.info(f"Asset Registry: {registry.total_assets} assets, "
                     f"{registry.total_maps} maps, {registry.total_blueprints} BPs, "
                     f"{registry.total_size_bytes / (1024*1024):.1f} MB total")

        return registry

    # =========================================================================
    # LEVEL SCANNING
    # =========================================================================

    def scan_levels(self, bridge=None) -> list:
        """
        Scan all .umap files and extract level information.
        If a UAssetBridge is provided, uses it to parse the binary data
        for detailed actor/function/logic information.

        Args:
            bridge: Optional UAssetBridge instance for deep inspection

        Returns:
            List of LevelInfo objects
        """
        logger.info("Scanning levels...")

        content_dir = self.project_root / "Content"
        if not content_dir.exists():
            return []

        levels = []
        map_files = list(content_dir.rglob("*.umap"))

        for map_path in map_files:
            rel_path = str(map_path.relative_to(self.project_root))

            level = LevelInfo(
                name=map_path.stem,
                path=rel_path,
                size_bytes=map_path.stat().st_size,
            )

            # Check for companion .uexp
            uexp_path = map_path.with_suffix('.uexp')
            if uexp_path.exists():
                level.size_bytes += uexp_path.stat().st_size

            # Determine if this is a persistent level or sub-level
            # Convention: persistent levels are often named without SL_ prefix
            # or are in a Maps/Persistent/ directory
            name_lower = level.name.lower()
            if 'persistent' in name_lower or ('sl_' not in name_lower and 'sub' not in name_lower):
                level.is_persistent = True

            # If bridge is available, do deep inspection
            if bridge:
                try:
                    asset = bridge.load_asset(str(map_path))
                    if asset:
                        # Get actor count
                        actors = bridge.get_actors(asset)
                        level.actor_count = len(actors) if actors else 0

                        # Check for Level Script Blueprint (has_logic)
                        exports = bridge.get_exports(asset)
                        for exp in exports:
                            exp_class = bridge.get_export_class(exp)
                            if 'ClassExport' in str(type(exp).__name__) or '_C' in str(bridge.get_export_name(exp)):
                                level.has_logic = True
                            if 'FunctionExport' in str(type(exp).__name__):
                                level.function_count += 1

                        # Get referenced Blueprints from imports
                        imports = bridge.get_imports(asset)
                        for imp in imports:
                            imp_name = str(imp.ObjectName)
                            if imp_name.endswith('_C') or 'BP_' in imp_name:
                                level.referenced_blueprints.append(imp_name)

                        bridge.close_asset(asset)

                except Exception as e:
                    logger.warning(f"Could not deep-inspect level {level.name}: {e}")

            levels.append(level)
            logger.info(f"  Level: {level.name} ({level.size_bytes / 1024:.0f} KB, "
                         f"actors={level.actor_count}, logic={level.has_logic})")

        self.levels = levels
        logger.info(f"Found {len(levels)} levels")
        return levels

    # =========================================================================
    # DEPENDENCY RESOLUTION
    # =========================================================================

    def resolve_dependencies(self, bridge=None) -> dict:
        """
        Build a cross-asset dependency graph by parsing import tables.
        This shows which assets reference which other assets.

        Args:
            bridge: Optional UAssetBridge for parsing import tables

        Returns:
            Dict with dependency graph data:
            {
                "nodes": [{name, path, type, size}],
                "edges": [{from, to, ref_type}],
                "orphans": [paths with no incoming references],
                "most_referenced": [paths sorted by reference count]
            }
        """
        logger.info("Resolving cross-asset dependencies...")

        if not self.asset_registry:
            self.scan_assets()

        graph = {
            "nodes": [],
            "edges": [],
            "orphans": [],
            "most_referenced": [],
        }

        # Build nodes from asset registry
        ref_counts = {}
        for path, asset in self.asset_registry.assets.items():
            graph["nodes"].append({
                "name": asset.name,
                "path": path,
                "type": asset.asset_type,
                "size": asset.size_bytes,
            })
            ref_counts[path] = 0

        # If bridge is available, parse imports for each asset to build edges
        if bridge:
            for path, asset_info in self.asset_registry.assets.items():
                full_path = str(self.project_root / path)
                try:
                    asset = bridge.load_asset(full_path)
                    if not asset:
                        continue

                    imports = bridge.get_imports(asset)
                    for imp in imports:
                        # Try to resolve import to a project asset
                        imp_path = str(imp.ObjectName)
                        # Match against known assets
                        for other_path, other_asset in self.asset_registry.assets.items():
                            if other_asset.name in imp_path:
                                graph["edges"].append({
                                    "from": path,
                                    "to": other_path,
                                    "ref_type": "import",
                                })
                                ref_counts[other_path] = ref_counts.get(other_path, 0) + 1
                                asset_info.references.append(other_path)
                                other_asset.referenced_by.append(path)
                                break

                    bridge.close_asset(asset)

                except Exception as e:
                    logger.debug(f"Could not parse imports for {path}: {e}")

        # Find orphans (assets with no incoming references, excluding maps)
        for path, count in ref_counts.items():
            asset = self.asset_registry.assets.get(path)
            if asset and count == 0 and asset.asset_type != "map":
                graph["orphans"].append(path)

        # Sort by reference count
        graph["most_referenced"] = sorted(
            ref_counts.items(), key=lambda x: x[1], reverse=True
        )[:20]  # Top 20

        logger.info(f"Dependency graph: {len(graph['nodes'])} nodes, "
                     f"{len(graph['edges'])} edges, {len(graph['orphans'])} orphans")

        return graph

    # =========================================================================
    # SOURCE CODE SCANNING
    # =========================================================================

    def scan_source(self) -> dict:
        """
        Scan the Source/ directory for C++ modules, file counts, and structure.

        Returns:
            Dict with source code statistics and structure
        """
        logger.info("Scanning Source/ directory...")

        source_dir = self.project_root / "Source"
        if not source_dir.exists():
            logger.info("No Source/ directory found")
            return {"exists": False}

        result = {
            "exists": True,
            "modules": [],
            "total_cpp_files": 0,
            "total_h_files": 0,
            "total_lines": 0,
            "build_targets": [],
        }

        # Count files
        cpp_files = list(source_dir.rglob("*.cpp"))
        h_files = list(source_dir.rglob("*.h"))
        result["total_cpp_files"] = len(cpp_files)
        result["total_h_files"] = len(h_files)

        # Count total lines (approximate)
        total_lines = 0
        for f in cpp_files + h_files:
            try:
                total_lines += sum(1 for _ in open(f, 'r', encoding='utf-8-sig', errors='ignore'))
            except Exception:
                pass
        result["total_lines"] = total_lines

        # Find .Target.cs files (build targets)
        for target_file in source_dir.rglob("*.Target.cs"):
            result["build_targets"].append(target_file.stem.replace(".Target", ""))

        # Module directories (each with a .Build.cs)
        for build_cs in source_dir.rglob("*.Build.cs"):
            mod_dir = build_cs.parent
            mod_name = build_cs.stem.replace(".Build", "")
            mod_cpps = len(list(mod_dir.rglob("*.cpp")))
            mod_hs = len(list(mod_dir.rglob("*.h")))

            result["modules"].append({
                "name": mod_name,
                "path": str(mod_dir.relative_to(self.project_root)),
                "cpp_files": mod_cpps,
                "h_files": mod_hs,
                "dependencies": self._parse_build_cs_dependencies(build_cs),
            })

        logger.info(f"Source: {result['total_cpp_files']} .cpp, {result['total_h_files']} .h, "
                     f"{result['total_lines']} lines, {len(result['modules'])} modules")

        return result

    # =========================================================================
    # CONFIG SCANNING
    # =========================================================================

    def scan_config(self) -> dict:
        """
        Scan the Config/ directory for .ini files and extract key settings.

        Returns:
            Dict with config file list and key settings
        """
        logger.info("Scanning Config/ directory...")

        config_dir = self.project_root / "Config"
        if not config_dir.exists():
            return {"exists": False}

        result = {
            "exists": True,
            "files": [],
            "key_settings": {},
        }

        for ini_file in config_dir.rglob("*.ini"):
            rel_path = str(ini_file.relative_to(self.project_root))
            size = ini_file.stat().st_size
            result["files"].append({
                "name": ini_file.name,
                "path": rel_path,
                "size": size,
            })

            # Extract key settings from DefaultEngine.ini and DefaultGame.ini
            if ini_file.name in ("DefaultEngine.ini", "DefaultGame.ini"):
                try:
                    content = ini_file.read_text(encoding='utf-8-sig', errors='ignore')
                    # Extract section headers and key values
                    current_section = ""
                    for line in content.split('\n'):
                        line = line.strip()
                        if line.startswith('[') and line.endswith(']'):
                            current_section = line[1:-1]
                        elif '=' in line and not line.startswith(';'):
                            key, _, value = line.partition('=')
                            full_key = f"{current_section}.{key.strip()}"
                            result["key_settings"][full_key] = value.strip()
                except Exception as e:
                    logger.warning(f"Could not parse {ini_file.name}: {e}")

        logger.info(f"Config: {len(result['files'])} .ini files, "
                     f"{len(result['key_settings'])} key settings")

        return result

    # =========================================================================
    # FULL SCAN
    # =========================================================================

    def full_scan(self, bridge=None) -> dict:
        """
        Run all scanners and return a comprehensive project report.

        Args:
            bridge: Optional UAssetBridge for deep asset inspection

        Returns:
            Dict with complete project analysis
        """
        logger.info(f"=== FULL PROJECT SCAN: {self.project_root} ===")

        report = {
            "project": self.parse_uproject().__dict__,
            "plugins": self.scan_plugins(),
            "assets": None,
            "levels": None,
            "source": self.scan_source(),
            "config": self.scan_config(),
            "dependencies": None,
        }

        # Asset scan
        registry = self.scan_assets()
        report["assets"] = {
            "total": registry.total_assets,
            "maps": registry.total_maps,
            "blueprints": registry.total_blueprints,
            "materials": registry.total_materials,
            "textures": registry.total_textures,
            "meshes": registry.total_meshes,
            "sounds": registry.total_sounds,
            "other": registry.total_other,
            "total_size_mb": round(registry.total_size_bytes / (1024 * 1024), 1),
            "by_type": {k: len(v) for k, v in registry.assets_by_type.items()},
        }

        # Level scan
        levels = self.scan_levels(bridge)
        report["levels"] = [
            {
                "name": l.name,
                "path": l.path,
                "size_kb": round(l.size_bytes / 1024, 1),
                "has_logic": l.has_logic,
                "is_persistent": l.is_persistent,
                "actor_count": l.actor_count,
                "function_count": l.function_count,
                "referenced_blueprints": l.referenced_blueprints,
            }
            for l in levels
        ]

        # Dependency resolution (only with bridge)
        if bridge:
            report["dependencies"] = self.resolve_dependencies(bridge)

        logger.info("=== FULL SCAN COMPLETE ===")
        return report

    # =========================================================================
    # SERIALIZATION
    # =========================================================================

    def to_json(self) -> str:
        """Serialize the current scan state to JSON."""
        data = {
            "project": self.project_info.__dict__ if self.project_info else None,
            "plugins": self.plugins,
            "levels": [l.__dict__ for l in self.levels],
        }
        if self.asset_registry:
            data["asset_registry"] = {
                "total_assets": self.asset_registry.total_assets,
                "total_maps": self.asset_registry.total_maps,
                "total_size_bytes": self.asset_registry.total_size_bytes,
            }
        return json.dumps(data, indent=2, default=str)
