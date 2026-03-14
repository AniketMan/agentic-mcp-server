<- Added audit scripts: audit-handlers.mjs, audit-test.mjs, WORKER REFERENCE: Load this file when executing quantized inference tools -->
# Quantized Inference Context

## Architecture Overview

The Quantized Inference Layer implements the Quantized Spatial Data Architecture
for the AgenticMCP pipeline. It replaces brute-force asset scanning with cached,
deterministic inference.

### Key Concepts

**Adaptive Asset Manifest**: A quantized representation of the entire UE5 project.
Assets are tiered by importance:
- **Hero** (float16 equivalent): Characters, key interactables -- full structural detail
- **Standard** (int16 equivalent): Triggers, markers, props -- name + class + position
- **Background** (int8 equivalent): Static meshes, lights -- name + class only

The manifest is cached to `.quantized_cache/asset_manifest.json` and refreshed
on demand. Subsequent reads skip the scan entirely.

**Wiring Plans**: Pre-computed MCP call sequences for each scene interaction.
Cross-references the manifest against the scene INSTRUCTIONS.md to produce
deterministic operation lists with confidence scores.

**Confidence Scoring**: Every operation reports confidence (0.0-1.0).
- 1.0: All referenced actors, sequences, and assets found in manifest
- 0.6: Actor not found (may need spawning or level loading)
- 0.5: Level Sequence not found (may not be created yet)
- Below 0.5: Critical references missing -- human review required

### Tool Reference

| Tool | Purpose | Read-Only |
|------|---------|-----------|
| `quantize_project` | Generate/refresh asset manifest | Yes |
| `get_scene_plan` | Get wiring plan for one scene | Yes |
| `get_all_scene_plans` | Get plans for all 8 scenes | Yes |
| `get_wiring_status` | Coordinator status overview | Yes |
| `execute_scene_step` | Get call sequence for a step | No |
| `mark_step_complete` | Persist step completion | No |
| `mark_scene_blocked` | Mark scene as blocked | No |

### Wiring Call Sequence (Per Interaction)

Each interaction follows this deterministic pattern:
1. `snapshot_graph` -- safety backup
2. `add_node(CustomEvent)` -- capture GUID
3. `add_node(CallFunction: GetSubsystem)` -- capture GUID
4. `add_node(CallFunction: BroadcastMessage)` -- capture GUID
5. `add_node(MakeStruct: Msg_StoryStep)` -- capture GUID
6. `set_pin_default(Step = N)` -- set step number
7. `connect_pins` x4 -- wire the execution chain

GUIDs from steps 2-5 are substituted into steps 6-7 using `${variableName}` syntax.

### Progress Persistence

Every completed step is immediately written to `scene_coordination/scene_N/STATUS.json`.
If the process is interrupted, it resumes from the last incomplete step.
No work is ever lost.
