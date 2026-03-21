`C:\Users\aniketbhatt\Desktop\SOH\Dev\correct\OrdinaryCourage\Plugins\agentic-mcp-server\MCP_REQUESTS.md`

# MCP Capability Requests

> Missing capabilities discovered during OrdinaryCourage build sessions.

---

## TODO

| # | Priority | Capability | What Happened | Workaround |
|---|----------|-----------|---------------|------------|
| 6 | P2 | **Viewport screenshot with camera control** | No MCP tool to position camera + take screenshot in one call. Need two separate Python commands. | Wrote Python scripts that call both |
| 8 | P2 | **TransitionPresetRegistry Factory** | `TransitionPresetRegistryFactory` doesn't exist — can't create this asset type via Python. | Use `createDataAsset` with `className: "TransitionPresetRegistry"` if it's a UDataAsset subclass. Otherwise needs a dedicated C++ factory. |
| 9 | P2 | **Pixel Streaming / vision** | MCP can't see the editor viewport. Worker model (Llama 8B) has no vision. | Existing `screenshot` tool returns base64 JPEG. Vision model integration is architectural. **NOT A PRIORITY — SAVE FOR LATER.** |

---

## Completed

| # | Priority | Capability | Fix | Handler |
|---|----------|-----------|-----|---------|
| 1 | P1 | **Create DataAsset by concrete class** | `createDataAsset` — takes `className`, `assetName`, `assetPath`. Sets `UDataAssetFactory::DataAssetClass` to resolved concrete subclass. | `Handlers_Utility.cpp` |
| 2 | P1 | **Add components to placed actors** | `addComponentToActor` — uses `NewObject` + `AddInstanceComponent` + `RegisterComponent`. Takes `actorName`, `componentClass`, optional `componentName`. | `Handlers_Utility.cpp` |
| 3 | P1 | **Enum value discovery** | `listEnumValues` — takes C++ enum name (with or without `E` prefix), returns all values with `pythonAccess` field showing exact `unreal.EnumName.VALUE` syntax. | `Handlers_Utility.cpp` |
| 4 | P1 | **Inline Python execution** | `pythonExecString` rewritten — always writes to GUID-based temp file, never uses `-c`. | `Handlers_PythonBridge.cpp` |
| 5 | P2 | **executePython output capture** | Both `pythonExecFile` and `pythonExecString` now capture stdout/stderr via GLog output device. Always return `stdout`, `stderr`, `hasErrors` fields. | `Handlers_PythonBridge.cpp` |
| 7 | P2 | **Text3D material assignment** | `setMaterialOnActor` — detects Text3D actors and routes through Python `set_front_material()` / `set_extrude_material()` / `set_bevel_material()` / `set_back_material()`. Standard meshes use `SetMaterial()` directly. | `Handlers_Utility.cpp` |
| 10 | P2 | **Property name discovery** | `listEditableProperties` — takes `className`, returns all editable properties with type, category, readOnly flag, `declaredIn` class. Partial-match suggestions if class not found. | `Handlers_Utility.cpp` |
| 11 | P3 | **Batch actor operations** | `spawnActorBatch` — takes `actors` array, spawns all in one call with per-actor results. Supports Blueprint and native classes, full transform. | `Handlers_Utility.cpp` |
| 12 | P3 | **Set world settings** | `setWorldSetting` — single or multi-property mode. Special handling for `DefaultGameMode` (class reference). Generic `FProperty::ImportText` for everything else. Returns current state. | `Handlers_Utility.cpp` |
| 13 | P3 | **Level sequence creation** | Already existed — 21 sequencer tools: `sequencerCreate`, `sequencerAddTrack`, `sequencerSetKeyframe`, `sequencerBindActor`, `sequencerAddCameraCut`, `sequencerAddAudioSection`, `sequencerRender`, etc. | `Handlers_SequencerEdit.cpp` |
| 14 | P3 | **Material graph node creation** | Already existed — `materialAddNode` supports TextureSample, Multiply, Add, Lerp, Constant, Constant3Vector, Fresnel, Panner, Time, Normalize, OneMinus, Power, Clamp, ScalarParameter, VectorParameter, TextureCoordinate. `materialConnectPins` wires them. | `Handlers_MaterialGraphEdit.cpp` |
| 15 | P3 | **Niagara system creation** | Already existed — `niagaraCreateSystem`, `niagaraAddEmitter`, `niagaraRemoveEmitter`, `niagaraSetParameter`, `niagaraSetSystemProperty`, `niagaraSpawnSystem` (16 tools total). Module-level config via `niagaraSetParameter` with parameter paths from `niagaraGetParameters`. | `Handlers_Niagara.cpp` |

---

## Session Log

| Date | Session | Result |
|------|---------|--------|
| 2026-03-18 | Plugin build + Scene 00 wiring | Discovered #1–#10 |
| 2026-03-18 | MCP fixes build session | Fixed #1, #2, #3, #4, #5, #7, #10, #11, #12. Verified #13, #14, #15 already existed. |

---

*Last updated: 2026-03-18*
