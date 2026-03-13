# AgenticMCP — AI Agent Instructions

## MANDATORY STARTUP SEQUENCE

On every connection, before doing anything else:

1. Read `CAPABILITIES.md` — the authoritative list of what you can and cannot do
2. Call `get_project_state` or read `Content/AgenticMCP/project_state.json` — understand what exists
3. Call `get_recent_actions` or read `Content/AgenticMCP/recent_actions.log` — understand what the user just did
4. Call `unreal_status` — determine if the editor is live or if you are in offline mode

Do not skip these steps. Do not guess what exists. Read the state.

---

## What This Is

AgenticMCP is a dual-path MCP server for Unreal Engine 5. It gives you (the AI agent) direct access to the running UE5 editor through Blueprint manipulation, actor management, level editing, Python script execution, and Level Sequence tools. If the editor is not running, it falls back to an offline binary injector that can read and modify `.umap` files directly.

## Architecture

```
AI Client <-> MCP Protocol (stdio) <-> Node.js MCP Bridge <-> HTTP (localhost:3000) <-> C++ Plugin (inside UE5 editor)
                                            |
                                            +-> Python Binary Injector (offline fallback, .umap files)
```

All live mutations happen on the UE5 game thread. Requests are queued and processed safely. When the editor is not running, the fallback path reads/writes `.umap` files directly.

## Connection Modes

### Live Editor (Primary)
When the UE5 editor is running with the AgenticMCP C++ plugin loaded, all tools are available over HTTP. You get real-time compilation, validation, Python execution, and the full tool set.

### Offline Fallback
When the editor is not running, a subset of tools is available through the Python binary injector. You can read level structure, inspect actors, and generate paste text. You cannot compile, validate, execute Python, or spawn actors in real-time.

### Check Status First
Always start by calling `unreal_status` to determine which mode is active and what tools are available.

---

## Tool Categories

### Read (Non-destructive)
- `list_blueprints` — List all Blueprint and Map assets. Maps contain level blueprints.
- `get_blueprint` — Get Blueprint details (graphs, variables, parent class).
- `get_graph` — Get all nodes, pins, connections in a graph. Primary inspection tool.
- `search_blueprints` — Search by name pattern.
- `find_references` — Find what references an asset.
- `list_classes` — Discover UClasses (for CallFunction node types).
- `list_functions` — List UFunctions on a class (for knowing what to call).
- `list_properties` — List UProperties on a class.
- `get_pin_info` — Inspect pin types and connections on a node.
- `rescan_assets` — Force asset registry refresh.

### Mutation (Modifies Blueprints)
- `add_node` — Add a node to a graph. Returns nodeId and all pins.
- `delete_node` — Remove a node (breaks connections first).
- `connect_pins` — Wire two pins together (type-validated).
- `disconnect_pin` — Break all connections on a pin.
- `set_pin_default` — Set a pin's default value.
- `move_node` — Reposition a node.
- `refresh_all_nodes` — Refresh all nodes (after variable/class changes).
- `create_blueprint` — Create a new Blueprint asset.
- `create_graph` — Add a function or macro graph.
- `delete_graph` — Remove a graph.
- `add_variable` — Add a variable to a Blueprint.
- `remove_variable` — Remove a variable.
- `compile_blueprint` — Compile and save.
- `set_node_comment` — Set comment bubble on a node.

### World (Actors and Levels)
- `list_actors` — List actors with optional class/name/level filters.
- `get_actor` — Get actor details: transform, components, properties.
- `spawn_actor` — Spawn a new actor (Blueprint or native class).
- `delete_actor` — Remove an actor.
- `set_actor_property` — Set a property on an actor or its components.
- `set_actor_transform` — Move/rotate/scale an actor.
- `list_levels` — List persistent and streaming levels.
- `load_level` — Load a sublevel.
- `get_level_blueprint` — Get level blueprint details.

### Python Execution
- `execute_python` — Run arbitrary Python in the editor's Python environment. This is the most powerful tool. Use it for Level Sequence operations, asset management, data table access, and anything the C++ handlers do not cover. See `contexts/python_scripting.md` and `contexts/level_sequence.md` for the full API reference.

### Level Sequence (via Python)
- `ls_create` — Create a new Level Sequence asset
- `ls_open` — Open a Level Sequence in Sequencer
- `ls_bind_actor` — Bind a level actor as possessable
- `ls_add_track` — Add a track to a binding (Transform, Animation, Audio, Event, Visibility)
- `ls_add_section` — Add a section to a track with frame range
- `ls_add_keyframe` — Add a keyframe to a channel
- `ls_add_camera` — Create a camera with camera cut track
- `ls_add_audio` — Add audio track with sound asset
- `ls_list_bindings` — List all bindings in a sequence

### Safety
- `validate_blueprint` — Compile and report errors/warnings without saving.
- `snapshot_graph` — Take a graph snapshot before making changes.
- `restore_graph` — Restore graph from snapshot.

### State Awareness
- `get_project_state` — Read the persistent project state (levels, actors, scenes, sequences)
- `update_project_state` — Write updates to the project state
- `get_recent_actions` — Read the last 15 user actions in the editor
- `get_output_log` — Read the last 50 lines of the UE5 output log
- `get_scene_spatial_map` — Get actors bucketed by spatial grid cell

### UE API Documentation
- `unreal_get_ue_context` — Load UE API reference by category or keyword search.

---

## Source Truth

The script is the input. Everything you build traces back to it.

- **Script**: `reference/source_truth/script_v2_ocvr.md` -- read this FIRST for any scene work
- **Implementation Roadmap**: `reference/source_truth/OC_VR_Implementation_Roadmap.md` -- technical translation of the script. Maps every beat to assets, components, triggers, VO lines, level sequences. Includes all `[makeTempBP]` specs with full logic pseudocode. Scenes 0-9.
- **Content Browser Hierarchy**: `reference/source_truth/ContentBrowser_Hierarchy.txt` -- full project asset dump. 3500+ lines. Every asset path. Cross-reference against the roadmap to know what exists vs what needs to be created.
- **Game Design Flowchart**: `reference/game_design/SOHGameDesign.webp` -- interaction flow validation
- **UE5 API Docs**: `Engine\Documentation\Builds` (relative to engine root, already on disk)
- **Build Config**: `reference/project_config/*.Target.cs`

You fill in the blanks. What the script defines explicitly is non-negotiable. The roadmap tells you exactly how to implement it. The Content Browser tells you what already exists. What is not covered, you infer and adapt until confidence is high.

---

## The Quantized Inference Path (Primary Workflow)

You are equipped with the **Quantized Inference Layer**, which replaces brute-force asset scanning with cached, deterministic scene wiring.

### Confidence Gate

All mutation tools (addNode, connectPins, spawnActor, etc.) are gated by a confidence threshold of **0.7**. You can read and plan freely -- no restrictions. But the MCP server will **reject** any mutation call below the threshold.

Confidence is calculated from four signals:

| Signal | Weight | How to earn it |
|--------|--------|----------------|
| Planned through quantized path | +0.4 | Use `unreal_get_scene_plan` + `unreal_execute_scene_step` |
| Asset verified in manifest | +0.3 | Call `unreal_quantize_project` first |
| Snapshot safety net | +0.2 | Call `unreal_snapshotGraph` before mutating |
| Script-aligned | +0.1 | Operation references a scene/event from the script |

If a mutation is blocked, the response includes a `suggestion` field telling you exactly what to do to increase confidence. **Adapt and retry.**

### Workflow

When asked to wire a scene (e.g., via the `wireSceneQuantized` command):
1. **Read the script** for the target scene from `reference/source_truth/script_v2_ocvr.md`.
2. **Initialize**: Call `unreal_quantize_project` to generate/refresh the Adaptive Asset Manifest.
3. **Check Status**: Call `unreal_get_wiring_status` to see which scenes need work.
4. **Get Plan**: Call `unreal_get_scene_plan` for the target scene. Review confidence scores.
5. **Snapshot**: Call `unreal_snapshotGraph` on the target blueprint before any mutations.
6. **Execute**: For each pending step, call `unreal_execute_scene_step`. This returns the exact MCP call sequence.
7. **Adapt**: If the gate blocks a call, read the suggestion, adjust, and retry.
8. **Persist**: After successfully executing a step's calls, call `unreal_mark_step_complete` to save progress.

You are free to try different approaches. The gate only blocks execution, never exploration.

### Project State Validator

After passing the confidence gate, every mutation is **cross-validated against the live UE5 project** before execution. The validator makes read-only calls to the editor to verify that what you claim exists actually exists. If it doesn't, the mutation is blocked and you receive:

- **What you claimed** vs **what actually exists** in the project
- The **exact read-only tool** to call to get the real data
- The **engine doc path** (`Engine/Documentation/Builds/...`) for the relevant API

This means you cannot hallucinate Blueprints, nodes, pins, actors, or sequences. Every mutation is ground-truth verified.

**Validation checks by tool:**

| Tool | Checks | What Gets Verified |
|------|--------|--------------------|
| `addNode` | 3 | Blueprint exists, graph exists, node class valid |
| `deleteNode` | 1 | Node exists in graph |
| `connectPins` | 3 | Source node exists, target node exists, pins exist and are compatible |
| `disconnectPin` | 1 | Node exists, pin exists, pin is connected |
| `setPinDefault` | 1 | Pin exists, value type matches, pin is input |
| `moveNode` | 1 | Node exists in graph |
| `createBlueprint` | 1 | Blueprint does NOT already exist |
| `createGraph` | 1 | Blueprint exists, graph name not taken |
| `deleteGraph` | 1 | Blueprint exists, graph exists |
| `addVariable` | 1 | Blueprint exists, variable name unique |
| `removeVariable` | 1 | Blueprint exists, variable exists |
| `compileBlueprint` | 1 | Blueprint exists |
| `spawnActor` | 3 | Level loaded, actor class valid, transform sane |
| `deleteActor` | 1 | Actor exists in level |
| `setActorProperty` | 1 | Actor exists, property exists |
| `setActorTransform` | 2 | Actor exists, transform sane |
| `ls_bind_actor` | 2 | Sequence exists, actor exists |
| `ls_add_track` | 1 | Sequence exists, binding valid |
| `ls_add_section` | 1 | Sequence exists |
| `ls_create` | 1 | Sequence does NOT already exist |
| `executePython` | 1 | No dangerous operations (rm, subprocess, eval) |

When a validation fails, read the failure response carefully. It tells you exactly what to do. Call the suggested read-only tool, get the real data, adjust your call, and retry.

---

## Standard Workflow (Manual Operations)

```
1. unreal_status                              // Check connection
2. get_project_state                          // Read what exists
3. get_recent_actions                         // See what user just did
4. unreal_get_ue_context(category="blueprint") // Load relevant API docs
5. get_blueprint("MyBlueprint")               // See what exists
6. get_graph("MyBlueprint", "EventGraph")     // See existing nodes
7. snapshot_graph("MyBlueprint", "Before changes")  // Save rollback point
8. add_node(...)                              // Make changes
9. connect_pins(...)                          // Wire nodes
10. set_pin_default(...)                      // Set values
11. validate_blueprint("MyBlueprint")         // Check for errors
12. compile_blueprint("MyBlueprint")          // Compile and save
13. update_project_state(...)                 // Record what was done
```

## Scene Assembly Workflow

When assembling a scene from a screenplay:

```
1. Read the screenplay/script for the target scene
2. get_project_state → find the scene mapping (scene name → level folder → master level)
3. load_level → load the master level and its sublevels
4. list_actors → get all actors with world positions
5. For each interaction described in the screenplay:
   a. Find the relevant actor by name/class fuzzy match
   b. Get its world position from the actor list
   c. spawn_actor → place interaction actors (teleport points, triggers) near the target
   d. set_actor_transform → position precisely based on spatial context
6. Open or create the Level Sequence for this scene
7. ls_bind_actor → bind characters and cameras
8. ls_add_track → add animation, audio, event tracks
9. ls_add_section → set timing for each track
10. Wire the Level Blueprint:
    a. get_level_blueprint → inspect existing logic
    b. add_node → add story step broadcasting, teleport listeners
    c. connect_pins → wire the interaction chain
    d. compile_blueprint → compile the level blueprint
11. update_project_state → mark the scene as complete
```

---

## Example: Bind a Delegate (OnGrabbed -> CustomEvent)

```
// Step 1: Snapshot
snapshot_graph("BP_InteractableTeacup", "Before grab binding")

// Step 2: Add the AddDelegate node for OnGrabbed
add_node(blueprint="BP_InteractableTeacup", graph="EventGraph",
  nodeType="AddDelegate", delegateName="OnGrabbed",
  ownerClass="GrabbableComponent", posX=0, posY=0)
// Returns: { nodeId: "AAA-111", pins: ["execute", "then", "Delegate", "self"] }

// Step 3: Add a CustomEvent to handle the grab
add_node(blueprint="BP_InteractableTeacup", graph="EventGraph",
  nodeType="CustomEvent", eventName="HandleTeacupGrabbed", posX=0, posY=200)
// Returns: { nodeId: "BBB-222", pins: ["then", "OutputDelegate"] }

// Step 4: Wire the delegate pin to the custom event
connect_pins(blueprint="BP_InteractableTeacup",
  sourceNodeId="BBB-222", sourcePinName="OutputDelegate",
  targetNodeId="AAA-111", targetPinName="Delegate")

// Step 5: Now wire HandleTeacupGrabbed's exec to whatever comes next
// (e.g. broadcast a GameplayMessage for the story step)
```

---

## Example: Add a BeginPlay -> PrintString Chain

```
// Step 1: Read the Blueprint
get_graph("MyBlueprint", "EventGraph")

// Step 2: Snapshot before changes
snapshot_graph("MyBlueprint", "Before adding print chain")

// Step 3: Add ReceiveBeginPlay event
add_node(blueprint="MyBlueprint", graph="EventGraph",
  nodeType="OverrideEvent", functionName="ReceiveBeginPlay", posX=0, posY=0)
// Returns: { nodeId: "ABC-123", pins: [...] }

// Step 4: Add PrintString call
add_node(blueprint="MyBlueprint", graph="EventGraph",
  nodeType="CallFunction", functionName="PrintString",
  className="UKismetSystemLibrary", posX=300, posY=0)
// Returns: { nodeId: "DEF-456", pins: [...] }

// Step 5: Wire them together
connect_pins(blueprint="MyBlueprint",
  sourceNodeId="ABC-123", sourcePinName="then",
  targetNodeId="DEF-456", targetPinName="execute")

// Step 6: Set the print text
set_pin_default(blueprint="MyBlueprint",
  nodeId="DEF-456", pinName="InString", value="Hello from AgenticMCP")

// Step 7: Validate
validate_blueprint("MyBlueprint")

// Step 8: Compile
compile_blueprint("MyBlueprint")
```

---

## Supported Node Types

| nodeType | Required Fields | Description |
|----------|----------------|-------------|
| CallFunction | functionName, className? | Call a UFunction |
| CustomEvent | eventName | Create a custom event |
| OverrideEvent | functionName | Override a parent class event (BeginPlay, Tick, etc.) |
| BreakStruct | typeName | Break a struct into individual pins |
| MakeStruct | typeName | Construct a struct from individual pins |
| VariableGet | variableName | Get a variable value |
| VariableSet | variableName | Set a variable value |
| DynamicCast | castTarget | Cast to a class |
| Branch | (none) | If/Then/Else |
| Sequence | (none) | Execution sequence (Then 0, Then 1, ...) |
| MacroInstance | macroName, macroSource? | Instantiate a macro (ForLoop, ForEachLoop, etc.) |
| SpawnActorFromClass | actorClass? | Spawn actor node |
| Select | (none) | Select node |
| Comment | comment?, width?, height? | Comment box |
| Reroute | (none) | Reroute/knot node |
| MultiGate | numOutputs? | MultiGate execution node |
| Delay | duration? | Delay node |
| SetTimer | functionName, time? | Set Timer by Function Name |
| LoadAsset | (none) | Async Load Asset |
| AddDelegate | delegateName, ownerClass? | Bind Event to delegate (e.g. OnGrabbed, OnComponentBeginOverlap). Returns Delegate pin for wiring to CustomEvent. |
| RemoveDelegate | delegateName, ownerClass? | Unbind Event from delegate |
| ClearDelegate | delegateName, ownerClass? | Unbind All Events from delegate |
| CreateDelegate | functionName? | Create a delegate reference (for Assign pattern) |
| CallParentFunction | functionName | Call parent class implementation |
| ForLoop | (none) | Standard for loop macro |
| ForEachLoop | (none) | For each loop macro |
| WhileLoop | (none) | While loop macro |
| ForLoopWithBreak | (none) | For loop with break macro |

---

## Available UE API Context Categories

The `unreal_get_ue_context` tool provides reference documentation for:
- `actor` — Actor spawning, components, transforms, attachment
- `animation` — Animation Blueprints, state machines, montages
- `assets` — Asset loading, references, soft/hard pointers
- `blueprint` — K2Node hierarchy, pin types, graph manipulation
- `character` — Character movement, capsule, mesh setup
- `enhanced_input` — Input actions, mappings, triggers
- `material` — Material expressions, instances, parameters
- `parallel_workflows` — Batch operations, parallel patterns
- `replication` — Network replication, RPCs, variable replication
- `slate` — Editor UI, widgets, panels
- `level_sequence` — Level Sequence Python API (tracks, bindings, keyframes)
- `python_scripting` — General Python editor scripting (actors, assets, levels)
- `scene_awareness` — Scene inference, spatial reasoning, contextual placement
- `data_tables` — Data Table and Data Asset read/write operations

---

## Offline Mode Tools

When the editor is not running, these tools are available:
- `offline_list_levels` — List all `.umap` files in the project
- `offline_list_actors` — List actors in a `.umap` file
- `offline_get_actor` — Get actor properties
- `offline_get_graph` — Get Blueprint graph from a level
- `offline_level_info` — Get level summary
- `offline_generate_paste_text` — Generate Blueprint paste text for manual import

---

## Critical Rules

1. **Always read project state on connection.** The state file is your memory. Without it you are blind.
2. **Always snapshot before destructive operations.** The snapshot system exists for a reason — use it.
3. **Always validate before compiling.** Catch errors before they persist.
4. **Node IDs are GUIDs.** Every `add_node` returns a GUID. Store it for subsequent `connect_pins` and `set_pin_default` calls.
5. **Pin names are case-sensitive.** Use `get_pin_info` to discover exact pin names before connecting.
6. **Level blueprints use map names.** Pass the `.umap` name (e.g., `"MyLevel"`), not a Blueprint path.
7. **Compile after all changes.** Batch your mutations, then compile once at the end.
8. **Check status first.** If the editor is not connected, only offline tools are available.
9. **Update project state after changes.** Every spawn, delete, move, or compile should update the state file.
10. **Use Python for anything the C++ handlers do not cover.** Level Sequences, Data Tables, asset imports, complex queries — Python can do it all.
11. **Do not hallucinate capabilities.** If it is not in CAPABILITIES.md, you cannot do it.
12. **Use scene inference.** When a screenplay says "Scene 8 — Pluma," search for S8 folders, check DA_GameData, and find the level. Do not ask the user to spell it out.

## Error Handling

- If a tool returns `isError: true`, read the error message carefully. It often includes available options (valid pin names, graph names, class names).
- If compilation fails, use `validate_blueprint` to get detailed error information.
- If you break something, use `restore_graph` to roll back to the last snapshot.
- If the editor disconnects mid-operation, the bridge will automatically switch to offline mode on the next call.
- If Python execution fails, check `get_output_log` for the full error traceback.
