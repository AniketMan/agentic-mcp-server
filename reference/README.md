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
  ue5_api/
    blueprint_api_index.json    # Searchable index of all 662 Blueprint API categories
    vr_relevant_api_index.json  # Focused subset: 190 VR-relevant categories
    ConsoleHelpTemplate.html    # UE5 console command reference
```

## UE5 Engine Documentation

The full UE5 API docs (C++, Blueprint, DocSource) ship with the engine and are NOT duplicated here. Claude reads them directly from the engine install at the relative path:

```
Engine\Documentation\Builds
```

The index files (`blueprint_api_index.json`, `vr_relevant_api_index.json`) provide a lightweight searchable catalog so Claude can locate the right doc page, then read the full HTML from the engine path.

## How Claude Uses This

1. **Script is source truth**: Every Blueprint graph, level sequence, and interaction traces back to `script_v2_ocvr.md`. If the script says "GAZE at Heather photo", the implementation must create a gaze trigger that activates the specified level sequence. The script is the input. The output is code, Blueprint graphs, and MCP calls that implement it.

2. **API index for lookup**: Claude searches `vr_relevant_api_index.json` to find the right Blueprint nodes/functions, then reads the full doc from `Engine\Documentation\Builds`.

3. **Game design for validation**: The flowchart confirms interaction flow and dependencies between scenes.

4. **Target.cs for build config**: Confirms the project uses `UnrealGame` module with `BuildEnvironment.Shared`.

## Confidence Gate

The MCP server enforces a confidence gate on all mutation operations. Claude can read and plan freely, but execution is blocked when inference confidence is below 0.7. See `Tools/confidence-gate.js` for the scoring breakdown:

| Signal | Weight | Description |
|--------|--------|-------------|
| Planned through quantized path | +0.4 | Operation was pre-computed by the inference layer |
| Asset verified in manifest | +0.3 | Referenced Blueprint/actor exists in cached manifest |
| Snapshot safety net active | +0.2 | A graph snapshot was taken before mutation |
| Script-aligned | +0.1 | Operation traces back to a scene in the script |
