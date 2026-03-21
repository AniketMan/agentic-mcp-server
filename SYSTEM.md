# SYSTEM.md: AgenticMCP System Configuration — OrdinaryCourage VR

This document defines the operational rules for the AgenticMCP system serving the **Ordinary Courage VR** project. The system uses **direct inference** with native tool calling. There is no external planner. A single local Llama model handles request interpretation, tool selection, and multi-step execution.

## Project Identity

| Property | Value |
|----------|-------|
| **Project** | Ordinary Courage VR (OCVR) |
| **Engine** | UE 5.6.1 Meta Oculus Fork (`C:\VRUnreal`) |
| **Engine GUID** | `{C89AE0A6-45F9-19FD-DA94-F78CB075395D}` |
| **Project Root** | `C:\Users\aniketbhatt\Desktop\SOH\Dev\Narrative` |
| **Module** | `OrdinaryCourage` (Runtime, minimal — 5 files) |
| **Core Plugin** | `VRNarrativeKit` (34 runtime + 18 editor + 8 test files) |
| **Target** | Meta Quest 2/Pro/3 + Windows VR |
| **Frame Budget** | 11.1ms (90fps) / 13.9ms (72fps) |

## System Flow

```
User Request (natural language)
    -> Request Handler (request-handler.js)
    -> Local Llama (native tool calling via llama.cpp)
    -> Validation Stack (rule engine, confidence gate, project state, idempotency)
    -> C++ Plugin (UE 5.6.1 Meta Oculus Fork, port 9847)
    -> Result feeds back to Llama for next step
    -> Loop until request_complete or max iterations
```

## Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `UNREAL_MCP_URL` | `http://localhost:9847` | C++ plugin HTTP endpoint |
| `WORKER_URL` | `http://localhost:8081` | llama.cpp Worker server |
| `VALIDATOR_URL` | `http://localhost:8080` | llama.cpp Validator server (optional) |
| `QA_URL` | `http://localhost:8082` | llama.cpp QA Auditor server (optional) |
| `SPATIAL_URL` | `http://localhost:8083` | Cosmos Reason2 Spatial server (optional) |
| `CONFIDENCE_THRESHOLD` | `0.80` | Minimum confidence to execute a tool call |
| `MAX_REQUEST_ITERATIONS` | `10` | Maximum inference-execution loops per request |
| `MCP_REQUEST_TIMEOUT_MS` | `30000` | Per-request HTTP timeout (ms) |
| `MCP_ASYNC_ENABLED` | `true` | Enable async task queue |
| `AGENTIC_FALLBACK_ENABLED` | `true` | Enable offline binary injector |
| `AGENTIC_PROJECT_ROOT` | `C:\Users\aniketbhatt\Desktop\SOH\Dev\Narrative` | UE project root |

## Context Sources

The Worker model receives context from these sources, in priority order:

1. **Tool results** from previous calls in the current session (highest priority)
2. **Source truth** (`reference/source_truth/`) — project-specific data (NarrativeData schema, asset paths, scene graph)
3. **Tool definitions** — 394 function definitions from `tool-registry.json`
4. **User context** (`user_context/`) — game script, architecture docs, audio audit
5. **Engine documentation** — UE docs at `C:\VRUnreal`
6. **Tool-specific context** (`Tools/contexts/`) — domain-specific API docs

## Project Architecture (What You're Working With)

### Core Systems (VRNarrativeKit Plugin)

| System | Class | Role |
|--------|-------|------|
| **Narrative Brain** | `UNarrativeDirector` | `UGameInstanceSubsystem` — manages chapters, scenes, interactions, experience state |
| **Scene Loading** | `USceneStreamingManager` | `UWorldSubsystem` — 5 streaming strategies (OverlaySwap, PreloadedSwap, FullTransition, AdditiveLoad, DataLayerToggle) |
| **Experience Data** | `UNarrativeData` | `UDataAsset` — Chapter → Scene → Interactions + NarrationCues + Transitions |
| **VR Player** | `AVRNarrativePawn` | Base pawn with ISDK hand/controller auto-switching, body mesh, capabilities |
| **Gaze** | `UGazeInteractionSubsystem` | `UTickableWorldSubsystem` — eye tracking with HMD fallback, dwell timers |
| **Signal Bridge** | `UInteractionSignalComponent` | Bridges ISDK events → NarrativeDirector, audio, VFX, haptics |
| **Narration** | `UNarrationManager` | VO playback, subtitle events, MetaXR Audio configuration |
| **Comfort** | `UComfortSettings` | `UGameInstanceSubsystem` — seated mode, vignette, subtitles, locomotion prefs |

### Meta SDK Stack (All interactions go through these — NEVER reimplement)

| SDK | Purpose | Module |
|-----|---------|--------|
| **ISDK** (OculusInteraction) | Grab, poke, ray, hand tracking, controller tracking | `OculusInteraction` |
| **Movement SDK** (OculusXRMovement) | Body/face/eye tracking, retargeting | `OculusXRMovement` |
| **MetaXR Audio** | Spatial audio, HRTF, acoustic geometry, ambisonics | `MetaXRAudio` |
| **MetaXR Haptics** | Haptic clips, HD haptics on Quest 3 | `MetaXRHaptics` |
| **OculusPlatform** | Cloud save, rich presence, entitlements | `OVRPlatform` |

### The Golden Rule

**Meta + Epic SDKs first, always.** VRNarrativeKit only fills gaps the SDKs don't cover. If ISDK can do it, use ISDK. If Movement SDK can do it, use Movement SDK. If MetaXR Audio can do it, use MetaXR Audio. Zero custom code for things SDKs already handle.

## Scene Structure (10 Scenes, 3 Chapters + Tutorial)

| Scene | ID | Title | Map | Environment |
|-------|----|-------|-----|-------------|
| 00 | Tutorial | Tutorial | Tutorial_Void | Dynamic/Void |
| 01 | Scene01 | Larger Than Life | ML_Main | Susan's Home — Day |
| 02 | Scene02 | Standing Up For Others | ML_Main | Susan's Home — Continuous |
| 03 | Scene03 | Rescuers | ML_Main | Susan's Home — Continuous |
| 04 | Scene04 | Stepping Into Adulthood | ML_Main | Susan's Home — Continuous |
| 05 | Scene05 | Dinner Together | ML_Restaurant | Restaurant — Magic Hour |
| 06 | Scene06 | The Charlottesville Rally | ML_Scene6 | Dynamic Environment |
| 07 | Scene07 | The Hospital | ML_Hospital | Hospital — Day |
| 08 | Scene08 | Turning Grief Into Action | ML_TrailerScene8 | Susan's Home — Evening |
| 09 | Scene09 | Legacy In Bloom | ML_Scene9 | Dynamic — Dawn |

### Interaction Types

| Type | ISDK Component | VRNarrativeKit Bridge |
|------|---------------|----------------------|
| **NAVIGATION** | Teleportation markers | `VRLocomotionComponent` |
| **GAZE** | Eye/HMD tracking + dwell | `GazeTargetComponent` → `InteractionSignalComponent` |
| **GRIP** | `IsdkGrabbableComponent` | `InteractionSignalComponent` (OnGrabbed → FireSignal) |
| **TRIGGER** | `IsdkRayInteractable` / Button | `InteractionSignalComponent` (OnSelected → FireSignal) |

## Workflow Rules

These rules are enforced by the deterministic Rule Engine, not by the LLM:

1. **Snapshot before mutation.** Any tool call that modifies a Blueprint must be preceded by a `snapshotGraph` call.
2. **Compile after mutation.** Any Blueprint modification sequence must end with `compileBlueprint`.
3. **Exact tool names.** Tool names must match `tool-registry.json`. The Rule Engine rejects unknown tools.
4. **Exact parameter names.** Parameter names must match the registry schema. Unknown parameters are flagged.
5. **Type checking.** Parameter types (string, number, boolean) are validated against the registry.
6. **Dependency ordering.** Steps that depend on prior results must wait for those results.
7. **GUID resolution.** GUID references from prior steps must be captured before use.
8. **Never reimplement SDK features.** Do not create custom grab/poke/ray/audio/haptic systems. Use ISDK and MetaXR.
9. **InteractionSignalComponent is the bridge.** All ISDK events flow through `InteractionSignalComponent.FireSignal()` to reach narrative/audio/VFX.
10. **NarrativeData is the source of truth.** Scene order, interaction requirements, narration cues — all live in `DA_NarrativeData`.

## MCP Tools

The system exposes these MCP tools to external clients (Claude, Cursor, Devmate, etc.):

| Tool | Description |
|------|-------------|
| `execute_request` | Send a natural language request. The system handles tool selection, validation, and execution autonomously. |
| All 394 UE tools | Direct tool access for clients that want to bypass inference and call tools directly. |

## Key Asset Paths

| Asset | Path |
|-------|------|
| NarrativeData | `/Game/Data/DA_NarrativeData` |
| Input Mapping Context | `/Game/Input/IMC_Default` |
| VR GameMode | `/Game/OrdinaryCourage/Blueprints/Core/VRGameMode` |
| MetaToolkit | `/Game/MetaToolkit/` |
| VR Assets | `/Game/VR/` (Haptics, Materials, VFX) |
| Characters | `/Game/Characters/Heathers/`, `/Game/Assets/Characters/` |
| Maps | `/Game/Maps/Game/` (Main, Restaurant, Scene6, Hospital, etc.) |
| Sequences | `/Game/Sequences/Scene1/` through `Scene9/` |
| Audio | `/Game/Sounds/` (VO, Music, SFX, SoundClasses) |

## File Structure

```
agentic-mcp-server/
  Tools/
    index.js              -- MCP bridge entry point
    request-handler.js    -- Native tool calling inference loop
    tool-registry.json    -- 394 tool definitions
    lib.js                -- Shared utilities
    gatekeeper/
      rule-engine.js      -- Deterministic validation
      llm-validator.js    -- Worker inference + confidence scoring
      dispatcher.js       -- Step execution
      technique-selector.js -- Routing logic
    contexts/             -- Tool-specific API documentation
  models/
    instructions/
      worker.md           -- Worker system prompt
      validator.md        -- Validator system prompt (optional)
      qa-auditor.md       -- QA Auditor system prompt (optional)
      spatial-reasoner.md -- Spatial Reasoner system prompt (optional)
    start-worker.bat      -- Start llama.cpp Worker server
    start-all.bat         -- Start all model servers
  reference/
    source_truth/         -- Project-specific asset data
    user_context/         -- User-provided documentation
  Source/
    AgenticMCP/           -- UE 5.6 C++ plugin source (66 handler files)
```
