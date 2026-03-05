"""
UE 5.6 Level Editor — Unreal Python Script Generator
=====================================================
Generates Python scripts that run inside UE5.6's editor via the `unreal` module.
Covers all major editor scripting domains: actors, assets, level sequences,
materials, Blueprints, PCG, animation, data tables, and more.

Each generator method returns a complete, runnable Python script string with:
  - Full imports
  - Logging for every operation (print statements for UE Python console)
  - Error handling with try/except
  - Validation checks before destructive operations

Usage:
    gen = ScriptGenerator()
    script = gen.actors.spawn("StaticMeshActor", location=(0, 0, 100))
    # script is a string you paste into UE5.6 Python console or run via
    # UE5Editor.exe -ExecutePythonScript="path/to/script.py"
"""

import json
import logging
import textwrap
from dataclasses import dataclass, field
from typing import Optional, Union

logger = logging.getLogger(__name__)


# =============================================================================
# Script Template Helpers
# =============================================================================

def _header(title: str, description: str = "") -> str:
    """Generate a script header with imports and logging setup."""
    lines = [
        '"""',
        f'UE 5.6 Level Editor — Generated Script',
        f'{title}',
        f'{description}' if description else '',
        '"""',
        'import unreal',
        'import traceback',
        '',
        '# --- Logging helper ---',
        'def log(msg, level="info"):',
        '    """Log to UE output log and Python console."""',
        '    prefix = f"[JARVIS] "',
        '    if level == "error":',
        '        unreal.log_error(prefix + str(msg))',
        '    elif level == "warning":',
        '        unreal.log_warning(prefix + str(msg))',
        '    else:',
        '        unreal.log(prefix + str(msg))',
        '    print(prefix + str(msg))',
        '',
    ]
    return '\n'.join(lines)


def _wrap_try(body: str) -> str:
    """Wrap script body in try/except with error logging."""
    indented = textwrap.indent(body, '    ')
    return (
        'try:\n'
        f'{indented}\n'
        'except Exception as e:\n'
        '    log(f"SCRIPT FAILED: {e}", "error")\n'
        '    traceback.print_exc()\n'
    )


def _vec3(x: float, y: float, z: float) -> str:
    """Generate an FVector constructor string."""
    return f'unreal.Vector({x}, {y}, {z})'


def _rot3(pitch: float, yaw: float, roll: float) -> str:
    """Generate an FRotator constructor string."""
    return f'unreal.Rotator({pitch}, {yaw}, {roll})'


def _transform(location=(0, 0, 0), rotation=(0, 0, 0), scale=(1, 1, 1)) -> str:
    """Generate an FTransform constructor string."""
    return (
        f'unreal.Transform(\n'
        f'    location={_vec3(*location)},\n'
        f'    rotation={_rot3(*rotation)},\n'
        f'    scale={_vec3(*scale)}\n'
        f')'
    )


# =============================================================================
# Domain: Actors
# =============================================================================

class ActorScripts:
    """Generate scripts for actor manipulation in the level."""

    def spawn(self, actor_class: str, name: str = "",
              location: tuple = (0, 0, 0),
              rotation: tuple = (0, 0, 0),
              scale: tuple = (1, 1, 1)) -> str:
        """Spawn an actor in the current level."""
        logger.info(f"Generating spawn script: {actor_class}")
        body = (
            f'log("Spawning actor: {actor_class}")\n'
            f'\n'
            f'# Get the editor actor subsystem\n'
            f'subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n'
            f'\n'
            f'# Spawn the actor\n'
            f'actor = subsystem.spawn_actor_from_class(\n'
            f'    unreal.EditorAssetLibrary.load_asset("/Script/Engine.{actor_class}"),\n'
            f'    {_vec3(*location)},\n'
            f'    {_rot3(*rotation)}\n'
            f')\n'
            f'\n'
            f'if actor:\n'
            f'    actor.set_actor_scale3d({_vec3(*scale)})\n'
        )
        if name:
            body += f'    actor.set_actor_label("{name}")\n'
        body += (
            f'    log(f"Spawned: {{actor.get_actor_label()}} at {{actor.get_actor_location()}}")\n'
            f'else:\n'
            f'    log("Failed to spawn actor", "error")\n'
        )
        return _header(f"Spawn {actor_class}") + _wrap_try(body)

    def spawn_blueprint(self, blueprint_path: str, name: str = "",
                        location: tuple = (0, 0, 0),
                        rotation: tuple = (0, 0, 0),
                        scale: tuple = (1, 1, 1)) -> str:
        """Spawn a Blueprint actor in the current level."""
        logger.info(f"Generating spawn BP script: {blueprint_path}")
        body = (
            f'log("Spawning Blueprint actor: {blueprint_path}")\n'
            f'\n'
            f'# Load the Blueprint asset\n'
            f'bp_asset = unreal.EditorAssetLibrary.load_asset("{blueprint_path}")\n'
            f'if not bp_asset:\n'
            f'    log("Failed to load Blueprint: {blueprint_path}", "error")\n'
            f'    raise RuntimeError("Blueprint not found")\n'
            f'\n'
            f'# Get the generated class\n'
            f'bp_class = unreal.load_object(None, bp_asset.generated_class().get_path_name())\n'
            f'\n'
            f'# Spawn via EditorActorSubsystem\n'
            f'subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n'
            f'actor = subsystem.spawn_actor_from_class(\n'
            f'    bp_class,\n'
            f'    {_vec3(*location)},\n'
            f'    {_rot3(*rotation)}\n'
            f')\n'
            f'\n'
            f'if actor:\n'
            f'    actor.set_actor_scale3d({_vec3(*scale)})\n'
        )
        if name:
            body += f'    actor.set_actor_label("{name}")\n'
        body += (
            f'    log(f"Spawned BP: {{actor.get_actor_label()}} ({{actor.get_class().get_name()}})")\n'
            f'else:\n'
            f'    log("Failed to spawn Blueprint actor", "error")\n'
        )
        return _header(f"Spawn Blueprint: {blueprint_path}") + _wrap_try(body)

    def delete(self, actor_label: str) -> str:
        """Delete an actor by label."""
        logger.info(f"Generating delete script: {actor_label}")
        body = (
            f'log("Deleting actor: {actor_label}")\n'
            f'\n'
            f'# Find the actor by label\n'
            f'subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n'
            f'all_actors = subsystem.get_all_level_actors()\n'
            f'target = None\n'
            f'for a in all_actors:\n'
            f'    if a.get_actor_label() == "{actor_label}":\n'
            f'        target = a\n'
            f'        break\n'
            f'\n'
            f'if target:\n'
            f'    subsystem.destroy_actor(target)\n'
            f'    log(f"Deleted: {actor_label}")\n'
            f'else:\n'
            f'    log("Actor not found: {actor_label}", "warning")\n'
        )
        return _header(f"Delete Actor: {actor_label}") + _wrap_try(body)

    def set_property(self, actor_label: str, property_name: str,
                     value: str, value_type: str = "str") -> str:
        """Set a property on an actor by label."""
        logger.info(f"Generating set_property script: {actor_label}.{property_name}")

        # Determine value assignment based on type
        if value_type == "float":
            val_str = f'{float(value)}'
        elif value_type == "int":
            val_str = f'{int(value)}'
        elif value_type == "bool":
            val_str = f'{value.capitalize()}'
        elif value_type == "vector":
            parts = [float(x.strip()) for x in value.split(',')]
            val_str = _vec3(*parts)
        else:
            val_str = f'"{value}"'

        body = (
            f'log("Setting {property_name} on {actor_label}")\n'
            f'\n'
            f'# Find the actor\n'
            f'subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n'
            f'all_actors = subsystem.get_all_level_actors()\n'
            f'target = None\n'
            f'for a in all_actors:\n'
            f'    if a.get_actor_label() == "{actor_label}":\n'
            f'        target = a\n'
            f'        break\n'
            f'\n'
            f'if not target:\n'
            f'    log("Actor not found: {actor_label}", "error")\n'
            f'    raise RuntimeError("Actor not found")\n'
            f'\n'
            f'# Set the property\n'
            f'target.set_editor_property("{property_name}", {val_str})\n'
            f'log(f"Set {property_name} = {val_str} on {actor_label}")\n'
        )
        return _header(f"Set Property: {actor_label}.{property_name}") + _wrap_try(body)

    def move(self, actor_label: str, location: tuple = (0, 0, 0),
             rotation: tuple = None) -> str:
        """Move an actor to a new location (and optionally rotation)."""
        logger.info(f"Generating move script: {actor_label}")
        body = (
            f'log("Moving actor: {actor_label}")\n'
            f'\n'
            f'subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n'
            f'all_actors = subsystem.get_all_level_actors()\n'
            f'target = None\n'
            f'for a in all_actors:\n'
            f'    if a.get_actor_label() == "{actor_label}":\n'
            f'        target = a\n'
            f'        break\n'
            f'\n'
            f'if not target:\n'
            f'    log("Actor not found: {actor_label}", "error")\n'
            f'    raise RuntimeError("Actor not found")\n'
            f'\n'
            f'target.set_actor_location({_vec3(*location)}, sweep=False, teleport=True)\n'
        )
        if rotation:
            body += f'target.set_actor_rotation({_rot3(*rotation)}, teleport_physics=True)\n'
        body += f'log(f"Moved {actor_label} to {{target.get_actor_location()}}")\n'
        return _header(f"Move Actor: {actor_label}") + _wrap_try(body)

    def duplicate(self, actor_label: str, new_name: str = "",
                  offset: tuple = (100, 0, 0)) -> str:
        """Duplicate an actor with an offset."""
        logger.info(f"Generating duplicate script: {actor_label}")
        body = (
            f'log("Duplicating actor: {actor_label}")\n'
            f'\n'
            f'subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n'
            f'all_actors = subsystem.get_all_level_actors()\n'
            f'target = None\n'
            f'for a in all_actors:\n'
            f'    if a.get_actor_label() == "{actor_label}":\n'
            f'        target = a\n'
            f'        break\n'
            f'\n'
            f'if not target:\n'
            f'    log("Actor not found: {actor_label}", "error")\n'
            f'    raise RuntimeError("Actor not found")\n'
            f'\n'
            f'# Duplicate via editor subsystem\n'
            f'subsystem.set_selected_level_actors([target])\n'
            f'subsystem.duplicate_selected_actors()\n'
            f'\n'
            f'# Get the new actor (last selected)\n'
            f'new_actors = subsystem.get_selected_level_actors()\n'
            f'if new_actors:\n'
            f'    new_actor = new_actors[0]\n'
            f'    # Apply offset\n'
            f'    orig_loc = target.get_actor_location()\n'
            f'    offset = {_vec3(*offset)}\n'
            f'    new_actor.set_actor_location(orig_loc + offset, sweep=False, teleport=True)\n'
        )
        if new_name:
            body += f'    new_actor.set_actor_label("{new_name}")\n'
        body += (
            f'    log(f"Duplicated: {{new_actor.get_actor_label()}} at {{new_actor.get_actor_location()}}")\n'
            f'else:\n'
            f'    log("Duplication failed", "error")\n'
        )
        return _header(f"Duplicate Actor: {actor_label}") + _wrap_try(body)

    def list_all(self, class_filter: str = "") -> str:
        """List all actors in the current level, optionally filtered by class."""
        logger.info(f"Generating list_all script, filter={class_filter}")
        body = (
            f'log("Listing all actors in current level")\n'
            f'\n'
            f'subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n'
            f'all_actors = subsystem.get_all_level_actors()\n'
            f'\n'
            f'log(f"Total actors: {{len(all_actors)}}")\n'
            f'for i, actor in enumerate(all_actors):\n'
            f'    class_name = actor.get_class().get_name()\n'
        )
        if class_filter:
            body += f'    if "{class_filter}" not in class_name:\n        continue\n'
        body += (
            f'    label = actor.get_actor_label()\n'
            f'    loc = actor.get_actor_location()\n'
            f'    log(f"  [{{i}}] {{label}} ({{class_name}}) at {{loc}}")\n'
        )
        return _header("List All Actors") + _wrap_try(body)

    def batch_set_property(self, class_filter: str, property_name: str,
                           value: str, value_type: str = "str") -> str:
        """Set a property on all actors matching a class filter."""
        logger.info(f"Generating batch_set_property: {class_filter}.{property_name}")

        if value_type == "float":
            val_str = f'{float(value)}'
        elif value_type == "int":
            val_str = f'{int(value)}'
        elif value_type == "bool":
            val_str = f'{value.capitalize()}'
        else:
            val_str = f'"{value}"'

        body = (
            f'log("Batch setting {property_name} on all {class_filter} actors")\n'
            f'\n'
            f'subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n'
            f'all_actors = subsystem.get_all_level_actors()\n'
            f'count = 0\n'
            f'\n'
            f'for actor in all_actors:\n'
            f'    if "{class_filter}" in actor.get_class().get_name():\n'
            f'        actor.set_editor_property("{property_name}", {val_str})\n'
            f'        count += 1\n'
            f'        log(f"  Set on: {{actor.get_actor_label()}}")\n'
            f'\n'
            f'log(f"Updated {{count}} actors")\n'
        )
        return _header(f"Batch Set: {class_filter}.{property_name}") + _wrap_try(body)


# =============================================================================
# Domain: Assets
# =============================================================================

class AssetScripts:
    """Generate scripts for asset operations."""

    def load(self, asset_path: str) -> str:
        """Load an asset and print its properties."""
        logger.info(f"Generating load script: {asset_path}")
        body = (
            f'log("Loading asset: {asset_path}")\n'
            f'\n'
            f'asset = unreal.EditorAssetLibrary.load_asset("{asset_path}")\n'
            f'if not asset:\n'
            f'    log("Asset not found: {asset_path}", "error")\n'
            f'    raise RuntimeError("Asset not found")\n'
            f'\n'
            f'log(f"Loaded: {{asset.get_name()}} ({{asset.get_class().get_name()}})")\n'
            f'log(f"Path: {{asset.get_path_name()}}")\n'
        )
        return _header(f"Load Asset: {asset_path}") + _wrap_try(body)

    def duplicate(self, source_path: str, dest_path: str) -> str:
        """Duplicate an asset to a new path."""
        logger.info(f"Generating duplicate script: {source_path} -> {dest_path}")
        body = (
            f'log("Duplicating asset: {source_path} -> {dest_path}")\n'
            f'\n'
            f'# Check source exists\n'
            f'if not unreal.EditorAssetLibrary.does_asset_exist("{source_path}"):\n'
            f'    log("Source not found: {source_path}", "error")\n'
            f'    raise RuntimeError("Source asset not found")\n'
            f'\n'
            f'# Check dest doesn\'t exist\n'
            f'if unreal.EditorAssetLibrary.does_asset_exist("{dest_path}"):\n'
            f'    log("Destination already exists: {dest_path}", "warning")\n'
            f'\n'
            f'result = unreal.EditorAssetLibrary.duplicate_asset("{source_path}", "{dest_path}")\n'
            f'if result:\n'
            f'    log(f"Duplicated to: {dest_path}")\n'
            f'else:\n'
            f'    log("Duplication failed", "error")\n'
        )
        return _header(f"Duplicate Asset") + _wrap_try(body)

    def delete(self, asset_path: str) -> str:
        """Delete an asset (with reference check)."""
        logger.info(f"Generating delete script: {asset_path}")
        body = (
            f'log("Deleting asset: {asset_path}")\n'
            f'\n'
            f'if not unreal.EditorAssetLibrary.does_asset_exist("{asset_path}"):\n'
            f'    log("Asset not found: {asset_path}", "warning")\n'
            f'else:\n'
            f'    # Check for references first\n'
            f'    refs = unreal.EditorAssetLibrary.find_package_referencers_for_asset("{asset_path}")\n'
            f'    if refs:\n'
            f'        log(f"WARNING: Asset has {{len(refs)}} referencers. Deleting anyway.", "warning")\n'
            f'        for r in refs:\n'
            f'            log(f"  Referenced by: {{r}}", "warning")\n'
            f'\n'
            f'    result = unreal.EditorAssetLibrary.delete_asset("{asset_path}")\n'
            f'    if result:\n'
            f'        log(f"Deleted: {asset_path}")\n'
            f'    else:\n'
            f'        log("Delete failed", "error")\n'
        )
        return _header(f"Delete Asset: {asset_path}") + _wrap_try(body)

    def rename(self, source_path: str, dest_path: str) -> str:
        """Rename/move an asset with fixup redirectors."""
        logger.info(f"Generating rename script: {source_path} -> {dest_path}")
        body = (
            f'log("Renaming asset: {source_path} -> {dest_path}")\n'
            f'\n'
            f'result = unreal.EditorAssetLibrary.rename_asset("{source_path}", "{dest_path}")\n'
            f'if result:\n'
            f'    log(f"Renamed to: {dest_path}")\n'
            f'    # Fix up redirectors\n'
            f'    unreal.EditorAssetLibrary.consolidate_assets(\n'
            f'        unreal.EditorAssetLibrary.load_asset("{dest_path}"), []\n'
            f'    )\n'
            f'    log("Redirectors consolidated")\n'
            f'else:\n'
            f'    log("Rename failed", "error")\n'
        )
        return _header(f"Rename Asset") + _wrap_try(body)

    def save_all_dirty(self) -> str:
        """Save all modified assets."""
        logger.info("Generating save_all_dirty script")
        body = (
            f'log("Saving all dirty assets...")\n'
            f'\n'
            f'unreal.EditorAssetLibrary.save_loaded_assets()\n'
            f'log("All dirty assets saved")\n'
        )
        return _header("Save All Dirty Assets") + _wrap_try(body)

    def list_assets(self, directory: str, recursive: bool = True,
                    class_filter: str = "") -> str:
        """List all assets in a directory."""
        logger.info(f"Generating list_assets script: {directory}")
        body = (
            f'log("Listing assets in: {directory}")\n'
            f'\n'
            f'assets = unreal.EditorAssetLibrary.list_assets(\n'
            f'    "{directory}",\n'
            f'    recursive={recursive},\n'
            f'    include_folder=False\n'
            f')\n'
            f'\n'
            f'log(f"Found {{len(assets)}} assets")\n'
            f'for path in assets:\n'
        )
        if class_filter:
            body += (
                f'    asset = unreal.EditorAssetLibrary.load_asset(path)\n'
                f'    if asset and "{class_filter}" in asset.get_class().get_name():\n'
                f'        log(f"  {{path}} ({{asset.get_class().get_name()}})")\n'
            )
        else:
            body += f'    log(f"  {{path}}")\n'
        return _header(f"List Assets: {directory}") + _wrap_try(body)


# =============================================================================
# Domain: Level Sequences
# =============================================================================

class SequenceScripts:
    """Generate scripts for Level Sequence operations."""

    def create(self, name: str, save_path: str) -> str:
        """Create a new Level Sequence asset."""
        logger.info(f"Generating create sequence script: {name}")
        body = (
            f'log("Creating Level Sequence: {name}")\n'
            f'\n'
            f'# Create the asset via AssetTools\n'
            f'asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n'
            f'factory = unreal.LevelSequenceFactoryNew()\n'
            f'sequence = asset_tools.create_asset(\n'
            f'    "{name}",\n'
            f'    "{save_path}",\n'
            f'    unreal.LevelSequence,\n'
            f'    factory\n'
            f')\n'
            f'\n'
            f'if sequence:\n'
            f'    log(f"Created: {{sequence.get_path_name()}}")\n'
            f'else:\n'
            f'    log("Failed to create sequence", "error")\n'
        )
        return _header(f"Create Level Sequence: {name}") + _wrap_try(body)

    def add_track(self, sequence_path: str, actor_label: str,
                  track_type: str = "Transform") -> str:
        """Add a track to a Level Sequence bound to an actor."""
        logger.info(f"Generating add_track script: {track_type} on {actor_label}")

        # Map friendly names to UE class names
        track_map = {
            "Transform": "MovieScene3DTransformTrack",
            "Float": "MovieSceneFloatTrack",
            "Bool": "MovieSceneBoolTrack",
            "Visibility": "MovieSceneVisibilityTrack",
            "Event": "MovieSceneEventTrack",
            "Audio": "MovieSceneAudioTrack",
            "Skeletal": "MovieSceneSkeletalAnimationTrack",
            "Material": "MovieSceneMaterialParameterCollectionTrack",
            "Fade": "MovieSceneFadeTrack",
            "CameraCut": "MovieSceneCameraCutTrack",
        }
        ue_track = track_map.get(track_type, track_type)

        body = (
            f'log("Adding {track_type} track to {actor_label} in {sequence_path}")\n'
            f'\n'
            f'# Load the sequence\n'
            f'sequence = unreal.EditorAssetLibrary.load_asset("{sequence_path}")\n'
            f'if not sequence:\n'
            f'    log("Sequence not found: {sequence_path}", "error")\n'
            f'    raise RuntimeError("Sequence not found")\n'
            f'\n'
            f'# Find the actor in the level\n'
            f'subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n'
            f'all_actors = subsystem.get_all_level_actors()\n'
            f'target = None\n'
            f'for a in all_actors:\n'
            f'    if a.get_actor_label() == "{actor_label}":\n'
            f'        target = a\n'
            f'        break\n'
            f'\n'
            f'if not target:\n'
            f'    log("Actor not found: {actor_label}", "error")\n'
            f'    raise RuntimeError("Actor not found")\n'
            f'\n'
            f'# Get or create binding for the actor\n'
            f'movie_scene = sequence.get_movie_scene()\n'
            f'binding = sequence.add_possessable(target)\n'
            f'\n'
            f'# Add the track\n'
            f'track = binding.add_track(unreal.{ue_track})\n'
            f'if track:\n'
            f'    # Add a default section spanning the sequence range\n'
            f'    section = track.add_section()\n'
            f'    section.set_range(\n'
            f'        movie_scene.get_playback_start(),\n'
            f'        movie_scene.get_playback_end()\n'
            f'    )\n'
            f'    log(f"Added {track_type} track to {{target.get_actor_label()}}")\n'
            f'else:\n'
            f'    log("Failed to add track", "error")\n'
        )
        return _header(f"Add {track_type} Track") + _wrap_try(body)

    def add_keyframe(self, sequence_path: str, actor_label: str,
                     track_type: str, frame: int,
                     value: Union[float, tuple, bool]) -> str:
        """Add a keyframe to a track at a specific frame."""
        logger.info(f"Generating add_keyframe: frame {frame} on {actor_label}")
        body = (
            f'log("Adding keyframe at frame {frame} on {actor_label}")\n'
            f'\n'
            f'# Load the sequence\n'
            f'sequence = unreal.EditorAssetLibrary.load_asset("{sequence_path}")\n'
            f'movie_scene = sequence.get_movie_scene()\n'
            f'display_rate = movie_scene.get_display_rate()\n'
            f'\n'
            f'# Find the binding for the actor\n'
            f'bindings = sequence.get_bindings()\n'
            f'target_binding = None\n'
            f'for b in bindings:\n'
            f'    if b.get_display_name() == "{actor_label}":\n'
            f'        target_binding = b\n'
            f'        break\n'
            f'\n'
            f'if not target_binding:\n'
            f'    log("Binding not found for: {actor_label}", "error")\n'
            f'    raise RuntimeError("Binding not found")\n'
            f'\n'
            f'# Find the track\n'
            f'tracks = target_binding.get_tracks()\n'
            f'target_track = None\n'
            f'for t in tracks:\n'
            f'    if "{track_type}" in t.get_class().get_name():\n'
            f'        target_track = t\n'
            f'        break\n'
            f'\n'
            f'if not target_track:\n'
            f'    log("Track not found", "error")\n'
            f'    raise RuntimeError("Track not found")\n'
            f'\n'
            f'# Get the section and add the key\n'
            f'sections = target_track.get_sections()\n'
            f'if sections:\n'
            f'    section = sections[0]\n'
            f'    frame_num = unreal.FrameNumber({frame})\n'
            f'    channels = section.get_all_channels()\n'
            f'    for ch in channels:\n'
            f'        ch.add_key(unreal.FrameNumber({frame}), {value})\n'
            f'    log(f"Added keyframe at frame {frame}")\n'
            f'else:\n'
            f'    log("No sections found on track", "error")\n'
        )
        return _header(f"Add Keyframe: {actor_label} @ frame {frame}") + _wrap_try(body)

    def set_playback_range(self, sequence_path: str,
                           start_frame: int, end_frame: int) -> str:
        """Set the playback range of a sequence."""
        logger.info(f"Generating set_playback_range: {start_frame}-{end_frame}")
        body = (
            f'log("Setting playback range: {start_frame} - {end_frame}")\n'
            f'\n'
            f'sequence = unreal.EditorAssetLibrary.load_asset("{sequence_path}")\n'
            f'movie_scene = sequence.get_movie_scene()\n'
            f'\n'
            f'movie_scene.set_playback_start({start_frame})\n'
            f'movie_scene.set_playback_end({end_frame})\n'
            f'log(f"Playback range set: {start_frame} - {end_frame}")\n'
        )
        return _header(f"Set Playback Range") + _wrap_try(body)

    def bind_actor(self, sequence_path: str, actor_label: str) -> str:
        """Bind an actor to a Level Sequence (possessable)."""
        logger.info(f"Generating bind_actor: {actor_label}")
        body = (
            f'log("Binding actor to sequence: {actor_label}")\n'
            f'\n'
            f'sequence = unreal.EditorAssetLibrary.load_asset("{sequence_path}")\n'
            f'if not sequence:\n'
            f'    log("Sequence not found", "error")\n'
            f'    raise RuntimeError("Sequence not found")\n'
            f'\n'
            f'# Find the actor\n'
            f'subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n'
            f'all_actors = subsystem.get_all_level_actors()\n'
            f'target = None\n'
            f'for a in all_actors:\n'
            f'    if a.get_actor_label() == "{actor_label}":\n'
            f'        target = a\n'
            f'        break\n'
            f'\n'
            f'if not target:\n'
            f'    log("Actor not found: {actor_label}", "error")\n'
            f'    raise RuntimeError("Actor not found")\n'
            f'\n'
            f'binding = sequence.add_possessable(target)\n'
            f'log(f"Bound {{target.get_actor_label()}} to sequence (ID: {{binding.get_id()}})")\n'
        )
        return _header(f"Bind Actor: {actor_label}") + _wrap_try(body)

    def list_bindings(self, sequence_path: str) -> str:
        """List all bindings in a Level Sequence."""
        logger.info(f"Generating list_bindings: {sequence_path}")
        body = (
            f'log("Listing bindings in: {sequence_path}")\n'
            f'\n'
            f'sequence = unreal.EditorAssetLibrary.load_asset("{sequence_path}")\n'
            f'bindings = sequence.get_bindings()\n'
            f'\n'
            f'log(f"Total bindings: {{len(bindings)}}")\n'
            f'for i, b in enumerate(bindings):\n'
            f'    tracks = b.get_tracks()\n'
            f'    track_names = [t.get_class().get_name() for t in tracks]\n'
            f'    log(f"  [{{i}}] {{b.get_display_name()}} — {{len(tracks)}} tracks: {{track_names}}")\n'
        )
        return _header(f"List Sequence Bindings") + _wrap_try(body)


# =============================================================================
# Domain: Materials
# =============================================================================

class MaterialScripts:
    """Generate scripts for material operations."""

    def set_parameter(self, material_path: str, param_name: str,
                      value: Union[float, tuple], param_type: str = "scalar") -> str:
        """Set a material parameter (scalar or vector)."""
        logger.info(f"Generating set_parameter: {material_path}.{param_name}")

        if param_type == "scalar":
            set_line = (
                f'instance.set_editor_property("{param_name}", {float(value)})\n'
            )
        elif param_type == "vector":
            r, g, b, a = value if len(value) == 4 else (*value, 1.0)
            set_line = (
                f'color = unreal.LinearColor({r}, {g}, {b}, {a})\n'
                f'instance.set_vector_parameter_value("{param_name}", color)\n'
            )
        elif param_type == "texture":
            set_line = (
                f'tex = unreal.EditorAssetLibrary.load_asset("{value}")\n'
                f'instance.set_texture_parameter_value("{param_name}", tex)\n'
            )
        else:
            set_line = f'# Unknown param type: {param_type}\n'

        body = (
            f'log("Setting material parameter: {param_name} on {material_path}")\n'
            f'\n'
            f'instance = unreal.EditorAssetLibrary.load_asset("{material_path}")\n'
            f'if not instance:\n'
            f'    log("Material not found: {material_path}", "error")\n'
            f'    raise RuntimeError("Material not found")\n'
            f'\n'
            f'{set_line}'
            f'log(f"Set {param_name} on {material_path}")\n'
        )
        return _header(f"Set Material Parameter: {param_name}") + _wrap_try(body)

    def create_instance(self, parent_path: str, instance_name: str,
                        save_path: str) -> str:
        """Create a Material Instance from a parent material."""
        logger.info(f"Generating create_instance: {instance_name}")
        body = (
            f'log("Creating Material Instance: {instance_name}")\n'
            f'\n'
            f'asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n'
            f'factory = unreal.MaterialInstanceConstantFactoryNew()\n'
            f'\n'
            f'# Set the parent material\n'
            f'parent = unreal.EditorAssetLibrary.load_asset("{parent_path}")\n'
            f'if not parent:\n'
            f'    log("Parent material not found: {parent_path}", "error")\n'
            f'    raise RuntimeError("Parent not found")\n'
            f'\n'
            f'factory.set_editor_property("initial_parent", parent)\n'
            f'\n'
            f'instance = asset_tools.create_asset(\n'
            f'    "{instance_name}",\n'
            f'    "{save_path}",\n'
            f'    unreal.MaterialInstanceConstant,\n'
            f'    factory\n'
            f')\n'
            f'\n'
            f'if instance:\n'
            f'    log(f"Created: {{instance.get_path_name()}}")\n'
            f'else:\n'
            f'    log("Failed to create material instance", "error")\n'
        )
        return _header(f"Create Material Instance: {instance_name}") + _wrap_try(body)

    def assign_to_actor(self, actor_label: str, material_path: str,
                        slot_index: int = 0) -> str:
        """Assign a material to an actor's mesh component."""
        logger.info(f"Generating assign_to_actor: {material_path} -> {actor_label}")
        body = (
            f'log("Assigning material to {actor_label}")\n'
            f'\n'
            f'# Find the actor\n'
            f'subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n'
            f'all_actors = subsystem.get_all_level_actors()\n'
            f'target = None\n'
            f'for a in all_actors:\n'
            f'    if a.get_actor_label() == "{actor_label}":\n'
            f'        target = a\n'
            f'        break\n'
            f'\n'
            f'if not target:\n'
            f'    log("Actor not found: {actor_label}", "error")\n'
            f'    raise RuntimeError("Actor not found")\n'
            f'\n'
            f'# Load the material\n'
            f'material = unreal.EditorAssetLibrary.load_asset("{material_path}")\n'
            f'if not material:\n'
            f'    log("Material not found: {material_path}", "error")\n'
            f'    raise RuntimeError("Material not found")\n'
            f'\n'
            f'# Find the mesh component\n'
            f'components = target.get_components_by_class(unreal.MeshComponent)\n'
            f'if components:\n'
            f'    components[0].set_material({slot_index}, material)\n'
            f'    log(f"Assigned material to slot {slot_index} on {{target.get_actor_label()}}")\n'
            f'else:\n'
            f'    log("No mesh component found on actor", "error")\n'
        )
        return _header(f"Assign Material to Actor") + _wrap_try(body)


# =============================================================================
# Domain: Blueprints
# =============================================================================

class BlueprintScripts:
    """Generate scripts for Blueprint operations."""

    def compile(self, blueprint_path: str) -> str:
        """Compile a Blueprint asset."""
        logger.info(f"Generating compile script: {blueprint_path}")
        body = (
            f'log("Compiling Blueprint: {blueprint_path}")\n'
            f'\n'
            f'bp = unreal.EditorAssetLibrary.load_asset("{blueprint_path}")\n'
            f'if not bp:\n'
            f'    log("Blueprint not found: {blueprint_path}", "error")\n'
            f'    raise RuntimeError("Blueprint not found")\n'
            f'\n'
            f'# Compile\n'
            f'unreal.KismetSystemLibrary.compile_blueprint(bp)\n'
            f'log(f"Compiled: {{bp.get_name()}}")\n'
            f'\n'
            f'# Save\n'
            f'unreal.EditorAssetLibrary.save_asset(bp.get_path_name())\n'
            f'log("Saved")\n'
        )
        return _header(f"Compile Blueprint: {blueprint_path}") + _wrap_try(body)

    def add_variable(self, blueprint_path: str, var_name: str,
                     var_type: str = "float", default_value: str = "") -> str:
        """Add a variable to a Blueprint."""
        logger.info(f"Generating add_variable: {var_name} ({var_type})")

        # Map friendly type names to UE property types
        type_map = {
            "float": "unreal.FloatProperty",
            "int": "unreal.IntProperty",
            "bool": "unreal.BoolProperty",
            "string": "unreal.StrProperty",
            "vector": "unreal.StructProperty",
            "rotator": "unreal.StructProperty",
            "name": "unreal.NameProperty",
        }

        body = (
            f'log("Adding variable {var_name} ({var_type}) to {blueprint_path}")\n'
            f'\n'
            f'bp = unreal.EditorAssetLibrary.load_asset("{blueprint_path}")\n'
            f'if not bp:\n'
            f'    log("Blueprint not found", "error")\n'
            f'    raise RuntimeError("Blueprint not found")\n'
            f'\n'
            f'# Add the variable via the Blueprint editor library\n'
            f'# Note: This uses the subsystem approach for UE 5.6\n'
            f'subsystem = unreal.get_editor_subsystem(unreal.SubobjectDataSubsystem)\n'
            f'log("Variable addition requires the C++ plugin (JarvisGraphEditor) for full support.")\n'
            f'log("Use the Blueprint editor manually or compile the C++ plugin.")\n'
        )
        return _header(f"Add Variable: {var_name}") + _wrap_try(body)

    def set_variable_default(self, blueprint_path: str, var_name: str,
                             value: str) -> str:
        """Set the default value of a Blueprint variable."""
        logger.info(f"Generating set_variable_default: {var_name} = {value}")
        body = (
            f'log("Setting default value: {var_name} = {value}")\n'
            f'\n'
            f'bp = unreal.EditorAssetLibrary.load_asset("{blueprint_path}")\n'
            f'if not bp:\n'
            f'    log("Blueprint not found", "error")\n'
            f'    raise RuntimeError("Blueprint not found")\n'
            f'\n'
            f'# Get the CDO (Class Default Object)\n'
            f'cdo = unreal.get_default_object(bp.generated_class())\n'
            f'cdo.set_editor_property("{var_name}", {value})\n'
            f'log(f"Set {var_name} = {value} on CDO")\n'
            f'\n'
            f'# Compile and save\n'
            f'unreal.KismetSystemLibrary.compile_blueprint(bp)\n'
            f'unreal.EditorAssetLibrary.save_asset(bp.get_path_name())\n'
            f'log("Compiled and saved")\n'
        )
        return _header(f"Set Variable Default: {var_name}") + _wrap_try(body)

    def create(self, name: str, save_path: str,
               parent_class: str = "Actor") -> str:
        """Create a new Blueprint asset."""
        logger.info(f"Generating create BP: {name} (parent: {parent_class})")
        body = (
            f'log("Creating Blueprint: {name} (parent: {parent_class})")\n'
            f'\n'
            f'asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n'
            f'factory = unreal.BlueprintFactory()\n'
            f'factory.set_editor_property("parent_class", unreal.{parent_class}.static_class())\n'
            f'\n'
            f'bp = asset_tools.create_asset(\n'
            f'    "{name}",\n'
            f'    "{save_path}",\n'
            f'    unreal.Blueprint,\n'
            f'    factory\n'
            f')\n'
            f'\n'
            f'if bp:\n'
            f'    log(f"Created: {{bp.get_path_name()}}")\n'
            f'else:\n'
            f'    log("Failed to create Blueprint", "error")\n'
        )
        return _header(f"Create Blueprint: {name}") + _wrap_try(body)

    def reparent(self, blueprint_path: str, new_parent_class: str) -> str:
        """Change the parent class of a Blueprint."""
        logger.info(f"Generating reparent: {blueprint_path} -> {new_parent_class}")
        body = (
            f'log("Reparenting Blueprint: {blueprint_path} -> {new_parent_class}")\n'
            f'\n'
            f'bp = unreal.EditorAssetLibrary.load_asset("{blueprint_path}")\n'
            f'if not bp:\n'
            f'    log("Blueprint not found", "error")\n'
            f'    raise RuntimeError("Blueprint not found")\n'
            f'\n'
            f'# Reparent\n'
            f'new_parent = unreal.{new_parent_class}.static_class()\n'
            f'bp.set_editor_property("parent_class", new_parent)\n'
            f'\n'
            f'# Compile and save\n'
            f'unreal.KismetSystemLibrary.compile_blueprint(bp)\n'
            f'unreal.EditorAssetLibrary.save_asset(bp.get_path_name())\n'
            f'log("Reparented, compiled, and saved")\n'
        )
        return _header(f"Reparent Blueprint") + _wrap_try(body)


# =============================================================================
# Domain: Animation
# =============================================================================

class AnimationScripts:
    """Generate scripts for animation and skeletal mesh operations."""

    def retarget(self, source_anim: str, target_skeleton: str,
                 save_path: str) -> str:
        """Retarget an animation to a different skeleton."""
        logger.info(f"Generating retarget: {source_anim} -> {target_skeleton}")
        body = (
            f'log("Retargeting animation: {source_anim}")\n'
            f'\n'
            f'# Load source animation\n'
            f'source = unreal.EditorAssetLibrary.load_asset("{source_anim}")\n'
            f'if not source:\n'
            f'    log("Source animation not found", "error")\n'
            f'    raise RuntimeError("Source not found")\n'
            f'\n'
            f'# Load target skeleton\n'
            f'target_skel = unreal.EditorAssetLibrary.load_asset("{target_skeleton}")\n'
            f'if not target_skel:\n'
            f'    log("Target skeleton not found", "error")\n'
            f'    raise RuntimeError("Target skeleton not found")\n'
            f'\n'
            f'log("Retargeting requires the Animation Editor or IK Retargeter asset.")\n'
            f'log("Use unreal.IKRetargeter for UE 5.6 retargeting pipeline.")\n'
        )
        return _header(f"Retarget Animation") + _wrap_try(body)

    def import_fbx(self, fbx_path: str, save_path: str,
                   skeleton_path: str = "") -> str:
        """Import an FBX animation file."""
        logger.info(f"Generating import_fbx: {fbx_path}")
        body = (
            f'log("Importing FBX: {fbx_path}")\n'
            f'\n'
            f'# Set up import task\n'
            f'task = unreal.AssetImportTask()\n'
            f'task.set_editor_property("filename", "{fbx_path}")\n'
            f'task.set_editor_property("destination_path", "{save_path}")\n'
            f'task.set_editor_property("automated", True)\n'
            f'task.set_editor_property("replace_existing", True)\n'
            f'task.set_editor_property("save", True)\n'
            f'\n'
            f'# FBX import options\n'
            f'options = unreal.FbxImportUI()\n'
            f'options.set_editor_property("import_mesh", False)\n'
            f'options.set_editor_property("import_animations", True)\n'
            f'options.set_editor_property("import_as_skeletal", True)\n'
        )
        if skeleton_path:
            body += (
                f'options.set_editor_property("skeleton",\n'
                f'    unreal.EditorAssetLibrary.load_asset("{skeleton_path}"))\n'
            )
        body += (
            f'\n'
            f'task.set_editor_property("options", options)\n'
            f'\n'
            f'# Execute import\n'
            f'unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])\n'
            f'\n'
            f'if task.get_editor_property("imported_object_paths"):\n'
            f'    for path in task.get_editor_property("imported_object_paths"):\n'
            f'        log(f"Imported: {{path}}")\n'
            f'else:\n'
            f'    log("Import may have failed — check Output Log", "warning")\n'
        )
        return _header(f"Import FBX Animation") + _wrap_try(body)

    def set_anim_on_skeletal_mesh(self, actor_label: str,
                                  anim_path: str) -> str:
        """Set an animation asset on a skeletal mesh actor."""
        logger.info(f"Generating set_anim: {anim_path} on {actor_label}")
        body = (
            f'log("Setting animation on {actor_label}")\n'
            f'\n'
            f'# Find the actor\n'
            f'subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n'
            f'all_actors = subsystem.get_all_level_actors()\n'
            f'target = None\n'
            f'for a in all_actors:\n'
            f'    if a.get_actor_label() == "{actor_label}":\n'
            f'        target = a\n'
            f'        break\n'
            f'\n'
            f'if not target:\n'
            f'    log("Actor not found", "error")\n'
            f'    raise RuntimeError("Actor not found")\n'
            f'\n'
            f'# Load the animation\n'
            f'anim = unreal.EditorAssetLibrary.load_asset("{anim_path}")\n'
            f'\n'
            f'# Get the skeletal mesh component\n'
            f'skel_comps = target.get_components_by_class(unreal.SkeletalMeshComponent)\n'
            f'if skel_comps:\n'
            f'    skel_comps[0].set_animation(anim)\n'
            f'    skel_comps[0].play_animation(anim, True)\n'
            f'    log(f"Set animation on {{target.get_actor_label()}}")\n'
            f'else:\n'
            f'    log("No SkeletalMeshComponent found", "error")\n'
        )
        return _header(f"Set Animation on Actor") + _wrap_try(body)


# =============================================================================
# Domain: Data Tables
# =============================================================================

class DataTableScripts:
    """Generate scripts for Data Table operations."""

    def list_rows(self, table_path: str) -> str:
        """List all rows in a Data Table."""
        logger.info(f"Generating list_rows: {table_path}")
        body = (
            f'log("Listing rows in: {table_path}")\n'
            f'\n'
            f'table = unreal.EditorAssetLibrary.load_asset("{table_path}")\n'
            f'if not table:\n'
            f'    log("Data Table not found", "error")\n'
            f'    raise RuntimeError("Data Table not found")\n'
            f'\n'
            f'row_names = unreal.DataTableFunctionLibrary.get_data_table_row_names(table)\n'
            f'log(f"Total rows: {{len(row_names)}}")\n'
            f'for name in row_names:\n'
            f'    log(f"  {{name}}")\n'
        )
        return _header(f"List Data Table Rows") + _wrap_try(body)

    def get_row(self, table_path: str, row_name: str) -> str:
        """Get a specific row from a Data Table."""
        logger.info(f"Generating get_row: {table_path}[{row_name}]")
        body = (
            f'log("Getting row: {row_name} from {table_path}")\n'
            f'\n'
            f'table = unreal.EditorAssetLibrary.load_asset("{table_path}")\n'
            f'if not table:\n'
            f'    log("Data Table not found", "error")\n'
            f'    raise RuntimeError("Data Table not found")\n'
            f'\n'
            f'# Get row as JSON string for inspection\n'
            f'json_str = unreal.DataTableFunctionLibrary.get_data_table_row_from_name(table, "{row_name}")\n'
            f'log(f"Row {row_name}: {{json_str}}")\n'
        )
        return _header(f"Get Data Table Row: {row_name}") + _wrap_try(body)


# =============================================================================
# Domain: Levels / World
# =============================================================================

class LevelScripts:
    """Generate scripts for level and world operations."""

    def load_level(self, level_path: str) -> str:
        """Load a level in the editor."""
        logger.info(f"Generating load_level: {level_path}")
        body = (
            f'log("Loading level: {level_path}")\n'
            f'\n'
            f'success = unreal.EditorLevelLibrary.load_level("{level_path}")\n'
            f'if success:\n'
            f'    log("Level loaded successfully")\n'
            f'else:\n'
            f'    log("Failed to load level", "error")\n'
        )
        return _header(f"Load Level: {level_path}") + _wrap_try(body)

    def save_current_level(self) -> str:
        """Save the current level."""
        logger.info("Generating save_current_level")
        body = (
            f'log("Saving current level...")\n'
            f'\n'
            f'unreal.EditorLevelLibrary.save_current_level()\n'
            f'log("Level saved")\n'
        )
        return _header("Save Current Level") + _wrap_try(body)

    def add_streaming_level(self, level_path: str,
                            transform: tuple = (0, 0, 0)) -> str:
        """Add a streaming sub-level to the current world."""
        logger.info(f"Generating add_streaming_level: {level_path}")
        body = (
            f'log("Adding streaming level: {level_path}")\n'
            f'\n'
            f'world = unreal.EditorLevelLibrary.get_editor_world()\n'
            f'levels_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)\n'
            f'\n'
            f'# Add the sub-level\n'
            f'streaming_level = unreal.EditorLevelUtils.add_level_to_world(\n'
            f'    world,\n'
            f'    "{level_path}",\n'
            f'    unreal.LevelStreamingDynamic.static_class()\n'
            f')\n'
            f'\n'
            f'if streaming_level:\n'
            f'    streaming_level.set_editor_property("level_transform",\n'
            f'        unreal.Transform(location={_vec3(*transform)}))\n'
            f'    log(f"Added streaming level: {{streaming_level.get_world_asset_package_name()}}")\n'
            f'else:\n'
            f'    log("Failed to add streaming level", "error")\n'
        )
        return _header(f"Add Streaming Level") + _wrap_try(body)

    def list_streaming_levels(self) -> str:
        """List all streaming levels in the current world."""
        logger.info("Generating list_streaming_levels")
        body = (
            f'log("Listing streaming levels...")\n'
            f'\n'
            f'world = unreal.EditorLevelLibrary.get_editor_world()\n'
            f'streaming_levels = world.get_streaming_levels()\n'
            f'\n'
            f'log(f"Total streaming levels: {{len(streaming_levels)}}")\n'
            f'for i, sl in enumerate(streaming_levels):\n'
            f'    name = sl.get_world_asset_package_name()\n'
            f'    loaded = sl.is_level_loaded()\n'
            f'    visible = sl.is_level_visible()\n'
            f'    log(f"  [{{i}}] {{name}} — loaded={{loaded}}, visible={{visible}}")\n'
        )
        return _header("List Streaming Levels") + _wrap_try(body)

    def set_level_visibility(self, level_name: str, visible: bool) -> str:
        """Set the visibility of a streaming level."""
        logger.info(f"Generating set_level_visibility: {level_name} = {visible}")
        body = (
            f'log("Setting level visibility: {level_name} = {visible}")\n'
            f'\n'
            f'world = unreal.EditorLevelLibrary.get_editor_world()\n'
            f'streaming_levels = world.get_streaming_levels()\n'
            f'\n'
            f'found = False\n'
            f'for sl in streaming_levels:\n'
            f'    if "{level_name}" in sl.get_world_asset_package_name():\n'
            f'        sl.set_should_be_visible({visible})\n'
            f'        sl.set_should_be_loaded({visible})\n'
            f'        log(f"Set {{sl.get_world_asset_package_name()}} visible={visible}")\n'
            f'        found = True\n'
            f'        break\n'
            f'\n'
            f'if not found:\n'
            f'    log("Streaming level not found: {level_name}", "warning")\n'
        )
        return _header(f"Set Level Visibility") + _wrap_try(body)


# =============================================================================
# Domain: PCG (Procedural Content Generation)
# =============================================================================

class PCGScripts:
    """Generate scripts for PCG operations."""

    def execute_graph(self, actor_label: str) -> str:
        """Execute a PCG graph on an actor."""
        logger.info(f"Generating execute_graph: {actor_label}")
        body = (
            f'log("Executing PCG graph on: {actor_label}")\n'
            f'\n'
            f'# Find the PCG actor\n'
            f'subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n'
            f'all_actors = subsystem.get_all_level_actors()\n'
            f'target = None\n'
            f'for a in all_actors:\n'
            f'    if a.get_actor_label() == "{actor_label}":\n'
            f'        target = a\n'
            f'        break\n'
            f'\n'
            f'if not target:\n'
            f'    log("PCG actor not found", "error")\n'
            f'    raise RuntimeError("Actor not found")\n'
            f'\n'
            f'# Get the PCG component and execute\n'
            f'pcg_comps = target.get_components_by_class(unreal.PCGComponent)\n'
            f'if pcg_comps:\n'
            f'    pcg_comps[0].generate(force=True)\n'
            f'    log("PCG graph executed")\n'
            f'else:\n'
            f'    log("No PCGComponent found on actor", "error")\n'
        )
        return _header(f"Execute PCG Graph") + _wrap_try(body)

    def set_pcg_parameter(self, actor_label: str, param_name: str,
                          value: str, value_type: str = "float") -> str:
        """Set an override parameter on a PCG component."""
        logger.info(f"Generating set_pcg_parameter: {param_name}")

        if value_type == "float":
            val_str = f'{float(value)}'
        elif value_type == "int":
            val_str = f'{int(value)}'
        else:
            val_str = f'"{value}"'

        body = (
            f'log("Setting PCG parameter: {param_name} = {value}")\n'
            f'\n'
            f'subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n'
            f'all_actors = subsystem.get_all_level_actors()\n'
            f'target = None\n'
            f'for a in all_actors:\n'
            f'    if a.get_actor_label() == "{actor_label}":\n'
            f'        target = a\n'
            f'        break\n'
            f'\n'
            f'if not target:\n'
            f'    log("Actor not found", "error")\n'
            f'    raise RuntimeError("Actor not found")\n'
            f'\n'
            f'pcg_comps = target.get_components_by_class(unreal.PCGComponent)\n'
            f'if pcg_comps:\n'
            f'    # Set the parameter override\n'
            f'    pcg_comps[0].set_editor_property("{param_name}", {val_str})\n'
            f'    log(f"Set {param_name} = {val_str}")\n'
            f'else:\n'
            f'    log("No PCGComponent found", "error")\n'
        )
        return _header(f"Set PCG Parameter") + _wrap_try(body)


# =============================================================================
# Domain: Utility / Editor
# =============================================================================

class UtilityScripts:
    """Generate scripts for general editor utility operations."""

    def run_commandlet(self, commandlet: str, args: str = "") -> str:
        """Run an editor commandlet."""
        logger.info(f"Generating run_commandlet: {commandlet}")
        body = (
            f'log("Running commandlet: {commandlet}")\n'
            f'\n'
            f'unreal.SystemLibrary.execute_console_command(\n'
            f'    None,\n'
            f'    "{commandlet} {args}"\n'
            f')\n'
            f'log("Commandlet executed")\n'
        )
        return _header(f"Run Commandlet: {commandlet}") + _wrap_try(body)

    def build_lighting(self, quality: str = "Production") -> str:
        """Build lighting for the current level."""
        logger.info(f"Generating build_lighting: {quality}")
        body = (
            f'log("Building lighting (quality: {quality})")\n'
            f'\n'
            f'# Trigger lighting build via editor subsystem\n'
            f'level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)\n'
            f'level_editor.build_lighting()\n'
            f'log("Lighting build started — check Build panel for progress")\n'
        )
        return _header(f"Build Lighting ({quality})") + _wrap_try(body)

    def take_screenshot(self, filename: str, resolution_x: int = 1920,
                        resolution_y: int = 1080) -> str:
        """Take a high-res screenshot of the current viewport."""
        logger.info(f"Generating take_screenshot: {filename}")
        body = (
            f'log("Taking screenshot: {filename}")\n'
            f'\n'
            f'# Use the automation library for viewport capture\n'
            f'unreal.AutomationLibrary.take_high_res_screenshot(\n'
            f'    {resolution_x}, {resolution_y}, "{filename}"\n'
            f')\n'
            f'log(f"Screenshot saved: {filename}")\n'
        )
        return _header(f"Take Screenshot") + _wrap_try(body)

    def fix_redirectors(self, directory: str = "/Game") -> str:
        """Fix up all redirectors in a directory."""
        logger.info(f"Generating fix_redirectors: {directory}")
        body = (
            f'log("Fixing redirectors in: {directory}")\n'
            f'\n'
            f'# Find all redirectors\n'
            f'asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()\n'
            f'redirectors = asset_registry.get_assets_by_class("ObjectRedirector", True)\n'
            f'\n'
            f'paths = []\n'
            f'for asset_data in redirectors:\n'
            f'    path = str(asset_data.get_editor_property("package_name"))\n'
            f'    if path.startswith("{directory}"):\n'
            f'        paths.append(path)\n'
            f'\n'
            f'log(f"Found {{len(paths)}} redirectors")\n'
            f'\n'
            f'# Fix them\n'
            f'if paths:\n'
            f'    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n'
            f'    loaded = [unreal.EditorAssetLibrary.load_asset(p) for p in paths]\n'
            f'    loaded = [a for a in loaded if a]\n'
            f'    if loaded:\n'
            f'        unreal.EditorAssetLibrary.consolidate_assets(loaded[0], loaded[1:])\n'
            f'    log("Redirectors fixed")\n'
            f'else:\n'
            f'    log("No redirectors found")\n'
        )
        return _header(f"Fix Redirectors") + _wrap_try(body)

    def custom(self, title: str, code: str) -> str:
        """Generate a custom script with the standard header and error handling."""
        logger.info(f"Generating custom script: {title}")
        return _header(title) + _wrap_try(code)


# =============================================================================
# Main Script Generator
# =============================================================================

class ScriptGenerator:
    """
    Main entry point for generating UE 5.6 Python scripts.

    Usage:
        gen = ScriptGenerator()
        script = gen.actors.spawn("StaticMeshActor", location=(0, 0, 100))
        script = gen.sequences.add_track("/Game/Seq", "MyActor", "Transform")
        script = gen.materials.set_parameter("/Game/Mat", "Roughness", 0.5)
    """

    def __init__(self):
        self.actors = ActorScripts()
        self.assets = AssetScripts()
        self.sequences = SequenceScripts()
        self.materials = MaterialScripts()
        self.blueprints = BlueprintScripts()
        self.animation = AnimationScripts()
        self.data_tables = DataTableScripts()
        self.levels = LevelScripts()
        self.pcg = PCGScripts()
        self.utility = UtilityScripts()

        logger.info("ScriptGenerator initialized with all domain modules")

    def list_domains(self) -> dict:
        """Return a dict of all available domains and their methods."""
        domains = {}
        for attr_name in ['actors', 'assets', 'sequences', 'materials',
                          'blueprints', 'animation', 'data_tables',
                          'levels', 'pcg', 'utility']:
            domain = getattr(self, attr_name)
            methods = [m for m in dir(domain)
                       if not m.startswith('_') and callable(getattr(domain, m))]
            domains[attr_name] = {
                'class': domain.__class__.__name__,
                'methods': methods,
                'doc': domain.__class__.__doc__.strip() if domain.__class__.__doc__ else ''
            }
        return domains

    def generate_from_request(self, domain: str, method: str,
                              params: dict) -> str:
        """
        Generate a script from a structured request.

        Args:
            domain: Domain name (e.g., "actors", "sequences")
            method: Method name (e.g., "spawn", "add_track")
            params: Dict of parameters to pass to the method

        Returns:
            Complete runnable Python script string
        """
        logger.info(f"Generating script: {domain}.{method}({params})")

        domain_obj = getattr(self, domain, None)
        if not domain_obj:
            raise ValueError(f"Unknown domain: {domain}. "
                           f"Available: {list(self.list_domains().keys())}")

        method_fn = getattr(domain_obj, method, None)
        if not method_fn:
            raise ValueError(f"Unknown method: {domain}.{method}. "
                           f"Available: {[m for m in dir(domain_obj) if not m.startswith('_')]}")

        return method_fn(**params)
