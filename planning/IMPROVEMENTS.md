# AgenticMCP Improvements Roadmap

This document defines every improvement needed to make AgenticMCP a production-grade tool where an AI agent can read a screenplay and assemble scenes from pre-built components. Each improvement includes the problem it solves, the implementation design, the UE5 APIs involved, and the file(s) that need to change.

---

## Improvement 1: Persistent Project Memory

### Problem

The AI has no memory between sessions. Every time it connects, it starts from zero — it does not know what levels exist, what actors are placed, what has been built, or what remains. The user has to re-explain context every time.

### Solution

A persistent state file stored at `Content/AgenticMCP/project_state.json` inside the UE5 project. This file survives build clears (Content folder is never wiped by Clean/Rebuild). The plugin reads it on startup and writes it after every significant operation.

### State File Schema (Token-Compressed)

The file uses short keys to minimize token consumption when the AI reads it. Every key is 1-3 characters. The schema is designed so an LLM can parse it in under 200 tokens for a project with 100 actors.

```json
{
  "v": 1,
  "ts": "2026-03-07T14:30:00Z",
  "lvls": [
    {
      "n": "ML_Trailer",
      "p": "/Game/Maps/Game/Trailer/ML_Trailer",
      "sl": ["SL_Trailer_Logic", "SL_Trailer_Art", "SL_Trailer_Lighting", "SL_Trailer_Blockout"],
      "st": "loaded"
    }
  ],
  "actors": [
    {"n": "BP_TeleportPoint_C_0", "c": "BP_TeleportPoint_C", "l": "SL_Trailer_Logic", "p": [1200, -340, 50], "r": [0, 90, 0], "tags": ["interaction", "teleport"]},
    {"n": "BP_Door_C_0", "c": "BP_Door_C", "l": "SL_Trailer_Logic", "p": [800, 200, 0], "r": [0, 0, 0], "tags": ["interaction", "door"]},
    {"n": "Couch_01", "c": "StaticMeshActor", "l": "SL_Trailer_Art", "p": [500, 100, 0], "r": [0, 45, 0], "tags": ["furniture", "prop"]}
  ],
  "graphs": {
    "SL_Trailer_Logic": {"nodes": 56, "chains": 4, "compiled": true, "last_edit": "2026-03-06T10:00:00Z"}
  },
  "scenes": {
    "Trailer": {"status": "complete", "steps": [3, 6, 7]},
    "Restaurant": {"status": "actors_placed", "steps": []},
    "Hospital": {"status": "wired", "steps": [10, 11, 12]},
    "Scene6": {"status": "wired", "steps": [20, 21]}
  },
  "seq": [
    {"n": "LS_1_1", "p": "/Game/Sequences/LS_1_1", "dur": 30.0, "fps": 30, "bindings": 3}
  ]
}
```

**Key legend** (included as a comment block at the top of the file):
- `v` = schema version, `ts` = last updated timestamp
- `lvls` = levels, `n` = name, `p` = path, `sl` = sublevels, `st` = status
- `actors`: `c` = class, `l` = level, `p` = position [x,y,z], `r` = rotation [pitch,yaw,roll]
- `graphs`: node count, chain count, compile status
- `scenes`: screenplay scene mapping with completion status
- `seq` = level sequences, `dur` = duration, `fps` = frame rate

### Implementation

**C++ Side (AgenticMCPEditorSubsystem):**

1. On `Initialize()`, read `Content/AgenticMCP/project_state.json` if it exists.
2. Bind to `FEditorDelegates::OnMapOpened` — when a level opens, scan all actors and update the state file.
3. Bind to `GEditor->OnActorMoved` — when an actor is moved, update its position entry.
4. Bind to `FEditorDelegates::OnNewActorsPlaced` — when actors are added, add them to state.
5. Bind to `FEditorDelegates::OnDeleteActorsEnd` — when actors are deleted, remove from state.
6. Bind to `FEditorDelegates::PostUndoRedo` — refresh state after undo/redo.
7. After any mutation tool call (add_node, connect_pins, spawn_actor, etc.), write updated state.

**MCP Side (index.js):**

8. On MCP connection, read `project_state.json` and inject it into the initial context alongside CAPABILITIES.md.
9. Add a `get_project_state` tool that returns the current state.
10. Add an `update_project_state` tool that lets the AI write structured updates.

### Files to Change

| File | Change |
|------|--------|
| `Source/AgenticMCP/Private/AgenticMCPEditorSubsystem.cpp` | Add delegate bindings and state file I/O |
| `Source/AgenticMCP/Public/AgenticMCPEditorSubsystem.h` | Add state management members |
| `Source/AgenticMCP/Private/Handlers_Validation.cpp` | Add `get_project_state` and `update_project_state` handlers |
| `Tools/index.js` | Read state file on connect, add to initial context |

### UE5 APIs Required

```cpp
#include "Editor.h"  // FEditorDelegates
#include "Misc/FileHelper.h"  // FFileHelper::SaveStringToFile
#include "Serialization/JsonSerializer.h"  // JSON read/write
#include "HAL/PlatformFileManager.h"  // File existence checks

// Delegate bindings:
FEditorDelegates::OnMapOpened.AddUObject(this, &UAgenticMCPEditorSubsystem::OnMapOpened);
FEditorDelegates::OnNewActorsPlaced.AddUObject(this, &UAgenticMCPEditorSubsystem::OnActorsPlaced);
FEditorDelegates::OnDeleteActorsEnd.AddUObject(this, &UAgenticMCPEditorSubsystem::OnActorsDeleted);
FEditorDelegates::PostUndoRedo.AddUObject(this, &UAgenticMCPEditorSubsystem::OnUndoRedo);
GEditor->OnActorMoved().AddUObject(this, &UAgenticMCPEditorSubsystem::OnActorMoved);
```

---

## Improvement 2: Recent Actions Buffer

### Problem

The AI cannot see what the user is doing in the editor. If the user clicks on an actor, moves something, or selects a group of objects, the AI has no awareness of these actions. The user has to describe everything verbally.

### Solution

A ring buffer of the last 15 user actions, stored at `Content/AgenticMCP/recent_actions.log`. Each entry is a single line in a compressed format. The buffer is categorized by action type for fast scanning.

### Buffer Format

```
[2026-03-07T14:30:01] SEL BP_TeleportPoint_C_0 @1200,-340,50
[2026-03-07T14:30:05] MOV BP_TeleportPoint_C_0 @1200,-340,50 -> @1300,-340,50
[2026-03-07T14:30:08] SEL BP_Door_C_0 @800,200,0
[2026-03-07T14:30:12] ADD StaticMeshActor Couch_02 @600,150,0
[2026-03-07T14:30:15] DEL StaticMeshActor OldProp_01
[2026-03-07T14:30:18] CAM @2000,500,300 R15,-45,0
[2026-03-07T14:30:22] SEL BP_TeleportPoint_C_0,BP_Door_C_0 (multi)
[2026-03-07T14:30:25] PROP BP_Door_C_0.bActorEnabled = true
[2026-03-07T14:30:28] COMPILE SL_Trailer_Logic OK
[2026-03-07T14:30:30] UNDO
```

**Action codes:** `SEL` (selection), `MOV` (move), `ADD` (actor placed), `DEL` (actor deleted), `CAM` (camera moved), `PROP` (property changed), `COMPILE` (Blueprint compiled), `UNDO`/`REDO`, `SAVE`, `LOAD` (level loaded), `PIE` (play-in-editor start/stop).

### Implementation

**C++ Side:**

Bind to these editor delegates:

| Delegate | Action Code | Data Captured |
|----------|-------------|---------------|
| `USelection::SelectionChangedEvent` | `SEL` | Selected actor name(s) + position |
| `GEditor->OnActorMoved()` | `MOV` | Actor name, old pos, new pos |
| `FEditorDelegates::OnNewActorsPlaced` | `ADD` | Class, name, position |
| `FEditorDelegates::OnDeleteActorsEnd` | `DEL` | Actor name |
| `FEditorDelegates::OnEditorCameraMoved` | `CAM` | Camera pos + rotation (throttled to 1/sec) |
| `FEditorDelegates::ActorPropertiesChange` | `PROP` | Actor, property, value |
| `FEditorDelegates::PostUndoRedo` | `UNDO`/`REDO` | — |
| `FEditorDelegates::OnMapOpened` | `LOAD` | Level name |
| `FEditorDelegates::PreSaveWorldWithContext` | `SAVE` | Level name |
| `FEditorDelegates::BeginPIE` / `EndPIE` | `PIE` | Start/stop |

The buffer is a `TArray<FString>` capped at 15 entries. On every new action, the oldest entry is dropped. The buffer is flushed to disk on every write (it is 15 lines — negligible I/O).

**Camera throttling:** `OnEditorCameraMoved` fires constantly during viewport navigation. Throttle to max 1 entry per second, and only log if the camera moved more than 100 units from the last logged position.

### Files to Change

| File | Change |
|------|--------|
| `Source/AgenticMCP/Private/AgenticMCPEditorSubsystem.cpp` | Add delegate bindings, ring buffer, file I/O |
| `Source/AgenticMCP/Public/AgenticMCPEditorSubsystem.h` | Add buffer members, throttle timer |
| `Tools/index.js` | Add `get_recent_actions` tool |

---

## Improvement 3: World Coordinate Dump on Level Load

### Problem

When a level loads, the AI does not know what is in it or where things are. It has to call `list_actors` manually, and even then the response may be too large to process efficiently.

### Solution

On every level load (`FEditorDelegates::OnMapOpened`), automatically dump all actor world coordinates to the project state file. The dump uses the compressed format from Improvement 1. Additionally, expose a `get_scene_spatial_map` tool that returns a grid-bucketed view of the scene.

### Spatial Grid Format

Divide the world into 1000-unit grid cells. Group actors by cell. This lets the AI quickly answer "what is near the couch?" without scanning every actor.

```json
{
  "grid_size": 1000,
  "cells": {
    "0,0": [
      {"n": "Floor_01", "c": "StaticMeshActor", "p": [100, 200, 0]},
      {"n": "Couch_01", "c": "StaticMeshActor", "p": [500, 100, 0]}
    ],
    "1,0": [
      {"n": "BP_TeleportPoint_C_0", "c": "BP_TeleportPoint_C", "p": [1200, -340, 50]}
    ]
  }
}
```

### Implementation

```cpp
void UAgenticMCPEditorSubsystem::OnMapOpened(const FString& MapName, bool bAsTemplate)
{
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) return;
    
    TSharedPtr<FJsonObject> StateJson = LoadProjectState();
    TArray<TSharedPtr<FJsonValue>> ActorsArray;
    
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor || Actor->IsHidden()) continue;
        
        FVector Loc = Actor->GetActorLocation();
        FRotator Rot = Actor->GetActorRotation();
        
        TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
        ActorObj->SetStringField("n", Actor->GetActorLabel());
        ActorObj->SetStringField("c", Actor->GetClass()->GetName());
        
        TArray<TSharedPtr<FJsonValue>> PosArr;
        PosArr.Add(MakeShared<FJsonValueNumber>(FMath::RoundToInt(Loc.X)));
        PosArr.Add(MakeShared<FJsonValueNumber>(FMath::RoundToInt(Loc.Y)));
        PosArr.Add(MakeShared<FJsonValueNumber>(FMath::RoundToInt(Loc.Z)));
        ActorObj->SetArrayField("p", PosArr);
        
        ActorsArray.Add(MakeShared<FJsonValueObject>(ActorObj));
    }
    
    StateJson->SetArrayField("actors", ActorsArray);
    SaveProjectState(StateJson);
}
```

### Files to Change

| File | Change |
|------|--------|
| `Source/AgenticMCP/Private/AgenticMCPEditorSubsystem.cpp` | Add `OnMapOpened` handler with actor scan |
| `Source/AgenticMCP/Private/Handlers_Read.cpp` | Add `get_scene_spatial_map` handler |
| `Source/AgenticMCP/Public/ManusMCPServer.h` | Add route registration |

---

## Improvement 4: Scene Inference Engine

### Problem

The AI cannot connect a screenplay reference like "Scene 8 — Pluma" to the actual level file at `/Game/Maps/Game/S8_CNA/`. It does not understand that "CNA" is the codename for "Pluma" or that the folder structure maps to scenes. The user has to spell this out every time.

### Solution

A scene mapping table stored in the project state file that the AI builds incrementally. The mapping connects screenplay scene names, codenames, folder paths, level names, and story step ranges.

### Scene Map Schema

```json
{
  "scene_map": [
    {
      "idx": 0,
      "screenplay_name": "Prologue",
      "codename": "Prologue",
      "folder": "/Game/Maps/Game/S0_Prologue/",
      "master_level": "ML_Prologue",
      "logic_level": "SL_Prologue_Logic",
      "step_range": [0, 2],
      "status": "not_started"
    },
    {
      "idx": 5,
      "screenplay_name": "Restaurant",
      "codename": "Restaurant",
      "folder": "/Game/Maps/Game/S5_Restaurant/",
      "master_level": "ML_Restaurant",
      "logic_level": "SL_Restaurant_Logic",
      "step_range": [15, 19],
      "status": "actors_placed"
    }
  ]
}
```

### Auto-Discovery Logic

On first connection (or when the scene map is empty), the plugin should:

1. Scan `/Game/Maps/Game/` for all subdirectories matching `S{N}_*` pattern.
2. For each, find the `ML_*` (master level) and `SL_*_Logic` (logic level) files.
3. Cross-reference with `DA_GameData` to get the screenplay scene name.
4. Cross-reference with `DT_StorySteps` to get the step range.
5. Populate the scene map and write to project state.

This can be implemented as a Python script executed via `execute_python`, since it needs access to `unreal.load_asset()` and `unreal.EditorAssetLibrary`.

### Contextual Placement Logic

When the AI reads a screenplay line like "She sits on the couch," it should:

1. Search the actor list for actors with "couch" in the name or class.
2. Get the couch's world position from the project state.
3. Place the teleport point near the couch (offset by a reasonable distance, e.g., 150 units in front).
4. Log the placement reasoning in the recent actions buffer.

This logic lives in the AI's reasoning, not in the plugin code. The plugin just needs to provide the data. The `CLAUDE.md` file should include instructions for this reasoning pattern.

### Files to Change

| File | Change |
|------|--------|
| `CLAUDE.md` | Add scene inference instructions and contextual placement patterns |
| `Tools/contexts/scene_inference.md` | New context doc with scene mapping rules |
| `Source/AgenticMCP/Private/Handlers_Read.cpp` | Add `get_scene_map` handler |

---

## Improvement 5: Level Sequence Python Tools

### Problem

The current plugin has no Level Sequence manipulation tools. Level Sequences are the backbone of cinematic VR experiences — they control camera cuts, actor animations, audio, events, and timing. Without these tools, the AI cannot assemble scenes.

### Solution

Add a comprehensive set of Level Sequence tools that wrap the `unreal.LevelSequenceEditorSubsystem` and `unreal.LevelSequenceEditorBlueprintLibrary` Python APIs. These are implemented as Python scripts executed via the `execute_python` tool, not as C++ handlers, because the Python API is more stable across engine versions.

### New Tools

| Tool | What It Does | Python API Used |
|------|-------------|-----------------|
| `ls_create` | Create a new Level Sequence asset | `AssetTools.create_asset()` with `LevelSequenceFactoryNew` |
| `ls_open` | Open a Level Sequence in Sequencer | `LevelSequenceEditorBlueprintLibrary.open_level_sequence()` |
| `ls_get_current` | Get the currently open sequence | `LevelSequenceEditorBlueprintLibrary.get_current_level_sequence()` |
| `ls_set_range` | Set playback start/end frames | `level_sequence.set_playback_start/end()` |
| `ls_set_fps` | Set frame rate | `level_sequence.set_display_rate()` |
| `ls_bind_actor` | Bind a level actor as possessable | `LevelSequenceEditorSubsystem.add_actors()` |
| `ls_add_spawnable` | Add a spawnable actor | `add_actors()` + `convert_to_spawnable()` |
| `ls_add_camera` | Add a camera with camera cut track | `LevelSequenceEditorSubsystem.create_camera()` |
| `ls_add_track` | Add a track to a binding | `binding.add_track(track_class)` |
| `ls_add_section` | Add a section to a track | `track.add_section()` |
| `ls_set_section_range` | Set section start/end frames | `section.set_range()` |
| `ls_add_keyframe` | Add a keyframe to a channel | `channel.add_key()` |
| `ls_set_animation` | Set animation asset on anim section | `section.params.animation = asset` |
| `ls_add_audio` | Add audio track with sound asset | Add `MovieSceneAudioTrack`, set sound |
| `ls_add_event` | Add event track with named event | Add `MovieSceneEventTrack` |
| `ls_list_bindings` | List all bindings in a sequence | `level_sequence.get_bindings()` |
| `ls_list_tracks` | List all tracks on a binding | `binding.get_tracks()` |
| `ls_focus_sub` | Focus on a sub-sequence | `focus_level_sequence()` |
| `ls_focus_parent` | Focus back to parent sequence | `focus_parent_sequence()` |

### Implementation Pattern

Each tool generates a Python script string and executes it via the `execute_python` handler. Example for `ls_bind_actor`:

```python
import unreal

# Get subsystems
actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ls_sub = unreal.get_editor_subsystem(unreal.LevelSequenceEditorSubsystem)

# Get the sequence
seq = unreal.load_asset("{sequence_path}")
unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)

# Find the actor by name
all_actors = actor_sub.get_all_level_actors()
target = None
for a in all_actors:
    if a.get_actor_label() == "{actor_name}":
        target = a
        break

if target:
    bindings = ls_sub.add_actors([target])
    print(f"Bound {{len(bindings)}} actor(s) to sequence")
else:
    print(f"ERROR: Actor '{actor_name}' not found")
```

### Files to Change

| File | Change |
|------|--------|
| `Tools/contexts/level_sequence.md` | New context doc with full Level Sequence Python API reference |
| `Tools/index.js` | Register new `ls_*` tools that generate and execute Python scripts |
| `Source/AgenticMCP/Private/Handlers_Mutation.cpp` | Ensure `execute_python` handler exists and works |

---

## Improvement 6: Execute Python Handler

### Problem

The most powerful capability — running arbitrary Python in the editor — is listed in CAPABILITIES.md but may not be fully implemented in the C++ plugin. This is the foundation for Level Sequence tools, asset operations, and anything the C++ handlers do not cover.

### Solution

A robust `execute_python` HTTP handler that:

1. Accepts a Python script string or file path.
2. Executes it in the editor's Python environment via `IPythonScriptPlugin`.
3. Captures stdout/stderr output.
4. Returns the output to the MCP client.

### Implementation

```cpp
void FAgenticMCPServer::HandleExecutePython(const FJsonObject& Request, FJsonObject& Response)
{
    FString Script = Request.GetStringField("script");
    
    // If it's a file path, read the file
    if (Script.StartsWith("/") || Script.StartsWith("C:"))
    {
        FFileHelper::LoadFileToString(Script, *Script);
    }
    
    // Redirect stdout to capture output
    FString Output;
    
    // Execute via IPythonScriptPlugin
    IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
    if (PythonPlugin)
    {
        // UE 5.6+ uses ExecPythonCommand
        bool bSuccess = PythonPlugin->ExecPythonCommand(*Script);
        Response.SetBoolField("success", bSuccess);
    }
    else
    {
        // Fallback: execute via console command
        GEngine->Exec(GEditor->GetEditorWorldContext().World(), 
            *FString::Printf(TEXT("py \"%s\""), *Script));
    }
    
    Response.SetStringField("output", Output);
}
```

**Note:** Capturing Python stdout in UE5 requires redirecting `sys.stdout` to a `StringIO` buffer within the script itself. The handler should automatically wrap the user's script:

```python
import sys, io
_buf = io.StringIO()
_old_stdout = sys.stdout
sys.stdout = _buf
try:
    {USER_SCRIPT}
finally:
    sys.stdout = _old_stdout
    _output = _buf.getvalue()
# _output is captured by the handler
```

### Files to Change

| File | Change |
|------|--------|
| `Source/AgenticMCP/Private/Handlers_Mutation.cpp` | Add `HandleExecutePython` |
| `Source/AgenticMCP/Public/AgenticMCPServer.h` | Declare handler |
| `Source/AgenticMCP/AgenticMCP.Build.cs` | Add `PythonScriptPlugin` dependency |

---

## Improvement 7: Auto-Read on MCP Connect

### Problem

When the AI connects via MCP, it does not automatically read the capabilities list, project state, or recent actions. It starts blind and has to be told what to do.

### Solution

The MCP server's `initialize` handler should automatically inject three things into the initial context:

1. `CAPABILITIES.md` — What tools are available
2. `project_state.json` — What exists in the project
3. `recent_actions.log` — What the user just did

### Implementation

In `index.js`, modify the server initialization:

```javascript
server.setRequestHandler(ListToolsRequestSchema, async (request) => {
    // Load capabilities
    const capabilities = fs.readFileSync(
        path.join(__dirname, '..', 'CAPABILITIES.md'), 'utf-8'
    );
    
    // Load project state (from UE project Content folder)
    let projectState = '{}';
    const statePath = await bridge.call('get_project_state_path', {});
    if (statePath && fs.existsSync(statePath)) {
        projectState = fs.readFileSync(statePath, 'utf-8');
    }
    
    // Load recent actions
    let recentActions = '';
    const actionsPath = await bridge.call('get_recent_actions_path', {});
    if (actionsPath && fs.existsSync(actionsPath)) {
        recentActions = fs.readFileSync(actionsPath, 'utf-8');
    }
    
    // Inject as system context
    const context = `# AgenticMCP Context\n\n## Capabilities\n${capabilities}\n\n## Project State\n\`\`\`json\n${projectState}\n\`\`\`\n\n## Recent Actions\n\`\`\`\n${recentActions}\n\`\`\``;
    
    // Return tools with context injected
    return {
        tools: registeredTools,
        _meta: { context }
    };
});
```

### MCP Protocol Note

The MCP protocol supports `resources` — static content that the client can read. The capabilities and project state should be exposed as MCP resources:

```javascript
server.setRequestHandler(ListResourcesRequestSchema, async () => {
    return {
        resources: [
            {
                uri: "agentic://capabilities",
                name: "AgenticMCP Capabilities",
                description: "Complete list of available tools and what they can do",
                mimeType: "text/markdown"
            },
            {
                uri: "agentic://project-state",
                name: "Project State",
                description: "Current state of all levels, actors, and scenes",
                mimeType: "application/json"
            },
            {
                uri: "agentic://recent-actions",
                name: "Recent Actions",
                description: "Last 15 user actions in the editor",
                mimeType: "text/plain"
            }
        ]
    };
});
```

### Files to Change

| File | Change |
|------|--------|
| `Tools/index.js` | Add resource handlers, inject context on connect |
| `Source/AgenticMCP/Private/Handlers_Read.cpp` | Add `get_project_state_path` and `get_recent_actions_path` handlers |

---

## Improvement 8: Component Management Tools

### Problem

The current plugin can spawn actors and set properties, but cannot add or configure components on actors. Components are how UE5 organizes functionality — an actor's behavior comes from its components (EnablerComponent, GrabbableComponent, TriggerBoxComponent, etc.).

### Solution

Add component management tools:

| Tool | What It Does |
|------|-------------|
| `add_component` | Add a component to an actor by class name |
| `remove_component` | Remove a component from an actor |
| `get_components` | List all components on an actor with their properties |
| `set_component_property` | Set a property on a specific component |

### Implementation

These use `unreal.EditorActorSubsystem` and direct actor manipulation via Python:

```python
import unreal

actor = unreal.EditorActorSubsystem.get_all_level_actors()  # find by name
# Add component
new_comp = actor.add_component_by_class(
    unreal.load_class(None, "/Script/SohVr.EnablerComponent"),
    False, unreal.Transform(), False
)
```

### Files to Change

| File | Change |
|------|--------|
| `Source/AgenticMCP/Private/Handlers_Actors.cpp` | Add component handlers |
| `Tools/index.js` | Register component tools |

---

## Improvement 9: Output Log Streaming

### Problem

When the AI executes Python scripts or Blueprint operations, it cannot see the UE5 output log. Errors, warnings, and print statements are invisible.

### Solution

Capture the last N lines of the output log and make them available via a tool.

### Implementation

```cpp
// In AgenticMCPEditorSubsystem, bind to log output
GLog->AddOutputDevice(this);

// Implement FOutputDevice::Serialize
virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
{
    if (LogBuffer.Num() >= MaxLogLines)
        LogBuffer.RemoveAt(0);
    
    LogBuffer.Add(FString::Printf(TEXT("[%s] %s: %s"),
        *Category.ToString(),
        ToString(Verbosity),
        V));
}
```

Add a `get_output_log` tool that returns the last 50 lines.

### Files to Change

| File | Change |
|------|--------|
| `Source/AgenticMCP/Private/AgenticMCPEditorSubsystem.cpp` | Add FOutputDevice implementation |
| `Source/AgenticMCP/Private/Handlers_Read.cpp` | Add `get_output_log` handler |

---

## Improvement 10: Data Table Read/Write

### Problem

The project uses Data Tables (`DT_StorySteps`) and Data Assets (`DA_GameData`) to define the story structure. The AI cannot read or modify these.

### Solution

Add tools to read and write Data Table rows via Python:

```python
import unreal

# Read
dt = unreal.load_asset("/Game/Blueprints/Data/DT_StorySteps")
row_names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
for name in row_names:
    # Access row data
    pass

# Write (via editor property system)
# DataTable rows can be modified via unreal.DataTableFunctionLibrary
```

### Files to Change

| File | Change |
|------|--------|
| `Tools/contexts/data_tables.md` | New context doc with Data Table Python API |
| `Tools/index.js` | Register `dt_read`, `dt_write`, `dt_list_rows` tools |

---

## Priority Order

| Priority | Improvement | Impact | Effort |
|----------|-------------|--------|--------|
| 1 | Persistent Project Memory | Critical — eliminates context loss | Medium |
| 2 | Recent Actions Buffer | Critical — gives AI eyes | Medium |
| 3 | Execute Python Handler | Critical — foundation for everything else | Low |
| 4 | World Coordinate Dump | High — spatial awareness | Low |
| 5 | Auto-Read on Connect | High — eliminates cold start | Low |
| 6 | Level Sequence Tools | High — enables scene assembly | Medium |
| 7 | Scene Inference Engine | High — connects screenplay to levels | Medium |
| 8 | Output Log Streaming | Medium — debugging visibility | Low |
| 9 | Component Management | Medium — full actor configuration | Medium |
| 10 | Data Table Read/Write | Medium — story structure access | Low |
