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

import logging
import threading
from typing import List, Dict, Optional, Tuple, Callable, Any
from dataclasses import dataclass, field
from copy import deepcopy
from .uasset_bridge import AssetFile, LevelExport, NormalExport, FPackageIndex
from .integrity import IntegrityValidator, ValidationReport

logger = logging.getLogger(__name__)


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

        Thread-safe: Uses a lock to prevent race conditions.

        Args:
            actor_name: The actor's object name
            new_position: Target position (0-based) in the actor list

        Returns:
            True if the actor was found and moved, False otherwise.

        Raises:
            IndexError: If new_position is out of range.
        """
        # Use a lock to make this operation atomic
        if not hasattr(self, '_reorder_lock'):
            self._reorder_lock = threading.Lock()

        with self._reorder_lock:
            # Capture count at start of atomic operation
            actor_count = self._level.Actors.Count

            current_idx = -1
            for i in range(actor_count):
                pkg_idx = self._level.Actors[i].Index
                if pkg_idx > 0:
                    exp = self.af.get_export(pkg_idx - 1)
                    if self.af.get_export_name(exp) == actor_name:
                        current_idx = i
                        break

            if current_idx < 0:
                return False

            if new_position < 0 or new_position >= actor_count:
                raise IndexError(f"Position {new_position} out of range (0-{actor_count - 1})")

            # Remove and re-insert atomically
            actor_ref = self._level.Actors[current_idx]
            self._level.Actors.RemoveAt(current_idx)
            self._level.Actors.Insert(new_position, actor_ref)

            logger.debug(f"Reordered actor '{actor_name}' from {current_idx} to {new_position}")
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
        except Exception as e:
            logger.debug(f"Error checking blueprint logic: {e}")

        return False

    def _count_children(self, parent_pkg_idx: int) -> int:
        """Count exports whose OuterIndex points to the given package index."""
        count = 0
        for i in range(self.af.export_count):
            exp = self.af.get_export(i)
            if exp.OuterIndex.Index == parent_pkg_idx:
                count += 1
        return count


# =============================================================================
# Undo/Redo System
# =============================================================================

@dataclass
class UndoAction:
    """Represents a single undoable action."""
    action_type: str
    description: str
    undo_data: Dict[str, Any]
    redo_data: Dict[str, Any]
    timestamp: float = field(default_factory=lambda: __import__('time').time())


class UndoStack:
    """
    Undo/Redo stack for level editing operations.

    Usage:
        stack = UndoStack(max_size=50)

        # Record an action
        stack.push(UndoAction(
            action_type="set_property",
            description="Set health to 100",
            undo_data={"actor": "Player", "prop": "Health", "old_value": 50},
            redo_data={"actor": "Player", "prop": "Health", "new_value": 100},
        ))

        # Undo
        action = stack.undo()
        # Apply undo_data...

        # Redo
        action = stack.redo()
        # Apply redo_data...
    """

    def __init__(self, max_size: int = 50):
        self._undo_stack: List[UndoAction] = []
        self._redo_stack: List[UndoAction] = []
        self._max_size = max_size
        self._lock = threading.Lock()

    def push(self, action: UndoAction) -> None:
        """Push a new action onto the undo stack. Clears redo stack."""
        with self._lock:
            self._undo_stack.append(action)
            self._redo_stack.clear()

            # Trim to max size
            while len(self._undo_stack) > self._max_size:
                self._undo_stack.pop(0)

            logger.debug(f"Pushed undo action: {action.description}")

    def undo(self) -> Optional[UndoAction]:
        """Pop from undo stack and push to redo stack."""
        with self._lock:
            if not self._undo_stack:
                return None

            action = self._undo_stack.pop()
            self._redo_stack.append(action)
            logger.info(f"Undo: {action.description}")
            return action

    def redo(self) -> Optional[UndoAction]:
        """Pop from redo stack and push to undo stack."""
        with self._lock:
            if not self._redo_stack:
                return None

            action = self._redo_stack.pop()
            self._undo_stack.append(action)
            logger.info(f"Redo: {action.description}")
            return action

    def can_undo(self) -> bool:
        """Check if undo is available."""
        return len(self._undo_stack) > 0

    def can_redo(self) -> bool:
        """Check if redo is available."""
        return len(self._redo_stack) > 0

    def clear(self) -> None:
        """Clear both stacks."""
        with self._lock:
            self._undo_stack.clear()
            self._redo_stack.clear()

    @property
    def undo_count(self) -> int:
        return len(self._undo_stack)

    @property
    def redo_count(self) -> int:
        return len(self._redo_stack)


# =============================================================================
# Batch Operations
# =============================================================================

@dataclass
class BatchResult:
    """Result of a batch operation."""
    total: int
    successful: int
    failed: int
    errors: List[Tuple[int, str, Exception]]

    @property
    def success_rate(self) -> float:
        return self.successful / self.total if self.total > 0 else 0.0


class BatchOperations:
    """
    Batch operations for efficient bulk editing.

    Usage:
        editor = LevelEditor(asset_file)
        batch = BatchOperations(editor)

        # Set property on multiple actors
        result = batch.set_property_bulk(
            actor_names=["Actor1", "Actor2", "Actor3"],
            prop_name="Health",
            value=100,
        )
        print(f"Updated {result.successful}/{result.total} actors")
    """

    def __init__(self, editor: LevelEditor):
        self.editor = editor
        self._lock = threading.Lock()

    def set_property_bulk(
        self,
        actor_names: List[str],
        prop_name: str,
        value: Any,
        continue_on_error: bool = True,
    ) -> BatchResult:
        """
        Set a property on multiple actors.

        Args:
            actor_names: List of actor names to modify
            prop_name: Property name to set
            value: Value to set
            continue_on_error: If True, continue on individual failures

        Returns:
            BatchResult with success/failure counts
        """
        errors = []
        successful = 0

        with self._lock:
            for i, name in enumerate(actor_names):
                try:
                    self.editor.set_actor_property(name, prop_name, value)
                    successful += 1
                except Exception as e:
                    logger.warning(f"Failed to set {prop_name} on {name}: {e}")
                    errors.append((i, name, e))
                    if not continue_on_error:
                        break

        return BatchResult(
            total=len(actor_names),
            successful=successful,
            failed=len(errors),
            errors=errors,
        )

    def remove_actors_bulk(
        self,
        actor_names: List[str],
        continue_on_error: bool = True,
    ) -> BatchResult:
        """
        Remove multiple actors from the level.

        Args:
            actor_names: List of actor names to remove
            continue_on_error: If True, continue on individual failures

        Returns:
            BatchResult with success/failure counts
        """
        errors = []
        successful = 0

        with self._lock:
            for i, name in enumerate(actor_names):
                try:
                    if self.editor.remove_actor_from_level(name):
                        successful += 1
                    else:
                        raise ValueError(f"Actor not found: {name}")
                except Exception as e:
                    logger.warning(f"Failed to remove {name}: {e}")
                    errors.append((i, name, e))
                    if not continue_on_error:
                        break

        return BatchResult(
            total=len(actor_names),
            successful=successful,
            failed=len(errors),
            errors=errors,
        )

    def get_actors_by_class_bulk(
        self,
        class_names: List[str],
    ) -> Dict[str, List[ActorInfo]]:
        """
        Get actors grouped by class name.

        Args:
            class_names: List of class names to search for

        Returns:
            Dict mapping class name to list of actors
        """
        result = {name: [] for name in class_names}

        for actor in self.editor.get_actors():
            if actor.class_name in class_names:
                result[actor.class_name].append(actor)

        return result

    def transform_actors(
        self,
        actor_names: List[str],
        transform_fn: Callable[[ActorInfo], Dict[str, Any]],
        continue_on_error: bool = True,
    ) -> BatchResult:
        """
        Apply a transformation function to multiple actors.

        Args:
            actor_names: List of actor names to transform
            transform_fn: Function that takes ActorInfo and returns property updates
            continue_on_error: If True, continue on individual failures

        Returns:
            BatchResult with success/failure counts
        """
        errors = []
        successful = 0

        with self._lock:
            for i, name in enumerate(actor_names):
                try:
                    actor = self.editor.get_actor_by_name(name)
                    if actor is None:
                        raise ValueError(f"Actor not found: {name}")

                    updates = transform_fn(actor)
                    for prop_name, value in updates.items():
                        self.editor.set_actor_property(name, prop_name, value)

                    successful += 1
                except Exception as e:
                    logger.warning(f"Failed to transform {name}: {e}")
                    errors.append((i, name, e))
                    if not continue_on_error:
                        break

        return BatchResult(
            total=len(actor_names),
            successful=successful,
            failed=len(errors),
            errors=errors,
        )
