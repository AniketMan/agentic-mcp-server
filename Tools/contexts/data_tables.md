<- Added audit scripts: audit-handlers.mjs, audit-test.mjs, WORKER REFERENCE: Load this file when executing data tables tools -->
# Data Table and Data Asset Operations

This document covers reading and writing UE5 Data Tables and Data Assets via Python.

## Reading Data Tables

```python
import unreal

# Load a data table
dt = unreal.load_asset("/Game/Data/DT_StorySteps")

# Get all row names
row_names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
print(f"Rows: {len(row_names)}")
for name in row_names:
    print(f"  {name}")
```

## Reading Data Assets

```python
# Load a data asset
da = unreal.load_asset("/Game/Blueprints/Data/DA_GameData")

# Access properties
scenes = da.get_editor_property("LevelSequence")  # TArray<FPlayableScene>
for i, scene in enumerate(scenes):
    name = scene.get_editor_property("SceneName")
    asset = scene.get_editor_property("SceneAsset")
    print(f"Scene {i}: {name} -> {asset}")
```

## Modifying Data Table Rows

```python
# Data table rows can be modified via the editor property system
# Note: This requires the row struct to be exposed to Python

dt = unreal.load_asset("/Game/Data/DT_StorySteps")

# For complex modifications, use the DataTableEditor subsystem
# or modify via JSON export/import:

# Export to JSON
json_str = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt)

# Import from JSON (after modification)
# unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(dt, modified_json)
```

## Common Data Structures

### FStoryStep (DT_StorySteps)

| Field | Type | Description |
|-------|------|-------------|
| LevelName | FName | Auto-populated from SceneIndex |
| SceneIndex | int32 | Index into DA_GameData.LevelSequence |
| SequenceAsset | ULevelSequence* | Level Sequence to play |
| AutoAdvanceOnFinish | bool | Auto-advance to next step when sequence ends |
| CanSkip | bool | Whether player can skip this step |
| StartTimeSeconds | float | Start time offset in the sequence |
| PlayRate | float | Playback speed multiplier |
| IsLoop | bool | Whether the sequence loops |
| AllowTeleport | bool | Whether teleportation is allowed during this step |

### FPlayableScene (DA_GameData)

| Field | Type | Description |
|-------|------|-------------|
| SceneName | FText | Display name of the scene |
| SceneAsset | TSoftObjectPtr<UWorld> | Reference to the master level |

## Creating New Data Table Rows

```python
# To add rows to a data table, use the JSON approach:
import json

dt = unreal.load_asset("/Game/Data/DT_StorySteps")
json_str = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt)
data = json.loads(json_str)

# Add new row
new_row = {
    "Name": "Step_35",
    "SceneIndex": 5,
    "SequenceAsset": "/Game/Sequences/LS_5_1",
    "AutoAdvanceOnFinish": False,
    "CanSkip": True,
    "StartTimeSeconds": 0.0,
    "PlayRate": 1.0,
    "IsLoop": False,
    "AllowTeleport": True
}
data.append(new_row)

# Re-import
modified_json = json.dumps(data)
# unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(dt, modified_json)
```
