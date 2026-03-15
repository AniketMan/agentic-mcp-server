# AgenticMCP

A dual-path MCP (Model Context Protocol) server for Unreal Engine 5.6. Gives AI agents full read-write access to every major editor subsystem -- Blueprints, Sequencer, Materials, Niagara, Landscape, Animation, AI, Audio, Lighting, PCG, Physics, UMG, and more -- with an offline fallback when the editor is not running.

**390 tools. 44 handler files. 27,000+ lines of C++. Full CRUD across every subsystem.**

## What Makes This Different

Every other UE5 MCP tool requires the editor to be running and only covers a handful of operations. AgenticMCP has two execution paths and complete editor coverage:

1. **Live Editor** -- C++ plugin runs inside UE5, exposes the full UEdGraph API and every editor subsystem over HTTP. Real-time compilation, validation, and the complete tool set.
2. **Offline Fallback** -- Python binary injector reads and modifies `.umap` files directly. Works when the editor is closed. Supports reading level data, inspecting actors, and generating Blueprint paste text.

Additionally:
- **Snapshot/Rollback** -- Save graph state before destructive operations, restore if something breaks.
- **Validation Endpoint** -- Pre-compilation error checking with detailed diagnostics.
- **Auto-Discovery** -- The MCP bridge dynamically discovers tools from the C++ plugin. No hardcoded tool lists.
- **Context Injection** -- Optionally injects UE5 API documentation into tool responses so the AI agent has reference material.
- **Async Task Queue** -- Long-running operations (compilation, batch edits) run asynchronously with progress reporting.
- **Inference-Gated Determinism** -- Three-layer architecture (Planner, Gatekeeper, Workers) ensures safe, predictable execution. See `ARCHITECTURE.md`.

## Architecture

```
AI Agent (Claude, Cursor, Manus, etc.)
    |
    | MCP Protocol (stdio)
    |
Node.js MCP Bridge
    |                   |
    | HTTP              | Python subprocess
    | (editor running)  | (editor closed)
    |                   |
C++ Plugin          Binary Injector
(in-editor)         (offline .umap)
```

## Installation

### 1. C++ Plugin (UE5 Editor)

Copy the plugin into your project:

```
YourProject/
  Plugins/
    AgenticMCP/
      AgenticMCP.uplugin
      Source/
        AgenticMCP/
          ...
```

Regenerate project files and build. The plugin is editor-only and auto-starts when the editor opens.

```bash
# Windows (from project root)
"C:\Program Files\Epic Games\UE_5.x\Engine\Build\BatchFiles\GenerateProjectFiles.bat" YourProject.uproject
```

Verify in the Output Log:

```
AgenticMCP: HTTP server started on port 9847 (editor mode)
```

### 2. MCP Bridge (Node.js)

```bash
cd AgenticMCP/Tools
npm install
```

### 3. Configure Your MCP Client

#### Claude Code / Claude Desktop

Add to your MCP config (`claude_desktop_config.json` or project `.mcp.json`):

```json
{
  "mcpServers": {
    "agentic-mcp": {
      "command": "node",
      "args": ["/path/to/AgenticMCP/Tools/index.js"],
      "env": {
        "UNREAL_MCP_URL": "http://localhost:9847",
        "AGENTIC_FALLBACK_ENABLED": "true"
      }
    }
  }
}
```

#### Cursor

Add to `.cursor/mcp.json`:

```json
{
  "mcpServers": {
    "agentic-mcp": {
      "command": "node",
      "args": ["/path/to/AgenticMCP/Tools/index.js"]
    }
  }
}
```

## Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `UNREAL_MCP_URL` | `http://localhost:9847` | C++ plugin HTTP endpoint |
| `MCP_REQUEST_TIMEOUT_MS` | `30000` | Per-request HTTP timeout (ms) |
| `MCP_ASYNC_ENABLED` | `true` | Enable async task queue |
| `MCP_ASYNC_TIMEOUT_MS` | `300000` | Async operation timeout (ms) |
| `MCP_POLL_INTERVAL_MS` | `2000` | Async poll interval (ms) |
| `INJECT_CONTEXT` | `false` | Auto-inject UE API docs in responses |
| `AGENTIC_FALLBACK_ENABLED` | `true` | Enable offline binary injector |
| `AGENTIC_PROJECT_ROOT` | (auto-detect) | UE project root for fallback |

## Tool Inventory (329 Tools)

All live editor endpoints are at `http://localhost:9847/api/<endpoint>`. Tools are accessed via the MCP bridge at `/mcp/tool/{name}`.

### Health and Lifecycle (4)

| Tool | Description |
|------|-------------|
| `health` | Server status, asset counts |
| `status` | MCP connection status |
| `rescan` | Force asset registry refresh |
| `waitReady` | Wait for assets/compile/render to complete |

### Blueprint Read (10)

| Tool | Description |
|------|-------------|
| `listBlueprints` | List Blueprints and Maps |
| `getBlueprint` | Get Blueprint details (class, parent, variables, graphs) |
| `getGraph` | Get graph nodes and connections |
| `searchAssets` | Search assets by name |
| `getReferences` | Find asset references |
| `listClasses` | List UClasses |
| `listFunctions` | List UFunctions on a class |
| `listProperties` | List UProperties on a class |
| `getPinInfo` | Get pin details |
| `findNodes` | Find nodes in a graph by type or name |

### Blueprint Mutation (14)

| Tool | Description |
|------|-------------|
| `addNode` | Add a node to a graph |
| `deleteNode` | Delete a node |
| `connectPins` | Connect two pins |
| `disconnectPin` | Break pin connections |
| `setPinDefault` | Set pin default value |
| `moveNode` | Move a node |
| `refreshAllNodes` | Refresh all nodes |
| `createBlueprint` | Create new Blueprint |
| `createGraph` | Create function/macro graph |
| `deleteGraph` | Delete a graph |
| `addVariable` | Add a variable |
| `removeVariable` | Remove a variable |
| `compileBlueprint` | Compile and save |
| `setNodeComment` | Set node comment |

### Blueprint Event Graph (8)

| Tool | Description |
|------|-------------|
| `bpCreateBlueprint` | Create a new Blueprint asset with parent class |
| `bpAddVariable` | Add a typed variable (bool, int, float, string, vector, rotator, transform, object) |
| `bpAddFunction` | Add a custom function graph |
| `bpAddNode` | Add a node (CallFunction, CustomEvent, Branch, VariableGet, VariableSet) |
| `bpConnectPins` | Wire two pins together by node ID and pin name |
| `bpCompile` | Compile a Blueprint and return errors/warnings |
| `bpGetGraph` | Introspect a Blueprint's event graph (all nodes, pins, connections) |
| `bpDeleteNode` | Delete a node from a Blueprint graph |

### Actor Management (10)

| Tool | Description |
|------|-------------|
| `listActors` | List actors in world |
| `getActor` | Get actor details |
| `spawnActor` | Spawn a new actor |
| `deleteActor` | Delete an actor |
| `setActorProperty` | Set actor property |
| `setActorTransform` | Set actor transform |
| `actorDuplicate` | Duplicate an actor with offset |
| `actorSetMobility` | Set actor mobility (Static, Stationary, Movable) |
| `actorSetTags` | Set actor tags |
| `actorSetLayer` | Assign actor to editor layer |

### Component Management (7)

| Tool | Description |
|------|-------------|
| `addComponent` | Add a component to an actor |
| `componentList` | List all components on an actor |
| `componentRemove` | Remove a component from an actor |
| `componentSetProperty` | Set a property on a component |
| `componentSetTransform` | Set relative transform on a component |
| `componentSetVisibility` | Set component visibility |
| `componentSetCollision` | Set component collision profile and responses |

### Level Management (10)

| Tool | Description |
|------|-------------|
| `listLevels` | List levels |
| `loadLevel` | Load a sublevel |
| `getLevelBlueprint` | Get level blueprint |
| `streamingLevelVisibility` | Set visibility of a streaming sublevel |
| `levelCreate` | Create a new map asset |
| `levelSave` | Save the current level |
| `levelAddSublevel` | Add a streaming sublevel |
| `levelSetCurrentLevel` | Set the active persistent level |
| `levelBuildLighting` | Build lighting for the level |
| `levelBuildNavigation` | Build navigation mesh |

### Sequencer (21)

| Tool | Description |
|------|-------------|
| `listSequences` | List all LevelSequence assets |
| `readSequence` | Read sequence tracks, audio cues, timing data |
| `removeAudioTracks` | Remove audio tracks from sequences |
| `sequencerCreate` | Create a new LevelSequence asset |
| `sequencerGetTracks` | Full introspection: bindings, tracks, sections, channels, playback range |
| `sequencerGetKeyframes` | Read back keyframe times and values from a track section |
| `sequencerBindActor` | Bind an actor as possessable, returns GUID |
| `sequencerAddSpawnable` | Add an actor as a spawnable binding |
| `sequencerAddTrack` | Add track (Transform, Float, Bool, Visibility, SkeletalAnimation, CameraCut, Audio, Event) |
| `sequencerAddSection` | Add a time-range section to a track |
| `sequencerSetKeyframe` | Set keyframes (Transform 9-channel, Float, Bool/Visibility) with interpolation |
| `sequencerDeleteSection` | Remove a section by index |
| `sequencerDeleteTrack` | Delete a track by index |
| `sequencerMoveSection` | Move a section to a new time range |
| `sequencerDuplicateSection` | Duplicate a section with optional time offset |
| `sequencerSetTrackMute` | Set mute/lock state on a track |
| `sequencerAddCameraCut` | Add camera cut section with auto-binding |
| `sequencerAddSubSequence` | Add a sub-sequence (shot) track |
| `sequencerAddFade` | Add a fade track |
| `sequencerSetAudioSection` | Assign a sound asset to an audio section |
| `sequencerSetEventPayload` | Set event function binding on an event section |
| `sequencerSetPlayRange` | Set playback start/end range |
| `sequencerRender` | Queue render via Movie Render Pipeline (PNG/EXR/JPG, resolution, AA) |
| `sequencerRenderStatus` | Check Movie Render Pipeline queue status |

### Material System (15)

| Tool | Description |
|------|-------------|
| `materialList` | List all material assets |
| `materialGetInfo` | Get material details |
| `materialGetGraph` | Get material graph nodes and connections |
| `materialCreate` | Create a new material asset |
| `materialListInstances` | List material instances |
| `materialSetParam` | Set a material parameter |
| `materialSetScalar` | Set a scalar parameter |
| `materialSetVector` | Set a vector parameter |
| `materialAddNode` | Add a material expression node |
| `materialDeleteNode` | Delete a material expression node |
| `materialConnectPins` | Connect material expression pins |
| `materialDisconnectPin` | Disconnect a material expression pin |
| `materialSetTextureParam` | Set a texture parameter on a material instance |
| `materialCreateInstance` | Create a material instance from a parent material |
| `materialAssignToActor` | Assign a material to an actor's mesh component |

### Niagara Particle Systems (16)

| Tool | Description |
|------|-------------|
| `niagaraGetStatus` | Get Niagara system status |
| `niagaraListSystems` | List active Niagara systems in the world |
| `niagaraGetSystemInfo` | Get detailed info about a Niagara system |
| `niagaraGetEmitters` | Get emitters for a Niagara component |
| `niagaraSetParameter` | Set a Niagara parameter value |
| `niagaraGetParameters` | Get all parameters for a Niagara component |
| `niagaraActivateSystem` | Activate a Niagara system |
| `niagaraSetEmitterEnable` | Enable/disable a specific emitter |
| `niagaraResetSystem` | Reset a Niagara system |
| `niagaraGetStats` | Get Niagara performance statistics |
| `niagaraDebugHUD` | Toggle Niagara debug HUD |
| `niagaraCreateSystem` | Create a new Niagara system asset |
| `niagaraAddEmitter` | Add an emitter to a Niagara system |
| `niagaraRemoveEmitter` | Remove an emitter from a Niagara system |
| `niagaraSetSystemProperty` | Set system properties (warmup, bounds, loop) |
| `niagaraSpawnSystem` | Spawn a Niagara system at a world location |

### Animation (15)

| Tool | Description |
|------|-------------|
| `animBPList` | List all Animation Blueprints |
| `animBPGetGraph` | Get AnimBP graph structure |
| `animBPGetStates` | Get state machine states |
| `animBPGetTransitions` | Get state machine transitions |
| `animBPGetSlotGroups` | Get animation slot groups |
| `animBPGetMontages` | Get animation montages |
| `animBPGetBlendSpace` | Get blend space info |
| `animBPGetStateMachine` | Full state machine introspection |
| `animBPAddState` | Add a state to a state machine |
| `animBPRemoveState` | Remove a state from a state machine |
| `animBPAddTransition` | Add a transition between states |
| `animBPSetTransitionRule` | Set transition rule expression |
| `animBPSetStateAnimation` | Set the animation asset for a state |
| `animBPAddBlendNode` | Add a blend node to a state machine |
| `animBPListMontages` | List all animation montage assets |

### AI / Behavior Trees (14)

| Tool | Description |
|------|-------------|
| `aiListBehaviorTrees` | List all Behavior Tree assets |
| `aiGetBehaviorTree` | Get Behavior Tree structure |
| `aiListBlackboards` | List all Blackboard assets |
| `aiGetBlackboard` | Get Blackboard keys and values |
| `aiListControllers` | List AI controllers |
| `aiGetEQSQueries` | Get EQS query definitions |
| `btAddTask` | Add a task node to a Behavior Tree |
| `btAddComposite` | Add a composite node (Selector, Sequence, SimpleParallel) |
| `btRemoveNode` | Remove a node from a Behavior Tree |
| `btAddDecorator` | Add a decorator to a BT node |
| `btAddService` | Add a service to a BT node |
| `btSetBlackboardValue` | Set a Blackboard key value |
| `btWireNodes` | Wire a child node to a parent composite |
| `btGetTree` | Full BT introspection (all nodes, decorators, services, connections) |

### Audio System (14)

| Tool | Description |
|------|-------------|
| `audioGetStatus` | Get audio system status |
| `audioListActiveSounds` | List currently playing sounds |
| `audioGetDeviceInfo` | Get audio device information |
| `audioListSoundClasses` | List sound classes |
| `audioSetVolume` | Set volume for a sound class |
| `audioGetStats` | Get audio statistics |
| `audioPlay` | Play a sound at location |
| `audioStop` | Stop a playing sound |
| `audioSetListener` | Set listener position/rotation |
| `audioDebugVisualize` | Toggle audio debug visualization |
| `audioCreateSoundCue` | Create a new Sound Cue asset |
| `audioSetAttenuation` | Set attenuation settings on a sound |
| `audioCreateAmbientSound` | Spawn an Ambient Sound actor |
| `audioCreateAudioVolume` | Create an Audio Volume with reverb/attenuation |

### PCG (Procedural Content Generation) (14)

| Tool | Description |
|------|-------------|
| `pcgListGraphs` | List all PCG graph assets |
| `pcgGetGraph` | Get PCG graph structure |
| `pcgGetNodeSettings` | Get settings for a PCG node |
| `pcgSetNodeSettings` | Set settings on a PCG node |
| `pcgListComponents` | List PCG components in the world |
| `pcgGetComponentInfo` | Get PCG component details |
| `pcgGenerate` | Execute a PCG graph |
| `pcgCleanup` | Clean up PCG-generated content |
| `pcgGetDebugInfo` | Get PCG debug information |
| `pcgExecuteGraph` | Execute a PCG graph with parameters |
| `pcgCreateGraph` | Create a new PCG graph asset |
| `pcgAddNode` | Add a node to a PCG graph |
| `pcgRemoveNode` | Remove a node from a PCG graph |
| `pcgConnectNodes` | Connect two PCG graph nodes |

### Landscape and Foliage (14)

| Tool | Description |
|------|-------------|
| `landscapeGetInfo` | Get landscape info (size, components, layers) |
| `landscapeGetLayerInfo` | Get landscape paint layer details |
| `landscapeSculpt` | Sculpt landscape heightmap (raise, lower, flatten, smooth) |
| `landscapePaint` | Paint a landscape layer at a location |
| `landscapeAddLayer` | Add a new paint layer to the landscape |
| `landscapeRemoveLayer` | Remove a paint layer |
| `landscapeImportHeightmap` | Import a heightmap from R16 file |
| `landscapeExportHeightmap` | Export the landscape heightmap to R16 file |
| `foliageGetInfo` | Get foliage info (types, instance counts) |
| `foliageAdd` | Place foliage instances at locations |
| `foliageRemove` | Remove foliage instances by radius |
| `foliageSetDensity` | Adjust foliage density for a type |
| `foliageGetTypes` | List all foliage types |
| `foliageGetInstances` | Get foliage instance locations |

### Skeletal Mesh (10)

| Tool | Description |
|------|-------------|
| `skelMeshList` | List all skeletal mesh assets |
| `skelMeshGetInfo` | Get skeletal mesh details |
| `skelMeshGetBones` | Get bone hierarchy |
| `skelMeshGetSockets` | Get socket list |
| `skelMeshGetMorphTargets` | Get morph target list |
| `skelMeshSetMorphTarget` | Set morph target weight on a component |
| `skelMeshAddSocket` | Add a socket to a skeletal mesh |
| `skelMeshRemoveSocket` | Remove a socket |
| `skelMeshSetMaterial` | Set material on a skeletal mesh slot |
| `skelMeshSetPhysicsAsset` | Assign a physics asset |

### Physics (11)

| Tool | Description |
|------|-------------|
| `physicsGetInfo` | Get physics body info for an actor |
| `physicsGetConstraints` | List physics constraints |
| `physicsGetCollisionProfile` | Get collision profile |
| `physicsSimulate` | Start/stop physics simulation |
| `physicsGetOverlaps` | Get overlapping actors |
| `physicsAddConstraint` | Add a physics constraint (Fixed, Hinge, Prismatic, BallSocket) |
| `physicsRemoveConstraint` | Remove a physics constraint |
| `physicsSetMass` | Set mass override on a component |
| `physicsSetDamping` | Set linear and angular damping |
| `physicsSetGravity` | Enable/disable gravity on a component |
| `physicsApplyImpulse` | Apply an impulse to a physics body |

### UMG (Widget Blueprints) (10)

| Tool | Description |
|------|-------------|
| `umgListWidgets` | List all Widget Blueprint assets |
| `umgGetWidgetTree` | Get widget hierarchy tree |
| `umgGetWidgetProperties` | Get widget properties |
| `umgListHUDs` | List HUD classes |
| `umgCreateWidget` | Create a new Widget Blueprint with root Canvas Panel |
| `umgAddChild` | Add a child widget (TextBlock, Button, Image, ProgressBar, etc.) |
| `umgRemoveChild` | Remove a widget by name |
| `umgSetWidgetProperty` | Set widget property (text, visibility, color, opacity, font size, alignment) |
| `umgBindEvent` | Bind a widget event to a function |
| `umgGetWidgetChildren` | Get widget children with types and properties |

### Lighting (3)

| Tool | Description |
|------|-------------|
| `lightCreate` | Spawn a light actor (Point, Spot, Directional, Rect, Sky) |
| `lightSetProperties` | Set light properties (intensity, color, temperature, shadows, cone angles, attenuation) |
| `lightList` | List all light actors in the level |

### Scene Hierarchy (10)

| Tool | Description |
|------|-------------|
| `sceneSnapshot` | Get hierarchical scene tree with short refs |
| `sceneGetHierarchy` | Get actor parent-child hierarchy |
| `sceneSetParent` | Set actor parent (attachment) |
| `sceneClearParent` | Clear actor parent |
| `sceneGetComponents` | Get components for an actor |
| `sceneGetTransformHierarchy` | Get transform chain |
| `sceneCreateFolder` | Create an outliner folder |
| `sceneDeleteFolder` | Delete an outliner folder |
| `sceneSetActorLabel` | Rename an actor's display label |
| `sceneHideActor` | Set actor editor visibility |

### DataTable (5)

| Tool | Description |
|------|-------------|
| `dataTableList` | List all DataTable assets |
| `dataTableRead` | Read DataTable rows |
| `dataTableAddRow` | Add a row to a DataTable |
| `dataTableDeleteRow` | Delete a row from a DataTable |
| `dataTableGetSchema` | Get DataTable row struct schema |

### Visual Agent / Automation (13)

| Tool | Description |
|------|-------------|
| `screenshot` | Capture viewport screenshot (base64 JPEG) |
| `focusActor` | Move editor camera to focus on an actor |
| `selectActor` | Select actor(s) in the editor |
| `setViewport` | Set camera position and rotation |
| `moveActor` | Move/transform an actor |
| `getCamera` | Get current viewport camera position, rotation, FOV |
| `listViewports` | List all editor viewports |
| `getSelection` | Get currently selected actors with transforms |
| `resolveRef` | Resolve a short ref to actor/component |
| `drawDebug` | Draw debug shapes in viewport |
| `clearDebug` | Clear debug drawings |
| `renderCapture` | Capture a high-res render |
| `outputLog` | Get recent output log entries |

### XR / VR (10)

| Tool | Description |
|------|-------------|
| `xrStatus` | Get XR/VR headset and runtime status |
| `xrControllers` | Get controller tracking data and button states |
| `xrGuardian` | Get guardian/play area boundary info |
| `xrHandTracking` | Get hand tracking data |
| `xrPassthrough` | Get passthrough mode status |
| `xrRecenter` | Recenter XR tracking origin |
| `xrSetDisplayFrequency` | Set HMD display refresh rate |
| `xrSetGuardianVisibility` | Show/hide guardian boundary |
| `xrSetPassthrough` | Enable/disable passthrough |
| `xrSetPerformanceLevels` | Set CPU/GPU performance levels |

### Pixel Streaming (7)

| Tool | Description |
|------|-------------|
| `pixelStreamingGetStatus` | Get Pixel Streaming status |
| `pixelStreamingStart` | Start Pixel Streaming |
| `pixelStreamingStop` | Stop Pixel Streaming |
| `pixelStreamingListStreamers` | List active streamers |
| `pixelStreamingGetCodec` | Get current codec settings |
| `pixelStreamingSetCodec` | Set codec settings |
| `pixelStreamingListPlayers` | List connected players |

### PIE Control (5)

| Tool | Description |
|------|-------------|
| `startPIE` | Start PIE session (SelectedViewport, NewEditorWindow, VR, MobilePreview) |
| `stopPIE` | Stop PIE session |
| `pausePIE` | Pause/resume PIE |
| `stepPIE` | Single-step paused PIE |
| `getPIEState` | Get PIE state (isRunning, isPaused, timeSeconds) |

### Console Commands (4)

| Tool | Description |
|------|-------------|
| `executeConsole` | Execute a console command |
| `getCVar` | Get a console variable value |
| `setCVar` | Set a console variable value |
| `simulateInput` | Simulate keyboard/mouse input |

### Editor Settings (7)

| Tool | Description |
|------|-------------|
| `settingsGetProject` | Get project settings |
| `settingsGetEditor` | Get editor preferences |
| `settingsGetRendering` | Get rendering settings |
| `settingsGetPlugins` | Get plugin list |
| `settingsSetProject` | Set project settings (name, company, description, version) |
| `settingsSetEditor` | Set editor preferences (auto-save, etc.) |
| `settingsSetRendering` | Set rendering settings (ray tracing, nanite, lumen, VSM) |

### Asset Management (7)

| Tool | Description |
|------|-------------|
| `assetImport` | Import an asset (FBX, texture, audio) |
| `assetExport` | Export an asset |
| `assetDuplicate` | Duplicate an asset |
| `assetRename` | Rename an asset |
| `assetDelete` | Delete an asset |
| `assetMove` | Move an asset between content folders |
| `openAsset` | Open an asset in the editor |

### World Partition (3)

| Tool | Description |
|------|-------------|
| `wpGetInfo` | Get World Partition info, streaming status, data layers |
| `wpSetActorDataLayer` | Assign an actor to a data layer |
| `wpSetActorRuntimeGrid` | Set runtime grid for an actor |

### MetaHuman and Groom (4)

| Tool | Description |
|------|-------------|
| `metahumanList` | List all MetaHuman assets |
| `metahumanSpawn` | Spawn a MetaHuman actor |
| `groomList` | List all groom assets |
| `groomSetBinding` | Bind a groom and binding asset to an actor |

### Python Bridge (2)

| Tool | Description |
|------|-------------|
| `pythonExecFile` | Execute a Python file in the editor's Python environment |
| `pythonExecString` | Execute inline Python code in the editor |

### Story / Narrative (4)

| Tool | Description |
|------|-------------|
| `storyState` | Get current story/narrative state |
| `storyAdvance` | Advance to the next story beat |
| `storyGoto` | Jump to a specific story beat or chapter |
| `storyPlay` | Play/resume story playback |

### Validation and Safety (6)

| Tool | Description |
|------|-------------|
| `validateBlueprint` | Validate a Blueprint for errors |
| `snapshotGraph` | Take graph snapshot for rollback |
| `restoreGraph` | Restore from snapshot |
| `beginTransaction` | Begin an undo transaction |
| `endTransaction` | End an undo transaction |
| `undo` | Undo the last transaction |

### Packaging and Build (4)

| Tool | Description |
|------|-------------|
| `buildCook` | Cook content for target platform |
| `buildPackage` | Package the project |
| `buildGetStatus` | Get build/cook status |
| `buildGetLog` | Get build log output |

### Source Control (2)

| Tool | Description |
|------|-------------|
| `sourceControlGetStatus` | Get source control status for assets |
| `sourceControlCheckout` | Check out assets for editing |

### Miscellaneous (4)

| Tool | Description |
|------|-------------|
| `diffBlueprint` | Diff two versions of a Blueprint |
| `executePython` | Execute raw Python in the editor (legacy) |
| `rescan` | Force asset registry refresh |
| `saveAll` | Save all modified assets |

## Supported Node Types

The `addNode` endpoint supports:

| nodeType | Required Fields | Description |
|---|---|---|
| `CallFunction` | `functionName`, optional `className` | Call a UFunction |
| `CustomEvent` | `eventName` | Custom event |
| `OverrideEvent` | `functionName` | Override parent event (BeginPlay, Tick, etc.) |
| `BreakStruct` | `typeName` | Break a struct |
| `MakeStruct` | `typeName` | Make a struct |
| `VariableGet` | `variableName` | Get variable |
| `VariableSet` | `variableName` | Set variable |
| `DynamicCast` | `castTarget` | Cast to class |
| `Branch` | -- | If/else |
| `Sequence` | -- | Execution sequence |
| `MacroInstance` | `macroName`, optional `macroSource` | Generic macro (ForLoop, ForEachLoop, etc.) |
| `SpawnActorFromClass` | optional `actorClass` | Spawn actor |
| `Select` | -- | Select node |
| `Comment` | optional `comment`, `width`, `height` | Comment box |
| `Reroute` | -- | Reroute/knot |
| `MultiGate` | optional `numOutputs` | Multi-output execution gate |
| `Delay` | optional `duration` | Timed delay |
| `SetTimer` | `functionName`, optional `time` | Timer by function name |
| `LoadAsset` | -- | Async asset loading |

## Offline Fallback Tools

When the editor is not running, these tools are available:

| Tool | Description |
|------|-------------|
| `offline_list_levels` | List all `.umap` files in project |
| `offline_list_actors` | List actors in a `.umap` file |
| `offline_get_actor` | Get actor properties |
| `offline_get_graph` | Get Blueprint graph data |
| `offline_level_info` | Get level summary |
| `offline_generate_paste_text` | Generate Blueprint paste text |

## Safety

- **Snapshots**: Always take a snapshot before making destructive changes. Max 50 snapshots in memory, oldest pruned automatically.
- **Transactions**: Wrap mutations in `beginTransaction` / `endTransaction` for undo support.
- **Validation**: Always compile after mutations to catch errors.
- **SEH Protection**: On Windows, compilation is wrapped in SEH handlers to prevent editor crashes.
- **Game Thread**: All mutations run on the game thread via the RequestQueue architecture. HTTP requests are queued and processed during editor Tick.
- **Error Handling**: Every handler returns structured JSON errors via `MakeErrorJson`. No silent failures.
- **Dual-Path Resilience**: If the editor goes down, the fallback path keeps working.

## Testing

```bash
cd AgenticMCP/Tools
npm test              # Run all tests
npm run test:watch    # Watch mode
npm run test:coverage # Coverage report
```

## Compatibility

- Unreal Engine 5.6
- Node.js 18+
- Python 3.11+ (for offline fallback)
- Works with: Claude Code, Claude Desktop, Cursor, Windsurf, Manus

## Credits

MCP bridge architecture based on [Natfii/ue5-mcp-bridge](https://github.com/Natfii/unrealclaude-mcp-bridge) (MIT).
UE API context documentation adapted from the same project.

## License

MIT
