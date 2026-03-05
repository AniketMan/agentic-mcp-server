"""
uasset_bridge.py -- Python bridge to UAssetAPI via pythonnet (.NET interop)

This module initializes the .NET runtime, loads UAssetAPI.dll, and provides
Pythonic wrappers around the core C# types for reading AND writing UE 5.6 assets.

All .NET type references are resolved lazily to avoid import-time failures.

WRITE OPERATIONS:
    add_import()            -- add a class/package reference to the import table
    add_export()            -- create a new NormalExport
    add_actor_to_level()    -- wire an export into LevelExport.Actors
    remove_actor_from_level() -- remove an actor from the level
    add_property()          -- add a property to an export's Data list
    remove_property()       -- remove a property by name
    set_property_value()    -- modify an existing property value
    validate()              -- pre-save integrity check
    save()                  -- write the modified asset to disk
"""

import os
import sys
import shutil
from pathlib import Path
from typing import Optional, List, Dict, Any

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

import clr  # noqa: E402 -- must come after set_runtime
sys.path.append(str(_LIB_DIR))
clr.AddReference(str(_DLL_PATH))

# ---------------------------------------------------------------------------
# Import .NET Types
# ---------------------------------------------------------------------------

# Core UAssetAPI types
from UAssetAPI import UAsset  # noqa: E402
from UAssetAPI.UnrealTypes import (  # noqa: E402
    EngineVersion, FName, FPackageIndex, FString,
    FPropertyTypeName, FPropertyTypeNameNode,
)
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

# Import type for adding new imports
from UAssetAPI import Import as UImport  # noqa: E402

# Try importing Kismet types
try:
    from UAssetAPI.Kismet.Bytecode import KismetExpression, EExprToken  # noqa: E402
    from UAssetAPI.Kismet.Bytecode import ExpressionSerializer  # noqa: E402
    KISMET_AVAILABLE = True
except ImportError:
    KISMET_AVAILABLE = False

# Property types
from UAssetAPI.PropertyTypes.Objects import PropertyData  # noqa: E402

# Import specific property data types for add_property()
from UAssetAPI.PropertyTypes.Objects import (  # noqa: E402
    BoolPropertyData,
    IntPropertyData,
    FloatPropertyData,
    StrPropertyData,
    NamePropertyData,
    ObjectPropertyData,
    SoftObjectPropertyData,
    EnumPropertyData,
    BytePropertyData,
    TextPropertyData,
    ArrayPropertyData,
)
from UAssetAPI.PropertyTypes.Structs import (  # noqa: E402
    StructPropertyData,
    VectorPropertyData,
    RotatorPropertyData,
    LinearColorPropertyData,
    GuidPropertyData,
    SoftObjectPathPropertyData,
)

# System types for .NET interop
import System  # noqa: E402
from System.Collections.Generic import List as NetList  # noqa: E402

# ---------------------------------------------------------------------------
# Engine Version Mapping
# ---------------------------------------------------------------------------

ENGINE_VERSION_MAP = {
    "5.0": EngineVersion.VER_UE5_0,
    "5.1": EngineVersion.VER_UE5_1,
    "5.2": EngineVersion.VER_UE5_2,
    "5.3": EngineVersion.VER_UE5_3,
    "5.4": EngineVersion.VER_UE5_4,
    "5.5": EngineVersion.VER_UE5_5,
    "5.6": EngineVersion.VER_UE5_6,
}

# Map Python-friendly type names to .NET PropertyData constructors
PROPERTY_TYPE_MAP = {
    "bool": BoolPropertyData,
    "int": IntPropertyData,
    "float": FloatPropertyData,
    "str": StrPropertyData,
    "name": NamePropertyData,
    "object": ObjectPropertyData,
    "soft_object": SoftObjectPropertyData,
    "enum": EnumPropertyData,
    "byte": BytePropertyData,
    "text": TextPropertyData,
    "array": ArrayPropertyData,
    "struct": StructPropertyData,
    "vector": VectorPropertyData,
    "rotator": RotatorPropertyData,
    "linear_color": LinearColorPropertyData,
    "guid": GuidPropertyData,
    "soft_object_path": SoftObjectPathPropertyData,
}

# Map Python-friendly type names to UE serialization type names for PropertyTypeName.
# UE 5.4+ requires PropertyTypeName on every property for correct serialization.
# Simple types have a single node; complex types (array, struct) need inner type info.
PTN_TYPE_MAP = {
    "bool": "BoolProperty",
    "int": "IntProperty",
    "float": "FloatProperty",
    "str": "StrProperty",
    "name": "NameProperty",
    "object": "ObjectProperty",
    "soft_object": "SoftObjectProperty",
    "enum": "EnumProperty",
    "byte": "ByteProperty",
    "text": "TextProperty",
    "array": "ArrayProperty",
    "struct": "StructProperty",
    "vector": "StructProperty",
    "rotator": "StructProperty",
    "linear_color": "StructProperty",
    "guid": "StructProperty",
    "soft_object_path": "StructProperty",
}


def _make_ptn(asset, type_name: str) -> FPropertyTypeName:
    """
    Construct a minimal FPropertyTypeName for a given UE type name.
    This creates a single-node PTN (sufficient for simple types).
    For complex types (array with inner type, struct with struct name),
    use _make_ptn_complex() instead.
    """
    node = FPropertyTypeNameNode.__new__(FPropertyTypeNameNode)
    node.Name = FName.FromString(asset, type_name)
    node.InnerCount = 0
    nodes_list = NetList[FPropertyTypeNameNode]()
    nodes_list.Add(node)
    return FPropertyTypeName(nodes_list, True)


def _make_ptn_struct(asset, struct_type: str, struct_name: str,
                     struct_package: str = "/Script/CoreUObject") -> FPropertyTypeName:
    """
    Construct a FPropertyTypeName for a struct property.
    Example: StructProperty -> Guid -> /Script/CoreUObject
    """
    nodes_list = NetList[FPropertyTypeNameNode]()

    # Root node: StructProperty with InnerCount=1
    root = FPropertyTypeNameNode.__new__(FPropertyTypeNameNode)
    root.Name = FName.FromString(asset, struct_type)
    root.InnerCount = 1
    nodes_list.Add(root)

    # Struct name node with InnerCount=1 (for the package)
    name_node = FPropertyTypeNameNode.__new__(FPropertyTypeNameNode)
    name_node.Name = FName.FromString(asset, struct_name)
    name_node.InnerCount = 1
    nodes_list.Add(name_node)

    # Package node
    pkg_node = FPropertyTypeNameNode.__new__(FPropertyTypeNameNode)
    pkg_node.Name = FName.FromString(asset, struct_package)
    pkg_node.InnerCount = 0
    nodes_list.Add(pkg_node)

    return FPropertyTypeName(nodes_list, True)


# ---------------------------------------------------------------------------
# Validation Error
# ---------------------------------------------------------------------------

class AssetValidationError(Exception):
    """Raised when an asset fails integrity validation before save."""
    pass


# ---------------------------------------------------------------------------
# Asset Wrapper
# ---------------------------------------------------------------------------

class AssetFile:
    """
    Pythonic wrapper around UAssetAPI.UAsset.

    Handles reading, modifying, and writing .uasset/.umap files
    with automatic backup, validation, and reference integrity checking.
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

        if engine_version not in ENGINE_VERSION_MAP:
            raise ValueError(
                f"Unsupported engine version '{engine_version}'. "
                f"Supported: {list(ENGINE_VERSION_MAP.keys())}"
            )
        self.engine_version = ENGINE_VERSION_MAP[engine_version]
        self.engine_version_str = engine_version

        try:
            self._asset = UAsset(str(self.filepath), self.engine_version)
        except Exception as e:
            raise RuntimeError(
                f"Failed to load asset '{self.filepath}': {e}"
            ) from e

        # Caches (invalidated on mutation)
        self._exports_cache = None
        self._names_cache = None

    # =====================================================================
    # Properties (read-only accessors)
    # =====================================================================

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
            name_list = self._asset.GetNameMapIndexList()
            for i in range(name_list.Count):
                self._names_cache.append(str(name_list[i]))
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

    def _invalidate_caches(self):
        """Clear all cached data after a mutation."""
        self._exports_cache = None
        self._names_cache = None

    # =====================================================================
    # Export Inspection (read)
    # =====================================================================

    def get_export(self, index: int):
        """Get an export by zero-based index."""
        if index < 0 or index >= self.export_count:
            raise IndexError(f"Export index {index} out of range (0-{self.export_count - 1})")
        return self._asset.Exports[index]

    def get_export_class_name(self, export) -> str:
        """Get the class name of an export."""
        try:
            class_idx = export.ClassIndex
            if class_idx.Index < 0:
                imp = self._asset.Imports[-(class_idx.Index + 1)]
                return str(imp.ObjectName)
            elif class_idx.Index > 0:
                exp = self._asset.Exports[class_idx.Index - 1]
                return str(exp.ObjectName)
            else:
                return "Class"
        except Exception:
            return "Unknown"

    def get_export_name(self, export) -> str:
        """Get the object name of an export."""
        try:
            return str(export.ObjectName)
        except Exception:
            return "Unknown"

    def find_exports_by_class(self, class_name: str) -> list:
        """Find all exports matching a class name. Returns [(index, export), ...]."""
        results = []
        for i, exp in enumerate(self.exports):
            if self.get_export_class_name(exp) == class_name:
                results.append((i, exp))
        return results

    def find_exports_by_name(self, name: str) -> list:
        """Find all exports matching an object name. Returns [(index, export), ...]."""
        results = []
        for i, exp in enumerate(self.exports):
            if self.get_export_name(exp) == name:
                results.append((i, exp))
        return results

    # =====================================================================
    # Level-Specific (read)
    # =====================================================================

    def get_level_export(self) -> Optional[LevelExport]:
        """Find and return the LevelExport (PersistentLevel) if this is a .umap."""
        for exp in self.exports:
            if isinstance(exp, LevelExport):
                return exp
        return None

    def get_level_actors(self) -> list:
        """
        Get all actor references from the level export.
        Returns list of dicts with package_index, export_index, class_name, object_name.
        """
        level = self.get_level_export()
        if level is None:
            return []

        actors = []
        for i in range(level.Actors.Count):
            pkg_idx = level.Actors[i]
            idx = pkg_idx.Index
            if idx > 0:
                exp = self._asset.Exports[idx - 1]
                actors.append({
                    "package_index": idx,
                    "export_index": idx - 1,
                    "class_name": self.get_export_class_name(exp),
                    "object_name": self.get_export_name(exp),
                })
            elif idx < 0:
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

    # =====================================================================
    # Blueprint / Kismet (read)
    # =====================================================================

    def get_struct_exports(self) -> list:
        """Find all StructExport instances."""
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
        """Find all ClassExport instances."""
        results = []
        for i, exp in enumerate(self.exports):
            if isinstance(exp, ClassExport):
                results.append((i, exp))
        return results

    def get_kismet_bytecode(self, struct_export: StructExport) -> list:
        """Extract Kismet bytecode expressions from a StructExport."""
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
        try:
            if hasattr(expr, "Value"):
                result["value"] = str(expr.Value)
        except Exception:
            pass
        return result

    # =====================================================================
    # Property Inspection (read)
    # =====================================================================

    def get_export_properties(self, export) -> list:
        """Get all properties from a NormalExport as a list of dicts."""
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
        try:
            if hasattr(prop, "Value"):
                val = prop.Value
                result["value"] = str(val) if val is not None else "None"
        except Exception:
            result["value"] = "<unreadable>"
        return result

    # =====================================================================
    # WRITE: Import Table
    # =====================================================================

    def add_import(self, class_package: str, class_name: str,
                   object_name: str, outer_index: int = 0) -> int:
        """
        Add a new entry to the import table.

        This is how you reference an external class (e.g., /Script/Engine.StaticMeshActor)
        that doesn't already exist in the asset's import table.

        Args:
            class_package: The package containing the class (e.g., "/Script/CoreUObject")
            class_name:    The class type name (e.g., "Package", "Class", "ScriptStruct")
            object_name:   The object being imported (e.g., "StaticMeshActor", "/Script/Engine")
            outer_index:   FPackageIndex of the outer (0 = no outer, negative = import index)

        Returns:
            The new import's FPackageIndex value (negative, 1-based).
            Use this value when setting ClassIndex or OuterIndex on exports.

        Example:
            # Add /Script/Engine package import
            engine_pkg_idx = af.add_import("/Script/CoreUObject", "Package", "/Script/Engine")
            # Add StaticMeshActor class import under /Script/Engine
            actor_class_idx = af.add_import("/Script/Engine", "Class", "StaticMeshActor",
                                            outer_index=engine_pkg_idx)
        """
        # Ensure names exist in the name map (AddNameReference takes FString)
        self._asset.AddNameReference(FString(class_package))
        self._asset.AddNameReference(FString(class_name))
        self._asset.AddNameReference(FString(object_name))

        new_import = UImport(
            FName.FromString(self._asset, class_package),
            FName.FromString(self._asset, class_name),
            FPackageIndex(outer_index),
            FName.FromString(self._asset, object_name),
            False  # bImportOptional
        )
        self._asset.Imports.Add(new_import)
        self._invalidate_caches()

        # Import indices are negative, 1-based: first import = -1, second = -2, etc.
        # The new import is at position (Count - 1), so its FPackageIndex = -Count
        return -self._asset.Imports.Count

    def find_import(self, object_name: str) -> Optional[int]:
        """
        Find an existing import by object name.

        Returns the FPackageIndex value (negative) or None if not found.
        """
        for i in range(self._asset.Imports.Count):
            imp = self._asset.Imports[i]
            if str(imp.ObjectName) == object_name:
                return -(i + 1)
        return None

    def find_or_add_import(self, class_package: str, class_name: str,
                           object_name: str, outer_index: int = 0) -> int:
        """
        Find an existing import or add a new one.
        Returns the FPackageIndex value (negative).
        """
        existing = self.find_import(object_name)
        if existing is not None:
            return existing
        return self.add_import(class_package, class_name, object_name, outer_index)

    # =====================================================================
    # WRITE: Export Table
    # =====================================================================

    def add_export(self, object_name: str, class_index: int,
                   outer_index: int = 0, template_index: int = 0,
                   object_flags: int = 0x00000001) -> int:
        """
        Add a new NormalExport to the asset.

        Args:
            object_name:    Name of the new object (e.g., "MyActor_0")
            class_index:    FPackageIndex of the class (negative = import)
            outer_index:    FPackageIndex of the outer object (0 = package root)
            template_index: FPackageIndex of the template/archetype (0 = none)
            object_flags:   EObjectFlags (default RF_Public = 0x01)

        Returns:
            The new export's FPackageIndex value (positive, 1-based).
            Use this value when adding actors to the level.

        IMPORTANT: The returned export has an empty Data list. Use add_property()
        to populate it, or access the NormalExport directly via get_export().
        """
        # Ensure the object name is in the name map (AddNameReference takes FString)
        self._asset.AddNameReference(FString(object_name))

        # Create a new NormalExport
        new_export = NormalExport(self._asset, System.Array[System.Byte](b''))
        new_export.ObjectName = FName.FromString(self._asset, object_name)
        new_export.ClassIndex = FPackageIndex(class_index)
        new_export.OuterIndex = FPackageIndex(outer_index)
        new_export.TemplateIndex = FPackageIndex(template_index)
        new_export.ObjectFlags = System.Enum.ToObject(
            clr.GetClrType(type(new_export.ObjectFlags)), object_flags
        ) if hasattr(new_export, 'ObjectFlags') else None

        # Initialize empty property data list
        new_export.Data = NetList[PropertyData]()

        # Initialize Extras as empty byte array (required for serialization)
        new_export.Extras = System.Array[System.Byte](b'')

        self._asset.Exports.Add(new_export)
        self._invalidate_caches()

        # Export indices are positive, 1-based
        return self._asset.Exports.Count

    # =====================================================================
    # WRITE: Level Actors
    # =====================================================================

    def add_actor_to_level(self, export_package_index: int) -> bool:
        """
        Add an export to the LevelExport's Actors list.

        Args:
            export_package_index: Positive, 1-based FPackageIndex of the export.
                                  This is the value returned by add_export().

        Returns:
            True on success.

        Raises:
            ValueError if this is not a .umap or has no LevelExport.
        """
        level = self.get_level_export()
        if level is None:
            raise ValueError("No LevelExport found. Is this a .umap file?")

        level.Actors.Add(FPackageIndex(export_package_index))
        self._invalidate_caches()
        return True

    def remove_actor_from_level(self, export_package_index: int) -> bool:
        """
        Remove an actor from the LevelExport's Actors list by its FPackageIndex.

        Args:
            export_package_index: The positive, 1-based FPackageIndex to remove.

        Returns:
            True if the actor was found and removed, False otherwise.
        """
        level = self.get_level_export()
        if level is None:
            raise ValueError("No LevelExport found. Is this a .umap file?")

        for i in range(level.Actors.Count):
            if level.Actors[i].Index == export_package_index:
                level.Actors.RemoveAt(i)
                self._invalidate_caches()
                return True
        return False

    # =====================================================================
    # WRITE: Properties
    # =====================================================================

    def add_property(self, export_index: int, prop_name: str,
                     prop_type: str, value=None) -> bool:
        """
        Add a new property to an export's Data list.

        Args:
            export_index: Zero-based export index
            prop_name:    Property name (e.g., "bCanEverTick")
            prop_type:    Type string: "bool", "int", "float", "str", "name",
                          "object", "enum", "byte", "text", "array", "struct"
            value:        Initial value (type-appropriate). None for default.

        Returns:
            True on success.

        Note:
            For UE 5.4+ assets, PropertyTypeName is automatically set.
            For complex types (array, struct), you may need to configure
            the property further via direct .NET access after creation.
        """
        export = self.get_export(export_index)
        if not isinstance(export, NormalExport):
            raise TypeError(f"Export {export_index} is not a NormalExport")

        if export.Data is None:
            export.Data = NetList[PropertyData]()

        if prop_type not in PROPERTY_TYPE_MAP:
            raise ValueError(
                f"Unknown property type '{prop_type}'. "
                f"Supported: {list(PROPERTY_TYPE_MAP.keys())}"
            )

        # Ensure property name is in the name map (AddNameReference takes FString)
        self._asset.AddNameReference(FString(prop_name))

        # Create the property
        prop_class = PROPERTY_TYPE_MAP[prop_type]
        fname = FName.FromString(self._asset, prop_name)
        new_prop = prop_class(fname)

        # Set PropertyTypeName (required for UE 5.4+ serialization).
        # Without this, MainSerializer.Write() will NullRef on property.PropertyTypeName.Write().
        ue_type_name = PTN_TYPE_MAP.get(prop_type)
        if ue_type_name:
            # Ensure the type name is in the name map
            self._asset.AddNameReference(FString(ue_type_name))

            # Struct-based types need a multi-node PTN with struct name and package
            STRUCT_PTN_INFO = {
                "vector": ("Vector", "/Script/CoreUObject"),
                "rotator": ("Rotator", "/Script/CoreUObject"),
                "linear_color": ("LinearColor", "/Script/CoreUObject"),
                "guid": ("Guid", "/Script/CoreUObject"),
                "soft_object_path": ("SoftObjectPath", "/Script/CoreUObject"),
            }

            if prop_type in STRUCT_PTN_INFO:
                struct_name, struct_pkg = STRUCT_PTN_INFO[prop_type]
                self._asset.AddNameReference(FString(struct_name))
                self._asset.AddNameReference(FString(struct_pkg))
                new_prop.PropertyTypeName = _make_ptn_struct(
                    self._asset, ue_type_name, struct_name, struct_pkg
                )
            else:
                new_prop.PropertyTypeName = _make_ptn(self._asset, ue_type_name)

        # Set value if provided
        if value is not None and hasattr(new_prop, "Value"):
            new_prop.Value = value

        export.Data.Add(new_prop)
        self._invalidate_caches()
        return True

    def remove_property(self, export_index: int, prop_name: str) -> bool:
        """
        Remove a property from an export's Data list by name.

        Args:
            export_index: Zero-based export index
            prop_name:    Property name to remove

        Returns:
            True if found and removed, False if not found.
        """
        export = self.get_export(export_index)
        if not isinstance(export, NormalExport):
            raise TypeError(f"Export {export_index} is not a NormalExport")

        if export.Data is None:
            return False

        for i in range(export.Data.Count):
            if str(export.Data[i].Name) == prop_name:
                export.Data.RemoveAt(i)
                self._invalidate_caches()
                return True
        return False

    def set_property_value(self, export_index: int, prop_name: str, new_value):
        """
        Set a property value on an export by name.

        For complex types, use the .NET objects directly.

        Args:
            export_index: Zero-based export index
            prop_name:    Property name to modify
            new_value:    New value (type must match the property type)

        Returns:
            True on success.

        Raises:
            KeyError if property not found.
            TypeError if export is not a NormalExport.
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
                    self._invalidate_caches()
                    return True
                else:
                    raise AttributeError(
                        f"Property '{prop_name}' does not have a simple Value field. "
                        "Use direct .NET object access for complex types."
                    )

        raise KeyError(f"Property '{prop_name}' not found on export {export_index}")

    # =====================================================================
    # Validation
    # =====================================================================

    def validate(self) -> List[str]:
        """
        Run integrity checks on the asset before saving.

        Returns a list of warning/error strings. Empty list = all clear.
        Critical errors are prefixed with "ERROR:".
        Non-critical issues are prefixed with "WARN:".
        """
        issues = []

        # 1. Check export ClassIndex references
        for i in range(self._asset.Exports.Count):
            exp = self._asset.Exports[i]
            ci = exp.ClassIndex.Index
            if ci < 0:
                imp_idx = -(ci + 1)
                if imp_idx >= self._asset.Imports.Count:
                    issues.append(
                        f"ERROR: Export[{i}] ClassIndex={ci} references "
                        f"import {imp_idx} but only {self._asset.Imports.Count} imports exist"
                    )
            elif ci > 0:
                exp_idx = ci - 1
                if exp_idx >= self._asset.Exports.Count:
                    issues.append(
                        f"ERROR: Export[{i}] ClassIndex={ci} references "
                        f"export {exp_idx} but only {self._asset.Exports.Count} exports exist"
                    )

        # 2. Check export OuterIndex references
        for i in range(self._asset.Exports.Count):
            exp = self._asset.Exports[i]
            oi = exp.OuterIndex.Index
            if oi < 0:
                imp_idx = -(oi + 1)
                if imp_idx >= self._asset.Imports.Count:
                    issues.append(
                        f"ERROR: Export[{i}] OuterIndex={oi} references "
                        f"import {imp_idx} but only {self._asset.Imports.Count} imports exist"
                    )
            elif oi > 0:
                exp_idx = oi - 1
                if exp_idx >= self._asset.Exports.Count:
                    issues.append(
                        f"ERROR: Export[{i}] OuterIndex={oi} references "
                        f"export {exp_idx} but only {self._asset.Exports.Count} exports exist"
                    )

        # 3. Check LevelExport actor references
        level = self.get_level_export()
        if level is not None:
            for i in range(level.Actors.Count):
                idx = level.Actors[i].Index
                if idx > 0:
                    exp_idx = idx - 1
                    if exp_idx >= self._asset.Exports.Count:
                        issues.append(
                            f"ERROR: LevelExport.Actors[{i}] references "
                            f"export {exp_idx} but only {self._asset.Exports.Count} exports exist"
                        )
                elif idx < 0:
                    imp_idx = -(idx + 1)
                    if imp_idx >= self._asset.Imports.Count:
                        issues.append(
                            f"ERROR: LevelExport.Actors[{i}] references "
                            f"import {imp_idx} but only {self._asset.Imports.Count} imports exist"
                        )

        # 4. Check import OuterIndex references
        for i in range(self._asset.Imports.Count):
            imp = self._asset.Imports[i]
            oi = imp.OuterIndex.Index
            if oi < 0:
                ref_idx = -(oi + 1)
                if ref_idx >= self._asset.Imports.Count:
                    issues.append(
                        f"ERROR: Import[{i}] OuterIndex={oi} references "
                        f"import {ref_idx} but only {self._asset.Imports.Count} imports exist"
                    )
            elif oi > 0:
                ref_idx = oi - 1
                if ref_idx >= self._asset.Exports.Count:
                    issues.append(
                        f"ERROR: Import[{i}] OuterIndex={oi} references "
                        f"export {ref_idx} but only {self._asset.Exports.Count} exports exist"
                    )

        return issues

    # =====================================================================
    # Save / Backup
    # =====================================================================

    def backup(self) -> Path:
        """Create a backup of the original file. Returns the backup path."""
        backup_path = self.filepath.with_suffix(self.filepath.suffix + ".bak")
        counter = 1
        while backup_path.exists():
            backup_path = self.filepath.with_suffix(f"{self.filepath.suffix}.bak{counter}")
            counter += 1
        shutil.copy2(self.filepath, backup_path)
        return backup_path

    def save(self, output_path: Optional[str] = None,
             create_backup: bool = True,
             validate_first: bool = True) -> Path:
        """
        Save the modified asset to disk.

        Args:
            output_path:    Where to save. If None, overwrites the original.
            create_backup:  If True, backs up the original before overwriting.
            validate_first: If True, runs validate() and raises on ERROR-level issues.

        Returns:
            Path to the saved file.

        Raises:
            AssetValidationError if validate_first=True and errors are found.
        """
        # Pre-save validation
        if validate_first:
            issues = self.validate()
            errors = [i for i in issues if i.startswith("ERROR:")]
            if errors:
                raise AssetValidationError(
                    f"Asset failed validation with {len(errors)} error(s):\n"
                    + "\n".join(errors)
                )

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

    # =====================================================================
    # Summary
    # =====================================================================

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
