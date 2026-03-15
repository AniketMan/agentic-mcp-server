# AgenticMCP Capabilities

This document is the **permanent capabilities manifest** for the AgenticMCP plugin. It is loaded automatically on every MCP connection. Any AI agent connected to this plugin **must read this file first** and treat it as the authoritative source of what operations are available.

Do not guess. Do not hallucinate capabilities. If it is not listed here, it is not available.

**390 tools. Full read-write access to every major Unreal Engine 5.6 editor subsystem.**

---

## 1. Blueprint Manipulation

Full CRUD on any Blueprint asset -- Actor Blueprints, Level Blueprints, Widget Blueprints, Animation Blueprints.

**Read (10 tools):** `listBlueprints`, `getBlueprint`, `getGraph`, `searchAssets`, `getReferences`, `listClasses`, `listFunctions`, `listProperties`, `getPinInfo`, `findNodes`

**Mutate (14 tools):** `addNode`, `deleteNode`, `connectPins`, `disconnectPin`, `setPinDefault`, `moveNode`, `refreshAllNodes`, `createBlueprint`, `createGraph`, `deleteGraph`, `addVariable`, `removeVariable`, `compileBlueprint`, `setNodeComment`

**Blueprint Event Graph (8 tools):** `bpCreateBlueprint`, `bpAddVariable`, `bpAddFunction`, `bpAddNode`, `bpConnectPins`, `bpCompile`, `bpGetGraph`, `bpDeleteNode`

Supported node types: `CallFunction`, `CustomEvent`, `OverrideEvent`, `BreakStruct`, `MakeStruct`, `VariableGet`, `VariableSet`, `DynamicCast`, `Branch`, `Sequence`, `MacroInstance`, `SpawnActorFromClass`, `Select`, `Comment`, `Reroute`, `MultiGate`, `Delay`, `SetTimer`, `LoadAsset`

---

## 2. Actor Management

Full CRUD on actors placed in the currently loaded level(s).

**Read:** `listActors`, `getActor`, `getSelection`

**Mutate:** `spawnActor`, `deleteActor`, `setActorProperty`, `setActorTransform`, `moveActor`, `actorDuplicate`, `actorSetMobility`, `actorSetTags`, `actorSetLayer`

---

## 3. Component Management

Full CRUD on actor components.

**Read:** `componentList`

**Mutate:** `addComponent`, `componentRemove`, `componentSetProperty`, `componentSetTransform`, `componentSetVisibility`, `componentSetCollision`

---

## 4. Level Management

Full level lifecycle control.

**Read:** `listLevels`, `getLevelBlueprint`

**Mutate:** `loadLevel`, `streamingLevelVisibility`, `levelCreate`, `levelSave`, `levelAddSublevel`, `levelSetCurrentLevel`, `levelBuildLighting`, `levelBuildNavigation`

---

## 5. Sequencer (Level Sequences)

Full cinematic pipeline -- create sequences, bind actors, add tracks, set keyframes, add camera cuts, render.

**Read (5):** `listSequences`, `readSequence`, `sequencerGetTracks`, `sequencerGetKeyframes`, `sequencerRenderStatus`

**Create (9):** `sequencerCreate`, `sequencerBindActor`, `sequencerAddSpawnable`, `sequencerAddTrack`, `sequencerAddSection`, `sequencerAddCameraCut`, `sequencerAddSubSequence`, `sequencerAddFade`, `sequencerSetAudioSection`

**Update (6):** `sequencerSetKeyframe`, `sequencerSetPlayRange`, `sequencerMoveSection`, `sequencerDuplicateSection`, `sequencerSetTrackMute`, `sequencerSetEventPayload`

**Delete (3):** `removeAudioTracks`, `sequencerDeleteSection`, `sequencerDeleteTrack`

**Execute (1):** `sequencerRender`

Keyframe support: Transform (9 channels: lx/ly/lz/rx/ry/rz/sx/sy/sz), Float, Bool, Visibility. Interpolation modes: cubic, linear, constant. All frame numbers use display-to-tick conversion.

---

## 6. Material System

Full material graph editing and instance management.

**Read (3):** `materialList`, `materialGetInfo`, `materialGetGraph`

**Mutate (12):** `materialCreate`, `materialListInstances`, `materialSetParam`, `materialSetScalar`, `materialSetVector`, `materialAddNode`, `materialDeleteNode`, `materialConnectPins`, `materialDisconnectPin`, `materialSetTextureParam`, `materialCreateInstance`, `materialAssignToActor`

---

## 7. Niagara Particle Systems

Full VFX pipeline -- create systems, add/remove emitters, set parameters, spawn at locations.

**Read (6):** `niagaraGetStatus`, `niagaraListSystems`, `niagaraGetSystemInfo`, `niagaraGetEmitters`, `niagaraGetParameters`, `niagaraGetStats`

**Mutate (10):** `niagaraSetParameter`, `niagaraActivateSystem`, `niagaraSetEmitterEnable`, `niagaraResetSystem`, `niagaraDebugHUD`, `niagaraCreateSystem`, `niagaraAddEmitter`, `niagaraRemoveEmitter`, `niagaraSetSystemProperty`, `niagaraSpawnSystem`

---

## 8. Animation Blueprints

Full state machine editing.

**Read (8):** `animBPList`, `animBPGetGraph`, `animBPGetStates`, `animBPGetTransitions`, `animBPGetSlotGroups`, `animBPGetMontages`, `animBPGetBlendSpace`, `animBPGetStateMachine`, `animBPListMontages`

**Mutate (6):** `animBPAddState`, `animBPRemoveState`, `animBPAddTransition`, `animBPSetTransitionRule`, `animBPSetStateAnimation`, `animBPAddBlendNode`

---

## 9. AI / Behavior Trees

Full behavior tree editing and blackboard management.

**Read (6):** `aiListBehaviorTrees`, `aiGetBehaviorTree`, `aiListBlackboards`, `aiGetBlackboard`, `aiListControllers`, `aiGetEQSQueries`

**Mutate (8):** `btAddTask`, `btAddComposite`, `btRemoveNode`, `btAddDecorator`, `btAddService`, `btSetBlackboardValue`, `btWireNodes`, `btGetTree`

---

## 10. Audio System

Full audio pipeline -- create sound cues, set attenuation, spawn ambient sounds, create audio volumes.

**Read (6):** `audioGetStatus`, `audioListActiveSounds`, `audioGetDeviceInfo`, `audioListSoundClasses`, `audioGetStats`, `audioDebugVisualize`

**Mutate (8):** `audioSetVolume`, `audioPlay`, `audioStop`, `audioSetListener`, `audioCreateSoundCue`, `audioSetAttenuation`, `audioCreateAmbientSound`, `audioCreateAudioVolume`

---

## 11. PCG (Procedural Content Generation)

Full PCG graph editing and execution.

**Read (5):** `pcgListGraphs`, `pcgGetGraph`, `pcgGetNodeSettings`, `pcgListComponents`, `pcgGetComponentInfo`, `pcgGetDebugInfo`

**Mutate (8):** `pcgSetNodeSettings`, `pcgGenerate`, `pcgCleanup`, `pcgExecuteGraph`, `pcgCreateGraph`, `pcgAddNode`, `pcgRemoveNode`, `pcgConnectNodes`

---

## 12. Landscape and Foliage

Full terrain sculpting, layer painting, heightmap import/export, foliage placement.

**Read (4):** `landscapeGetInfo`, `landscapeGetLayerInfo`, `foliageGetInfo`, `foliageGetTypes`, `foliageGetInstances`

**Mutate (9):** `landscapeSculpt`, `landscapePaint`, `landscapeAddLayer`, `landscapeRemoveLayer`, `landscapeImportHeightmap`, `landscapeExportHeightmap`, `foliageAdd`, `foliageRemove`, `foliageSetDensity`

---

## 13. Skeletal Mesh

Morph targets, sockets, materials, physics assets.

**Read (5):** `skelMeshList`, `skelMeshGetInfo`, `skelMeshGetBones`, `skelMeshGetSockets`, `skelMeshGetMorphTargets`

**Mutate (5):** `skelMeshSetMorphTarget`, `skelMeshAddSocket`, `skelMeshRemoveSocket`, `skelMeshSetMaterial`, `skelMeshSetPhysicsAsset`

---

## 14. Physics

Constraints, mass, damping, gravity, impulse.

**Read (5):** `physicsGetInfo`, `physicsGetConstraints`, `physicsGetCollisionProfile`, `physicsSimulate`, `physicsGetOverlaps`

**Mutate (6):** `physicsAddConstraint`, `physicsRemoveConstraint`, `physicsSetMass`, `physicsSetDamping`, `physicsSetGravity`, `physicsApplyImpulse`

---

## 15. UMG (Widget Blueprints)

Full widget creation and manipulation.

**Read (4):** `umgListWidgets`, `umgGetWidgetTree`, `umgGetWidgetProperties`, `umgListHUDs`

**Mutate (6):** `umgCreateWidget`, `umgAddChild`, `umgRemoveChild`, `umgSetWidgetProperty`, `umgBindEvent`, `umgGetWidgetChildren`

---

## 16. Lighting

Create and configure all light types.

**Mutate (3):** `lightCreate` (Point, Spot, Directional, Rect, Sky), `lightSetProperties` (intensity, color, temperature, shadows, cone angles, attenuation, rect dimensions), `lightList`

---

## 17. Scene Hierarchy

Outliner management, actor labeling, folder organization.

**Read (4):** `sceneSnapshot`, `sceneGetHierarchy`, `sceneGetComponents`, `sceneGetTransformHierarchy`

**Mutate (6):** `sceneSetParent`, `sceneClearParent`, `sceneCreateFolder`, `sceneDeleteFolder`, `sceneSetActorLabel`, `sceneHideActor`

---

## 18. DataTable

Full row CRUD and schema introspection.

**Read (2):** `dataTableList`, `dataTableRead`

**Mutate (3):** `dataTableAddRow`, `dataTableDeleteRow`, `dataTableGetSchema`

---

## 19. Editor Settings

Project, editor, and rendering configuration.

**Read (4):** `settingsGetProject`, `settingsGetEditor`, `settingsGetRendering`, `settingsGetPlugins`

**Mutate (3):** `settingsSetProject`, `settingsSetEditor`, `settingsSetRendering`

---

## 20. Asset Management

Import, export, duplicate, rename, delete, move.

**Tools (7):** `assetImport`, `assetExport`, `assetDuplicate`, `assetRename`, `assetDelete`, `assetMove`, `openAsset`

---

## 21. World Partition

Data layers and runtime grids.

**Read (1):** `wpGetInfo`

**Mutate (2):** `wpSetActorDataLayer`, `wpSetActorRuntimeGrid`

---

## 22. MetaHuman and Groom

List, spawn, and bind MetaHumans and groom assets.

**Tools (4):** `metahumanList`, `metahumanSpawn`, `groomList`, `groomSetBinding`

---

## 23. Python Bridge

Execute Python scripts and inline code in the editor's Python environment.

**Tools (2):** `pythonExecFile`, `pythonExecString`

The Python bridge gives access to the full `unreal` module, which includes every editor subsystem. This is the escape hatch for anything not covered by a dedicated tool.

---

## 24. Visual Agent / Automation (13)

`screenshot`, `focusActor`, `selectActor`, `setViewport`, `moveActor`, `getCamera`, `listViewports`, `getSelection`, `resolveRef`, `drawDebug`, `clearDebug`, `renderCapture`, `outputLog`

---

## 25. XR / VR (10)

`xrStatus`, `xrControllers`, `xrGuardian`, `xrHandTracking`, `xrPassthrough`, `xrRecenter`, `xrSetDisplayFrequency`, `xrSetGuardianVisibility`, `xrSetPassthrough`, `xrSetPerformanceLevels`

---

## 26. Pixel Streaming (7)

`pixelStreamingGetStatus`, `pixelStreamingStart`, `pixelStreamingStop`, `pixelStreamingListStreamers`, `pixelStreamingGetCodec`, `pixelStreamingSetCodec`, `pixelStreamingListPlayers`

---

## 27. PIE Control (5)

`startPIE`, `stopPIE`, `pausePIE`, `stepPIE`, `getPIEState`

---

## 28. Console Commands (4)

`executeConsole`, `getCVar`, `setCVar`, `simulateInput`

---

## 29. Validation and Safety (6)

`validateBlueprint`, `snapshotGraph`, `restoreGraph`, `beginTransaction`, `endTransaction`, `undo`

---

## 30. Packaging and Build (4)

`buildCook`, `buildPackage`, `buildGetStatus`, `buildGetLog`

---

## 31. Source Control (2)

`sourceControlGetStatus`, `sourceControlCheckout`

---

## 32. Story / Narrative (4)

`storyState`, `storyAdvance`, `storyGoto`, `storyPlay`

---

## What This Plugin CANNOT Do

| Cannot Do | Why | Workaround |
|-----------|-----|------------|
| Compile C++ code | Requires full build toolchain | Use Blueprint or Python instead |
| Access runtime-only state | Editor operates in edit mode | Start PIE via `startPIE` and query |
| Modify engine source | Read-only access to engine | Modify project source only |
| Record motion capture | Requires external hardware | Import recorded data via FBX |
| Generate 3D assets | Not a generative tool | Import pre-made assets |

---

## Connection Protocol

On every MCP connection, the following happens automatically:

1. This `CAPABILITIES.md` file is loaded and injected into the AI context
2. The `CLAUDE.md` file is loaded with workflow instructions
3. All context docs from `Tools/contexts/` are available on demand
4. The persistent project state file (if it exists) at `Content/AgenticMCP/project_state.json` is read
5. The recent actions buffer is read from `Content/AgenticMCP/recent_actions.log`

The AI agent should:
1. Read this capabilities list first
2. Read the project state to understand what exists
3. Read the recent actions to understand what the user just did
4. Only then begin executing the requested task
