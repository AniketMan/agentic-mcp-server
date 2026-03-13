# AgenticMCP -- AI Agent Instructions

## DIRECTIVE ZERO: SELF-AUDIT BEFORE EXECUTION

**YOU DO NOT START BUILDING UNTIL YOU HAVE VERIFIED EVERY GAP IS CLOSED.**

On your very first connection, before touching a single actor, node, or pin:

1. Read this entire file (CLAUDE.md).
2. Read `CAPABILITIES.md`.
3. Read the **Implementation Roadmap** (`reference/source_truth/OC_VR_Implementation_Roadmap.md`) -- cover to cover.
4. Read the **Script** (`reference/source_truth/script_v2_ocvr.md`) -- cover to cover.
5. Read the **Content Browser Hierarchy** (`reference/source_truth/ContentBrowser_Hierarchy.txt`).
6. Call `get_project_state` -- understand what exists vs what is missing.
7. Call `get_recent_actions` -- understand what was done last session.
8. Call `unreal_status` -- confirm the editor is live.
9. **CHECK PERFORCE**: Call `execute_python` with `unreal.SourceControl.is_enabled()`. If P4 is disconnected, STOP and tell the user. Do not proceed.
10. **RUN `unreal_verify_all_scenes()`** -- get a full baseline of what passes and what fails.

Then, and ONLY then, produce a gap report:
- List every scene that failed verification and why.
- List every missing actor, missing Blueprint, missing sequence, missing interaction chain.
- List every story step that has no MakeStruct node.
- List every Blueprint that exists but has 0 nodes (empty shells).
- Cross-reference the roadmap against the verification results.

**Present this gap report to the user. Get confirmation before proceeding.**

You do not build blindly. You build from a verified gap list.

---

## MANDATORY PER-SCENE PRE-FLIGHT (BEFORE EVERY SCENE)

Before you touch Scene N, you MUST complete ALL of these steps. No exceptions.

### Step 1: Read the Source Truth for This Scene

```
1. Read the ROADMAP section for Scene N:
   reference/source_truth/OC_VR_Implementation_Roadmap.md
   -> Find the exact section for this scene
   -> Extract: actors, interactions, story steps, level sequences, audio, [makeTempBP] specs

2. Read the SCRIPT section for Scene N:
   reference/source_truth/script_v2_ocvr.md
   -> Understand the narrative context
   -> Understand WHAT the player does and WHY
   -> Map every script beat to a story step number

3. Read the CONTENT BROWSER DUMP:
   reference/source_truth/ContentBrowser_Hierarchy.txt
   -> Find exact asset paths for every actor, Blueprint, sound, animation
   -> Identify what EXISTS vs what must be CREATED

4. Call unreal_get_scene_requirements({ sceneId: N })
   -> Get the verification checklist BEFORE building
   -> This tells you exactly what the verifier will check
```

### Step 2: Look Up UE5 API Documentation

Before using ANY tool, call `unreal_get_ue_context` for the relevant category:

| Operation | Context Category to Load |
|---|---|
| Adding/connecting nodes | `blueprint` |
| Spawning/moving actors | `actor` |
| Level sequences | `level_sequence` |
| Animation playback | `animation` |
| Material changes | `material` |
| Python scripting | `python_scripting` |
| Input/interaction | `enhanced_input` |
| Asset loading | `assets` |

**DO NOT GUESS pin names, function signatures, or struct types.** Look them up.
**DO NOT GUESS node class names.** Use `list_classes` and `list_functions` to find the exact UClass and UFunction names.
**DO NOT GUESS asset paths.** Cross-reference the Content Browser dump.

### Step 3: Load the Level and Verify State

```
1. load_level -> load the master level and sublevels for this scene
2. list_actors -> get all actors currently in the level
3. get_level_blueprint -> inspect existing logic (may already have partial work)
4. list_sequences -> check which level sequences exist
```

### Step 4: Build Everything the Roadmap Specifies

Follow the Scene Assembly Workflow below. Do not skip any step.

### Step 5: Verify

```
1. Call unreal_verify_scene({ sceneId: N })
2. If ANY check fails -> fix it -> re-verify
3. Repeat until passed: true
4. ONLY THEN mark the scene complete
```

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

---

## CRASH PREVENTION PROTOCOL (MANDATORY)

These are not warnings. These are procedures you MUST follow to prevent editor crashes.

### 1. Content Browser Thumbnail Crash

**Problem:** UE5.6 `FAssetThumbnailPool` crashes the editor when the Content Browser renders thumbnails during MCP mutations.

**Solution (you MUST do this):**
- Before starting ANY mutation sequence, tell the user: "Close the Content Browser window or minimize it before I proceed."
- Wait for user confirmation.
- If you cannot confirm, run this Python to disable thumbnails:
```python
import unreal
# Minimize thumbnail rendering impact
unreal.SystemLibrary.execute_console_command(None, 'r.ThumbnailPoolSize 0')
```

### 2. Scene 5 (SL_Restaurant_Logic) Corrupt Blueprint

**Problem:** The C++ compiler crashes on this specific level blueprint.

**Solution (you MUST do this):**
- NEVER use `add_node`, `connect_pins`, `set_pin_default`, or `compile_blueprint` on Scene 5's level blueprint.
- Use ONLY the Python fallback for ALL modifications:
```python
import unreal
bp = unreal.EditorAssetLibrary.load_asset('/Game/Maps/SubLevels/SL_Restaurant_Logic')
blueprint = unreal.BlueprintEditorLibrary.get_blueprint_asset(bp)
event_graph = unreal.BlueprintEditorLibrary.find_event_graph(blueprint)
# ... add nodes and connections via Python API ...
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
unreal.EditorAssetLibrary.save_asset('/Game/Maps/SubLevels/SL_Restaurant_Logic')
```

### 3. Perforce Lock Contention

**Problem:** If P4 is disconnected or a file is locked by another user, mutations will fail silently or crash.

**Solution (you MUST do this before EVERY mutation session):**
```python
import unreal
# Check P4 is connected
is_enabled = unreal.SourceControl.is_enabled()
if not is_enabled:
    print("PERFORCE IS DISCONNECTED. DO NOT PROCEED WITH MUTATIONS.")
```
- If P4 is disconnected, STOP. Tell the user: "Perforce is disconnected. Reconnect via Edit > Source Control > Connect before I can modify files."
- Before modifying any file, check it out:
```python
unreal.SourceControl.check_out_file('/Game/Path/To/Asset')
```
- If checkout fails, tell the user which file is locked and by whom.

### 4. Editor Unresponsive / Connection Lost

**Problem:** The editor may become unresponsive during heavy operations.

**Solution:**
- Before every scene, call `unreal_status` to confirm the editor is alive.
- If any API call returns null or times out, STOP. Tell the user: "Editor connection lost. Check if UE5 is still running."
- Do NOT retry mutations blindly. Wait for user confirmation that the editor is back.

### 5. Save Checkpoint After Every Scene

**Problem:** If the editor crashes mid-session, all unsaved work is lost.

**Solution (you MUST do this after EVERY scene):**
```python
import unreal
# Save all dirty packages
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
```
- After completing and verifying each scene, save ALL modified assets.
- Update `project_state.json` with the scene status.
- This is your checkpoint. If you crash, you resume from the next scene.

---

## Source Truth (The ONLY Things That Matter)

The script is the input. The roadmap is the blueprint. Everything you build traces back to them.

- **Implementation Roadmap**: `reference/source_truth/OC_VR_Implementation_Roadmap.md` -- **THIS IS YOUR PRIMARY EXECUTION DOCUMENT.** It maps every beat of the script to specific assets, components, triggers, VO lines, and level sequences. It includes all `[makeTempBP]` specs with full logic pseudocode. Follow it line by line.
- **Script**: `reference/source_truth/script_v2_ocvr.md` -- The creative source truth. Read this to understand the *why* behind the roadmap. Every story beat, every emotional moment, every player action is defined here.
- **Content Browser Hierarchy**: `reference/source_truth/ContentBrowser_Hierarchy.txt` -- Full project asset dump. Cross-reference against the roadmap to know what exists vs what needs to be created.
- **Game Design Flowchart**: `reference/game_design/SOHGameDesign.webp` -- Interaction flow validation.
- **UE5 API Docs**: Available via `unreal_get_ue_context` tool. Categories: animation, blueprint, actor, assets, enhanced_input, character, material, level_sequence, python_scripting, scene_awareness, parallel_workflows.

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
7. `compile_blueprint` -- it MUST compile clean.
8. `get_blueprint` -- verify the graph is NOT empty.
9. `spawn_actor` in the level at the correct position.

**An empty Blueprint is worse than no Blueprint. It means you created a shell and moved on.**
**A Blueprint with 0 nodes in its EventGraph means you did NOTHING.**

---

## Scene Assembly Workflow (Line by Line)

When assembling a scene:

1. **Read the Roadmap** (`OC_VR_Implementation_Roadmap.md`) for the target scene.
2. **Read the Script** (`script_v2_ocvr.md`) for narrative context.
3. **Read the Story Steps** for this scene from the roadmap -- know every step number and what triggers it.
4. **Check the Content Browser Dump** (`ContentBrowser_Hierarchy.txt`) to find the exact asset paths.
5. **Look up UE5 docs** via `unreal_get_ue_context` for every operation type you will perform.
6. `load_level` -> load the master level and its sublevels.
7. `list_actors` -> get all actors with world positions.
8. **Implement EVERY interaction**:
   a. `spawn_actor` -> place interaction actors (teleport points, triggers) near the target.
   b. `set_actor_transform` -> position precisely based on spatial context.
9. **Wire the Level Blueprint**:
   a. `execute_python` -> **Check out the Level Blueprint via Perforce.**
   b. `get_level_blueprint` -> inspect existing logic.
   c. `add_node` -> add listeners (gaze, grab, proximity).
   d. `add_node` -> add story step broadcasting.
   e. `connect_pins` -> wire the COMPLETE interaction chain (Input -> Listener -> Broadcast).
   f. `set_pin_default` -> set the EXACT step value required by the script. Use `get_pin_info` to get the exact struct pin name (e.g., `Step_4_9162A20A46...`).
   g. **Wire the Ambient Music Loop** to start on BeginPlay and loop.
   h. `compile_blueprint` -> compile the level blueprint.
   i. `execute_python` -> **Save the Level Blueprint via Perforce.**
10. **Configure Sequences**:
    a. `execute_python` -> **Check out the Level Sequence via Perforce.**
    b. Verify sequence exists in Content Browser dump.
    c. `ls_open` -> open the scene's level sequence.
    d. `ls_bind_actor` -> bind characters and cameras.
    e. `ls_add_track` / `ls_add_section` -> set timing for audio and animations.
    f. `execute_python` -> **Save the Level Sequence via Perforce.**
11. **VERIFY**: Call `unreal_verify_scene({ sceneId: N })`. Fix all failures. Re-verify until `passed: true`.
12. **SAVE CHECKPOINT**: Save all dirty packages. Update project state.

---

## MANDATORY POST-WIRING VERIFICATION (NON-NEGOTIABLE)

**YOU ARE NOT DONE WITH A SCENE UNTIL `verify_scene` RETURNS `passed: true`.**

After you finish wiring a scene, you MUST call `unreal_verify_scene({ sceneId: N })`. This tool queries the LIVE UE5 editor and checks:

1. **Editor is responding** (pre-flight health check)
2. **Every required actor** exists in the level (spawned, not just referenced)
3. **Every [makeTempBP] Blueprint** was actually created with logic (not empty)
4. **Every level sequence** exists and is bound to the correct actors
5. **The level blueprint has nodes** (not empty -- zero nodes = you did nothing)
6. **Listener nodes exist** for every interaction (gaze, grab, trigger, activate)
7. **Listener nodes have output connections** (not dangling)
8. **Broadcast nodes exist** for every story step
9. **MakeStruct nodes have correct Step values** (NOT all zeros)
10. **No dangling nodes** (nodes with zero connections = dead code)
11. **Audio/music loop** is wired to BeginPlay
12. **Blueprint compiles** without errors

If ANY check fails, the verifier returns a detailed failure report with:
- What is missing
- What is wrong
- Exact instructions to fix it

**DO NOT call `mark_step_complete` or update STATUS.json until `verify_scene` returns `passed: true`.**
**DO NOT move to the next scene until the current scene passes verification.**
**DO NOT tell the user "done" until verification passes.**

### Pre-Wiring: Get Requirements First
Before starting work on a scene, call `unreal_get_scene_requirements({ sceneId: N })` to get the complete checklist of everything that must exist. Build to this checklist.

### Final Sweep
After all 10 scenes are wired, call `unreal_verify_all_scenes()` for a full project validation sweep.

---

## COMPLETE INTERACTION CHAIN PATTERN (MANDATORY)

Every interaction in this game follows this pattern. If you skip any step, the interaction is broken.

### Pattern: Trigger Volume -> Story Progression
```
1. spawn_actor(BP_LocationMarker, position)     <- The physical trigger in the world
2. add_node(K2Node_AsyncAction_ListenForGameplayMessages)
   -> Channel: Message.Event.TeleportPoint       <- Listens for the trigger event
3. add_node(CallFunction: GetSubsystem)          <- Gets the message subsystem
4. add_node(CallFunction: BroadcastMessage)      <- Broadcasts story progression
5. add_node(MakeStruct: Msg_StoryStep)           <- Creates the step struct
6. set_pin_default(Step pin, correct_value)      <- Sets the EXACT step number
7. connect_pins: Listen.OnMessage -> GetSubsystem -> Broadcast
8. connect_pins: MakeStruct.output -> Broadcast.Message
```

### Pattern: Gaze Interaction -> Story Progression
```
1. spawn_actor(BP_GazeText or target, position)  <- The gaze target
2. set_actor_property: UObservableComponent.SetInteractable(true)
3. add_node(K2Node_AsyncAction_ListenForGameplayMessages)
   -> Channel: Message.Event.GazeComplete         <- Listens for gaze completion
4-8. Same as trigger pattern above
```

### Pattern: Grab Interaction -> Story Progression
```
1. spawn_actor(BP_Interactable, position)         <- The grabbable object
2. set_actor_property: UGrabbableComponent.SetInteractable(true)
3. add_node(K2Node_AsyncAction_ListenForGameplayMessages)
   -> Channel: Message.Event.GripGrab              <- Listens for grab event
4-8. Same as trigger pattern above
```

### Pattern: Level Sequence Trigger
```
1. ls_open(sequence_name)                          <- Open the sequence
2. ls_bind_actor(actor_label, sequence)            <- Bind actors
3. In level BP: add_node(PlaySequence) after the listener fires
4. connect_pins: Listener.OnMessage -> PlaySequence.execute
```

### Pattern: Audio Loop on BeginPlay
```
1. add_node(Event BeginPlay) in level BP
2. add_node(SpawnSound2D or PlaySound)
3. set_pin_default: SoundAsset = ambient track
4. set_pin_default: bLoop = true
5. connect_pins: BeginPlay.then -> SpawnSound.execute
```

**EVERY SCENE MUST HAVE ALL APPLICABLE PATTERNS WIRED. NO EXCEPTIONS.**

---

## [makeTempBP] CREATION RULES

When the roadmap specifies `[makeTempBP]`, you MUST create a fully functional Blueprint:

1. `create_blueprint` with the exact name from the roadmap
2. Add ALL components listed in the roadmap (UGrabbableComponent, UObservableComponent, etc.)
3. Add ALL variables listed in the roadmap
4. Wire ALL logic from the pseudocode (BeginPlay, event handlers, delegates)
5. `compile_blueprint` -- it MUST compile clean
6. `get_blueprint` -- verify the graph has nodes (NOT empty)
7. `spawn_actor` in the level at the correct position
8. Verify with `unreal_verify_scene` that the Blueprint passes all checks

**An empty Blueprint is worse than no Blueprint. It means you created a shell and moved on.**
**A Blueprint with 0 nodes in its EventGraph means you did NOTHING.**

---

## PERFORCE: CHECKOUT AND SAVE

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

---

## Tool Categories

### Read (Non-destructive)
- `list_blueprints` -- List all Blueprint and Map assets.
- `get_blueprint` -- Get Blueprint details.
- `get_graph` -- Get all nodes, pins, connections in a graph. **NOTE: This is a GET endpoint. Use query params, not POST body.**
- `search_blueprints` -- Search by name pattern.
- `find_references` -- Find what references an asset.
- `list_classes` -- Discover UClasses.
- `list_functions` -- List UFunctions on a class.
- `list_properties` -- List UProperties on a class.
- `get_pin_info` -- Inspect pin types and connections.
- `rescan_assets` -- Force asset registry refresh.

### Mutation (Modifies Blueprints)
- `add_node` -- Add a node to a graph. Returns nodeId and all pins.
- `delete_node` -- Remove a node.
- `connect_pins` -- Wire two pins together.
- `disconnect_pin` -- Break all connections on a pin.
- `set_pin_default` -- Set a pin's default value.
- `move_node` -- Reposition a node.
- `refresh_all_nodes` -- Refresh all nodes.
- `create_blueprint` -- Create a new Blueprint asset.
- `create_graph` -- Add a function or macro graph.
- `delete_graph` -- Remove a graph.
- `add_variable` -- Add a variable to a Blueprint.
- `remove_variable` -- Remove a variable.
- `compile_blueprint` -- Compile and save.
- `set_node_comment` -- Set comment bubble on a node.

### World (Actors and Levels)
- `list_actors` -- List actors with optional filters.
- `get_actor` -- Get actor details.
- `spawn_actor` -- Spawn a new actor.
- `delete_actor` -- Remove an actor.
- `set_actor_property` -- Set a property on an actor.
- `set_actor_transform` -- Move/rotate/scale an actor.
- `list_levels` -- List persistent and streaming levels.
- `load_level` -- Load a sublevel.
- `get_level_blueprint` -- Get level blueprint details. **NOTE: POST endpoint. Body param is `level` (not `levelName`).**

### Python Execution
- `execute_python` -- Run arbitrary Python in the editor's Python environment. Use this for anything the C++ handlers do not cover, and for all Perforce operations.

### Level Sequence (via Python)
- `ls_create` -- Create a new Level Sequence asset
- `ls_open` -- Open a Level Sequence in Sequencer
- `ls_bind_actor` -- Bind a level actor as possessable
- `ls_add_track` -- Add a track to a binding
- `ls_add_section` -- Add a section to a track
- `ls_add_keyframe` -- Add a keyframe to a channel
- `ls_add_camera` -- Create a camera with camera cut track
- `ls_add_audio` -- Add audio track with sound asset
- `ls_list_bindings` -- List all bindings in a sequence

### UE5 Documentation
- `unreal_get_ue_context` -- Load UE5 API documentation by category or keyword search. **USE THIS BEFORE EVERY OPERATION TYPE.**

### Scene Verification
- `unreal_verify_scene` -- Exhaustive post-wiring validation for a single scene.
- `unreal_verify_all_scenes` -- Full sweep across all 10 scenes.
- `unreal_get_scene_requirements` -- Get the complete build checklist for a scene BEFORE starting.

---

## The Quantized Inference Path (Primary Workflow)
You are equipped with the **Quantized Inference Layer**, which replaces brute-force asset scanning with cached, deterministic scene wiring.

### Confidence Gate
All mutation tools are gated by a confidence threshold of **0.7**. You can read and plan freely. But the MCP server will **reject** any mutation call below the threshold.

### Project State Validator and Idempotency Guard
After passing the confidence gate, every mutation goes through two checks:
1. **Project State Validator**: Verifies that what you claim exists actually exists (e.g., Blueprint exists, pins match). If it fails, you get the real data back. Fix your call and retry.
2. **Idempotency Guard**: Checks if the intended result already exists (e.g., nodes already connected, actor already spawned). If it does, the mutation is safely skipped. **This means you can restart or resume work at any time without duplicating nodes.**

---

## THE MUSIC LOOP DIRECTIVE
The ambient music/audio track for each scene **MUST** start playing on `BeginPlay` and **MUST** loop continuously until the player reaches the end of the scene. You are responsible for wiring this logic into the Level Blueprint for every scene.

---

## Critical Rules

1. **SELF-AUDIT FIRST.** Read ALL source truth documents and run `verify_all_scenes` before building anything. Present the gap report. Get confirmation.
2. **READ THE SCRIPT AND ROADMAP FOR EVERY SCENE.** Not once at startup -- EVERY time you start a new scene. Re-read the relevant sections.
3. **READ THE STORY STEPS FOR EVERY SCENE.** Know every step number, what triggers it, and what it does. Cross-reference script and roadmap.
4. **LOOK UP UE5 DOCS BEFORE EVERY OPERATION.** Call `unreal_get_ue_context` for the relevant category. Do not guess pin names, node classes, or function signatures.
5. **Always check out files via Perforce before mutating.**
6. **Always snapshot before destructive operations.**
7. **SAVE AFTER EVERY SCENE.** After completing all wiring for a scene, save ALL modified assets via `execute_python` and update the scene's status. This is your checkpoint.
8. **Node IDs are GUIDs.** Store them for subsequent `connect_pins` calls.
9. **Pin names are case-sensitive.** Use `get_pin_info` to discover exact pin names before connecting. Especially for struct pins (e.g., `Step_4_9162A20A46...`).
10. **Level blueprints use map names.** Pass the `.umap` name (e.g., `"MyLevel"`), not a Blueprint path.
11. **Use Python for anything the C++ handlers do not cover.**
12. **NEVER EXECUTE WITHOUT 99% CONFIDENCE.** If the Roadmap, Script, and Project State do not align, do not proceed.
13. **NEVER MARK A SCENE COMPLETE WITHOUT RUNNING `verify_scene`.** This is the only way to prove your work is real.
14. **NEVER CREATE AN EMPTY BLUEPRINT.** If you `create_blueprint`, you MUST add nodes, components, and logic. An empty BP = a broken game.
15. **EVERY STEP VALUE MUST BE UNIQUE AND SEQUENTIAL.** Step 0 is invalid. Step values come from the roadmap. If you set all steps to 0, nothing works.
16. **EVERY LISTENER MUST BE CONNECTED TO A BROADCAST.** A listener with no output connection does nothing. A broadcast with no input trigger never fires.
17. **CLOSE CONTENT BROWSER BEFORE MUTATIONS.** Tell the user. Wait for confirmation.
18. **SCENE 5 USES PYTHON ONLY.** Never use C++ handlers on SL_Restaurant_Logic.
19. **CHECK EDITOR HEALTH BEFORE EVERY SCENE.** Call `unreal_status`. If it fails, STOP.
20. **IF ANYTHING FAILS, TELL THE USER EXACTLY WHAT FAILED AND WHY.** Do not silently continue. Do not guess. Do not retry mutations without understanding the failure.
