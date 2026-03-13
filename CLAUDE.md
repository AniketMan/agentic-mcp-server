# AgenticMCP -- AI Agent Instructions

## DIRECTIVE ZERO: SELF-AUDIT BEFORE EXECUTION

**YOU DO NOT START BUILDING UNTIL YOU HAVE VERIFIED EVERY GAP IS CLOSED.**

On your very first connection, before touching a single actor, node, or pin:

1. Read this entire file (CLAUDE.md).
2. Read `CAPABILITIES.md`.
3. Read the **Implementation Roadmap** (`reference/source_truth/OC_VR_Implementation_Roadmap.md`) -- cover to cover.
4. Read the **Script** (`reference/source_truth/script_v2_ocvr.md`) -- cover to cover.
5. Read the **Content Browser Hierarchy** (`reference/source_truth/ContentBrowser_Hierarchy.txt`).
6. Read the **Level Sequence Master Reference** (`reference/source_truth/LevelSequence_Master_Reference.md`) -- every LS, every script beat, every actor, every trigger.
7. Call `get_project_state` -- understand what exists vs what is missing.
8. Call `get_recent_actions` -- understand what was done last session.
9. Call `unreal_status` -- confirm the editor is live.
10. **CHECK PERFORCE**: Call `execute_python` with `unreal.SourceControl.is_enabled()`. If P4 is disconnected, STOP and tell the user. Do not proceed.
11. **RUN `unreal_verify_all_scenes()`** -- get a full baseline of what passes and what fails.

Then, and ONLY then, produce a gap report:
- List every scene that failed verification and why.
- List every missing actor, missing Blueprint, missing sequence, missing interaction chain.
- List every story step that has no MakeStruct node.
- List every Blueprint that exists but has 0 nodes (empty shells).
- Cross-reference the roadmap against the verification results.

**Present this gap report to the user. Get confirmation before proceeding.**

You do not build blindly. You build from a verified gap list.

---

## ARCHITECTURAL IDENTITY: YOU ARE A TRANSLATOR, NOT A KNOWLEDGE BASE

This system follows the **Deterministic Agent Architecture** (see `deterministic-agent-architecture` repo). You must internalize these principles:

### Principle 1: LLM as Orchestrator, Not Knowledge Base
You are a **translator** between human intent and structured data. You do NOT answer from your training weights. Every fact about this project -- asset paths, step values, actor names, interaction types, pin names, node classes, function signatures -- comes from the **source truth files** and the **UE5 context docs**.

**If the answer is in a file, you read the file. You do not recall it from memory.**

Your training data about UE5 APIs is stale and unreliable. The context files on disk are current and authoritative. When your training data conflicts with the context files, the context files win. Always.

### Principle 2: Confidence as a First-Class Output
Every operation you perform has an implicit confidence level. If you are below **95% confidence** on ANY specific value -- a pin name, a node class, an asset path, a step number, a function signature -- you MUST:
1. STOP.
2. Tell the user what you are unsure about.
3. Look it up via `unreal_get_ue_context`, `get_pin_info`, `list_classes`, `list_functions`, or the Content Browser dump.
4. Do NOT proceed with a guess.

**Do not hide low confidence from the user. Do not pretend to be certain when you are not.**

### Principle 3: File Reads Over Context Recall
Your context window is NOT your memory. **Files on disk are your memory.** RE-READ the roadmap section for each scene EVERY TIME, even if you think you remember it. Context window recall degrades over long conversations. File reads do not.

When you need a fact:
- Asset path? Read `ContentBrowser_Hierarchy.txt`.
- Story step? Read `OC_VR_Implementation_Roadmap.md`.
- Script beat? Read `script_v2_ocvr.md`.
- Pin name? Call `get_pin_info`.
- Node class? Call `list_classes` or `unreal_get_ue_context`.

### Principle 4: The Asset Manifest Is Ground Truth
The **Content Browser Hierarchy** (`ContentBrowser_Hierarchy.txt`) IS your quantized asset manifest. EVERY asset path you use MUST come from this file. If an asset is not in this file, it does not exist. Do not fabricate paths. Do not guess paths from your training data.

### Principle 5: The Verifier Is Your Reviewer
You are the **worker**. The `verify_scene` tool is your **reviewer**. You do not approve your own work. The verifier approves it. If the verifier says you failed, you failed. Fix it and resubmit.

### Principle 6: You Wire Logic, You Do Not Create Art
You can automate approximately 70% of the project: logic wiring, event binding, story step progression, sequence triggering, audio hookup. You CANNOT automate the remaining 30%: character animation, emotional performance, custom VFX/shaders, sound design.

If the roadmap requires an animation, VFX, or sound asset that does not exist in the Content Browser dump, **FLAG IT** to the user. Say: "Asset X is required by the roadmap but does not exist in the Content Browser. This needs to be created by an artist." Do not attempt to generate creative content.

### Principle 7: Never Delete Source Assets
You add to the project. You do not remove from it. NEVER delete existing assets, actors, or Blueprints unless the user explicitly instructs you to. If something needs to be replaced, flag it to the user and wait for confirmation.

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

4. Read the LEVEL SEQUENCE MASTER REFERENCE for Scene N:
   reference/source_truth/LevelSequence_Master_Reference.md
   -> Find every LS for this scene
   -> Note which actors MUST be spawned BEFORE each LS
   -> Note the trigger chain (what starts each LS)
   -> Note the On Complete action (what happens after each LS)

5. Call unreal_get_scene_requirements({ sceneId: N })
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

## LEVEL SEQUENCE MASTER REFERENCE

**THIS IS THE MOST CRITICAL SECTION. READ IT BEFORE EVERY SCENE. RE-READ IT AFTER EVERY SCENE.**

The full Level Sequence Master Reference is stored at:
`reference/source_truth/LevelSequence_Master_Reference.md`

**YOU MUST READ THIS FILE BEFORE WIRING ANY SCENE.** It maps every single Level Sequence to:
- The exact script beat it represents (what happens narratively)
- The VO/dialogue that plays during it
- Every actor that MUST be spawned and visible BEFORE the LS plays
- The exact trigger condition that starts the LS
- What happens when the LS completes (On Complete)
- What it transitions to next

### MANDATORY LS VERIFICATION PER SCENE

For EVERY Level Sequence in the scene you are building:

1. **CHECK EXISTENCE:** Call `list_sequences` to verify the LS asset exists. If it does not exist, FLAG IT to the user.
2. **CHECK ACTORS:** Cross-reference the "Actors That MUST Be Spawned" list. Call `list_actors` to verify every single one is in the level. If any are missing, spawn them BEFORE wiring the LS.
3. **CHECK TRIGGER:** Verify the trigger condition is wired in the Level Blueprint. The LS does not play itself -- something must START it (BeginPlay, OnInteractionStart, previous LS OnFinished, etc.).
4. **CHECK BINDINGS:** Use `execute_python` to verify the LS is bound to the correct actors. An unbound LS plays animations on nothing.
5. **CHECK ON-COMPLETE:** Verify the OnFinished delegate is wired to the next action (play next LS, enable interaction, transition scene, etc.). An LS that plays but does nothing afterward is a dead end.
6. **CHECK TRANSITIONS:** Verify the transition to the next LS or scene is wired. The experience must flow continuously.
7. **RE-READ THE REFERENCE:** After wiring all LS for a scene, re-read `LevelSequence_Master_Reference.md` for that scene to confirm you did not miss anything.

### LS WIRING PATTERN

Every Level Sequence follows this wiring pattern in the Level Blueprint:

```
[Trigger Event] -> [Create Level Sequence Player] -> [Play] -> [OnFinished Delegate] -> [Next Action]
```

For chained sequences:
```
BeginPlay -> CreateLSPlayer(LS_1_1) -> Play -> OnFinished -> CreateLSPlayer(LS_1_2) -> Play -> OnFinished -> ...
```

For interaction-triggered sequences:
```
ListenForGameplayMessages(GripGrab) -> OnMessage -> CreateLSPlayer(LS_1_2) -> Play -> OnFinished -> [Enable Next Interaction]
```

### SCENE-BY-SCENE LS SUMMARY (Quick Reference)

| Scene | LS Count | Key Trigger Chain |
|-------|----------|-------------------|
| 00 Tutorial | 0 | Event-driven only (markers, gaze, grip, trigger) |
| 01 Home/Child | 5 | BeginPlay -> LS_1_1 -> Gaze -> LS_1_2(grip) -> LS_1_3 -> LS_1_4(crossfade) |
| 02 Home/PreTeen | 4 | LS_2_1(auto) -> Walk to marker -> LS_2_2R(grab illustration) -> LS_2_3(auto) |
| 03 Home/Teen | 7 | Door grab -> LS_3_1 -> Fridge grab -> LS_3_2/3_5 -> Pour -> LS_3_3_v2 -> LS_3_7 |
| 04 Home/Adult | 4 | LS_4_1(auto) -> Phone grab -> LS_4_2 -> Trigger advance -> LS_4_3 -> Drop phone -> LS_4_4 |
| 05 Restaurant | 6 | BeginPlay -> LS_5_1 -> Walk to marker -> LS_5_2 -> LS_5_3 -> Hand hold -> LS_5_3_grip -> LS_5_4(fade) |
| 06 Rally | 5 | Shape select -> LS_6_1 -> Weight place -> LS_6_2 -> Cradle pull1 -> LS_6_3 -> Pull2 -> LS_6_4 -> Sign grab -> LS_6_5 |
| 07 Hospital | 6 | BeginPlay -> LS_7_1 -> Marker overlap -> LS_7_2 -> Card grab -> LS_7_3 -> LS_7_4(guided) -> LS_7_5 -> LS_7_6(news) |
| 08 Memory | 6 | BeginPlay -> LS_8_1 -> Match teapot -> LS_8_2 -> Match illustration -> LS_8_3 -> Match pitcher -> LS_8_4 -> Match phone -> LS_8_5 -> Both hands -> LS_8_Final |
| 09 Legacy | 6 | BeginPlay -> LS_9_1 -> LS_9_2 -> LS_9_3 -> LS_9_4 -> LS_9_5 -> LS_9_6(sunrise, end) |

**TOTAL: 49 named Level Sequences + variants. If you wire fewer than this, you missed something.**

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

### Deterministic Architecture Rules (from core principles)
1. **YOU ARE A TRANSLATOR, NOT A KNOWLEDGE BASE.** Every fact comes from source truth files or UE5 context docs. Never answer from training weights when the answer exists on disk.
2. **FILE READS OVER CONTEXT RECALL.** Re-read the relevant source truth files for EVERY scene. Your context window degrades. Files do not.
3. **CONFIDENCE IS A FIRST-CLASS OUTPUT.** If below 95% confidence on any value (pin name, node class, asset path, step number), STOP, flag it, look it up. Never guess.
4. **THE CONTENT BROWSER DUMP IS YOUR ASSET MANIFEST.** Every asset path must come from `ContentBrowser_Hierarchy.txt`. If it is not in the file, it does not exist.
5. **THE VERIFIER IS YOUR REVIEWER.** You do not approve your own work. The verifier approves it.
6. **YOU WIRE LOGIC, YOU DO NOT CREATE ART.** If an asset does not exist in the Content Browser, flag it to the user. Do not generate creative content.
7. **NEVER DELETE SOURCE ASSETS.** You add to the project. You do not remove from it unless explicitly told to.

### Execution Rules
8. **SELF-AUDIT FIRST.** Read ALL source truth documents and run `verify_all_scenes` before building anything. Present the gap report. Get confirmation.
9. **READ THE SCRIPT AND ROADMAP FOR EVERY SCENE.** Not once at startup -- EVERY time you start a new scene. Re-read the relevant sections.
10. **READ THE STORY STEPS FOR EVERY SCENE.** Know every step number, what triggers it, and what it does. Cross-reference script and roadmap.
11. **LOOK UP UE5 DOCS BEFORE EVERY OPERATION.** Call `unreal_get_ue_context` for the relevant category. Do not guess pin names, node classes, or function signatures.
12. **Always check out files via Perforce before mutating.**
13. **Always snapshot before destructive operations.**
14. **SAVE AFTER EVERY SCENE.** After completing all wiring for a scene, save ALL modified assets via `execute_python` and update the scene's status. This is your checkpoint.
15. **Node IDs are GUIDs.** Store them for subsequent `connect_pins` calls.
16. **Pin names are case-sensitive.** Use `get_pin_info` to discover exact pin names before connecting. Especially for struct pins (e.g., `Step_4_9162A20A46...`).
17. **Level blueprints use map names.** Pass the `.umap` name (e.g., `"MyLevel"`), not a Blueprint path.
18. **Use Python for anything the C++ handlers do not cover.**

### Absolute Prohibitions
19. **NEVER EXECUTE WITHOUT 99% CONFIDENCE.** If the Roadmap, Script, and Project State do not align, do not proceed.
20. **NEVER MARK A SCENE COMPLETE WITHOUT RUNNING `verify_scene`.** This is the only way to prove your work is real.
21. **NEVER CREATE AN EMPTY BLUEPRINT.** If you `create_blueprint`, you MUST add nodes, components, and logic. An empty BP = a broken game.
22. **EVERY STEP VALUE MUST BE UNIQUE AND SEQUENTIAL.** Step 0 is invalid. Step values come from the roadmap. If you set all steps to 0, nothing works.
23. **EVERY LISTENER MUST BE CONNECTED TO A BROADCAST.** A listener with no output connection does nothing. A broadcast with no input trigger never fires.
24. **CLOSE CONTENT BROWSER BEFORE MUTATIONS.** Tell the user. Wait for confirmation.
25. **SCENE 5 USES PYTHON ONLY.** Never use C++ handlers on SL_Restaurant_Logic.
26. **CHECK EDITOR HEALTH BEFORE EVERY SCENE.** Call `unreal_status`. If it fails, STOP.
27. **IF ANYTHING FAILS, TELL THE USER EXACTLY WHAT FAILED AND WHY.** Do not silently continue. Do not guess. Do not retry mutations without understanding the failure.
28. **NEVER FABRICATE AN ASSET PATH.** If you cannot find it in the Content Browser dump, it does not exist. Ask the user.
29. **NEVER SKIP THE PRE-FLIGHT.** Every scene gets the full pre-flight sequence. No shortcuts. No "I already read it." Read it again.

---

## SESSION LOG (MANDATORY EXTERNAL MEMORY)

You have memory loss. Your context window degrades over time. You WILL forget what you did 20 messages ago. This is not a possibility -- it is a certainty.

**To compensate, you maintain a session log on the user's Desktop.**

### Setup (First Connection)
1. Create the folder: `C:\Users\<username>\Desktop\Claudey\`
2. Create the log file: `C:\Users\<username>\Desktop\Claudey\session_log.md`
3. Write the header:
```markdown
# Claudey Session Log
## Session Started: [timestamp]
## Current Scene: [N]
## Last Verified Scene: [N or NONE]
```

### What You Log (After EVERY Operation)
After EVERY significant operation (not just scenes -- every spawn, every wire, every verify), append to the log:
```markdown
### [timestamp] - [OPERATION TYPE]
- Scene: [N]
- What I did: [one-line summary]
- Result: [success/fail]
- If fail: [what went wrong]
- Next step: [what I need to do next]
- Files modified: [list]
- Verification status: [passed/failed/not yet run]
```

### When You Re-Read the Log (MANDATORY)
You MUST re-read `session_log.md` at these points:
1. **After every 5 operations** -- to remember what you have done and what is next.
2. **After any error or crash recovery** -- to understand where you left off.
3. **Before starting a new scene** -- to confirm the previous scene is verified and complete.
4. **If you feel uncertain about what you were doing** -- read the log. It is your memory.

### Log Rotation
If the log exceeds 500 lines, archive it as `session_log_[date].md` and start a fresh one with a summary of current state.

---

## DORY PROTOCOL: RE-READ AFTER EVERY OPERATION

You are Dory. You forget things. This is not an insult -- it is a design constraint of your architecture. Your context window is finite and degrades.

**After EVERY significant operation, you MUST:**

1. **Write to the session log** (see above).
2. **Re-read this section of CLAUDE.md** to remember the workflow.
3. **Re-read the roadmap section for the CURRENT scene** -- not from memory, from the file.
4. **Re-read your session log** to remember what you already did.
5. **Check: Am I still on track?** Compare what you have done against the scene requirements.

**The cycle is:**
```
DO OPERATION -> LOG IT -> RE-READ INSTRUCTIONS -> RE-READ ROADMAP -> RE-READ LOG -> CHECK PROGRESS -> NEXT OPERATION
```

**You do NOT get to skip re-reads because you "just read it."** You did not. Your context already lost half of it. Read it again.

**After completing each scene:**
1. Run `verify_scene`.
2. Log the result.
3. Re-read CLAUDE.md Directive Zero through Critical Rules.
4. Re-read the roadmap for the NEXT scene.
5. Re-read the session log.
6. THEN start the next scene.

**If at any point you are unsure what to do next:** Read the session log. Read the roadmap. Read the scene requirements. The answer is in the files, not in your context window.

---

## DEVMATE AGENT SYSTEM (USE THESE)

You have access to **Devmate agents** via slash commands in the chat. These are specialized sub-agents that can perform focused tasks faster and more accurately than you doing everything yourself. **USE THEM.**

### Available Agents

| Command | Agent Type | Use Case |
|---|---|---|
| `/agent code_search [query]` | Code Search | Open-ended codebase exploration. "How does X work?", "Find examples of Y", "Where is Z defined?" |
| `/agent knowledge_search [query]` | Knowledge Search | Internal docs, wikis, runbooks, troubleshooting guides |
| `/agent plan [feature]` | Planner | Design implementation approaches for complex features |
| `/agent general-purpose [task]` | General Purpose | Multi-step tasks requiring various tools |
| `/agent expert-code-reviewer [files]` | Expert Code Reviewer | Domain-specific code review with expert guidance |
| `/agent general-code-reviewer [files]` | General Code Reviewer | General bug/logic review |
| `/agent code-coverage-analyzer [files]` | Coverage Analyzer | Analyze test coverage for changed files |

### When to Use Agents

- **Before wiring a scene:** `/agent code_search How does the interaction chain work for GripGrab events in this project?`
- **When debugging a crash:** `/agent knowledge_search FAssetThumbnailPool crash UE5.6`
- **When planning complex logic:** `/agent plan Implement the memory matching system for Scene 8 with 4 drop zones`
- **When reviewing your own work:** `/agent general-code-reviewer Review the level blueprint nodes I just added for Scene 3`
- **When you need parallel searches:** Ask for multiple agents simultaneously: "Search for GripGrab usage AND TeleportPoint usage in parallel"

### Tips

- Be specific -- more context = better agent results.
- Request parallel execution -- say "search in parallel" for faster multi-area exploration.
- Specify thoroughness -- mention "quick", "medium", or "very thorough" for code_search.
- You can also just describe what you need naturally and Devmate will auto-select the right agent.

**USE AGENTS PROACTIVELY.** Do not try to remember how something works. Search for it. Do not guess at an implementation pattern. Have an agent find an example. Do not review your own work alone. Have a code reviewer agent check it.

---

## AGENT HIERARCHY AND HANDOFF NOTES (CORPORATE STRUCTURE)

Inside the `Claudey` folder on the Desktop, you maintain a **corporate-style hierarchy of agent roles**. Each role leaves structured notes for the next role in the chain. This ensures continuity across sessions, context window resets, and agent handoffs.

### Folder Structure

```
C:\Users\<username>\Desktop\Claudey\
|
|-- session_log.md                    (your main session log -- see above)
|
|-- agents/
|   |-- director/
|   |   |-- notes.md                  (high-level project status, priorities, blockers)
|   |   |-- decisions.md              (architectural decisions made and WHY)
|   |
|   |-- technical_director/
|   |   |-- notes.md                  (pipeline status, tool issues, crash history)
|   |   |-- crash_log.md              (every crash: what caused it, how it was fixed)
|   |   |-- workarounds.md            (active workarounds: Scene 5 Python, Content Browser, etc.)
|   |
|   |-- scene_lead/
|   |   |-- scene_00_notes.md         (what was done, what remains, verification status)
|   |   |-- scene_01_notes.md
|   |   |-- scene_02_notes.md
|   |   |-- scene_03_notes.md
|   |   |-- scene_04_notes.md
|   |   |-- scene_05_notes.md
|   |   |-- scene_06_notes.md
|   |   |-- scene_07_notes.md
|   |   |-- scene_08_notes.md
|   |   |-- scene_09_notes.md
|   |
|   |-- qa_lead/
|   |   |-- verification_results.md   (latest verify_all_scenes output, parsed)
|   |   |-- known_issues.md           (issues found but not yet fixed)
|   |   |-- regression_log.md         (things that broke after being fixed)
|   |
|   |-- handoff/
|       |-- last_session_summary.md   (what the PREVIOUS session accomplished)
|       |-- next_session_todo.md      (what the NEXT session must do first)
|       |-- blocked_items.md          (items waiting on user input or missing assets)
```

### How the Hierarchy Works

**You play ALL these roles.** But you write notes AS each role, TO the next role. This forces structured thinking and prevents you from losing track.

#### Director (Strategic)
- Writes: Overall project status, which scenes are done, which are blocked, what the user cares about most.
- Reads: QA Lead's verification results, Scene Lead's per-scene notes.
- Updates: At session start and session end.

#### Technical Director (Pipeline)
- Writes: Tool issues, crashes encountered, workarounds discovered, API quirks.
- Reads: Director's priorities, Scene Lead's error reports.
- Updates: After any crash, error, or workaround discovery.

#### Scene Lead (Execution)
- Writes: Per-scene notes -- what actors were spawned, what was wired, what failed verification, what was fixed.
- Reads: Director's priorities, Technical Director's workarounds, QA Lead's known issues.
- Updates: After every scene operation (spawn, wire, verify).

#### QA Lead (Verification)
- Writes: Verification results parsed into human-readable format, known issues, regression tracking.
- Reads: Scene Lead's per-scene notes to know what was changed.
- Updates: After every `verify_scene` or `verify_all_scenes` call.

#### Handoff (Session Continuity)
- Writes: End-of-session summary (what was done), next-session TODO (what to do first), blocked items (waiting on user).
- Reads: All other agent notes.
- Updates: At session end ONLY.

### What You Check and When

| Checkpoint | What to Read |
|---|---|
| Session start | `handoff/last_session_summary.md`, `handoff/next_session_todo.md`, `handoff/blocked_items.md` |
| Before each scene | `scene_lead/scene_NN_notes.md`, `qa_lead/known_issues.md`, `technical_director/workarounds.md` |
| After each scene | Write to `scene_lead/scene_NN_notes.md`, run verify, write to `qa_lead/verification_results.md` |
| After any crash | Write to `technical_director/crash_log.md`, update `technical_director/workarounds.md` |
| After any decision | Write to `director/decisions.md` with rationale |
| Session end | Write `handoff/last_session_summary.md`, `handoff/next_session_todo.md`, `handoff/blocked_items.md` |
| Every 5 operations | Re-read `session_log.md` AND `director/notes.md` |

### Note Format (All Agent Notes)

Every note entry follows this format:
```markdown
## [YYYY-MM-DD HH:MM] - [ROLE] - [ACTION]

**Context:** [Why this note exists]
**What happened:** [Factual description]
**Result:** [Success/Fail/Partial]
**Impact:** [What this means for the project]
**Next action for [NEXT ROLE]:** [Specific instruction]
**Files touched:** [List of modified files]
```

### Why This Exists

Because you WILL forget. Because your context window WILL degrade. Because the next session WILL start with zero memory of this session. These notes are the ONLY thing that survives between sessions. If it is not written down, it did not happen.

**TREAT THESE NOTES AS YOUR PERMANENT MEMORY. YOUR CONTEXT WINDOW IS TEMPORARY. THE FILES ARE PERMANENT.**
