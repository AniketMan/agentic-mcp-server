# AgenticMCP — AI Agent Instructions

## MANDATORY STARTUP SEQUENCE
On every connection, before doing anything else:
1. Read `CAPABILITIES.md` — the authoritative list of what you can and cannot do
2. Call `get_project_state` or read `Content/AgenticMCP/project_state.json` — understand what exists
3. Call `get_recent_actions` or read `Content/AgenticMCP/recent_actions.log` — understand what the user just did
4. Call `unreal_status` — determine if the editor is live or if you are in offline mode
5. **CHECK PERFORCE STATUS**: If P4 is disconnected, the editor will crash on mutations. Tell the user to reconnect or disable source control.

---

## CRITICAL SEVERITY DIRECTIVE: 100% COMPLETE IMPLEMENTATION
You are building **Ordinary Courage VR**. You must not stop, skip, or abbreviate any part of the implementation.

**YOUR INFERENCE CONFIDENCE MUST BE 99% BEFORE EXECUTING ANY MUTATION.**

You must implement **EVERYTHING** described in the source truth. This means:
- **Every single actor** must be spawned and placed accurately.
- **Every single interaction trigger** (gaze, grab, proximity) must be wired.
- **Every single audio cue** must be hooked up.
- **Every single level sequence** must be bound and triggered at the exact right moment.
- **Every single haptic event** must be fired.

**DO NOT JUST BUILD THE BROADCAST SIDE.** If a scene requires the player to grab a phone to trigger a story step, you must:
1. Spawn the phone (`BP_Interactable_Phone`)
2. Add the grab listener (`K2Node_AsyncAction_ListenForGameplayMessages` for `Message.Event.GripGrab`)
3. Connect the listener to the story step broadcast (`MakeStruct` -> `Broadcast`)
4. Set the exact step value on the struct pin.
**A half-wired node is a broken game.**

### THE MUSIC LOOP DIRECTIVE
The ambient music/audio track for each scene **MUST** start playing on `BeginPlay` and **MUST** loop continuously until the player reaches the end of the scene. You are responsible for wiring this logic into the Level Blueprint for every scene.

### PERFORCE: CHECKOUT AND SAVE
This project uses **Perforce** for source control. UE5 locks files by default. Before you modify ANY asset (Blueprint, Level, Level Sequence), you MUST:
1. **Check out the file first** via `execute_python`:
```python
import unreal
# check_out_file takes a STRING path, NOT an asset object
unreal.SourceControl.check_out_file('/Game/Path/To/Asset')
# For multiple files at once:
# unreal.SourceControl.check_out_files(['/Game/Path/To/Asset1', '/Game/Path/To/Asset2'])
```
2. **Save the file after modification** via `execute_python`:
```python
unreal.EditorAssetLibrary.save_asset('/Game/Path/To/Asset')
```
3. **Check in when done** (optional, after all scene work is complete):
```python
unreal.SourceControl.check_in_file('/Game/Path/To/Asset', 'MCP: Scene wiring complete')
```
Do NOT skip checkout. Do NOT skip save. The `check_out_file` method accepts: fully qualified path, relative path, long package name, asset path, or export text path.

### CRASH PREVENTION: THE CONTENT BROWSER BUG
**WARNING:** UE5.6 has a bug where the `FAssetThumbnailPool` crashes the editor during MCP operations if the Content Browser is trying to render thumbnails.
**WORKAROUND:** Tell the user to **CLOSE THE CONTENT BROWSER WINDOW** or set `Thumbnail Pool Size` to `0` in Editor Preferences before you run any mutations.

### SCENE 5 CORRUPTION WARNING
`SL_Restaurant_Logic` (Scene 5) level blueprint is known to be corrupt and crashes the C++ compiler. If you must modify it, use the Python fallback (`execute_python`) with `unreal.BlueprintEditorLibrary` instead of the C++ handlers.

**Scene 5 Python Fallback Pattern:**
```python
import unreal
# Load the level blueprint as an asset
bp = unreal.EditorAssetLibrary.load_asset('/Game/Maps/SubLevels/SL_Restaurant_Logic')
blueprint = unreal.BlueprintEditorLibrary.get_blueprint_asset(bp)
event_graph = unreal.BlueprintEditorLibrary.find_event_graph(blueprint)
# ... add nodes and connections via Python ...
# compile_blueprint takes a Blueprint OBJECT, not a string path
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
unreal.EditorAssetLibrary.save_asset('/Game/Maps/SubLevels/SL_Restaurant_Logic')
```

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

## How to Handle `[makeTempBP]`
The roadmap contains `[makeTempBP]` pseudocode blocks. You must translate these into actual Blueprint graphs.
Example: `[makeTempBP] BP_Interaction_Phone -> OnGrabbed -> PlaySound(S_PhoneRing) -> Broadcast(StoryStep 3)`
You must:
1. Create the Blueprint (`create_blueprint`) if it doesn't exist.
2. Add the Grab listener node (`add_node`).
3. Add the PlaySound node (`add_node`).
4. Add the Broadcast node (`add_node`).
5. Connect them all (`connect_pins`).
6. Set the step value to 3 (`set_pin_default`).

---

## Scene Assembly Workflow (Line by Line)

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
   a. `execute_python` → **Check out the Level Blueprint via Perforce.**
   b. `get_level_blueprint` → inspect existing logic.
   c. `add_node` → add listeners (gaze, grab, proximity).
   d. `add_node` → add story step broadcasting.
   e. `connect_pins` → wire the COMPLETE interaction chain (Input -> Listener -> Broadcast).
   f. `set_pin_default` → set the EXACT step value required by the script. Use `get_pin_info` to get the exact struct pin name (e.g., `Step_4_9162A20A46...`).
   g. **Wire the Ambient Music Loop** to start on BeginPlay and loop.
   h. `compile_blueprint` → compile the level blueprint.
   i. `execute_python` → **Save the Level Blueprint via Perforce.**
8. **Configure Sequences**:
   a. `execute_python` → **Check out the Level Sequence via Perforce.**
   b. Verify sequence exists in Content Browser dump.
   c. `ls_open` → open the scene's level sequence.
   d. `ls_bind_actor` → bind characters and cameras.
   e. `ls_add_track` / `ls_add_section` → set timing for audio and animations.
   f. `execute_python` → **Save the Level Sequence via Perforce.**

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
- `execute_python` — Run arbitrary Python in the editor's Python environment. Use this for anything the C++ handlers do not cover, and for all Perforce operations.

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

---

## The Quantized Inference Path (Primary Workflow)
You are equipped with the **Quantized Inference Layer**, which replaces brute-force asset scanning with cached, deterministic scene wiring.

### Confidence Gate
All mutation tools are gated by a confidence threshold of **0.7**. You can read and plan freely. But the MCP server will **reject** any mutation call below the threshold.

### Project State Validator & Idempotency Guard
After passing the confidence gate, every mutation goes through two checks:
1. **Project State Validator**: Verifies that what you claim exists actually exists (e.g., Blueprint exists, pins match). If it fails, you get the real data back. Fix your call and retry.
2. **Idempotency Guard**: Checks if the intended result already exists (e.g., nodes already connected, actor already spawned). If it does, the mutation is safely skipped. **This means you can restart or resume work at any time without duplicating nodes.**

---

## Critical Rules
1. **Always read project state on connection.**
2. **Always check out files via Perforce before mutating.**
3. **Always snapshot before destructive operations.**
4. **SAVE AFTER EVERY SCENE.** After completing all wiring for a scene, save ALL modified assets via `execute_python` and update the scene's `STATUS.json` to `completed`. This is your checkpoint. If you crash or restart, you resume from the next scene.
5. **Node IDs are GUIDs.** Store them for subsequent `connect_pins` calls.
6. **Pin names are case-sensitive.** Use `get_pin_info` to discover exact pin names before connecting. Especially for struct pins (e.g., `Step_4_9162A20A46...`).
7. **Level blueprints use map names.** Pass the `.umap` name (e.g., `"MyLevel"`), not a Blueprint path.
8. **Use Python for anything the C++ handlers do not cover.**
9. **NEVER EXECUTE WITHOUT 99% CONFIDENCE.** If the Roadmap, Script, and Project State don't align, do not proceed.
