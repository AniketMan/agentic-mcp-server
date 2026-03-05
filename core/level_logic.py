"""
level_logic.py — Safe level logic editing operations for UE 5.6 .umap files.

Provides high-level operations for modifying level actors, their properties,
and Blueprint connections while maintaining reference integrity.

SAFETY RULES:
  1. Always validate before saving (integrity.py)
  2. Never modify FPackageIndex values without updating ALL references
  3. Never remove an export without checking for references to it
  4. Always add new FNames to the name map before using them
  5. Preserve the extras byte array on LevelExport (contains PrecomputedVisibility etc.)
"""

from typing import List, Dict, Optional, Tuple
from dataclasses import dataclass
from .uasset_bridge import AssetFile, LevelExport, NormalExport, FPackageIndex
from .integrity import IntegrityValidator, ValidationReport


@dataclass
class ActorInfo:
    """Structured representation of a level actor."""
    export_index: int       # 0-based index into exports array
    package_index: int      # FPackageIndex value (1-based for exports)
    class_name: str         # e.g., "StaticMeshActor"
    object_name: str        # e.g., "StaticMeshActor_0"
    outer_name: str         # Name of the outer object
    properties: list        # List of property dicts
    has_blueprint: bool     # Whether this actor has Blueprint logic
    component_count: int    # Number of child components


class LevelEditor:
    """
    High-level level logic editor.
    
    Wraps AssetFile with operations specific to .umap editing:
    actor manipulation, property editing, and reference management.
    """

    def __init__(self, asset_file: AssetFile):
        """
        Args:
            asset_file: An opened AssetFile (must be a .umap)
        """
        self.af = asset_file
        self.validator = IntegrityValidator(asset_file)

        # Verify this is a map file with a level export
        self._level = self.af.get_level_export()
        if self._level is None:
            raise ValueError(
                f"No LevelExport found in '{asset_file.filepath}'. "
                "This may not be a valid .umap file."
            )

    @property
    def level(self) -> LevelExport:
        """The LevelExport object."""
        return self._level

    @property
    def actor_count(self) -> int:
        """Number of actors in the level."""
        return self._level.Actors.Count

    # ----- Actor Inspection -----

    def get_actors(self) -> List[ActorInfo]:
        """
        Get all actors in the level with their metadata.
        Returns a list of ActorInfo objects.
        """
        actors = []
        for i in range(self._level.Actors.Count):
            pkg_idx = self._level.Actors[i].Index
            if pkg_idx <= 0:
                # Null or import reference — skip or handle
                actors.append(ActorInfo(
                    export_index=-1,
                    package_index=pkg_idx,
                    class_name="Null" if pkg_idx == 0 else "Import",
                    object_name="Null",
                    outer_name="",
                    properties=[],
                    has_blueprint=False,
                    component_count=0,
                ))
                continue

            exp_idx = pkg_idx - 1  # Convert to 0-based
            exp = self.af.get_export(exp_idx)
            class_name = self.af.get_export_class_name(exp)
            obj_name = self.af.get_export_name(exp)

            # Get outer name
            outer_name = ""
            try:
                if exp.OuterIndex.Index > 0:
                    outer_exp = self.af.get_export(exp.OuterIndex.Index - 1)
                    outer_name = self.af.get_export_name(outer_exp)
            except Exception:
                pass

            # Check for Blueprint logic
            has_bp = class_name in (
                "BlueprintGeneratedClass",
                "WidgetBlueprintGeneratedClass",
            ) or self._actor_has_blueprint_logic(exp_idx)

            # Count child components (exports whose outer is this actor)
            component_count = self._count_children(pkg_idx)

            # Get properties
            props = self.af.get_export_properties(exp) if isinstance(exp, NormalExport) else []

            actors.append(ActorInfo(
                export_index=exp_idx,
                package_index=pkg_idx,
                class_name=class_name,
                object_name=obj_name,
                outer_name=outer_name,
                properties=props,
                has_blueprint=has_bp,
                component_count=component_count,
            ))

        return actors

    def get_actor_by_name(self, name: str) -> Optional[ActorInfo]:
        """Find an actor by its object name."""
        for actor in self.get_actors():
            if actor.object_name == name:
                return actor
        return None

    def get_actor_by_class(self, class_name: str) -> List[ActorInfo]:
        """Find all actors of a given class."""
        return [a for a in self.get_actors() if a.class_name == class_name]

    # ----- Actor Components -----

    def get_actor_components(self, actor: ActorInfo) -> List[Dict]:
        """
        Get all components belonging to an actor.
        Components are exports whose OuterIndex points to the actor's export.
        """
        if actor.export_index < 0:
            return []

        components = []
        target_pkg_idx = actor.package_index  # 1-based

        for i in range(self.af.export_count):
            exp = self.af.get_export(i)
            if exp.OuterIndex.Index == target_pkg_idx:
                components.append({
                    "export_index": i,
                    "class_name": self.af.get_export_class_name(exp),
                    "object_name": self.af.get_export_name(exp),
                    "properties": self.af.get_export_properties(exp) if isinstance(exp, NormalExport) else [],
                })

        return components

    # ----- Property Editing -----

    def set_actor_property(self, actor_name: str, prop_name: str, value) -> bool:
        """
        Set a property value on an actor by name.
        
        Args:
            actor_name: The actor's object name
            prop_name: The property name to modify
            value: The new value
            
        Returns:
            True if successful
        """
        actor = self.get_actor_by_name(actor_name)
        if actor is None:
            raise KeyError(f"Actor '{actor_name}' not found in level")
        if actor.export_index < 0:
            raise ValueError(f"Actor '{actor_name}' is not an export (cannot edit)")

        return self.af.set_property_value(actor.export_index, prop_name, value)

    def get_actor_property(self, actor_name: str, prop_name: str):
        """
        Get a property value from an actor by name.
        
        Returns the property dict, or None if not found.
        """
        actor = self.get_actor_by_name(actor_name)
        if actor is None:
            raise KeyError(f"Actor '{actor_name}' not found in level")

        for prop in actor.properties:
            if prop["name"] == prop_name:
                return prop
        return None

    # ----- Actor List Manipulation -----

    def remove_actor_from_level(self, actor_name: str) -> bool:
        """
        Remove an actor reference from the level's actor list.
        
        WARNING: This does NOT delete the export — it only removes the reference
        from LevelExport.Actors. The export data remains in the file.
        This is the safe approach: UE will ignore unreferenced exports.
        
        Args:
            actor_name: The actor's object name
            
        Returns:
            True if the actor was found and removed
        """
        for i in range(self._level.Actors.Count):
            pkg_idx = self._level.Actors[i].Index
            if pkg_idx > 0:
                exp = self.af.get_export(pkg_idx - 1)
                if self.af.get_export_name(exp) == actor_name:
                    self._level.Actors.RemoveAt(i)
                    return True
        return False

    def reorder_actor(self, actor_name: str, new_position: int) -> bool:
        """
        Move an actor to a different position in the level's actor list.
        This can affect load order and initialization order.
        
        Args:
            actor_name: The actor's object name
            new_position: Target position (0-based) in the actor list
        """
        current_idx = -1
        for i in range(self._level.Actors.Count):
            pkg_idx = self._level.Actors[i].Index
            if pkg_idx > 0:
                exp = self.af.get_export(pkg_idx - 1)
                if self.af.get_export_name(exp) == actor_name:
                    current_idx = i
                    break

        if current_idx < 0:
            return False

        if new_position < 0 or new_position >= self._level.Actors.Count:
            raise IndexError(f"Position {new_position} out of range (0-{self._level.Actors.Count - 1})")

        # Remove and re-insert
        actor_ref = self._level.Actors[current_idx]
        self._level.Actors.RemoveAt(current_idx)
        self._level.Actors.Insert(new_position, actor_ref)
        return True

    # ----- Reference Scanning -----

    def find_references_to_export(self, export_index: int) -> List[Dict]:
        """
        Find all references to a specific export throughout the asset.
        This is critical for safe deletion — you must know what references
        an export before removing it.
        
        Args:
            export_index: 0-based export index
            
        Returns:
            List of dicts describing each reference location
        """
        target_pkg_idx = export_index + 1  # Convert to 1-based FPackageIndex
        refs = []

        # Check other exports' structural references
        for i in range(self.af.export_count):
            exp = self.af.get_export(i)
            for field_name in ["ClassIndex", "SuperIndex", "TemplateIndex", "OuterIndex"]:
                try:
                    idx = getattr(exp, field_name).Index
                    if idx == target_pkg_idx:
                        refs.append({
                            "type": "export_field",
                            "source_export": i,
                            "field": field_name,
                            "source_name": self.af.get_export_name(exp),
                        })
                except Exception:
                    pass

        # Check level actor list
        level = self.af.get_level_export()
        if level:
            for i in range(level.Actors.Count):
                if level.Actors[i].Index == target_pkg_idx:
                    refs.append({
                        "type": "level_actor",
                        "actor_index": i,
                    })

        return refs

    # ----- Validation -----

    def validate(self) -> ValidationReport:
        """Run full integrity validation on the asset."""
        return self.validator.validate_all()

    def save(self, output_path: Optional[str] = None, 
             validate_first: bool = True, create_backup: bool = True):
        """
        Save the modified level with safety checks.
        
        Args:
            output_path: Where to save. None = overwrite original.
            validate_first: If True, runs validation before saving.
            create_backup: If True, backs up the original.
            
        Returns:
            Tuple of (saved_path, validation_report)
        """
        report = None
        if validate_first:
            report = self.validate()
            if not report.passed:
                raise RuntimeError(
                    f"Validation FAILED with {report.error_count} errors. "
                    f"Fix the issues before saving.\n"
                    + "\n".join(str(i) for i in report.issues if i.severity.value in ("error", "critical"))
                )

        saved_path = self.af.save(output_path, create_backup)
        return saved_path, report

    # ----- Internal Helpers -----

    def _actor_has_blueprint_logic(self, export_index: int) -> bool:
        """Check if an actor has associated Blueprint bytecode."""
        # Look for function exports whose outer is this actor's class
        exp = self.af.get_export(export_index)
        class_idx = exp.ClassIndex.Index

        if class_idx <= 0:
            return False

        # Check if the class export is a ClassExport with functions
        try:
            from .uasset_bridge import ClassExport, FunctionExport
            class_exp = self.af.get_export(class_idx - 1)
            if isinstance(class_exp, ClassExport):
                return True
        except Exception:
            pass

        return False

    def _count_children(self, parent_pkg_idx: int) -> int:
        """Count exports whose OuterIndex points to the given package index."""
        count = 0
        for i in range(self.af.export_count):
            exp = self.af.get_export(i)
            if exp.OuterIndex.Index == parent_pkg_idx:
                count += 1
        return count
