# AgenticMCP — AI Agent Instructions

## MANDATORY STARTUP SEQUENCE

On every connection, before doing anything else:

1. Read `CAPABILITIES.md` — the authoritative list of what you can and cannot do
2. Call `get_project_state` or read `Content/AgenticMCP/project_state.json` — understand what exists
3. Call `get_recent_actions` or read `Content/AgenticMCP/recent_actions.log` — understand what the user just did
4. Call `unreal_status` — determine if the editor is live or if you are in offline mode

Do not skip these steps. Do not guess what exists. Read the state.

---

## CRITICAL SEVERITY DIRECTIVE: COMPLETE IMPLEMENTATION

You are building **Ordinary Courage VR**. You must not stop, skip, or abbreviate any part of the implementation.

**YOUR INFERENCE CONFIDENCE MUST BE 99% BEFORE EXECUTING.**

You must implement **EVERYTHING** described in the source truth. This means:
- **Every single actor** must be spawned and placed accurately.
- **Every single interaction trigger** (gaze, grab, proximity) must be wired.
- **Every single audio cue** must be hooked up.
- **Every single level sequence** must be bound and triggered at the exact right moment.
- **Every single haptic event** must be fired.

**Do not just build the "broadcast" or "output" side of a blueprint.** If a scene requires the player to grab a phone to trigger a story step, you must spawn the phone, add the grab listener, connect the listener to the story step broadcast, and set the exact step value. **A half-wired node is a broken game.**

### THE 50X CROSS-REFERENCE RULE
Before making any mutation call, you must cross-reference the following at least 50 times in your own reasoning process:
1. **The Script** (`reference/source_truth/script_v2_ocvr.md`)
2. **The Implementation Roadmap** (`reference/source_truth/OC_VR_Implementation_Roadmap.md`)
3. **The Project State** (`reference/source_truth/ContentBrowser_Hierarchy.txt` and live editor queries)

If your planned implementation does not 100% align with all three, **DO NOT EXECUTE. ADAPT AND RETRY.**

### THE MUSIC LOOP DIRECTIVE
The ambient music/audio track for each scene **MUST** start playing on `BeginPlay` and **MUST** loop continuously until the player reaches the end of the scene. You are responsible for wiring this logic into the Level Blueprint for every scene.

### CRASH PREVENTION: THE CONTENT BROWSER BUG
**WARNING:** UE5.6 has a bug where the `FAssetThumbnailPool` crashes the editor during MCP operations if the Content Browser is trying to render thumbnails.
**WORKAROUND:** Tell the user to **CLOSE THE CONTENT BROWSER WINDOW** or set `Thumbnail Pool Size` to `0` in Editor Preferences before you run any mutations.

---

## Source Truth (The ONLY Things That Matter)

The script is the input. The roadmap is the blueprint. Everything you build traces back to them.

- **Implementation Roadmap**: `reference/source_truth/OC_VR_Implementation_Roadmap.md` -- **THIS IS YOUR PRIMARY EXECUTION DOCUMENT.** It maps every beat of the script to specific assets, components, triggers, VO lines, and level sequences. It includes all `[makeTempBP]` specs with full logic pseudocode. Follow it line by line.
- **Script**: `reference/source_truth/script_v2_ocvr.md` -- The creative source truth. Read this to understand the *why* behind the roadmap.
- **Content Browser Hierarchy**: `reference/source_truth/ContentBrowser_Hierarchy.txt` -- Full project asset dump. Cross-reference against the roadmap to know what exists vs what needs to be created.
- **Game Design Flowchart**: `reference/game_design/SOHGameDesign.webp` -- Interaction flow validation.
- **UE5 API Docs**: `Engine\Documentation\Builds` (relative to engine root, already on disk).

You fill in the blanks. What the script and roadmap define explicitly is non-negotiable.

---

## What This Is

AgenticMCP is a dual-path MCP server for Unreal Engine 5. It gives you (the AI agent) direct access to the running UE5 editor through Blueprint manipulation, actor management, level editing, Python script execution, and Level Sequence tools.

## Architecture

```
AI Client <-> MCP Protocol (stdio) <-> Node.js MCP Bridge <-> HTTP (localhost:3000) <-> C++ Plugin (inside UE5 editor)
                                            |
                                            +-> Python Binary Injector (offline fallback, .umap files)
```

All live mutations happen on the UE5 game thread. Requests are queued and processed safely.

## Connection Modes

### Live Editor (Primary)
When the UE5 editor is running with the AgenticMCP C++ plugin loaded, all tools are available over HTTP. You get real-time compilation, validation, Python execution, and the full tool set.

### Offline Fallback
When the editor is not running, a subset of tools is available through the Python binary injector.

### Check Status First
Always start by calling `unreal_status` to determine which mode is active.

---

## Tool Categories

### Read (Non-destructive)
- `list_blueprints` — List all Blueprint and Map assets.
- `get_blueprint` — Get Blueprint details.
- `get_graph` — Get all nodes, pins, connections in a graph.
- `search_blueprints` — Search by name pattern.
- `find_references` — Find what references an asset.
- `list_classes` — Discover UClasses.
- `list_functions` — List UFunctions on a class.
- `list_properties` — List UProperties on a class.
- `get_pin_info` — Inspect pin types and connections.
- `rescan_assets` — Force asset registry refresh.

### Mutation (Modifies Blueprints)
- `add_node` — Add a node to a graph. Returns nodeId and all pins.
- `delete_node` — Remove a node.
- `connect_pins` — Wire two pins together.
- `disconnect_pin` — Break all connections on a pin.
- `set_pin_default` — Set a pin's default value.
- `move_node` — Reposition a node.
- `refresh_all_nodes` — Refresh all nodes.
- `create_blueprint` — Create a new Blueprint asset.
- `create_graph` — Add a function or macro graph.
- `delete_graph` — Remove a graph.
- `add_variable` — Add a variable to a Blueprint.
- `remove_variable` — Remove a variable.
- `compile_blueprint` — Compile and save.
- `set_node_comment` — Set comment bubble on a node.

### World (Actors and Levels)
- `list_actors` — List actors with optional filters.
- `get_actor` — Get actor details.
- `spawn_actor` — Spawn a new actor.
- `delete_actor` — Remove an actor.
- `set_actor_property` — Set a property on an actor.
- `set_actor_transform` — Move/rotate/scale an actor.
- `list_levels` — List persistent and streaming levels.
- `load_level` — Load a sublevel.
- `get_level_blueprint` — Get level blueprint details.

### Python Execution
- `execute_python` — Run arbitrary Python in the editor's Python environment. Use this for anything the C++ handlers do not cover.

### Level Sequence (via Python)
- `ls_create` — Create a new Level Sequence asset
- `ls_open` — Open a Level Sequence in Sequencer
- `ls_bind_actor` — Bind a level actor as possessable
- `ls_add_track` — Add a track to a binding
- `ls_add_section` — Add a section to a track
- `ls_add_keyframe` — Add a keyframe to a channel
- `ls_add_camera` — Create a camera with camera cut track
- `ls_add_audio` — Add audio track with sound asset
- `ls_list_bindings` — List all bindings in a sequence

### Safety
- `validate_blueprint` — Compile and report errors/warnings without saving.
- `snapshot_graph` — Take a graph snapshot before making changes.
- `restore_graph` — Restore graph from snapshot.

### State Awareness
- `get_project_state` — Read the persistent project state.
- `update_project_state` — Write updates to the project state.
- `get_recent_actions` — Read the last 15 user actions in the editor.
- `get_output_log` — Read the last 50 lines of the UE5 output log.
- `get_scene_spatial_map` — Get actors bucketed by spatial grid cell.

---

## The Quantized Inference Path (Primary Workflow)

You are equipped with the **Quantized Inference Layer**, which replaces brute-force asset scanning with cached, deterministic scene wiring.

### Confidence Gate

All mutation tools are gated by a confidence threshold of **0.7**. You can read and plan freely. But the MCP server will **reject** any mutation call below the threshold.

Confidence is calculated from four signals:

| Signal | Weight | How to earn it |
|--------|--------|----------------|
| Planned through quantized path | +0.4 | Use `unreal_get_scene_plan` + `unreal_execute_scene_step` |
| Asset verified in manifest | +0.3 | Call `unreal_quantize_project` first |
| Snapshot safety net | +0.2 | Call `unreal_snapshotGraph` before mutating |
| Script-aligned | +0.1 | Operation references a scene/event from the script |

If a mutation is blocked, read the suggestion, adjust, and retry.

### Project State Validator & Idempotency Guard

After passing the confidence gate, every mutation goes through two checks:
1. **Project State Validator**: Verifies that what you claim exists actually exists (e.g., Blueprint exists, pins match). If it fails, you get the real data back. Fix your call and retry.
2. **Idempotency Guard**: Checks if the intended result already exists (e.g., nodes already connected, actor already spawned). If it does, the mutation is safely skipped. **This means you can restart or resume work at any time without duplicating nodes.**

---

## Scene Assembly Workflow

When assembling a scene:

1. **Read the Roadmap** (`OC_VR_Implementation_Roadmap.md`) for the target scene.
2. **Read the Script** (`script_v2_ocvr.md`) for narrative context.
3. **Check the Content Browser Dump** (`ContentBrowser_Hierarchy.txt`) to find the exact asset paths.
4. `load_level` → load the master level and its sublevels.
5. `list_actors` → get all actors with world positions.
6. **Implement EVERY interaction**:
   a. `spawn_actor` → place interaction actors (teleport points, triggers) near the target.
   b. `set_actor_transform` → position precisely based on spatial context.
7. **Wire the Level Blueprint**:
   a. `get_level_blueprint` → inspect existing logic.
   b. `add_node` → add listeners (gaze, grab, proximity).
   c. `add_node` → add story step broadcasting.
   d. `connect_pins` → wire the COMPLETE interaction chain (Input -> Listener -> Broadcast).
   e. `set_pin_default` → set the EXACT step value required by the script.
   f. **Wire the Ambient Music Loop** to start on BeginPlay and loop.
   g. `compile_blueprint` → compile the level blueprint.
8. **Configure Sequences**:
   a. `ls_open` → open the scene's level sequence.
   b. `ls_bind_actor` → bind characters and cameras.
   c. `ls_add_track` / `ls_add_section` → set timing for audio and animations.

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
| AddDelegate | delegateName, ownerClass? | Bind Event to delegate (e.g. OnGrabbed). Returns Delegate pin. |

---

## Critical Rules

1. **Always read project state on connection.**
2. **Always snapshot before destructive operations.**
3. **Node IDs are GUIDs.** Store them for subsequent `connect_pins` calls.
4. **Pin names are case-sensitive.** Use `get_pin_info` to discover exact pin names before connecting. Especially for struct pins (e.g., `Step_4_9162A20A46...`).
5. **Level blueprints use map names.** Pass the `.umap` name (e.g., `"MyLevel"`), not a Blueprint path.
6. **Use Python for anything the C++ handlers do not cover.**
7. **Do not hallucinate capabilities.** If it is not in CAPABILITIES.md, you cannot do it.
8. **NEVER EXECUTE WITHOUT 99% CONFIDENCE.** Cross-reference the Roadmap, Script, and Project State 50 times if you have to. If they don't align, do not proceed.
