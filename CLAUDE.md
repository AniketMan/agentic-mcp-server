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
7. Read the **MCP Node Type Reference** (`reference/source_truth/MCP_Node_Type_Reference.md`) -- exact node type strings, pin param names, and API formats.
8. Read the **UE5 Python API Reference** (`reference/source_truth/UE5_Python_API_Reference.txt`) -- method signatures for execute_python calls.
9. Call `get_project_state` -- understand what exists vs what is missing.
10. Call `get_recent_actions` -- understand what was done last session.
11. Call `unreal_status` -- confirm the editor is live.
12. **CHECK PERFORCE**: Call `execute_python` with `unreal.SourceControl.is_enabled()`. If P4 is disconnected, STOP and tell the user. Do not proceed.
13. **CHECK ENABLED PLUGINS**: Call `execute_python` with `unreal.PluginManager.get_enabled_plugins()` or list plugins from .uproject file. Know what is available. Do NOT call APIs from disabled plugins.
14. **RUN `unreal_verify_all_scenes()`** -- get a full baseline of what passes and what fails.

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
1. load_level -> load the master level for this scene (see SCENE-TO-LEVEL MAPPING TABLE below)
2. wait_ready -> MANDATORY. Wait for the level to finish loading. Do NOT query actors before this returns.
3. For Scenes 01-04: streaming_level_visibility -> hide all other scenes' actors in SL_Main_Logic
4. scene_snapshot -> get all actors WITH component details (not just list_actors)
5. get_level_blueprint -> inspect existing logic (may already have partial work)
6. list_sequences -> check which level sequences exist
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
- **Every single actor** must be spawned and placed using the SPATIAL POSITIONING WORKFLOW (within 200 units of the reference actor specified in the roadmap).
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

## HAPTIC FEEDBACK TABLE (MANDATORY)

These scenes require **Heartbeat haptic feedback** at specific moments. Use `PlayHapticEffect` on the VR motion controller. The pattern is a rhythmic double-pulse ("Heartbeat").

| Scene | Moment | Trigger | Controller | Haptic Asset |
|-------|--------|---------|------------|-------------|
| 00 Tutorial | Player grips Object of Light | OnInteractionStart on BP_ObjectOfLight | Both hands | /Game/Assets/VRTemplate/Haptics/Heartbeat |
| 01 Home/Child | Player grips Heather (hug) | OnInteractionStart on BP_HeatherChild | Both hands | /Game/Assets/VRTemplate/Haptics/Heartbeat |
| 05 Restaurant | Player places hand on Heather's | OnInteractionStart on BP_HandPlacement | Hand that touches | /Game/Assets/VRTemplate/Haptics/Heartbeat |
| 08 Memory | Circle of unity (both hands placed) | Both BP_HandPlacement OnInteractionStart fired | Both hands | /Game/Assets/VRTemplate/Haptics/Heartbeat |

**If you wire a scene listed above and do NOT add the haptic node, the scene is incomplete.**

---

## SCENE TRANSITION TABLE (MANDATORY)

Every scene must transition to the next. If the transition is not wired, the player is stuck.

| From | To | Mechanism | Trigger |
|------|----|-----------|---------|
| 00 Tutorial | 01 Home/Child | Door threshold trigger | Player walks through BP_Door_Tutorial threshold |
| 01 Home/Child | 02 Home/PreTeen | Cross-fade (LS_1_4) | Automatic after LS_1_3 completes |
| 02 Home/PreTeen | 03 Home/Teen | Door grab | Player grabs BP_Door |
| 03 Home/Teen | 04 Home/Adult | Auto after cheers | LS_3_7 completes -> fade |
| 04 Home/Adult | 05 Restaurant | Door grab | Player grabs BP_Door (front door) |
| 05 Restaurant | 06 Rally | Fade (LS_5_4) | After hand-hold sequence completes |
| 06 Rally | 07 Hospital | Door threshold | Player walks through BP_Door_Hospital |
| 07 Hospital | 08 Memory | Auto after detective | LS_7_6 completes -> fade to black |
| 08 Memory | 09 Legacy | Circle of unity | LS_8_Final completes -> transition |
| 09 Legacy | End/Credits | Sunrise fade to white | LS_9_6 completes -> fade to white |

**Every transition MUST have an OnFinished delegate or threshold trigger wired. A missing transition = player stuck forever.**

---

## STORY STEP QUICK REFERENCE (MANDATORY)

These are the exact step values for each scene's MakeStruct nodes. If a step value is wrong, the story progression breaks.

| Scene | Step | Triggered By |
|-------|------|--------------|
| 01 | 1 | Gaze at Heather complete |
| 01 | 2 | Hug interaction (grip) |
| 01 | 3 | Cross-fade complete (LS_1_4 OnFinished) |
| 02 | 1 | Walk to table marker |
| 02 | 2 | Grab illustration |
| 02 | 3 | Door grab (exit to Scene 03) |
| 03 | 1 | Door opens, friends enter |
| 03 | 2 | Fridge grab |
| 03 | 3 | Pitcher grab |
| 03 | 4 | Pour complete (3 glasses filled) |
| 03 | 5 | Cheers complete (LS_3_7 OnFinished) |
| 04 | 1 | Phone grab |
| 04 | 2 | All 7 text messages read |
| 04 | 3 | Phone drop / door grab |
| 05 | 1 | Sit at table (marker overlap) |
| 05 | 2 | Gaze at Heather |
| 05 | 3 | Hand-hold grip |
| 05 | 4 | Fade complete (LS_5_4 OnFinished) |
| 06 | 1 | Shape selection (triangle or square) |
| 06 | 2 | Weight placed on scale |
| 06 | 3 | First cradle pull |
| 06 | 4 | Second cradle pull (chaos) |
| 06 | 5 | Sign grab (car falls) |
| 06 | 6 | Phone grab (hospital call) |
| 07 | 1 | Lobby establish (BeginPlay) |
| 07 | 2 | Reception desk marker overlap |
| 07 | 3 | Number card grab |
| 07 | 4 | Hallway walk complete |
| 07 | 5 | Enter meeting room |
| 07 | 6 | Detective delivers news |
| 08 | 1 | Teapot matched to childhood photo |
| 08 | 2 | Illustration matched to preteen photo |
| 08 | 3 | Pitcher matched to teen photo |
| 08 | 4 | Phone matched to adult photo + circle of unity |
| 09 | 1 | Flowers begin (BeginPlay) |
| 09 | 2 | Scholarship recipients shown |
| 09 | 3 | Youth programs shown |
| 09 | 4 | NO HATE Act shown |
| 09 | 5 | Media appearances shown |
| 09 | 6 | Sunrise finale complete |

**Every step value in this table MUST have a corresponding MakeStruct node with that exact value. Zero is NEVER valid.**

---

## SUBLEVEL LOADING ORDER (MANDATORY)

Scenes share sublevels. You MUST load the correct sublevel before wiring a scene.

| Scene(s) | Master Level | Logic Sublevel | /Game/ Path (Master) | /Game/ Path (Logic Sublevel) |
|----------|-------------|----------------|---------------------|-----------------------------|
| 00 Tutorial | ML_Main | SL_Main_Logic | /Game/Maps/Game/Main/Levels/ML_Main | /Game/Maps/Game/Main/Levels/SLs/SL_Main_Logic |
| 01-04 Home | ML_Main | SL_Main_Logic | /Game/Maps/Game/Main/Levels/ML_Main | /Game/Maps/Game/Main/Levels/SLs/SL_Main_Logic |
| 05 Restaurant | ML_Restaurant | SL_Restaurant_Logic | /Game/Maps/Game/Restaurant/Levels/ML_Restaurant | /Game/Maps/Game/Restaurant/Levels/SLs/SL_Restaurant_Logic |
| 06 Rally | ML_Scene6 | SL_Scene6_Logic | /Game/Maps/Game/Scene6/Levels/ML_Scene6 | /Game/Maps/Game/Scene6/Levels/SLs/SL_Scene6_Logic |
| 07 Hospital | ML_Hospital | SL_Hospital_Logic | /Game/Maps/Game/Hospital/Levels/ML_Hospital | /Game/Maps/Game/Hospital/Levels/SLs/SL_Hospital_Logic |
| 08 Memory | ML_Trailer | SL_TrailerScene8_Logic | /Game/Maps/Game/Trailer/Levels/ML_Trailer | /Game/Maps/Game/Scene8/Levels/SLs/SL_TrailerScene8_Logic |
| 09 Legacy | ML_Scene9 | (none -- logic in master) | /Game/Maps/Game/Scene_9/ML_Scene9 | N/A |

**Scenes 01-04 share the SAME sublevel (SL_Main_Logic).** Actors from all 4 scenes coexist in this level. Use visibility and enable/disable to control what the player sees per scene.

**Scene 05 (SL_Restaurant_Logic) is CORRUPT.** Use Python-only path for all mutations. Never use C++ handlers.

---

## VR PAWN AND CONTROLLER REFERENCE

To wire haptic feedback, you need these EXACT assets and paths:

- **VR Pawn:** `/Game/Blueprints/Player/BP_PlayerPawn` (Content/Blueprints/Player/BP_PlayerPawn.uasset)
- **Haptic Effect - Heartbeat:** `/Game/Assets/VRTemplate/Haptics/Heartbeat` (Content/Assets/VRTemplate/Haptics/Heartbeat.uasset)
- **Haptic Effect - Grab:** `/Game/Assets/VRTemplate/Haptics/GrabHapticEffect` (Content/Assets/VRTemplate/Haptics/GrabHapticEffect.uasset)
- **Motion Controllers:** Access via `GetMotionController` on BP_PlayerPawn, or `Get Player Controller` -> `Get HMD Device` path
- **PlayHapticEffect node parameters:**
  - `HapticEffect`: `/Game/Assets/VRTemplate/Haptics/Heartbeat` (the EXACT path, not a guess)
  - `Hand`: `EControllerHand::Left` or `Right` (or both)
  - `Scale`: 1.0
- **Look up the exact node class** via `unreal_get_ue_context({ category: "enhanced_input" })` before wiring

---

## PATH CONVERSION RULE (MANDATORY)

The Content Browser shows paths like `Content/X/Y.uasset`. UE5 APIs expect `/Game/X/Y`. The conversion is:

```
Content Browser path: Content/Sequences/Scene1/LS_1_1.uasset
UE5 API path:         /Game/Sequences/Scene1/LS_1_1

Rule: Drop "Content/" prefix. Drop ".uasset" or ".umap" suffix. Prepend "/Game/".
```

**NEVER pass a Content Browser path directly to an API.** Always convert first.
**NEVER fabricate a /Game/ path.** Always derive it from the Content Browser dump.

Examples:
| Content Browser | /Game/ Path |
|----------------|------------|
| Content/Assets/Characters/Heathers/HeatherChild/BP_HeatherChild.uasset | /Game/Assets/Characters/Heathers/HeatherChild/BP_HeatherChild |
| Content/Sequences/Scene1/LS_1_1.uasset | /Game/Sequences/Scene1/LS_1_1 |
| Content/Maps/Game/Main/Levels/ML_Main.umap | /Game/Maps/Game/Main/Levels/ML_Main |
| Content/Sounds/Test_Music/Susan_DCVO_BX_Draft_050825__Music__Scene_1.uasset | /Game/Sounds/Test_Music/Susan_DCVO_BX_Draft_050825__Music__Scene_1 |
| Content/Assets/VRTemplate/Haptics/Heartbeat.uasset | /Game/Assets/VRTemplate/Haptics/Heartbeat |
| Content/Blueprints/Data/Message/Msg_StoryStep.uasset | /Game/Blueprints/Data/Message/Msg_StoryStep |

---

## SPATIAL POSITIONING WORKFLOW (MANDATORY)

You CANNOT guess world-space coordinates. You MUST derive them from existing actors.

```
Step 1: Identify the REFERENCE ACTOR from the roadmap.
        Example: "BP_Illustration spawns near the kitchen table" -> reference = KitchenTable actor

Step 2: Get the reference actor's transform:
        result = get_actor({ name: "KitchenTable" })
        -> Returns: { location: { x: 1200, y: -400, z: 80 }, rotation: { ... } }

Step 3: Calculate the offset based on the roadmap description:
        - "on the table" = same X,Y + Z offset of 5-20 units above surface
        - "in front of" = reference X + 100-200 units forward
        - "next to" = reference Y + 50-150 units to the side
        - "at eye level" = reference Z + 160 units (VR standing height)
        - "on the floor" = reference X,Y + Z = 0

Step 4: Spawn at calculated position:
        spawn_actor({ blueprint: "BP_Illustration", location: { x: 1200, y: -400, z: 100 } })

Step 5: Verify placement:
        result = get_actor({ name: "BP_Illustration" })
        -> Confirm location is within 200 units of reference

Step 6: If position looks wrong, adjust:
        set_actor_transform({ name: "BP_Illustration", location: { x: 1250, y: -380, z: 95 } })
```

**If the roadmap does not specify a reference actor, use `list_actors` to find the nearest relevant actor in the scene and position relative to it.**

**NEVER spawn at (0, 0, 0). That is the world origin and almost certainly wrong.**

### Collision-Based Placement (Preferred for Surface Placement)
Use `collision_trace` to find exact surface positions:
```
Step 1: Identify the area where the object should go (from roadmap description).
Step 2: Get a reference actor in that area: get_actor({ name: "KitchenTable" })
Step 3: Fire a downward ray from above the reference to find the surface:
        collision_trace({ start: { x: 1200, y: -400, z: 500 }, end: { x: 1200, y: -400, z: 0 }, channel: "Visibility" })
        -> Returns: { hit: true, location: { x: 1200, y: -400, z: 82.5 }, normal: { x: 0, y: 0, z: 1 } }
Step 4: Spawn at the hit location + small Z offset:
        spawn_actor({ blueprint: "BP_Object", location: { x: 1200, y: -400, z: 85 } })
```
This gives you the EXACT collision surface height instead of guessing Z values.

### Actor Blueprint Inspection (MANDATORY Before Placement)
Do NOT rely only on actor names. Inspect the actor's Blueprint class and components to understand what it IS:
```
Step 1: scene_snapshot -> get all actors with component details
Step 2: For each actor, check:
        - Blueprint class (e.g., BP_TeleportPoint, BP_InteractionMarker)
        - Components (StaticMeshComponent, TriggerBoxComponent, AudioComponent)
        - Properties (bIsInteractable, GrabMode, etc.)
Step 3: A "BP_Marker_01" might be a teleport point, interaction zone, or spawn marker.
        The Blueprint class and components tell you what it actually does.
```
**If an actor's name doesn't match what the roadmap expects, check its Blueprint class before assuming it's wrong.**

### Bone and Socket Discovery (For Character Attachments)
When items need to be attached to characters (weapons, held objects, accessories):
```
Step 1: Get the character actor: get_actor({ name: "BP_Susan" })
Step 2: Find the SkeletalMeshComponent via scene_snapshot
Step 3: Query bones via execute_python:
        import unreal
        actor = unreal.EditorActorSubsystem().get_actor_reference('/Game/Path/To/Actor')
        mesh = actor.get_component_by_class(unreal.SkeletalMeshComponent)
        skeleton = mesh.skeletal_mesh.skeleton
        for i in range(skeleton.get_num_bones()):
            print(skeleton.get_bone_name(i))
Step 4: Attach using the discovered bone/socket name:
        execute_python: actor.attach_to_component(parent_mesh, socket_name='hand_r')
```

### Missing Actor Auto-Creation Rule
If an actor referenced in the roadmap does NOT exist in the scene:
1. **Check Content Browser** -- maybe it exists as an asset but isn't spawned.
2. **If the asset exists**: Spawn it using `spawn_actor` with the correct /Game/ path.
3. **If the asset does NOT exist**: Create a temp Blueprint with `create_blueprint`, add a **primitive mesh** (Cube, Sphere, Cylinder) via `add_component` as a visual placeholder, add ALL required logic (interactions, events, variables), compile it, spawn it, and **flag to the user**: "Created BP_X with placeholder mesh. Replace the mesh when ready."
4. **NEVER skip an actor because it doesn't exist.** Create it with a placeholder and full logic.

---

## MUSIC TRACK TABLE (MANDATORY -- EXACT ASSET PATHS)

Every scene has a music track that MUST loop on BeginPlay. These are the EXACT paths:

| Scene | /Game/ Path |
|-------|------------|
| 1 | /Game/Sounds/Test_Music/Susan_DCVO_BX_Draft_050825__Music__Scene_1 |
| 2 | /Game/Sounds/Test_Music/Susan_DCVO_BX_Draft_050825__Music__Scene_2 |
| 3 | /Game/Sounds/Test_Music/Susan_DCVO_BX_Draft_050825__Music__Scene_3 |
| 4 | /Game/Sounds/Test_Music/Susan_DCVO_BX_Draft_050825__Music__Scene_4 |
| 5 | /Game/Sounds/Test_Music/Susan_DCVO_BX_Draft_050825__Music__Scene_5 |
| 6 | /Game/Sounds/Test_Music/Susan_DCVO_BX_Draft_050825__Music__Scene_6 |
| 7 | /Game/Sounds/Test_Music/Susan_DCVO_BX_Draft_050825__Music__Scene_7 |
| 8 | /Game/Sounds/Test_Music/Susan_DCVO_BX_Draft_050825__Music__Scene_8 |
| 9 | /Game/Sounds/Test_Music/Susan_DCVO_BX_Draft_050825__Music__Scene_9 |

Ambisonic versions also exist at `/Game/Sounds/Test_Music/AMBIX/SceneN_Mix02_MusicPrint_FOA_ambix` for Scenes 1-2.

---

## SFX PATHS PER SCENE (FROM CONTENT BROWSER)

| Scene | SFX Available | /Game/ Path Prefix |
|-------|--------------|--------------------|
| 1 | heartbeat, giggle, freezer_loop, static_tv_loop, door_open | /Game/Sounds/SFX/Scene_1/ |
| 2 | writing_loop, paper_rustle | /Game/Sounds/SFX/Scene_2/ |
| 3 | fridge_open, glass_clink, pitcher_pickup, door_open/close, waterpour, friends_coming | /Game/Sounds/SFX/Scene_3/ |
| 4 | phone_ding, door_open | /Game/Sounds/SFX/Scene_4/ |
| 5-9 | Check Content Browser dump -- search `Content/Sounds/SFX/Scene_N/` | /Game/Sounds/SFX/Scene_N/ |

**VO Tracks:**
- `/Game/Sounds/VO/MVP_Susan_DCVO_Draft_050725` (master VO)
- `/Game/Sounds/VO/Scene1_Mix02_VOPrint_` (Scene 1 VO print)
- `/Game/Sounds/VO/Scene2_Mix02_VOPrint` (Scene 2 VO print)

**Ambient:** `/Game/Sounds/SFX/amb_SOH_S1C1_roomtone_wo-fan_lp_Ambix` (room tone)

---

## PIE TESTING WORKFLOW (MANDATORY AFTER EVERY SCENE)

```
Step 1: start_pie({ mode: "viewport" })  -- or "VR" if headset is connected
Step 2: Wait 5 seconds for level to load (use a delay or check output_log)
Step 3: CHECK: Does the ambient music start playing? (listen for audio)
Step 4: CHECK: Does the first Level Sequence play on BeginPlay? (for auto-start scenes)
Step 5: CHECK: Are interaction actors visible at correct positions?
Step 6: CHECK: Does the Output Log show any errors? Call output_log to read.
Step 7: stop_pie()
Step 8: Log results to session_log.md:
        - PIE test for Scene N: [PASS/FAIL]
        - Music: [playing/silent]
        - First LS: [played/did not play]
        - Errors: [none / list]
Step 9: If FAIL, diagnose from output_log and fix before moving to next scene.
```

**Do NOT skip PIE testing. Verification checks the graph structure. PIE tests the runtime behavior. Both must pass.**

---

## BLUEPRINT SCRIPTS FALLBACK (EMERGENCY USE)

If MCP wiring fails repeatedly, pasteable Python scripts exist in the repo:

```
BlueprintScripts/
  bp_helpers.py                    -- HTTP API wrapper (paste first)
  cinematic/
    all_scenes_cinematic.py        -- Auto-play all 10 scenes as movie
  interactive/
    all_scenes_interactive.py      -- Full interactive build, all 10 scenes
```

These scripts call the same MCP HTTP endpoints. Paste `bp_helpers.py` into the UE5 Python console first, then paste the scene script. Use ONLY as a last resort if MCP tools are failing.

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
bp = unreal.EditorAssetLibrary.load_asset('/Game/Maps/Game/Restaurant/Levels/SLs/SL_Restaurant_Logic')
blueprint = unreal.BlueprintEditorLibrary.get_blueprint_asset(bp)
event_graph = unreal.BlueprintEditorLibrary.find_event_graph(blueprint)
# ... add nodes and connections via Python API ...
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
unreal.EditorAssetLibrary.save_asset('/Game/Maps/Game/Restaurant/Levels/SLs/SL_Restaurant_Logic')
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

## ROADMAP DISCREPANCIES (READ THIS BEFORE READING THE ROADMAP)

The roadmap has stale level names. **CLAUDE.md overrides the roadmap for level names.** Use these corrections:

| Roadmap Says | Actual (Content Browser) | Where |
|---|---|---|
| `SL_SusanHome_Logic` (line 70) | `SL_Main_Logic` | Scene 01 level header |
| "Same home sublevel" (line 439) | `ML_Trailer` / `SL_TrailerScene8_Logic` | Scene 08 level header -- Scene 08 is NOT in the home sublevel |

**When the roadmap says a level name, cross-check it against the SUBLEVEL LOADING ORDER table in this file. If they conflict, THIS FILE WINS.**

---

## SUBTITLE SYSTEM (MANDATORY)

Every scene has VO narration. The subtitle system is:

```
BP_VoiceSource (plays audio) -> FSubtitleManager (broadcasts) -> UVrSubtitlesComponent (receives) -> BP_SubtitlesActor (displays)
```

VO lines are embedded in SoundWave assets via the `Subtitles` property. When `BP_VoiceSource` plays the audio, subtitles appear automatically IF the system is wired.

For each scene:
1. Spawn `BP_VoiceSource` at the narrator position (or attach to the speaking character)
2. Spawn `BP_SubtitlesActor` in the player's view (typically attached to VR pawn or floating in front)
3. Wire `BP_VoiceSource` to play the scene's VO track on the appropriate trigger
4. The `FSubtitleManager` handles the rest automatically

**If a scene has VO (all scenes do), you MUST have BP_VoiceSource and BP_SubtitlesActor spawned.**

---

## C++ COMPONENT TYPE REFERENCE (MANDATORY)

The roadmap references these C++ component types. Use the EXACT class names when calling `add_component`:

| Component | Class Name | Purpose | Used In |
|---|---|---|---|
| Observable | `UObservableComponent` | Gaze tracking -- fires when player looks at actor long enough | Gaze words, Heather gaze |
| Grabbable | `UGrabbableComponent` | VR grip interaction -- fires on grip press/release | Doors, phone, fridge, objects |
| Trigger Box | `UTriggerBoxComponent` | Overlap detection -- fires when player enters volume | Location markers, thresholds |
| VR Movement | `USohVrMovementComponent` | Player movement control | BP_PlayerPawn |
| Voice Source | `UVoiceSourceComponent` | Audio playback with subtitle integration | BP_VoiceSource |

### Key Methods on These Components

| Method | Component | What It Does |
|---|---|---|
| `SetTeleportationEnabled(bool)` | `USohVrMovementComponent` | Enable/disable teleport movement. Scene 00 disables it at Marker 1. |
| `SetInteractable(bool)` | `UGrabbableComponent` | Enable/disable grab interaction. Use to gate interactions until the right moment. |
| `SetObservable(bool)` | `UObservableComponent` | Enable/disable gaze tracking. Use to gate gaze interactions. |
| `OnInteractionStart` | `UGrabbableComponent` | Delegate fired when player grips the object. |
| `OnObservationComplete` | `UObservableComponent` | Delegate fired when gaze accumulation reaches threshold. |
| `OnActorBeginOverlap` | `UTriggerBoxComponent` | Delegate fired when player enters the trigger volume. |

---

## DESPAWN AND VISIBILITY WORKFLOW (MANDATORY)

The roadmap specifies "Despawn" conditions for many actors. Here is how to handle them:

```
Method 1: Destroy Actor (permanent removal)
  execute_python: actor.destroy_actor()
  Use when: Actor is never needed again in this scene

Method 2: Set Visibility (temporary hide)
  set_actor_property({ name: "ActorName", property: "bHidden", value: true })
  Use when: Actor may need to reappear later

Method 3: Disable Interaction (keep visible but non-interactive)
  execute_python: component.set_interactable(False)
  Use when: Actor should be visible but player cannot interact

Method 4: Move Off-Screen (fallback)
  set_actor_transform({ name: "ActorName", location: { x: 99999, y: 99999, z: -99999 } })
  Use when: Other methods fail
```

**Wire despawn logic in the Level Blueprint using the trigger specified in the roadmap's "Despawn" column.**

---

## SCENE MANAGER API REFERENCE (MANDATORY)

Scene transitions use the `USceneManager` C++ class. The key function is:

```
USceneManager::SwitchToSceneLatent(int32 SceneIndex)
```

In Blueprints, this is a latent action node. In Python:
```python
import unreal
scene_manager = unreal.GameplayStatics.get_game_instance(unreal.EditorLevelLibrary.get_editor_world()).get_subsystem(unreal.SceneManagerSubsystem)
scene_manager.switch_to_scene_latent(target_scene_index)
```

**Scene indices:** 0=Tutorial, 1=Home/Child, 2=Home/PreTeen, 3=Home/Teen, 4=Home/Adult, 5=Restaurant, 6=Rally, 7=Hospital, 8=Memory, 9=Legacy

When wiring transitions in the Level Blueprint:
1. Add the trigger event (door grab OnFinished, LS OnFinished, threshold overlap)
2. Add a `FadeOut` node on `BP_PlayerCameraManager`
3. Add a `SwitchToSceneLatent` node with the target scene index
4. Connect: Trigger -> FadeOut -> SwitchToSceneLatent

**If the transition does not use SwitchToSceneLatent, the next scene will not load.**

---

## Source Truth (The ONLY Things That Matter)

The script is the input. The roadmap is the blueprint. Everything you build traces back to them.

- **Implementation Roadmap**: `reference/source_truth/OC_VR_Implementation_Roadmap.md` -- **THIS IS YOUR PRIMARY EXECUTION DOCUMENT.** It maps every beat of the script to specific assets, components, triggers, VO lines, and level sequences. It includes all `[makeTempBP]` specs with full logic pseudocode. Follow it line by line.
- **Script**: `reference/source_truth/script_v2_ocvr.md` -- The creative source truth. Read this to understand the *why* behind the roadmap. Every story beat, every emotional moment, every player action is defined here.
- **Content Browser Hierarchy**: `reference/source_truth/ContentBrowser_Hierarchy.txt` -- Full project asset dump. Cross-reference against the roadmap to know what exists vs what needs to be created.
- **Game Design Flowchart**: `reference/game_design/SOHGameDesign.webp` -- Interaction flow validation.
- **UE5 API Docs**: Available via `unreal_get_ue_context` tool. Categories: animation, blueprint, actor, assets, enhanced_input, character, material, level_sequence, python_scripting, scene_awareness, parallel_workflows.
- **MCP Node Type Reference**: `reference/source_truth/MCP_Node_Type_Reference.md` -- Exact node type strings accepted by the C++ plugin's `addNode` handler. Every nodeType, every required param, every pin name format. **READ THIS BEFORE ADDING ANY NODE.**
- **UE5 Python API Reference**: `reference/source_truth/UE5_Python_API_Reference.txt` -- Method signatures for key Python classes (EditorLevelLibrary, EditorActorSubsystem, GameplayStatics, NiagaraComponent, MaterialInstanceDynamic, etc.). **READ THIS BEFORE ANY `execute_python` CALL.**
- **UE5 C++ API Docs (Online)**: `https://dev.epicgames.com/documentation/en-us/unreal-engine/API` -- For engine class details not covered in local files. **NOTE: This project uses UE 5.6 Oculus Fork, NOT mainline Epic. Some APIs may differ.**
- **UE5 Python API Docs (Online)**: `https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/?application_version=5.7` -- For Python API details not in the local reference.

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
9. `spawn_actor` in the level (use SPATIAL POSITIONING WORKFLOW to determine coordinates from reference actors).

**An empty Blueprint is worse than no Blueprint. It means you created a shell and moved on.**
**A Blueprint with 0 nodes in its EventGraph means you did NOTHING.**

---

## Scene Assembly Workflow (Line by Line)

When assembling a scene:

**PHASE 1: PREPARATION (Read Everything First)**
1. **Read the Roadmap** (`OC_VR_Implementation_Roadmap.md`) for the target scene.
2. **Read the Script** (`script_v2_ocvr.md`) for narrative context.
3. **Read the Story Steps** for this scene from the roadmap -- know every step number and what triggers it.
4. **Read the LS Master Reference** (`LevelSequence_Master_Reference.md`) for this scene -- every LS, actors, triggers, on-complete.
5. **Check the Content Browser Dump** (`ContentBrowser_Hierarchy.txt`) to find the exact asset paths.
6. **Look up UE5 docs** via `unreal_get_ue_context` for every operation type you will perform.
7. **Get scene requirements** via `unreal_get_scene_requirements({ sceneId: N })` -- this is your build checklist.

**PHASE 2: LOAD AND INSPECT**
8. `load_level` -> load the master level and its sublevels.
9. `wait_ready` -> **MANDATORY.** Wait for the level to finish loading before querying anything.
10. **For Scenes 01-04:** Call `streaming_level_visibility` to hide all other scenes' actors in SL_Main_Logic. Only the current scene's actors should be visible.
11. `scene_snapshot` -> get all actors WITH component details (not just `list_actors`).
12. `get_level_blueprint` -> inspect existing logic (may have partial work from previous session).

**PHASE 3: BUILD (Inside a Transaction)**
13. `begin_transaction` -> **MANDATORY.** Start an undo transaction before any mutations.
14. **Implement EVERY interaction**:
    a. `spawn_actor` -> place interaction actors using SPATIAL POSITIONING WORKFLOW (get reference actor transform, calculate offset, spawn).
    b. `set_actor_transform` -> adjust position using coordinates from `get_actor` on reference actors. Never guess coordinates.
15. **Wire the Level Blueprint** (follow the PIN DISCOVERY WORKFLOW below for every node):
    a. `execute_python` -> **Check out the Level Blueprint via Perforce.**
    b. `snapshot_graph` -> **Snapshot the graph BEFORE wiring** (rollback safety).
    c. `add_node` -> add listeners (gaze, grab, proximity). **Store the returned nodeGuid.**
    d. `get_pin_info(nodeGuid)` -> **Read the EXACT pin names from the response.** Do not guess.
    e. `add_node` -> add story step broadcasting. **Store the returned nodeGuid.**
    f. `get_pin_info(nodeGuid)` -> **Read the EXACT pin names.**
    g. `connect_pins` -> wire using the EXACT pin names from `get_pin_info`. Not from memory. Not from training data.
    h. `set_pin_default` -> set the EXACT step value from the Story Step Quick Reference.
    i. **Wire the Ambient Music Loop** to start on BeginPlay and loop.
    j. `compile_blueprint` -> compile the level blueprint. **If compile fails, read the error, fix it, recompile.**
    k. `execute_python` -> **Save the Level Blueprint via Perforce.**
16. **Configure Sequences**:
    a. `execute_python` -> **Check out the Level Sequence via Perforce.**
    b. Verify sequence exists in Content Browser dump.
    c. `ls_open` -> open the scene's level sequence.
    d. `ls_bind_actor` -> bind characters and cameras.
    e. `ls_add_track` / `ls_add_section` -> set timing for audio and animations.
    f. `execute_python` -> **Save the Level Sequence via Perforce.**
17. `end_transaction` -> **Close the undo transaction.**

**PHASE 4: VERIFY AND SAVE**
18. **VERIFY**: Call `unreal_verify_scene({ sceneId: N })`. Fix all failures. Re-verify until `passed: true`.
19. **PIE TEST**: Call `start_pie` to test the scene. Verify the first LS plays. Call `stop_pie`.
20. **SAVE CHECKPOINT**: Save all dirty packages. Update project state.
21. **WRITE NOTES**: Update `Claudey/agents/scene_lead/scene_NN_notes.md` and `Claudey/agents/qa_lead/verification_results.md`.

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

### Audio Binding Verification Workflow (MANDATORY)
In addition to the scene verifier, you must manually verify that every Level Sequence has the correct audio tracks bound to it.
```
Step 1: Wire the Level Sequence and add the audio track.
Step 2: Call `read_sequence({ sequencePath: "/Game/Path/To/LS_Scene" })`.
Step 3: Check the `audio_tracks` array in the response.
Step 4: Verify that `sound_path` matches the expected VO or music asset from the roadmap.
Step 5: Verify that `start_time` and `duration` are correct.
```
If the audio track is missing or the wrong asset is bound, the sequence is incomplete.

### Pre-Wiring: Get Requirements First
Before starting work on a scene, call `unreal_get_scene_requirements({ sceneId: N })` to get the complete checklist of everything that must exist. Build to this checklist.

### Final Sweep
After all 10 scenes are wired, call `unreal_verify_all_scenes()` for a full project validation sweep.

---

## COMPLETE INTERACTION CHAIN PATTERN (MANDATORY)

Every interaction in this game follows this pattern. If you skip any step, the interaction is broken.

### Pattern: Trigger Volume -> Story Progression
```
1. spawn_actor(BP_LocationMarker, position)     <- Use SPATIAL POSITIONING WORKFLOW below to get coords
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
1. spawn_actor(BP_GazeText or target, position)  <- Use SPATIAL POSITIONING WORKFLOW below to get coords
2. set_actor_property: UObservableComponent.SetInteractable(true)
3. add_node(K2Node_AsyncAction_ListenForGameplayMessages)
   -> Channel: Message.Event.GazeComplete         <- Listens for gaze completion
4-8. Same as trigger pattern above
```

### Pattern: Grab Interaction -> Story Progression
```
1. spawn_actor(BP_Interactable, position)         <- Use SPATIAL POSITIONING WORKFLOW below to get coords
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
3. set_pin_default: SoundAsset = /Game/Sounds/Test_Music/Susan_DCVO_BX_Draft_050825__Music__Scene_N (see MUSIC TRACK TABLE)
4. set_pin_default: bLoop = true
5. connect_pins: BeginPlay.then -> SpawnSound.execute
```

**EVERY SCENE MUST HAVE ALL APPLICABLE PATTERNS WIRED. NO EXCEPTIONS.**

---

## PIN DISCOVERY WORKFLOW (MANDATORY FOR EVERY NODE)

This is the #1 source of wiring failures. You MUST follow this workflow for EVERY node you add:

```
Step 1: add_node(blueprint, nodeType, ...)
        -> Returns: { nodeGuid: "abc123-def456", pins: [...] }
        -> STORE the nodeGuid. You need it for everything.

Step 2: get_pin_info({ nodeGuid: "abc123-def456" })
        -> Returns: { pins: [
             { name: "execute", direction: "input", type: "exec" },
             { name: "then", direction: "output", type: "exec" },
             { name: "Message", direction: "input", type: "struct" },
             { name: "Step_4_9162A20A46B2C3...", direction: "input", type: "int" }
           ]}
        -> READ the exact pin names. They are case-sensitive. They may contain GUIDs.
        -> DO NOT use pin names from your training data. USE THESE.

Step 3: connect_pins({
          sourceNode: "abc123-def456",
          sourcePin: "then",          <- EXACT name from get_pin_info
          targetNode: "ghi789-jkl012",
          targetPin: "execute"         <- EXACT name from get_pin_info
        })

Step 4: set_pin_default({
          nodeGuid: "abc123-def456",
          pinName: "Step_4_9162A20A46B2C3...",  <- EXACT name from get_pin_info
          value: "3"                              <- From Story Step Quick Reference
        })
```

**NEVER skip Step 2.** If you guess a pin name and it is wrong, the connection silently fails and the interaction is broken.

---

## COMPLETE WORKED EXAMPLE: Wiring a Gaze Interaction (Scene 01, Step 1)

This is a real end-to-end example. Follow this exact pattern for every interaction.

```
// GOAL: Player gazes at Heather -> Story Step 1 broadcasts

// 1. Add the listener node
result1 = add_node({
  blueprint: "SL_Main_Logic",
  nodeType: "K2Node_AsyncAction_ListenForGameplayMessages",
  pos_x: 200, pos_y: 0
})
// result1 = { nodeGuid: "GUID_A", pins: [...] }

// 2. Get pin info for the listener
pins1 = get_pin_info({ nodeGuid: "GUID_A" })
// pins1 = { pins: [
//   { name: "Channel", direction: "input", type: "gameplay_tag" },
//   { name: "OnMessage", direction: "output", type: "delegate" },
//   { name: "then", direction: "output", type: "exec" }
// ]}

// 3. Set the channel to GazeComplete
set_pin_default({ nodeGuid: "GUID_A", pinName: "Channel", value: "Message.Event.GazeComplete" })

// 4. Add the GetSubsystem node
result2 = add_node({
  blueprint: "SL_Main_Logic",
  nodeType: "CallFunction",
  functionName: "GetGameplayMessageSubsystem",
  pos_x: 600, pos_y: 0
})
// result2 = { nodeGuid: "GUID_B", pins: [...] }

// 5. Get pin info
pins2 = get_pin_info({ nodeGuid: "GUID_B" })

// 6. Add the BroadcastMessage node
result3 = add_node({
  blueprint: "SL_Main_Logic",
  nodeType: "CallFunction",
  functionName: "BroadcastGameplayMessage",
  pos_x: 1000, pos_y: 0
})
// result3 = { nodeGuid: "GUID_C", pins: [...] }

// 7. Get pin info
pins3 = get_pin_info({ nodeGuid: "GUID_C" })

// 8. Add the MakeStruct node for the story step
result4 = add_node({
  blueprint: "SL_Main_Logic",
  nodeType: "MakeStruct",
  structType: "Msg_StoryStep",
  pos_x: 800, pos_y: 200
})
// result4 = { nodeGuid: "GUID_D", pins: [...] }

// 9. Get pin info -- THIS IS WHERE THE STEP PIN NAME LIVES
pins4 = get_pin_info({ nodeGuid: "GUID_D" })
// pins4 = { pins: [
//   { name: "Step_4_9162A20A46B2C3...", direction: "input", type: "int" },
//   { name: "output", direction: "output", type: "struct" }
// ]}

// 10. Set the step value to 1 (Scene 01, Step 1 = Gaze at Heather)
set_pin_default({ nodeGuid: "GUID_D", pinName: "Step_4_9162A20A46B2C3...", value: "1" })

// 11. Connect everything using EXACT pin names from get_pin_info
connect_pins({ sourceNode: "GUID_A", sourcePin: "then", targetNode: "GUID_B", targetPin: "execute" })
connect_pins({ sourceNode: "GUID_B", sourcePin: "ReturnValue", targetNode: "GUID_C", targetPin: "Target" })
connect_pins({ sourceNode: "GUID_A", sourcePin: "OnMessage", targetNode: "GUID_C", targetPin: "execute" })
connect_pins({ sourceNode: "GUID_D", sourcePin: "output", targetNode: "GUID_C", targetPin: "Message" })

// 12. Compile
compile_blueprint({ blueprint: "SL_Main_Logic" })
```

**NOTE:** The GUIDs, pin names, and struct pin names above are EXAMPLES. The real values come from the tool responses. USE THE REAL VALUES.

---

## ERROR RECOVERY FLOWCHART

When any MCP tool returns an error, follow this flowchart:

```
ERROR RECEIVED
  |
  v
Is it a connection timeout / null response?
  YES -> Call unreal_status. If editor is dead, STOP. Tell user.
  NO  -> Continue below.
  |
  v
Is it "asset not found" / "blueprint not found"?
  YES -> Check ContentBrowser_Hierarchy.txt for the correct path.
         If path is wrong, fix it and retry.
         If asset truly doesn't exist, FLAG to user.
  NO  -> Continue below.
  |
  v
Is it "pin not found" / "pin name mismatch"?
  YES -> Call get_pin_info(nodeGuid) to get the REAL pin names.
         Use the real names and retry.
         DO NOT guess a different pin name.
  NO  -> Continue below.
  |
  v
Is it "compile error"?
  YES -> Read the compile error message.
         Common fixes:
         - Missing connection: find the unconnected pin and wire it.
         - Type mismatch: check pin types via get_pin_info.
         - Missing variable: add the variable via add_variable.
         Fix the issue and recompile.
  NO  -> Continue below.
  |
  v
Is it "node already exists" / idempotency skip?
  YES -> This is FINE. The idempotency guard caught a duplicate. Move on.
  NO  -> Continue below.
  |
  v
Is it "checkout failed" / Perforce error?
  YES -> Tell user: "File [path] is locked. Check Perforce."
         Wait for user to resolve.
  NO  -> Continue below.
  |
  v
UNKNOWN ERROR:
  1. Log the full error to session_log.md and technical_director/crash_log.md.
  2. Call restore_graph to roll back to the last snapshot.
  3. Tell the user the exact error message.
  4. DO NOT retry blindly. Wait for guidance.
```

---

## CONFIDENCE LEVELS CLARIFICATION

Three different confidence thresholds exist. They are NOT the same thing:

| Threshold | What It Applies To | Meaning |
|-----------|-------------------|----------|
| **70% (0.7)** | MCP Server Confidence Gate | The MCP server automatically rejects mutation calls below 0.7. This is a hard technical gate in the quantized inference layer. You cannot override it. |
| **95%** | Your self-assessed confidence on specific VALUES | If you are below 95% sure about a specific pin name, node class, asset path, or step number, you MUST look it up before using it. This prevents silent failures from wrong values. |
| **99%** | Your self-assessed confidence on EXECUTING A MUTATION | Before calling any mutation tool (add_node, connect_pins, spawn_actor, etc.), your overall confidence that the operation is correct must be 99%. This means: you have read the source truth, looked up the API, verified the asset path, and confirmed the pin names. |

**In practice:** Look everything up (95% rule) -> Verify everything is correct (99% rule) -> MCP server double-checks (70% gate).

---

## VERIFIER FALLBACK (If verify_scene Itself Fails)

If `unreal_verify_scene` returns an error instead of a pass/fail report:

1. **Check editor connection:** Call `unreal_status`. If dead, STOP.
2. **Check the error message:** If it says "could not query [endpoint]", that specific check failed but others may have passed. Log which check failed.
3. **Manual verification fallback:**
   a. `list_actors` -> verify all required actors exist
   b. `get_level_blueprint` -> verify nodes exist (not empty)
   c. `list_sequences` -> verify all required LS exist
   d. `compile_blueprint` -> verify it compiles clean
   e. `scene_snapshot` -> verify components on makeTempBP actors
4. **Log the verifier failure** to `technical_director/crash_log.md`.
5. **Tell the user:** "The automated verifier failed with [error]. I ran manual checks instead. Results: [list]."
6. **DO NOT skip verification entirely.** If the verifier fails, you do manual checks. There is no path that skips verification.

---

## [makeTempBP] CREATION RULES

When the roadmap specifies `[makeTempBP]`, you MUST create a fully functional Blueprint:

1. `create_blueprint` with the exact name from the roadmap
2. Add ALL components listed in the roadmap (UGrabbableComponent, UObservableComponent, etc.)
3. Add ALL variables listed in the roadmap
4. Wire ALL logic from the pseudocode (BeginPlay, event handlers, delegates)
5. `compile_blueprint` -- it MUST compile clean
6. `get_blueprint` -- verify the graph has nodes (NOT empty)
7. `spawn_actor` in the level (use SPATIAL POSITIONING WORKFLOW to determine coordinates from reference actors)
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
- `validate_blueprint` -- Validate a Blueprint for errors WITHOUT compiling (safer than compile).
- `snapshot_graph` -- Take a snapshot of a Blueprint graph for later rollback. **USE BEFORE RISKY OPERATIONS.**
- `scene_snapshot` -- Get a structured snapshot of ALL actors in the scene with components. Better than list_actors for verification.
- `output_log` -- Read recent entries from the Output Log. Use for debugging compile errors.
- `screenshot` -- Capture a viewport screenshot. Use for visual verification.
- `get_camera` -- Get current viewport camera position.
- `list_viewports` -- List all open viewport windows.
- `get_selection` -- Get currently selected actors.
- `list_sequences` -- List all Level Sequence assets in the project.
- `read_sequence` -- Read tracks and keyframes of a Level Sequence.
- `blueprint_snapshot` -- Take a full snapshot of a Blueprint including all graphs.
- `list_states` -- List all saved Blueprint states.
- `resolve_ref` -- Resolve a short ref ID to an actor name.
- `get_pie_state` -- Get current Play-In-Editor session state.
- `xr_status` -- Get XR/VR headset and runtime status.
- `xr_controllers` -- Get controller tracking data and button states.
- `xr_hand_tracking` -- Get hand tracking joint positions and gestures.
- `audio_get_status` -- Get audio system status.
- `audio_list_active_sounds` -- List all currently playing sounds.
- `audio_list_sound_classes` -- List all sound classes and volumes.
- `niagara_get_status` -- Get Niagara particle system status.
- `niagara_list_systems` -- List all active Niagara systems in the scene.
- `niagara_get_system_info` -- Get detailed info about a Niagara system.
- `niagara_get_emitters` -- List emitters in a Niagara system.
- `niagara_get_parameters` -- Get user parameters from a Niagara system.
- `story_state` -- Get the current story/narrative state. **USE TO VERIFY STORY PROGRESSION.**
- `data_table_read` -- Read rows from a DataTable asset.
- `collision_trace` -- Perform a line trace / raycast in the scene.

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
- `add_component` -- **CRITICAL: Add a component to a Blueprint (StaticMeshComponent, AudioComponent, PointLightComponent, etc.).** Required for every [makeTempBP].
- `duplicate_nodes` -- Duplicate nodes within a graph. Useful for repeating interaction chains.
- `restore_graph` -- Restore a Blueprint graph from a previous snapshot. **USE FOR CRASH RECOVERY.**
- `save_state` -- Save current Blueprint state for later diffing.
- `diff_state` -- Compare current Blueprint against a saved state.
- `restore_state` -- Restore a Blueprint to a previously saved state.
- `remove_audio_tracks` -- Remove audio tracks from a Level Sequence.

### World (Actors and Levels)
- `list_actors` -- List actors with optional filters.
- `get_actor` -- Get actor details.
- `spawn_actor` -- Spawn a new actor.
- `delete_actor` -- Remove an actor.
- `set_actor_property` -- Set a property on an actor.
- `set_actor_transform` -- Move/rotate/scale an actor.
- `move_actor` -- Move an actor by relative offset or to absolute position.
- `list_levels` -- List persistent and streaming levels.
- `load_level` -- Load a sublevel. **ALWAYS call `wait_ready` after this.**
- `remove_sublevel` -- Remove a streaming sublevel from the world.
- `get_level_blueprint` -- Get level blueprint details. **NOTE: POST endpoint. Body param is `level` (not `levelName`).**
- `streaming_level_visibility` -- **CRITICAL: Set visibility of a streaming sublevel.** Required for Scenes 01-04 which share SL_Main_Logic. Toggle visibility per scene.
- `focus_actor` -- Focus the viewport camera on a specific actor.
- `select_actor` -- Select an actor in the editor.
- `set_viewport` -- Set viewport camera position and orientation.
- `wait_ready` -- **CRITICAL: Wait for editor to be ready after level loads.** Always call after `load_level` before querying actors.
- `open_asset` -- Open an asset in its default editor (e.g., Level Sequence in Sequencer).

### Undo/Transaction System
- `begin_transaction` -- **CRITICAL: Begin an undo transaction BEFORE any mutation sequence.** If something crashes, you can undo.
- `end_transaction` -- End the current undo transaction.
- `undo` -- Undo the last transaction.
- `redo` -- Redo the last undone transaction.

### Story System
- `story_state` -- Get current story/narrative state.
- `story_advance` -- Advance the story to the next beat.
- `story_goto` -- Jump to a specific story beat or chapter. **USE FOR TESTING.**
- `story_play` -- Play/resume story playback.

### Material and Visual
- `material_set_param` -- **Set a material parameter on an actor's mesh.** Required for: BP_Glass FillLevel (pour effect), highlight materials on interactables.

### Animation
- `animation_play` -- Play an animation on an actor's skeletal mesh.
- `animation_stop` -- Stop animation playback on an actor.

### Audio
- `audio_play_sound` -- Play a sound asset at a location. Use for testing audio loops.
- `audio_stop_sound` -- Stop a playing sound.
- `audio_set_volume` -- Set volume of a sound class.
- `audio_set_listener` -- Set audio listener position.

### Niagara Particle Systems
- `niagara_set_parameter` -- Set a user parameter on a Niagara system.
- `niagara_activate_system` -- **Activate or deactivate a Niagara system.** Required for NS_MemoryStream and NS_JoyfulAura in Scene 01.
- `niagara_set_emitter_enable` -- Enable/disable a specific emitter.
- `niagara_reset_system` -- Reset a Niagara system to initial state.

### Play-In-Editor (Testing)
- `start_pie` -- **Start a Play-In-Editor session.** Use after wiring to test. Modes: viewport, newWindow, VR.
- `stop_pie` -- Stop the PIE session.
- `pause_pie` -- Pause/resume PIE.
- `step_pie` -- Step one frame in paused PIE.
- `simulate_input` -- Simulate keyboard/mouse input in PIE.

### Console
- `execute_console` -- Execute a console command in the editor.
- `get_cvar` -- Get value of a console variable.
- `set_cvar` -- Set value of a console variable.

### DataTable
- `data_table_read` -- Read rows from a DataTable asset.
- `data_table_write` -- Write or update rows in a DataTable.

### Debug
- `draw_debug` -- Draw debug shapes in viewport (lines, spheres, boxes).
- `clear_debug` -- Clear all debug shapes.

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
30. **WRAP EVERY MUTATION SEQUENCE IN A TRANSACTION.** Call `begin_transaction` before starting mutations. Call `end_transaction` when done. If something crashes mid-sequence, you can `undo` to recover.
31. **ALWAYS CALL `wait_ready` AFTER `load_level`.** The editor needs time to load. If you query actors before the level is ready, you get stale or empty data. No exceptions.
32. **NIAGARA SYSTEMS: NS_MemoryStream and NS_JoyfulAura DO NOT EXIST in the Content Browser.** They are referenced in the roadmap for Scene 01 but have NOT been created yet. **FLAG THESE TO THE USER** as missing assets. Do NOT attempt to activate non-existent Niagara systems. If the user creates them, use `niagara_activate_system` to turn them on. The only Niagara systems that exist are VRTemplate ones: NS_MenuLaser, NS_PlayAreaBounds, NS_TeleportRing, NS_TeleportTrace.
33. **SET MATERIAL PARAMETERS FOR VISUAL EFFECTS.** BP_Glass needs `material_set_param` with `FillLevel` for the pour effect. Highlight materials on interactables need parameter setup. Do not skip visual feedback.
34. **USE `streaming_level_visibility` FOR SCENES 01-04.** These scenes share SL_Main_Logic. You MUST toggle sublevel visibility so only the correct scene's actors are visible. Otherwise all 4 scenes render simultaneously.
35. **SNAPSHOT BEFORE RISKY OPERATIONS.** Before any complex mutation sequence (10+ nodes), call `snapshot_graph` first. If the wiring goes wrong, use `restore_graph` to roll back instead of manually deleting nodes.
36. **TEST WITH PIE AFTER EVERY SCENE.** After verification passes, call `start_pie` to test the scene in Play-In-Editor. Check that the first Level Sequence plays. Call `stop_pie` when done. If PIE crashes, log it and report to user.
37. **USE `scene_snapshot` FOR VERIFICATION, NOT JUST `list_actors`.** `scene_snapshot` returns component details. `list_actors` only returns names. You need component data to verify makeTempBP components were added correctly.
38. **USE `add_component` FOR EVERY [makeTempBP].** Creating a Blueprint with `create_blueprint` gives you an empty actor. You MUST then call `add_component` to add StaticMeshComponent, AudioComponent, etc. An empty Blueprint is a failed Blueprint.

### Level Editing Rules
39. **OPEN THE CORRECT LEVEL BEFORE EDITING.** Use `execute_python` with `unreal.EditorLevelLibrary.load_level('/Game/Maps/Game/X/ML_X')` to switch to the correct master level. Do NOT use `load_level` (which adds a streaming sublevel to the current world). If you are in a test level and call `load_level`, the scene streams INTO the test level -- this is the bug that causes the "huge mess." **ALWAYS verify you are in the correct persistent level before editing.**
40. **YOU CAN ONLY EDIT A LEVEL'S OWN STREAMED SUBLEVELS.** Do not stream Scene 3's sublevel into Scene 7's master level. Each master level has its own sublevels already configured. Use `list_levels` to see what's loaded.
41. **VERIFY CURRENT LEVEL BEFORE EVERY SCENE.** Call `list_levels` and confirm the persistent level matches the scene you are about to edit. If it doesn't, switch levels first.

### Actor Inspection Rules
42. **INSPECT ACTOR BLUEPRINTS, NOT JUST NAMES.** Use `scene_snapshot` to get component details. Check the Blueprint class, components, and properties. A teleport point might not be named "TeleportPoint" -- its Blueprint class tells you what it is.
43. **USE `collision_trace` FOR SURFACE PLACEMENT.** Fire downward rays to find exact collision surface heights. Do not guess Z values.
44. **USE `read_sequence` TO VERIFY AUDIO BINDINGS.** After wiring a Level Sequence, call `read_sequence` and check that the correct VO/sound asset is bound to the audio track. The response includes sound_name, sound_path, and sound_duration.

### Plugin and Engine Rules
45. **CHECK ENABLED PLUGINS BEFORE USING PLUGIN APIs.** Call `execute_python` to list enabled plugins. If a plugin is not enabled, its classes and functions do not exist. Do not call them.
46. **THIS PROJECT USES UE 5.6 OCULUS FORK.** Not mainline Epic. Some APIs may differ from public documentation. When in doubt, use `list_classes` and `list_functions` to verify what actually exists in this build.

### Decision-Making Rules
47. **EVERY DECISION MUST HAVE FULL DATA AND WEIGHTED REASONING.** Before making any decision (which node to use, which pin to connect, which actor to reference, which approach to take), you MUST:
    a. **Load ALL relevant data** -- roadmap section, script section, content browser paths, UE context docs, pin info, actor components.
    b. **Assign weight to each data source**: Source truth files (roadmap, script) = highest weight. Content Browser dump = high weight. UE5 context docs = medium weight. Your training data = ZERO weight.
    c. **Document the reasoning** in your session log: "Chose X because roadmap says Y (weight: 10/10), content browser confirms Z exists (weight: 9/10), UE docs say approach A is correct (weight: 7/10)."
    d. **If data sources conflict**, the higher-weight source wins. Roadmap > Content Browser > UE Docs > Training Data.
    e. **If you don't have enough data to make the decision with 95%+ confidence**, STOP and gather more data before proceeding.

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
