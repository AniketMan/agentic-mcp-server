# Reference Material

This directory contains reference material that the local Worker model reads from the filesystem for inference. No MCP overhead -- the Worker loads what it needs on demand.

## Directory Structure

```
reference/
  source_truth/
    script_v2_ocvr.md                  # THE source truth. OC VR screenplay v2.
    OC_VR_Implementation_Roadmap.md    # Technical implementation roadmap. Maps every script
                                        # beat to assets, components, triggers, VO lines,
                                        # and level sequences. Scenes 0-9. Includes all
                                        # [makeTempBP] specs with full logic pseudocode.
    ContentBrowser_Hierarchy.txt        # Full UE5 Content Browser dump. 3500+ lines.
                                        # Every asset path in the project. Characters,
                                        # Blueprints, Sequences (52), Maps (7), Props,
                                        # Interactables, Audio, VFX, Materials, Textures.
    LevelSequence_Master_Reference.md  # All Level Sequences with verification checklists.
  game_design/
    SOHGameDesign.webp                  # Full interaction flowchart (8 scenes, color-coded)
                                        # GAZE=green, TRIGGER=red/pink, NAVIGATION=blue
  project_config/
    UnrealClient.Target.cs              # UE5 build target: Client
    UnrealEditor.Target.cs              # UE5 build target: Editor
    UnrealGame.Target.cs                # UE5 build target: Game (UnrealGame module)
    UnrealServer.Target.cs              # UE5 build target: Server
```

## Source Truth Hierarchy

1. **Script** (`script_v2_ocvr.md`) -- the creative source truth. What the experience IS.
2. **Implementation Roadmap** (`OC_VR_Implementation_Roadmap.md`) -- the technical translation. Maps script to UE5 assets, components, and logic. Includes all `[makeTempBP]` specs.
3. **Content Browser Hierarchy** (`ContentBrowser_Hierarchy.txt`) -- the project state. Every asset that currently exists. Cross-reference against the roadmap to know what's built vs what's missing.

## UE5 Engine Documentation

The full UE5 API docs (C++, Blueprint, DocSource) ship with the engine and are NOT duplicated here. The Worker references them directly from the engine install path:

```
C:\UE56\Engine\Documentation\Builds
```

This contains the complete C++ API reference, Blueprint API reference, and DocSource. When looking up a UE5 class, function, or node, read the HTML directly from that path.

## How the Worker Uses This

1. **Script is source truth**: Every Blueprint graph, level sequence, and interaction traces back to `script_v2_ocvr.md`. The script is the input. The output is tool calls that implement it.

2. **Roadmap is the technical plan**: `OC_VR_Implementation_Roadmap.md` maps every script beat to exact assets, components, triggers, and VO timing. It includes pseudocode for every `[makeTempBP]` that needs to be created. Follow it.

3. **Content Browser is ground truth for assets**: `ContentBrowser_Hierarchy.txt` tells you what exists right now. Cross-reference against the roadmap. If the roadmap says `BP_Fridge [makeTempBP]` and it's not in the Content Browser, it needs to be created.

4. **Engine docs for API lookup**: Read the full UE5 API documentation directly from `Engine\Documentation\Builds`.

5. **Game design for validation**: The flowchart confirms interaction flow and dependencies between scenes.

6. **Target.cs for build config**: Confirms the project uses `UnrealGame` module with `BuildEnvironment.Shared`.

## User Context

The `user_context/` folder contains user-provided documentation and persistent chat logs.

- **User-provided docs** are read-only law. The system never modifies them.
- **Chat logs** are auto-saved by the TUI after each session, providing persistent memory across sessions.
- The more context in this folder, the fewer read-only queries the Worker needs before executing mutations.

## Confidence Gate + Project State Validator

The MCP server enforces a two-layer defense on all mutation operations:

**Layer 1 -- Confidence Gate** (`Tools/confidence-gate.js`):
The Worker can read freely, but execution is blocked when inference confidence is below 0.7.

| Signal | Weight | Description |
|--------|--------|-------------|
| Planned through quantized path | +0.4 | Operation was pre-computed by the inference layer |
| Asset verified in manifest | +0.3 | Referenced Blueprint/actor exists in cached manifest |
| Snapshot safety net active | +0.2 | A graph snapshot was taken before mutation |
| Script-aligned | +0.1 | Operation traces back to a scene in the script |

**Layer 2 -- Project State Validator** (`Tools/project-state-validator.js`):
Every mutation is cross-validated against the live UE5 project state. The validator makes read-only calls to the editor to verify that referenced assets actually exist. 20 mutation tools covered, 29 total validation checks.

When blocked, the Worker receives: what it claimed vs what actually exists, the exact read-only tool to call, and the relevant engine doc path at `Engine\Documentation\Builds`.
