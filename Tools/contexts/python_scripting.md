<- Added audit scripts: audit-handlers.mjs, audit-test.mjs, WORKER REFERENCE: Load this file when executing python scripting tools -->
# UE5 Python Editor Scripting Reference

This document covers the Python APIs available inside the UE5 editor for asset management, actor manipulation, level operations, and editor utilities.

## Actor Operations

### Get All Actors

```python
import unreal
actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = actor_sub.get_all_level_actors()
for a in all_actors:
    print(f"{a.get_actor_label()} | {a.get_class().get_name()} | {a.get_actor_location()}")
```

### Get Selected Actors

```python
selected = actor_sub.get_selected_level_actors()
```

### Spawn Actor

```python
# Spawn from class
actor_class = unreal.load_class(None, "/Script/Engine.StaticMeshActor")
location = unreal.Vector(100, 200, 0)
rotation = unreal.Rotator(0, 0, 0)
actor = actor_sub.spawn_actor_from_class(actor_class, location, rotation)

# Spawn from Blueprint
bp_class = unreal.load_class(None, "/Game/Blueprints/BP_TeleportPoint.BP_TeleportPoint_C")
actor = actor_sub.spawn_actor_from_class(bp_class, location, rotation)
```

### Set Actor Transform

```python
actor.set_actor_location(unreal.Vector(500, 300, 0), False, False)
actor.set_actor_rotation(unreal.Rotator(0, 90, 0), False)
actor.set_actor_scale3d(unreal.Vector(1, 1, 1))
```

### Get/Set Actor Properties

```python
# Get property
value = actor.get_editor_property("bActorEnabled")

# Set property
actor.set_editor_property("bActorEnabled", True)

# For component properties
comp = actor.get_component_by_class(unreal.StaticMeshComponent)
mesh = comp.get_editor_property("static_mesh")
comp.set_editor_property("static_mesh", unreal.load_asset("/Game/Meshes/SM_Couch"))
```

### Destroy Actor

```python
actor_sub.destroy_actor(actor)
```

### Duplicate Actor

```python
duplicated = actor_sub.duplicate_actors([actor])
```

### Select Actor Programmatically

```python
actor_sub.set_selected_level_actors([actor])
```

## Asset Operations

### Find Assets

```python
asset_lib = unreal.EditorAssetLibrary

# Check if asset exists
exists = asset_lib.does_asset_exist("/Game/Meshes/SM_Couch")

# List assets in directory
assets = asset_lib.list_assets("/Game/Blueprints/", recursive=True)
for a in assets:
    print(a)

# Find assets by class
registry = unreal.AssetRegistryHelpers.get_asset_registry()
assets = registry.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Engine", "StaticMesh"))
```

### Load Assets

```python
# Load any asset
asset = unreal.load_asset("/Game/Meshes/SM_Couch")

# Load with specific class
mesh = unreal.load_asset("/Game/Meshes/SM_Couch", unreal.StaticMesh)

# Load class (for spawning)
bp_class = unreal.load_class(None, "/Game/Blueprints/BP_Door.BP_Door_C")
```

### Create Assets

```python
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

# Create Blueprint
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
bp = asset_tools.create_asset("BP_NewActor", "/Game/Blueprints", unreal.Blueprint, factory)

# Create Material Instance
mi_factory = unreal.MaterialInstanceConstantFactoryNew()
mi = asset_tools.create_asset("MI_Custom", "/Game/Materials", unreal.MaterialInstanceConstant, mi_factory)
```

### Rename/Move/Delete Assets

```python
# Rename
asset_lib.rename_asset("/Game/Old/Asset", "/Game/New/Asset")

# Duplicate
asset_lib.duplicate_asset("/Game/Source", "/Game/Destination")

# Delete
asset_lib.delete_asset("/Game/Obsolete/Asset")
```

### Import Assets

```python
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

# Import FBX
import_task = unreal.AssetImportTask()
import_task.set_editor_property("filename", "C:/Models/character.fbx")
import_task.set_editor_property("destination_path", "/Game/Meshes")
import_task.set_editor_property("automated", True)
import_task.set_editor_property("save", True)
asset_tools.import_asset_tasks([import_task])
```

## Level Operations

### Load/Save Levels

```python
level_lib = unreal.EditorLevelLibrary

# Get current level
world = level_lib.get_editor_world()

# Load level
unreal.EditorLoadingAndSavingUtils.load_map("/Game/Maps/MyLevel")

# Save current level
unreal.EditorLoadingAndSavingUtils.save_current_level()

# Save all dirty packages
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
```

### Streaming Levels

```python
world = unreal.EditorLevelLibrary.get_editor_world()
streaming_levels = world.get_streaming_levels()
for sl in streaming_levels:
    print(f"{sl.get_world_asset_package_f_name()} loaded={sl.is_level_loaded()}")
```

## Data Table Operations

### Read Data Table

```python
dt = unreal.load_asset("/Game/Data/DT_ExampleTable")
row_names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
for name in row_names:
    print(f"Row: {name}")
```

### Get Data Table Row

```python
# Access row struct fields via Python
# Note: Requires the struct to be exposed to Python
dt = unreal.load_asset("/Game/Data/DT_ExampleTable")
# Use evaluate_data_table_row for typed access
```

## Console Commands

```python
# Execute any console command
unreal.SystemLibrary.execute_console_command(None, "stat fps")

# Execute editor command
unreal.EditorLevelLibrary.editor_exec_command("ACTOR SELECT ALL")
```

## Editor Dialogs

```python
# Show message box
result = unreal.EditorDialog.show_message(
    "Title",
    "Message text",
    unreal.AppMsgType.OK,
    unreal.AppReturnType.OK
)
```

## Utility

### Get Project Paths

```python
project_dir = unreal.Paths.project_dir()
content_dir = unreal.Paths.project_content_dir()
saved_dir = unreal.Paths.project_saved_dir()
```

### Log Output

```python
unreal.log("Info message")
unreal.log_warning("Warning message")
unreal.log_error("Error message")
```

