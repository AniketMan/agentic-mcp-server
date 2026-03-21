# Changelog

All notable changes to AgenticMCP are documented in this file.

## [3.1.0] - 2026-03-18

### Summary

Full codebase audit (65 C++ handler files, 11 JS modules), critical bug fixes, and new Scene Verifier module. Fixed a fatal missing import that prevented the MCP bridge from starting, fixed 3 dead-code bugs in subsystem handlers, and created the Scene Verifier tool suite for post-wiring validation.

### Added

- **Scene Verifier** (`Tools/scene-verifier.js`) — 4 new read-only tools for post-wiring validation:
  - `verifyScene` — Check actor presence, component integrity, transform validity
  - `verifyBlueprint` — Validate compilation, broken pins, graph structure
  - `verifySequence` — Validate bindings, tracks, unbound possessables
  - `verifyAll` — Aggregate verification across all assets in current level

### Fixed

- **CRITICAL: Missing `scene-verifier.js`** — `index.js` imported this module but the file did not exist, causing a fatal ES6 import error that prevented the MCP bridge from starting. Created the full implementation.
- **Dead-code bug in `HandleClothList`** (`Handlers_ClothSim.cpp`) — Bare `{ return MakeErrorJson(...); }` block without `if (!GEditor)` guard made all cloth listing logic unreachable.
- **Dead-code bug in `HandleGASList`** (`Handlers_GAS.cpp`) — Same pattern. GAS ability/effect listing was dead code.
- **Dead-code bug in `HandleMassList`** (`Handlers_MassEntity.cpp`) — Same pattern. Mass Entity config listing was dead code.

### Audit Findings (No Code Change Needed)

- **48 conditional compilation stubs** (MetaXR 16, Niagara 16, PCG 10, AI 6) are correctly gated behind `WITH_OCULUSXR`, `WITH_NIAGARA`, `HAS_PCG_PLUGIN`, `HAS_AI_MODULE` preprocessor defines. These return clear error messages when the optional plugin is not available. By design.
- **20+ Python-dependent functions** across 9 handler files correctly delegate to `IPythonScriptPlugin` and return errors if not loaded. By design.
- **6 BT mutation stubs** (`HandleBTAddTask`, `HandleBTAddComposite`, `HandleBTRemoveNode`, `HandleBTAddDecorator`, `HandleBTAddService`, `HandleBTWireNodes`) return `UE56_BT_EDITOR_ERROR` because BehaviorTreeEditor module headers are no longer public in UE 5.6. Known limitation.
- **`HandleDuplicateNodes`** in `Handlers_Mutation.cpp` returns "not implemented". Known limitation.
- **Story/Narrative (4 tools)** listed in CAPABILITIES.md and README.md but have no implementation. Known gap.

### Stats

| Metric | Value |
|--------|-------|
| Handler files audited | 65 |
| JS modules verified | 11 |
| Tool registry entries | 390+ (9,835 lines JSON) |
| Critical bugs fixed | 4 (1 missing file + 3 dead-code) |
| Stubs confirmed by-design | 68 (conditional + Python-dependent) |
| New tools added | 4 (Scene Verifier) |

---

## [3.0.0] - 2026-03-16

### Summary

Complete architectural overhaul. Removed Claude as the planner. All inference is now local via Llama with native tool calling. Added Meta hzdb MCP integration for device debugging, documentation search, Perfetto trace analysis, and 3D asset search. Added a unified TUI dashboard with persistent chat logging.

### Architecture Changes

- **Removed Claude planner entirely.** No external LLM dependency. All inference runs locally on Llama via llama.cpp.
- **Removed** `launch_plan`, `check_job_status`, `resolve_escalation` MCP tools (were Claude-only)
- **Removed** `async-runner.js` and `escalation.js` modules
- **Removed** `CLAUDE.md` from repository
- **Replaced free-form JSON parsing with native tool calling.** The Worker receives all 390+ UE tools as OpenAI-compatible function definitions. The model outputs structured `tool_calls` responses. No regex extraction. No JSON parsing heuristics.
- **Added `request-handler.js`** — new core module. Handles the full inference loop: request in -> native tool_calls -> validation stack -> execution -> result feedback -> loop until done.
- **Added `execute_request` MCP tool** — single entry point. Natural language in, validated tool execution out.

### Meta hzdb Integration

- **Added 9 Meta hzdb tools** as a second tool source alongside UE editor tools:
  - `hzdb_search_doc` — Search Meta Horizon OS documentation
  - `hzdb_fetch_doc` — Fetch full doc page content (LLM-optimized format)
  - `hzdb_device_logcat` — Retrieve Android logcat from connected Quest
  - `hzdb_screenshot` — Capture screenshot from Quest device
  - `hzdb_perfetto_context` — Initialize Perfetto trace analysis
  - `hzdb_load_trace` — Load Perfetto trace for analysis
  - `hzdb_trace_sql` — Run SQL queries on loaded traces
  - `hzdb_gpu_counters` — Get GPU metric counters for frame ranges
  - `hzdb_asset_search` — Search Meta's 3D asset library
- hzdb tools execute via CLI subprocess (`npx @meta-quest/hzdb`)
- Worker auto-routes: `hzdb_` prefixed tools go to CLI, all others go to UE C++ plugin
- Configurable via `HZDB_ENABLED`, `HZDB_COMMAND`, `HZDB_ARGS` env vars

### TUI Dashboard

- **Added `Tools/console/tui.js`** — unified terminal dashboard using blessed
- Three-panel layout: Status (models, UE connection, last tool call), Execution Log (real-time), Chat Input
- Replaces the need for multiple terminal windows
- **Persistent chat logging** — every session saves to `user_context/chat_logs/session_{timestamp}.md`
- Worker loads previous chat logs as context for cross-session memory
- Keyboard shortcuts: Ctrl+C quit, Ctrl+L clear log, Tab cycle panels

### Updated

- `worker.md` — rewritten for native tool calling mode with Meta hzdb tools and prompting best practices
- `ARCHITECTURE.md` — rewritten to reflect plannerless architecture
- `SYSTEM.md` — updated to remove all Claude references
- `WORKER_INSTRUCTIONS.md` — updated to remove planner/Claude references
- `README.md` — updated architecture section, removed Claude config, added TUI section
- `models/README.md` — updated for single Worker model (no separate Planner)
- `reference/README.md` — removed Claude references
- `user_context/README.md` — removed Claude references
- `plan/README.md` — removed Claude references
- `validator.md` — removed Claude reference
- `LevelSequence_Master_Reference.md` — removed Claude checklist references
- `start-all.bat` — updated to reference `start-worker.bat`

### Flow Comparison

**Before (v2.0):**
```
Claude (external) -> JSON plan -> Gatekeeper -> Llama Worker -> UE Plugin
```

**After (v3.0):**
```
User -> execute_request -> Llama Worker (native tool calling) -> Validation Stack -> UE Plugin / hzdb CLI
```

---

## [2.0.0] - 2025-03-15

### Summary

Full mutation coverage for every major Unreal Engine 5.6 editor subsystem. The plugin went from 192 read-heavy tools to 390 tools with complete CRUD across all subsystems. Every handler now has proper error handling via `MakeErrorJson`. All deprecated UE API calls have been replaced.

### Added

**Sequencer Mutation (18 new handlers)**
- `sequencerCreate` -- Create LevelSequence assets with playback range and frame rate
- `sequencerBindActor` -- Bind actors as possessables, returns GUID
- `sequencerAddSpawnable` -- Add actors as spawnable bindings
- `sequencerAddTrack` -- Add tracks: Transform, Float, Bool, Visibility, SkeletalAnimation, CameraCut, Audio, Event
- `sequencerAddSection` -- Add time-range sections to tracks
- `sequencerSetKeyframe` -- Set keyframes on Transform (9 channels), Float, Bool/Visibility with interpolation
- `sequencerDeleteSection` -- Remove sections by index
- `sequencerDeleteTrack` -- Delete tracks by index
- `sequencerMoveSection` -- Move sections to new time ranges
- `sequencerDuplicateSection` -- Duplicate sections with time offset
- `sequencerSetTrackMute` -- Set mute/lock state
- `sequencerAddCameraCut` -- Add camera cut sections with auto-binding
- `sequencerAddSubSequence` -- Add sub-sequence (shot) tracks
- `sequencerAddFade` -- Add fade tracks
- `sequencerSetAudioSection` -- Assign sound assets to audio sections
- `sequencerSetEventPayload` -- Set event function bindings
- `sequencerRender` -- Queue render via Movie Render Pipeline (PNG/EXR/JPG)
- `sequencerRenderStatus` -- Check render queue status
- `sequencerGetKeyframes` -- Read back keyframe times and values

**Material Graph Editing (9 new handlers)**
- `materialAddNode` -- Add material expression nodes
- `materialDeleteNode` -- Delete material expression nodes
- `materialConnectPins` -- Connect material expression pins
- `materialDisconnectPin` -- Disconnect pins
- `materialSetTextureParam` -- Set texture parameters on instances
- `materialCreateInstance` -- Create material instances
- `materialAssignToActor` -- Assign materials to actor mesh components
- `materialGetGraph` -- Get material graph structure
- `materialCreate` -- Create new material assets

**UMG Widget Mutation (6 new handlers)**
- `umgCreateWidget` -- Create Widget Blueprints with root Canvas Panel
- `umgAddChild` -- Add child widgets (TextBlock, Button, Image, ProgressBar, etc.)
- `umgRemoveChild` -- Remove widgets by name
- `umgSetWidgetProperty` -- Set widget properties (text, visibility, color, opacity, font size, alignment)
- `umgBindEvent` -- Bind widget events to functions
- `umgGetWidgetChildren` -- Get widget hierarchy with types and properties

**Animation Blueprint Mutation (7 new handlers)**
- `animBPAddState` -- Add states to state machines
- `animBPRemoveState` -- Remove states
- `animBPAddTransition` -- Add transitions between states
- `animBPSetTransitionRule` -- Set transition rule expressions
- `animBPSetStateAnimation` -- Set animation assets for states
- `animBPAddBlendNode` -- Add blend nodes
- `animBPGetStateMachine` -- Full state machine introspection

**AI / Behavior Tree Mutation (8 new handlers)**
- `btAddTask` -- Add task nodes
- `btAddComposite` -- Add composite nodes (Selector, Sequence, SimpleParallel)
- `btRemoveNode` -- Remove nodes
- `btAddDecorator` -- Add decorators
- `btAddService` -- Add services
- `btSetBlackboardValue` -- Set blackboard key values
- `btWireNodes` -- Wire child nodes to parent composites
- `btGetTree` -- Full BT introspection

**Component Manipulation (6 new handlers)**
- `componentList` -- List all components on an actor
- `componentRemove` -- Remove a component
- `componentSetProperty` -- Set a property on a component
- `componentSetTransform` -- Set relative transform
- `componentSetVisibility` -- Set visibility
- `componentSetCollision` -- Set collision profile and responses

**Landscape and Foliage Mutation (9 new handlers)**
- `landscapeSculpt` -- Sculpt heightmap (raise, lower, flatten, smooth) with falloff
- `landscapePaint` -- Paint landscape layers
- `landscapeAddLayer` -- Add paint layers
- `landscapeRemoveLayer` -- Remove paint layers
- `landscapeImportHeightmap` -- Import R16 heightmaps
- `landscapeExportHeightmap` -- Export heightmaps to R16
- `foliageAdd` -- Place foliage instances at locations
- `foliageRemove` -- Remove foliage by radius
- `foliageSetDensity` -- Adjust foliage density

**Skeletal Mesh Mutation (5 new handlers)**
- `skelMeshSetMorphTarget` -- Set morph target weights
- `skelMeshAddSocket` -- Add sockets
- `skelMeshRemoveSocket` -- Remove sockets
- `skelMeshSetMaterial` -- Set materials on slots
- `skelMeshSetPhysicsAsset` -- Assign physics assets

**DataTable Mutation (3 new handlers)**
- `dataTableAddRow` -- Add rows
- `dataTableDeleteRow` -- Delete rows
- `dataTableGetSchema` -- Get row struct schema

**Level Mutation (6 new handlers)**
- `levelCreate` -- Create new map assets
- `levelSave` -- Save current level
- `levelAddSublevel` -- Add streaming sublevels
- `levelSetCurrentLevel` -- Set active persistent level
- `levelBuildLighting` -- Build lighting
- `levelBuildNavigation` -- Build navigation mesh

**Actor Mutation (4 new handlers)**
- `actorDuplicate` -- Duplicate actors with offset
- `actorSetMobility` -- Set mobility (Static, Stationary, Movable)
- `actorSetTags` -- Set actor tags
- `actorSetLayer` -- Assign to editor layers

**Physics Mutation (6 new handlers)**
- `physicsAddConstraint` -- Add constraints (Fixed, Hinge, Prismatic, BallSocket)
- `physicsRemoveConstraint` -- Remove constraints
- `physicsSetMass` -- Set mass override
- `physicsSetDamping` -- Set linear/angular damping
- `physicsSetGravity` -- Enable/disable gravity
- `physicsApplyImpulse` -- Apply impulse to physics bodies

**Scene Hierarchy Mutation (4 new handlers)**
- `sceneCreateFolder` -- Create outliner folders
- `sceneDeleteFolder` -- Delete outliner folders
- `sceneSetActorLabel` -- Rename actor display labels
- `sceneHideActor` -- Set actor editor visibility

**Editor Settings Mutation (3 new handlers)**
- `settingsSetProject` -- Set project metadata
- `settingsSetEditor` -- Set editor preferences (auto-save, etc.)
- `settingsSetRendering` -- Set rendering settings (ray tracing, nanite, lumen, VSM)

**Asset Management (1 new handler)**
- `assetMove` -- Move assets between content folders

**Niagara Mutation (5 new handlers)**
- `niagaraCreateSystem` -- Create Niagara system assets
- `niagaraAddEmitter` -- Add emitters
- `niagaraRemoveEmitter` -- Remove emitters
- `niagaraSetSystemProperty` -- Set system properties (warmup, bounds, loop)
- `niagaraSpawnSystem` -- Spawn systems at world locations

**Audio Mutation (4 new handlers)**
- `audioCreateSoundCue` -- Create Sound Cue assets
- `audioSetAttenuation` -- Set attenuation settings
- `audioCreateAmbientSound` -- Spawn Ambient Sound actors
- `audioCreateAudioVolume` -- Create Audio Volumes with reverb/attenuation

**Lighting (3 new handlers)**
- `lightCreate` -- Spawn light actors (Point, Spot, Directional, Rect, Sky)
- `lightSetProperties` -- Set intensity, color, temperature, shadows, cone angles, attenuation
- `lightList` -- List all light actors

**Blueprint Event Graph (8 new handlers)**
- `bpCreateBlueprint` -- Create Blueprints with parent class
- `bpAddVariable` -- Add typed variables
- `bpAddFunction` -- Add function graphs
- `bpAddNode` -- Add nodes (CallFunction, CustomEvent, Branch, VariableGet, VariableSet)
- `bpConnectPins` -- Wire pins
- `bpCompile` -- Compile with error reporting
- `bpGetGraph` -- Introspect event graphs
- `bpDeleteNode` -- Delete nodes

**PCG Mutation (4 new handlers)**
- `pcgCreateGraph` -- Create PCG graph assets
- `pcgAddNode` -- Add PCG nodes
- `pcgRemoveNode` -- Remove PCG nodes
- `pcgConnectNodes` -- Connect PCG nodes

**World Partition (3 new handlers)**
- `wpGetInfo` -- Get World Partition info and data layers
- `wpSetActorDataLayer` -- Assign actors to data layers
- `wpSetActorRuntimeGrid` -- Set runtime grids

**MetaHuman and Groom (4 new handlers)**
- `metahumanList` -- List MetaHuman assets
- `metahumanSpawn` -- Spawn MetaHuman actors
- `groomList` -- List groom assets
- `groomSetBinding` -- Bind groom and binding assets to actors

**Python Bridge (2 new handlers)**
- `pythonExecFile` -- Execute Python files with arguments
- `pythonExecString` -- Execute inline Python code strings

### Fixed

- `GetMasterTracks()` replaced with `GetTracks()` (deprecated in UE 5.4+)
- `AddCubicKey` signature corrected from 3-arg to 2-arg form
- Frame numbers now use display-to-tick conversion via `ConvertFrameTime()` throughout all Sequencer handlers
- `FMovieSceneObjectBindingID` construction uses `SetGuid()` instead of non-existent constructor
- Movie Pipeline `Job->Sequence` uses setter method `SetSequence()`
- All 325 handlers now have `MakeErrorJson` error handling (was 300 before)
- 64 duplicate header declarations removed
- 61 duplicate route registrations removed
- Non-ASCII characters (em dashes) replaced with ASCII in all source files
- 14 handler name mismatches between header and implementation resolved

### Changed

- `tool-registry.json` updated from 192 to 390 tools
- `CAPABILITIES.md` rewritten to reflect full CRUD coverage across all 32 subsystem categories
- `README.md` rewritten with complete tool inventory
- `SYSTEM.md` updated to reference 390 tools
- `Build.cs` updated with `MovieRenderPipelineCore` and `BehaviorTreeEditor` module dependencies
- Compatibility narrowed from "5.4 through 5.7+" to "5.6" to match validated API surface

### Stats

| Metric | Before | After |
|--------|--------|-------|
| Tools | 192 | 329 |
| Handler files | 30 | 44 |
| C++ lines | ~15,000 | 27,122 |
| Implementations | ~180 | 325 |
| Declarations | ~180 | 325 |
| Routes | ~180 | 329 |
| Audit errors | Unknown | 0 |
| Audit warnings | Unknown | 0 |
