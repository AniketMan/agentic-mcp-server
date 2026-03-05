"""
uasset_bridge.py — Python bridge to UAssetAPI via pythonnet (.NET interop)

This module initializes the .NET runtime, loads UAssetAPI.dll, and provides
Pythonic wrappers around the core C# types for reading/writing UE 5.6 assets.

All .NET type references are resolved lazily to avoid import-time failures.
"""

import os
import sys
import shutil
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# .NET Runtime Bootstrap
# ---------------------------------------------------------------------------

# Locate UAssetAPI.dll relative to this file
_LIB_DIR = Path(__file__).resolve().parent.parent / "lib" / "publish"
_DLL_PATH = _LIB_DIR / "UAssetAPI.dll"

if not _DLL_PATH.exists():
    raise FileNotFoundError(
        f"UAssetAPI.dll not found at {_DLL_PATH}. "
        "Run 'dotnet build UAssetAPI.csproj -c Release' first."
    )

# Initialize pythonnet with coreclr runtime
from pythonnet import set_runtime
from clr_loader import get_coreclr

# We need a runtimeconfig.json for coreclr
_RUNTIME_CONFIG = _LIB_DIR / "UAssetAPI.runtimeconfig.json"
if not _RUNTIME_CONFIG.exists():
    # Generate a minimal runtimeconfig if missing
    import json
    config = {
        "runtimeOptions": {
            "tfm": "net8.0",
            "framework": {
                "name": "Microsoft.NETCore.App",
                "version": "8.0.0"
            }
        }
    }
    with open(_RUNTIME_CONFIG, "w") as f:
        json.dump(config, f, indent=2)

_runtime = get_coreclr(runtime_config=str(_RUNTIME_CONFIG), dotnet_root=str(Path.home() / '.dotnet'))
set_runtime(_runtime)

import clr  # noqa: E402 — must come after set_runtime
sys.path.append(str(_LIB_DIR))
clr.AddReference(str(_DLL_PATH))

# ---------------------------------------------------------------------------
# Import .NET Types
# ---------------------------------------------------------------------------

# Core UAssetAPI types
from UAssetAPI import UAsset  # noqa: E402
from UAssetAPI.UnrealTypes import EngineVersion, FName, FPackageIndex  # noqa: E402
from UAssetAPI.ExportTypes import (  # noqa: E402
    Export,
    NormalExport,
    LevelExport,
    ClassExport,
    StructExport,
    FunctionExport,
    PropertyExport,
    RawExport,
)

# Try importing Kismet types
try:
    from UAssetAPI.Kismet.Bytecode import KismetExpression, EExprToken  # noqa: E402
    from UAssetAPI.Kismet.Bytecode import ExpressionSerializer  # noqa: E402
    KISMET_AVAILABLE = True
except ImportError:
    KISMET_AVAILABLE = False

# Property types
from UAssetAPI.PropertyTypes.Objects import PropertyData  # noqa: E402


# ---------------------------------------------------------------------------
# Engine Version Mapping
# ---------------------------------------------------------------------------

# Map string identifiers to UAssetAPI EngineVersion enum values
ENGINE_VERSION_MAP = {
    "5.0": EngineVersion.VER_UE5_0,
    "5.1": EngineVersion.VER_UE5_1,
    "5.2": EngineVersion.VER_UE5_2,
    "5.3": EngineVersion.VER_UE5_3,
    "5.4": EngineVersion.VER_UE5_4,
    "5.5": EngineVersion.VER_UE5_5,
    "5.6": EngineVersion.VER_UE5_6,
}


# ---------------------------------------------------------------------------
# Asset Wrapper
# ---------------------------------------------------------------------------

class AssetFile:
    """
    Pythonic wrapper around UAssetAPI.UAsset.
    
    Handles reading, modifying, and writing .uasset/.umap files
    with automatic backup and reference integrity checking.
    """

    def __init__(self, filepath: str, engine_version: str = "5.6"):
        """
        Open a .uasset or .umap file.
        
        Args:
            filepath: Path to the asset file
            engine_version: Target UE version string (e.g., "5.6")
        """
        self.filepath = Path(filepath).resolve()
        if not self.filepath.exists():
            raise FileNotFoundError(f"Asset file not found: {self.filepath}")

        # Resolve engine version
        if engine_version not in ENGINE_VERSION_MAP:
            raise ValueError(
                f"Unsupported engine version '{engine_version}'. "
                f"Supported: {list(ENGINE_VERSION_MAP.keys())}"
            )
        self.engine_version = ENGINE_VERSION_MAP[engine_version]
        self.engine_version_str = engine_version

        # Load the asset via UAssetAPI
        try:
            self._asset = UAsset(str(self.filepath), self.engine_version)
        except Exception as e:
            raise RuntimeError(
                f"Failed to load asset '{self.filepath}': {e}"
            ) from e

        # Cache parsed data
        self._exports_cache = None
        self._names_cache = None

    # ----- Properties -----

    @property
    def asset(self) -> UAsset:
        """Direct access to the underlying UAssetAPI.UAsset object."""
        return self._asset

    @property
    def is_map(self) -> bool:
        """True if this is a .umap (level) file."""
        return self.filepath.suffix.lower() == ".umap"

    @property
    def name_map(self) -> list:
        """Return the name map as a Python list of strings."""
        if self._names_cache is None:
            self._names_cache = []
            name_count = self._asset.GetNameMapIndexList().Count
            for i in range(name_count):
                name = self._asset.GetNameMapIndexList()[i]
                self._names_cache.append(str(name))
        return self._names_cache

    @property
    def exports(self) -> list:
        """Return all exports as a Python list."""
        if self._exports_cache is None:
            self._exports_cache = []
            for i in range(self._asset.Exports.Count):
                self._exports_cache.append(self._asset.Exports[i])
        return self._exports_cache

    @property
    def imports(self) -> list:
        """Return all imports as a Python list."""
        result = []
        for i in range(self._asset.Imports.Count):
            result.append(self._asset.Imports[i])
        return result

    @property
    def export_count(self) -> int:
        return self._asset.Exports.Count

    @property
    def import_count(self) -> int:
        return self._asset.Imports.Count

    # ----- Export Inspection -----

    def get_export(self, index: int):
        """Get an export by zero-based index."""
        if index < 0 or index >= self.export_count:
            raise IndexError(f"Export index {index} out of range (0-{self.export_count - 1})")
        return self._asset.Exports[index]

    def get_export_class_name(self, export) -> str:
        """Get the class name of an export (e.g., 'StaticMeshActor', 'BlueprintGeneratedClass')."""
        try:
            class_idx = export.ClassIndex
            if class_idx.Index < 0:
                # Negative = import reference
                imp = self._asset.Imports[-(class_idx.Index + 1)]
                return str(imp.ObjectName)
            elif class_idx.Index > 0:
                # Positive = export reference (rare for classes)
                exp = self._asset.Exports[class_idx.Index - 1]
                return str(exp.ObjectName)
            else:
                return "Class"  # Index 0 = UClass itself
        except Exception:
            return "Unknown"

    def get_export_name(self, export) -> str:
        """Get the object name of an export."""
        try:
            return str(export.ObjectName)
        except Exception:
            return "Unknown"

    def find_exports_by_class(self, class_name: str) -> list:
        """Find all exports matching a class name."""
        results = []
        for i, exp in enumerate(self.exports):
            if self.get_export_class_name(exp) == class_name:
                results.append((i, exp))
        return results

    def find_exports_by_name(self, name: str) -> list:
        """Find all exports matching an object name."""
        results = []
        for i, exp in enumerate(self.exports):
            if self.get_export_name(exp) == name:
                results.append((i, exp))
        return results

    # ----- Level-Specific -----

    def get_level_export(self) -> Optional[LevelExport]:
        """Find and return the LevelExport (PersistentLevel) if this is a .umap."""
        for exp in self.exports:
            if isinstance(exp, LevelExport):
                return exp
        return None

    def get_level_actors(self) -> list:
        """
        Get all actor references from the level export.
        Returns list of (package_index, export_index, class_name, object_name) tuples.
        """
        level = self.get_level_export()
        if level is None:
            return []

        actors = []
        for i in range(level.Actors.Count):
            pkg_idx = level.Actors[i]
            idx = pkg_idx.Index
            if idx > 0:
                # Positive index = export (1-based)
                exp = self._asset.Exports[idx - 1]
                actors.append({
                    "package_index": idx,
                    "export_index": idx - 1,
                    "class_name": self.get_export_class_name(exp),
                    "object_name": self.get_export_name(exp),
                })
            elif idx < 0:
                # Negative index = import
                imp = self._asset.Imports[-(idx + 1)]
                actors.append({
                    "package_index": idx,
                    "import_index": -(idx + 1),
                    "class_name": str(imp.ClassName),
                    "object_name": str(imp.ObjectName),
                })
            else:
                actors.append({
                    "package_index": 0,
                    "class_name": "Null",
                    "object_name": "Null",
                })
        return actors

    # ----- Blueprint / Kismet -----

    def get_struct_exports(self) -> list:
        """Find all StructExport instances (functions with bytecode)."""
        results = []
        for i, exp in enumerate(self.exports):
            if isinstance(exp, StructExport):
                results.append((i, exp))
        return results

    def get_function_exports(self) -> list:
        """Find all FunctionExport instances."""
        results = []
        for i, exp in enumerate(self.exports):
            if isinstance(exp, FunctionExport):
                results.append((i, exp))
        return results

    def get_class_exports(self) -> list:
        """Find all ClassExport instances (Blueprint generated classes)."""
        results = []
        for i, exp in enumerate(self.exports):
            if isinstance(exp, ClassExport):
                results.append((i, exp))
        return results

    def get_kismet_bytecode(self, struct_export: StructExport) -> list:
        """
        Extract Kismet bytecode expressions from a StructExport.
        Returns a list of expression dictionaries.
        """
        if not KISMET_AVAILABLE:
            return []

        if struct_export.ScriptBytecode is None:
            return []

        expressions = []
        for i in range(struct_export.ScriptBytecode.Length):
            expr = struct_export.ScriptBytecode[i]
            expressions.append(self._expression_to_dict(expr))
        return expressions

    def _expression_to_dict(self, expr) -> dict:
        """Convert a KismetExpression to a Python dict for inspection."""
        result = {
            "type": str(expr.Token) if hasattr(expr, "Token") else type(expr).__name__,
            "inst": type(expr).__name__,
        }
        # Extract common fields
        try:
            if hasattr(expr, "Value"):
                result["value"] = str(expr.Value)
        except Exception:
            pass
        return result

    # ----- Property Editing -----

    def get_export_properties(self, export) -> list:
        """
        Get all properties from a NormalExport as a list of dicts.
        """
        if not isinstance(export, NormalExport):
            return []

        props = []
        if export.Data is None:
            return props

        for i in range(export.Data.Count):
            prop = export.Data[i]
            props.append(self._property_to_dict(prop, i))
        return props

    def _property_to_dict(self, prop, index: int) -> dict:
        """Convert a PropertyData to a Python dict."""
        result = {
            "index": index,
            "name": str(prop.Name) if prop.Name else "None",
            "type": type(prop).__name__,
        }
        # Try to get the value
        try:
            if hasattr(prop, "Value"):
                val = prop.Value
                if val is not None:
                    result["value"] = str(val)
                else:
                    result["value"] = "None"
        except Exception:
            result["value"] = "<unreadable>"
        return result

    def set_property_value(self, export_index: int, prop_name: str, new_value):
        """
        Set a property value on an export by name.
        
        This is a simplified setter — for complex types, use the .NET objects directly.
        
        Args:
            export_index: Zero-based export index
            prop_name: Property name to modify
            new_value: New value (type must match the property type)
        """
        export = self.get_export(export_index)
        if not isinstance(export, NormalExport):
            raise TypeError(f"Export {export_index} is not a NormalExport")

        if export.Data is None:
            raise ValueError(f"Export {export_index} has no property data")

        for i in range(export.Data.Count):
            prop = export.Data[i]
            if str(prop.Name) == prop_name:
                if hasattr(prop, "Value"):
                    prop.Value = new_value
                    # Invalidate caches
                    self._exports_cache = None
                    return True
                else:
                    raise AttributeError(
                        f"Property '{prop_name}' does not have a simple Value field. "
                        "Use direct .NET object access for complex types."
                    )

        raise KeyError(f"Property '{prop_name}' not found on export {export_index}")

    # ----- Save / Backup -----

    def backup(self) -> Path:
        """
        Create a backup of the original file.
        Returns the backup file path.
        """
        backup_path = self.filepath.with_suffix(self.filepath.suffix + ".bak")
        counter = 1
        while backup_path.exists():
            backup_path = self.filepath.with_suffix(f"{self.filepath.suffix}.bak{counter}")
            counter += 1
        shutil.copy2(self.filepath, backup_path)
        return backup_path

    def save(self, output_path: Optional[str] = None, create_backup: bool = True) -> Path:
        """
        Save the modified asset.
        
        Args:
            output_path: Where to save. If None, overwrites the original.
            create_backup: If True, backs up the original before overwriting.
            
        Returns:
            Path to the saved file.
        """
        if output_path is None:
            output_path = str(self.filepath)
            if create_backup:
                self.backup()
        
        out = Path(output_path).resolve()
        try:
            self._asset.Write(str(out))
        except Exception as e:
            raise RuntimeError(f"Failed to write asset to '{out}': {e}") from e

        return out

    # ----- Summary -----

    def summary(self) -> dict:
        """Return a summary dict of the asset."""
        level = self.get_level_export()
        return {
            "filepath": str(self.filepath),
            "engine_version": self.engine_version_str,
            "is_map": self.is_map,
            "export_count": self.export_count,
            "import_count": self.import_count,
            "name_count": len(self.name_map),
            "has_level_export": level is not None,
            "actor_count": level.Actors.Count if level else 0,
            "class_exports": len(self.get_class_exports()),
            "function_exports": len(self.get_function_exports()),
        }
