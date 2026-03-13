# Reference Material

This directory contains reference material that Claude Code reads directly from the filesystem for inference. No MCP overhead -- Claude loads what it needs on demand.

## Directory Structure

```
reference/
  source_truth/
    script_v2_ocvr.md          # THE source truth. OC VR screenplay v2.
                                 # Every implementation decision traces back to this.
  game_design/
    SOHGameDesign.webp          # Full interaction flowchart (8 scenes, color-coded)
                                 # GAZE=green, TRIGGER=red/pink, NAVIGATION=blue
  project_config/
    UnrealClient.Target.cs      # UE5 build target: Client
    UnrealEditor.Target.cs      # UE5 build target: Editor
    UnrealGame.Target.cs        # UE5 build target: Game (UnrealGame module)
    UnrealServer.Target.cs      # UE5 build target: Server
```

## UE5 Engine Documentation

The full UE5 API docs (C++, Blueprint, DocSource) ship with the engine and are NOT duplicated here. Claude reads them directly from the engine install at the relative path:

```
Engine\Documentation\Builds
```

This contains the complete C++ API reference, Blueprint API reference, and DocSource. When you need to look up a UE5 class, function, or node, read the HTML directly from that path. No indexes, no shortcuts -- read the actual docs.

## How Claude Uses This

1. **Script is source truth**: Every Blueprint graph, level sequence, and interaction traces back to `script_v2_ocvr.md`. If the script says "GAZE at Heather photo", the implementation must create a gaze trigger that activates the specified level sequence. The script is the input. The output is code, Blueprint graphs, and MCP calls that implement it.

2. **Engine docs for API lookup**: Read the full UE5 API documentation directly from `Engine\Documentation\Builds`. C++ API, Blueprint API, and DocSource are all there.

3. **Game design for validation**: The flowchart confirms interaction flow and dependencies between scenes.

4. **Target.cs for build config**: Confirms the project uses `UnrealGame` module with `BuildEnvironment.Shared`.

## Confidence Gate + Project State Validator

The MCP server enforces a two-layer defense on all mutation operations:

**Layer 1 -- Confidence Gate** (`Tools/confidence-gate.js`):
Claude can read and plan freely, but execution is blocked when inference confidence is below 0.7.

| Signal | Weight | Description |
|--------|--------|-------------|
| Planned through quantized path | +0.4 | Operation was pre-computed by the inference layer |
| Asset verified in manifest | +0.3 | Referenced Blueprint/actor exists in cached manifest |
| Snapshot safety net active | +0.2 | A graph snapshot was taken before mutation |
| Script-aligned | +0.1 | Operation traces back to a scene in the script |

**Layer 2 -- Project State Validator** (`Tools/project-state-validator.js`):
Every mutation is cross-validated against the live UE5 project state. The validator makes read-only calls to the editor to verify that what Claude claims exists actually exists. 20 mutation tools covered, 29 total validation checks.

When blocked, Claude receives: what it claimed vs what actually exists, the exact read-only tool to call, and the relevant engine doc path at `Engine\Documentation\Builds`.
