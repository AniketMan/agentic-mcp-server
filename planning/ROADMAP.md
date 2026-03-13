# AgenticMCP Implementation Roadmap

## Target: Full UE5.6 Automation with Meta XR Integration

**Started**: March 10, 2026
**Engine**: UE5.6 (Meta Oculus Fork)
**Project**: SohVr @ `C:\Users\aniketbhatt\Desktop\SOH\Dev\Main\SohVr.uproject`

---

## Phase 1: Meta XR / OculusXR 5.6 ✅ COMPLETE

### HMD & Device (4 endpoints)
- [x] `xrGetHMDState` - Device type, pose, tracking, display frequency, GPU metrics
- [x] `xrSetTracking` - Enable/disable position and orientation tracking
- [x] `xrRecenter` - Reset HMD base rotation and position
- [x] `xrGetGuardian` - Guardian bounds, play area dimensions

### Controllers & Input (3 endpoints)
- [x] `xrGetControllers` - Left/right controller tracking status, type
- [x] `xrGetHandTracking` - Hand position, confidence, scale, dominant hand
- [x] `xrTriggerHaptic` - Set haptic feedback by frequency/amplitude/location
- [x] `xrStopHaptic` - Stop haptic effects

### Passthrough / Mixed Reality (2 endpoints)
- [x] `xrGetPassthrough` - Passthrough support status, color, recommendation
- [x] `xrSetPassthrough` - Enable/disable environment depth, hand removal, dimming

### Movement Tracking (6 endpoints)
- [x] `xrGetEyeTracking` - Eye gaze state, positions, confidence
- [x] `xrSetEyeTracking` - Start/stop eye tracking
- [x] `xrGetFaceTracking` - Face expression weights (70 blendshapes)
- [x] `xrSetFaceTracking` - Start/stop face tracking, visemes
- [x] `xrGetBodyTracking` - Body joint state (84 joints full body)
- [x] `xrSetBodyTracking` - Start/stop body tracking, calibration

### PIE Control (5 endpoints)
- [x] `startPIE` - Start Play In Editor session
- [x] `stopPIE` - Stop PIE session
- [x] `pausePIE` - Pause/resume PIE
- [x] `stepPIE` - Single frame step
- [x] `getPIEState` - Get current PIE status

### Console Commands (4 endpoints)
- [x] `executeConsole` - Execute console command
- [x] `getCVar` - Get console variable value
- [x] `setCVar` - Set console variable
- [x] `listCVars` - List matching console variables

**Total Phase 1**: 20 new endpoints

---

## Phase 2: RenderDoc Integration (HIGH PRIORITY) ⏳ PENDING

**Location**: `C:\Users\aniketbhatt\AppData\Roaming\odh\packages\tools\renderdoc-oculus\`

### Planned Endpoints
- [ ] `renderdocConnect` - Connect to RenderDoc
- [ ] `renderdocCapture` - Capture frame
- [ ] `renderdocGetCaptures` - List captures
- [ ] `renderdocLoadCapture` - Load capture for analysis
- [ ] `renderdocGetDrawcalls` - Get drawcall list
- [ ] `renderdocGetTextureInfo` - Texture resource info
- [ ] `renderdocGetShaderInfo` - Shader compilation info
- [ ] `renderdocExportCapture` - Export to file

---

## Phase 3: HZOS MCP Bridge ⏳ PENDING

**Location**: `C:\Program Files\Meta Quest Developer Hub\resources\bin\hzos-dev-mcp.exe`

### Planned Endpoints
- [ ] `hzosConnect` - Connect to HZOS MCP
- [ ] `hzosInstallApp` - Install APK to Quest
- [ ] `hzosLaunchApp` - Launch app on device
- [ ] `hzosStopApp` - Stop running app
- [ ] `hzosGetDevices` - List connected Quest devices
- [ ] `hzosScreencast` - Start/stop screencast

---

## Phase 4: OVR Metrics ⏳ PENDING

### Planned Endpoints
- [ ] `ovrGetMetrics` - Get performance metrics
- [ ] `ovrStartPerfCapture` - Start Perfetto trace
- [ ] `ovrStopPerfCapture` - Stop Perfetto trace
- [ ] `ovrGetBatteryStatus` - Battery info
- [ ] `ovrGetThermalStatus` - Thermal throttling info

---

## Phase 5: LiveLink + ARKit Face ⏳ PENDING

### Planned Endpoints (LiveLink)
- [ ] `liveLinkGetSources` - List LiveLink sources
- [ ] `liveLinkConnect` - Connect to source
- [ ] `liveLinkDisconnect` - Disconnect source
- [ ] `liveLinkGetSubjects` - Get tracked subjects
- [ ] `liveLinkGetFrameData` - Get current frame data

### Planned Endpoints (ARKit Face)
- [ ] `arkitFaceGetBlendshapes` - Get 52 ARKit blendshapes
- [ ] `arkitFaceMapToMetaHuman` - Map to MetaHuman face rig
- [ ] `arkitFaceRecord` - Record face animation
- [ ] `arkitFaceExport` - Export to animation asset

---

## File Summary

| File | Status | Endpoints |
|------|--------|-----------|
| `Handlers_MetaXR.cpp` | ✅ Complete | 16 |
| `Handlers_PIE.cpp` | ✅ Complete | 5 |
| `Handlers_Console.cpp` | ✅ Complete | 4 |
| `AgenticMCPServer.cpp` | ✅ Updated | (registration) |
| `AgenticMCPServer.h` | ✅ Updated | (declarations) |
| `AgenticMCP.Build.cs` | ✅ Updated | (OculusXR deps) |

---

## Build Status

```
Last Build: Pending
Status: Code complete, not yet compiled
Dependencies Added: OculusXRHMD, OculusXRInput, OculusXRMovement
```

---

## Notes

- All OculusXR functions use **5.6 API signatures** verified from actual headers
- Eye/Face/Body tracking via `OculusXRMovementFunctionLibrary` (not deprecated OculusXREyeTracker)
- Passthrough uses `UOculusXRFunctionLibrary` environment depth APIs
- Haptics support Touch Pro finger locations (Hand, Thumb, Index)
# AgenticMCP Implementation Roadmap

This document tracks the implementation progress of all AgenticMCP endpoints.

**Last Updated:** 2026-03-10

---

## Current Status

| Category | Existing | Implemented | Planned | Total |
|----------|----------|-------------|---------|-------|
| Blueprint Read | 10 | - | - | 10 |
| Blueprint Mutation | 15 | - | - | 15 |
| Actor Management | 6 | - | - | 6 |
| Level Management | 3 | - | - | 3 |
| Validation | 3 | - | - | 3 |
| Visual/Viewport | 9 | - | - | 9 |
| Undo/Redo | 5 | - | - | 5 |
| State Persistence | 4 | - | - | 4 |
| **PIE Control** | - | 5 | - | 5 |
| **Console Commands** | - | 4 | - | 4 |
| **MetaXR/OculusXR** | - | 0 | 8 | 8 |
| **MetaXR Haptics** | - | 0 | 4 | 4 |
| **MetaXR Audio** | - | 0 | 3 | 3 |
| **RenderDoc** | - | 0 | 8 | 8 |
| **HZOS MCP Bridge** | - | 0 | 6 | 6 |
| **OVR Metrics** | - | 0 | 5 | 5 |
| **LiveLink** | - | 0 | 6 | 6 |
| **ARKit Face** | - | 0 | 5 | 5 |
| **Takes** | - | 0 | 5 | 5 |
| **Asset Import** | - | 0 | 8 | 8 |
| **Level Sequence** | - | 0 | 8 | 8 |
| **Build Commands** | - | 0 | 5 | 5 |
| **Content Browser** | - | 0 | 5 | 5 |
| **Editor Notifications** | - | 0 | 3 | 3 |
| **Source Control** | - | 0 | 4 | 4 |
| **Project Settings** | - | 0 | 3 | 3 |
| **World Partition** | - | 0 | 4 | 4 |
| **Material Instance** | - | 0 | 5 | 5 |
| **Animation BP** | - | 0 | 5 | 5 |
| **Data Tables** | - | 0 | 4 | 4 |
| **Niagara** | - | 0 | 4 | 4 |
| **Audio Control** | - | 0 | 4 | 4 |
| **Skeleton/Pose** | - | 0 | 4 | 4 |
| **Modeling Mode** | - | 0 | 6 | 6 |
| **Landscape Mode** | - | 0 | 7 | 7 |
| **Animation Graphs** | - | 0 | 8 | 8 |
| **PCG** | - | 0 | 8 | 8 |
| **MetaHumans** | - | 0 | 9 | 9 |
| **Control Rig** | - | 0 | 4 | 4 |
| **Movie Render Pipeline** | - | 0 | 5 | 5 |
| **Geometry Scripting** | - | 0 | 4 | 4 |
| **Datasmith** | - | 0 | 3 | 3 |
| **USD** | - | 0 | 4 | 4 |
| **TOTAL** | 55 | 9 | ~161 | ~225 |

---

## Phase 1: Meta XR (In Progress)

### MetaXR/OculusXR VR Endpoints
| Endpoint | Status | Description |
|----------|--------|-------------|
| `xrGetHMDState` | ⏳ | Get HMD transform, IPD, refresh rate |
| `xrSetHMDTracking` | ⏳ | Enable/disable tracking |
| `xrGetControllers` | ⏳ | Get controller transforms, buttons |
| `xrTriggerHaptic` | ⏳ | Fire haptic feedback |
| `xrRecenterHMD` | ⏳ | Recenter tracking origin |
| `xrGetGuardian` | ⏳ | Get guardian boundary |
| `xrSetPassthrough` | ⏳ | Enable/disable passthrough |
| `xrGetEyeTracking` | ⏳ | Get eye tracking data |

### MetaXR Haptics Endpoints
| Endpoint | Status | Description |
|----------|--------|-------------|
| `hapticPlayClip` | ⏳ | Play haptic clip |
| `hapticStop` | ⏳ | Stop haptics |
| `hapticSetIntensity` | ⏳ | Set intensity |
| `hapticListClips` | ⏳ | List available clips |

### MetaXR Audio Endpoints
| Endpoint | Status | Description |
|----------|--------|-------------|
| `audioSetSpatial` | ⏳ | Set spatial audio settings |
| `audioSetOcclusion` | ⏳ | Set occlusion parameters |
| `audioGetState` | ⏳ | Get audio state |

---

## Phase 2: RenderDoc (#1 Priority)

| Endpoint | Status | Description |
|----------|--------|-------------|
| `renderDocCapture` | ⏳ | Start GPU frame capture |
| `renderDocInject` | ⏳ | Inject into running process |
| `renderDocReplay` | ⏳ | Replay a capture file |
| `renderDocStartServer` | ⏳ | Start remote replay server |
| `renderDocStopServer` | ⏳ | Stop server |
| `renderDocSaveCapture` | ⏳ | Save capture to file |
| `renderDocGetCaptures` | ⏳ | List available captures |
| `renderDocAnalyze` | ⏳ | Get capture analysis |

**RenderDoc Location:** `C:\Users\aniketbhatt\AppData\Roaming\odh\packages\tools\renderdoc-oculus\`

---

## Phase 3: HZOS MCP Bridge

| Endpoint | Status | Description |
|----------|--------|-------------|
| `hzosConnect` | ⏳ | Connect to HZOS MCP server |
| `hzosListDevices` | ⏳ | List connected Quest devices |
| `hzosInstallAPK` | ⏳ | Install APK to device |
| `hzosLaunchApp` | ⏳ | Launch app on device |
| `hzosScreencast` | ⏳ | Start screencasting |
| `hzosMetaWand` | ⏳ | Generate 3D model via Meta Wand API |

**HZOS Location:** `C:\Program Files\Meta Quest Developer Hub\resources\bin\hzos-dev-mcp.exe`

---

## Phase 4: OVR Metrics

| Endpoint | Status | Description |
|----------|--------|-------------|
| `ovrStartProfiling` | ⏳ | Start performance profiling |
| `ovrStopProfiling` | ⏳ | Stop and save profile |
| `ovrGetMetrics` | ⏳ | Get current metrics (FPS, GPU, CPU) |
| `ovrSetOverlay` | ⏳ | Toggle metrics overlay |
| `ovrExportReport` | ⏳ | Export performance report |

---

## Phase 5: LiveLink & ARKit Face

### LiveLink Endpoints
| Endpoint | Status | Description |
|----------|--------|-------------|
| `liveLinkListSources` | ⏳ | List LiveLink sources |
| `liveLinkAddSource` | ⏳ | Add LiveLink source |
| `liveLinkRemoveSource` | ⏳ | Remove LiveLink source |
| `liveLinkGetSubjects` | ⏳ | Get LiveLink subjects |
| `liveLinkSetEnabled` | ⏳ | Enable/disable subject |
| `liveLinkLoadPreset` | ⏳ | Load LiveLink preset |

### ARKit Face Endpoints
| Endpoint | Status | Description |
|----------|--------|-------------|
| `arkitEnableFace` | ⏳ | Enable ARKit face tracking |
| `arkitGetBlendshapes` | ⏳ | Get 52 blendshapes |
| `arkitCalibrate` | ⏳ | Calibrate face tracking |
| `arkitMapToCharacter` | ⏳ | Map to character |
| `arkitGetTrackingQuality` | ⏳ | Get tracking quality |

---

## Phase 6: Takes (Recording)

| Endpoint | Status | Description |
|----------|--------|-------------|
| `takeStart` | ⏳ | Start recording |
| `takeStop` | ⏳ | Stop recording |
| `takeList` | ⏳ | List takes |
| `takeReview` | ⏳ | Review take |
| `takeMarkGood` | ⏳ | Mark take as good |

---

## Phase 7: Asset Import/Export

| Endpoint | Status | Description |
|----------|--------|-------------|
| `importFBX` | ⏳ | Import FBX file |
| `importTexture` | ⏳ | Import texture |
| `importAudio` | ⏳ | Import audio file |
| `importDatasmith` | ⏳ | Import Datasmith |
| `importUSD` | ⏳ | Import USD |
| `exportAsset` | ⏳ | Export asset |
| `reimportAsset` | ⏳ | Reimport asset |
| `getImportSettings` | ⏳ | Get import settings |

---

## Phase 8: Level Sequence

| Endpoint | Status | Description |
|----------|--------|-------------|
| `seqCreate` | ⏳ | Create level sequence |
| `seqOpen` | ⏳ | Open sequence |
| `seqAddBinding` | ⏳ | Add actor binding |
| `seqAddTrack` | ⏳ | Add track |
| `seqSetKeyframe` | ⏳ | Set keyframe |
| `seqPlay` | ⏳ | Play sequence |
| `seqPause` | ⏳ | Pause sequence |
| `seqGetInfo` | ⏳ | Get sequence info |

---

## Phase 9: Build Commands

| Endpoint | Status | Description |
|----------|--------|-------------|
| `buildLighting` | ⏳ | Build lighting |
| `buildNavigation` | ⏳ | Build navigation mesh |
| `buildHLODs` | ⏳ | Build HLODs |
| `buildAll` | ⏳ | Build all |
| `buildGPULightmaps` | ⏳ | Build GPU lightmaps |

---

## Phase 10-15: Remaining Categories

See CAPABILITIES.md for full endpoint documentation as they are implemented.

---

## External Tools Integration

| Tool | Location | Status |
|------|----------|--------|
| **RenderDoc (Meta Fork)** | `odh\packages\tools\renderdoc-oculus` | ⏳ |
| **HZOS Dev MCP** | `Meta Quest Developer Hub\bin\hzos-dev-mcp.exe` | ⏳ |
| **OVR Metrics Tool** | `odh\packages\lib\ovr-metrics-tool-sdk` | ⏳ |
| **Meta Haptics Studio** | `odh\packages\tools\meta-haptics-studio-win` | ⏳ |

---

## Enabled Plugins (SohVr Project)

| Plugin | Version | Status |
|--------|---------|--------|
| OculusXR | 5.6 | ✅ Enabled |
| MetaXRAudio | 5.6 | ✅ Enabled |
| MetaXRHaptics | 5.6 | ✅ Enabled |
| LiveLink | 5.6 | ✅ Enabled |
| LiveLinkControlRig | 5.6 | ✅ Enabled |
| AppleARKitFaceSupport | 5.6 | ✅ Enabled |
| Takes | 5.6 | ✅ Enabled |
| MovieRenderPipeline | 5.6 | ✅ Enabled |
| ModelingToolsEditorMode | 5.6 | ✅ Enabled |
| GeometryScripting | 5.6 | ✅ Enabled |
| DatasmithImporter | 5.6 | ✅ Enabled |
| USDImporter | 5.6 | ✅ Enabled |
| GPULightmass | 5.6 | ✅ Enabled |

---

## Legend

- ✅ Complete
- 🔨 In Progress
- ⏳ Pending
- ❌ Blocked

---

## Changelog

### 2026-03-10
- Created roadmap document
- Implemented PIE Control handlers (5 endpoints)
- Implemented Console Commands handlers (4 endpoints)
- Started Phase 1: MetaXR implementation
