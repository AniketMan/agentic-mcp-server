# Perforce Audit: SOH VR Project (new-main stream)

## Depot Structure
- Server: ssl:perforce.icvr.io:5900
- Stream: //SOH_VR/new-main
- Total files: 3,538
- Latest changelist: 34337 (Mar 11, 2026)

## File Type Breakdown
| Extension | Count | Notes |
|---|---|---|
| .uasset | 3,140 | UE5 binary assets |
| .h | 121 | C++ headers |
| .cpp | 107 | C++ source |
| .umap | 63 | Level files |
| .py | 19 | Python scripts |
| .cs | 15 | C# (Build.cs files) |
| .png | 13 | Images |
| .ini | 13 | Config |
| .md | 9 | Documentation |
| .json | 9 | JSON configs |
| .uplugin | 6 | Plugin descriptors |

## Content Directory Structure
- Assets/ (Characters, FPWeapon, Interactables, LevelPrototyping, Mixamo, MotionCapture, Props, SELs, Skies, TextMessages, VRSpectator, VRTemplate)
- Blueprints/ (Audio, Data, Game, GamePlay, Player, Test, UI)
- Characters/ (Heathers)
- DynamicEnv/
- Input/
- Maps/Game/ (Hospital, Main, Restaurant, Scene6, Scene8, Scene_9, Trailer)
- Maps/Test/ (18 test levels)
- MasterMaterial/
- Materials/
- Movies/
- Sequences/ (Scene1-Scene9, ~50 Level Sequences)
- Sounds/
- Textures/
- UsdAssets/
- VFX/

## Level Architecture (Master + Sublevels)
Each scene uses ML_ (Master Level) + SL_ (Sublevel) pattern:
- Hospital: ML_Hospital + SL_Hospital_{Art, Blockout, Lighting, Logic}
- Main: ML_Main + SL_Main_{Art, Blockout, Lighting, Logic}
- Restaurant: ML_Restaurant + SL_Restaurant_{Art, Blockout, Lighting, Logic}
- Scene6: ML_Scene6 + SL_Scene6_{Art, Blockout, Lighting, Logic}
- Scene8: ML_TrailerScene8 + SL_TrailerScene8_{Art, Blockout, Lighting, Logic}
- Scene_9: ML_Scene9 + ML_Scene9_{Art, Blockout, Lighting}
- Trailer: ML_Trailer + SL_Trailer_{Art, Blockout, Lighting, Logic}

## Level Sequences (50 total)
Scene1: LS_1_1 through LS_1_4, LS_HugLoop
Scene2: LS_2_1, LS_2_2R, LS_2_2R1, LS_2_2R2, LS_2_3
Scene3: LS_3_1 through LS_3_7 (with v2 variants)
Scene4: LS_4_1 through LS_4_4
Scene5: LS_5_1 through LS_5_4 (+ intro_loop, grip variants)
Scene6: LS_6_1 through LS_6_5
Scene7: LS_7_1 through LS_7_6
Scene8: LS_8_1 through LS_8_5
Scene9: LS_9_1 through LS_9_4

## Existing Plugins
1. DevmateMCP - Added 2026/03/06 (CL 34288). Has MCP server, subsystem. This is an earlier/simpler version of the AgenticMCP concept.
2. JarvisEditor - Added 2026/03/06 (CL 34288). Has BlueprintLibrary, SOHInteractionInjector.
3. MetaXRAudio
4. MetaXRHaptics
5. OculusXRMovement
6. GameplayMessageRouter

## C++ Source Structure (SohVr module)
- Game/: GameMode, GameState, SceneManager, StoryHelpers, SceneSequenceOverride, PlayerStartPoint
- Interaction/: ActivatableActor, ActivatableComponent, CollisionInteraction, GazeSubsystem, GrabbableComponent, HandGrabberComponent, HandInteractorComponent, InteractableComponent
- Player/: (likely VR pawn, movement)
- UI/: (likely HUD, menus)

## Recent Activity (Last 10 CLs)
- CL 34337 (Mar 11): ricardotorres - added flowers alembic animation
- CL 34313 (Mar 10): aniketbhatt - HISM/ISM components solved
- CL 34295 (Mar 7): aniketbhatt - Reverted SL_Restaurant_Logic
- CL 34294 (Mar 7): aniketbhatt - Fix build: Deleted corrupted MoreLogicTests
- CL 34293 (Mar 7): aniketbhatt - Reverted SL_Restaurant_Logic
- CL 34292 (Mar 7): aniketbhatt - SOH VR Updates: DevmateMCP get-...
- CL 34291 (Mar 6): ricardotorres - added final color versions
- CL 34290 (Mar 6): aniketbhatt - UE5.6 Oculus Fork - GPU Lightmass
- CL 34289 (Mar 6): aniketbhatt - GPU Lightmass build scripts
- CL 34288 (Mar 6): aniketbhatt - DevmateMCP + JarvisEditor Plugins

## Key Observations
1. DevmateMCP is already IN the project (CL 34288). This is a simpler MCP server. AgenticMCP would REPLACE or EXTEND this.
2. JarvisEditor has SOHInteractionInjector - this is likely the "maniac AI" code that went rogue.
3. The project is on UE5.6 Oculus Fork with GPU Lightmass.
4. Two team members: aniketbhatt (you) and ricardotorres (art).
5. Restaurant scene had logic issues (3 reverts in 2 days).
6. 50 Level Sequences across 9 scenes = substantial animation/cinematic content.

## CRITICAL: Existing Plugin Analysis

### DevmateMCP (already in project, port 8080)
A simpler MCP server with 7 routes:
- /api/health
- /api/list-blueprints
- /api/list-actors (with location data)
- /api/execute-python (arbitrary Python in editor)
- /api/run-injector (triggers SOHInteractionInjector)
- /api/create-playground (runs Scripts/create_vr_playground.py)
- /api/get-selection (selected actors with full transform)

### JarvisEditor / SOHInteractionInjector (966 lines)
This is the "maniac AI" code. It is a hardcoded interaction chain injector that:
1. Defines ALL interaction-to-step mappings for ALL 9 scenes (Steps 1-42)
2. Creates 4-node Blueprint chains: CustomEvent -> GetSubsystem -> BroadcastMessage -> MakeStruct
3. Wires pins programmatically using JarvisBlueprintLibrary helper functions
4. Has idempotency checks (skips existing events)
5. Has undo transaction support (Ctrl+Z undoes entire level injection)
6. Has dry-run mode
7. Trigger wiring (Grip/Nav/Overlap/Timer -> CustomEvent) is TODO - logged but not implemented

### What This Means for AgenticMCP Integration
- DevmateMCP runs on port 8080, AgenticMCP runs on port 9847. They can coexist.
- The SOHInteractionInjector is a STATIC, hardcoded version of what AgenticMCP does dynamically.
- JarvisBlueprintLibrary already has helper functions: AddCustomEventNode, AddGetSubsystemNode, AddCallFunctionNode, AddMakeStructNode, ConnectPins, SetPinDefaultValue, GetNodePins, CompileBlueprint, BeginTransaction, EndTransaction
- These helpers are PROVEN to work (they successfully create and wire nodes in the SOH project)
- The trigger wiring (line 947-966) is marked TODO - this is likely where the AI "went rogue" trying to complete incomplete logic

### Level Definitions (from SOHInteractionInjector)
- Main (Scenes 1-2): SL_Main_Logic, 4 interactions (Steps 2-5)
- Trailer (Scenes 3-4): SL_Trailer_Logic, 8 interactions (Steps 7-14)
- Hospital (Scene 5): SL_Hospital_Logic, interactions for Steps 15+
- Restaurant (Scene 6): SL_Restaurant_Logic
- Scene6, Scene8, Scene_9: Additional scenes
