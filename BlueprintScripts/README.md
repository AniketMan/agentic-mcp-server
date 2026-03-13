# Blueprint Scripts -- Ordinary Courage VR

Pasteable Python scripts that build complete Blueprint graphs for every scene via the Agentic MCP C++ plugin HTTP API.

## Prerequisites

1. UE5 editor is open with the SOH project loaded
2. The Agentic MCP C++ plugin is running (listening on `localhost:9847`)
3. The UE5 Python console is accessible (Window > Developer Tools > Output Log, then switch to Python)

## Directory Structure

```
BlueprintScripts/
  bp_helpers.py              -- Shared helper module (paste first or import)
  cinematic/
    scene_00_cinematic.py    -- Auto-play Tutorial (no interaction)
    scene_01_cinematic.py    -- Auto-play Scene 01
    ...
    scene_09_cinematic.py    -- Auto-play Scene 09
  interactive/
    scene_00_interactive.py  -- Full interactive Tutorial with all logic
    scene_01_interactive.py  -- Full interactive Scene 01
    ...
    scene_09_interactive.py  -- Full interactive Scene 09
```

## How to Use

### Option A: Paste into UE5 Python Console
1. Open the Output Log in UE5 (Window > Developer Tools > Output Log)
2. Switch to the Python tab
3. Paste the contents of `bp_helpers.py` first
4. Then paste the desired scene script

### Option B: Execute via File
1. Copy the `BlueprintScripts` folder into your project's `Scripts` directory
2. In UE5 Python console: `exec(open("C:/path/to/scene_00_cinematic.py").read())`

### Option C: Via MCP executePython Tool
Claude can run these through the `executePython` MCP tool with `file` parameter.

## What Each Script Does

### Cinematic Scripts
- Places the player at the correct position for the scene
- Plays all Level Sequences back-to-back with timed delays
- No interaction required -- plays like a movie
- Useful for review, QA, and client demos

### Interactive Scripts
- Spawns all required actors with correct transforms
- Creates all [makeTempBP] Blueprints with components, variables, and logic
- Wires the Level Blueprint Event Graph with full interaction chains
- Binds Level Sequences to actors
- Hooks up audio (ambient, VO, SFX)
- Configures all interaction components (Grabbable, Observable, Activatable, etc.)
- Sets story step values for progression

## Error Handling
- Every API call is wrapped with try/catch
- Errors are logged to `bp_helpers._error_log`
- Call `bp_helpers.print_summary()` at the end to see all errors
- Scripts will continue past individual failures (no crash on single node failure)
- Each script prints progress as it runs

## Verified Against
- `OC_VR_Implementation_Roadmap.md` -- every actor, sequence, interaction
- `ContentBrowser_Hierarchy.txt` -- every asset path
- `tool-registry.json` -- every API endpoint name and parameter
- `script_v2_ocvr.md` -- every VO line and story beat
