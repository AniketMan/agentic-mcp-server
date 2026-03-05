# Manus Agent Integration Guide: UE 5.6 Level Editor

**Audience:** This document is written for the Manus agent connected to the Perforce server and UE5.6 editor. It describes how to use the UE 5.6 Level Editor toolchain to safely inspect and modify project assets.

---

## RULE ZERO: NEVER PUSH, SUBMIT, OR WRITE WITHOUT USER APPROVAL

**Do not push anything to Perforce, do not submit any changelist, do not write any file to the project, do not execute any script inside the editor, and do not modify any asset — without explicitly asking the user for permission first and receiving a clear "yes."**

This is not optional. This is not a suggestion. Every single destructive or write operation requires user confirmation before execution. The workflow is:

1. You inspect.
2. You generate a script.
3. You **show the user what the script does and ask permission.**
4. Only after the user says yes, you execute.
5. You **show the user the results and ask permission** before submitting to Perforce.

If you skip steps 3 or 5, you are breaking the workflow. The user's project is a production VR game. Unsanctioned writes to assets can corrupt Blueprints, break Level Sequences, and destroy weeks of work.

---

## Architecture: Who Does What

There are two agents and one human in this pipeline. Understand your role.

| Role | Responsibility | Has Access To |
| :--- | :--- | :--- |
| **This tool (Level Editor API)** | Inspects `.uasset`/`.umap` files. Generates Python scripts. Validates plugins. Read-only. | Asset files (read-only), script generation API |
| **You (Perforce Manus agent)** | Pulls files from Perforce. Runs generated scripts inside UE editor. Submits changes back to Perforce **after user approval.** | Perforce depot, UE5.6 editor, project filesystem |
| **The user (Boss)** | Approves all writes. Reviews all generated scripts before execution. Reviews all results before P4 submit. | Everything. Final authority on all decisions. |

**You do not decide what gets written. You do not decide what gets submitted. You propose, the user approves.**

---

## Workflow: Step by Step

### Step 1: Pull Latest from Perforce

Sync the project workspace to head revision before doing anything.

```bash
p4 sync //depot/SOH_VR/...
```

### Step 2: Load Assets into the Level Editor API

The Level Editor API server runs in a sandbox. Send it the asset files you need to inspect.

**Load a single level:**
```bash
curl -X POST http://<level-editor-host>:8080/api/load \
  -H "Content-Type: application/json" \
  -d '{"filepath": "/path/to/workspace/SOH_VR/Content/Maps/SL_Trailer_Logic.umap"}'
```

**Load multiple levels:**
```bash
curl -X POST http://<level-editor-host>:8080/api/load-multi \
  -H "Content-Type: application/json" \
  -d '{"filepaths": [
    "/path/to/workspace/SOH_VR/Content/Maps/SL_Trailer_Logic.umap",
    "/path/to/workspace/SOH_VR/Content/Maps/SL_Restaurant_Logic.umap",
    "/path/to/workspace/SOH_VR/Content/Maps/SL_Scene6_Logic.umap",
    "/path/to/workspace/SOH_VR/Content/Maps/SL_Hospital_Logic.umap"
  ]}'
```

**Scan the full project:**
```bash
curl -X POST http://<level-editor-host>:8080/api/project/scan \
  -H "Content-Type: application/json" \
  -d '{"project_root": "/path/to/workspace/SOH_VR"}'
```

### Step 3: Inspect (Read-Only — No Approval Needed)

These are all GET requests. They read data. They change nothing.

| Endpoint | What It Returns |
| :--- | :--- |
| `GET /api/actors` | All actors in the loaded level — class, name, components, properties |
| `GET /api/actor/<name>` | Detail for a single actor including full property list |
| `GET /api/functions` | All Blueprint functions with bytecode status |
| `GET /api/graph/<export_index>` | Blueprint graph with K2 nodes and connections |
| `GET /api/levels` | All loaded levels with actors, K2 nodes, functions |
| `GET /api/validate` | Integrity validation results |
| `GET /api/script/operations` | Full list of all 45+ script generation operations with parameters |
| `GET /api/project/info` | Full project scan results |

Use these freely to understand the project state before proposing any edits.

### Step 4: Generate a Script (Still Read-Only — No Approval Needed)

When you know what edit is needed, generate the script via the API. This does NOT execute anything. It returns a Python script as text.

```bash
curl -X POST http://<level-editor-host>:8080/api/script/generate \
  -H "Content-Type: application/json" \
  -d '{
    "domain": "actors",
    "method": "spawn",
    "params": {
      "class_name": "StaticMeshActor",
      "label": "NewActor_01",
      "location": [150.0, 0.0, 100.0]
    }
  }'
```

The response contains a `"script"` field with the full Python code. Save this to a file.

### Step 5: STOP — Show the Script to the User and Ask for Approval

**Before executing anything, present the generated script to the user.** Explain:
- What the script will do in plain language
- Which assets it will modify
- Whether it is reversible (Ctrl+Z in editor)

Wait for the user to say yes. If the user says no or asks for changes, go back to Step 4.

### Step 6: Execute the Script in UE Editor (Only After Approval)

Run the approved script inside the Unreal Editor. The editor must be open with the target project loaded.

**Option A — Python console (interactive):**
In the UE editor, go to `Window > Developer Tools > Output Log`, switch the command bar from "Cmd" to "Python", and paste the script.

**Option B — Command line (headless):**
```bash
UE5Editor "/path/to/SOH_VR/SOH_VR.uproject" -ExecutePythonScript="/path/to/approved_script.py"
```

**Monitor the Output Log.** All JARVIS operations log with the `[JARVIS]` prefix. If the plugin is installed, Blueprint graph operations log under `LogJarvis`.

**If anything goes wrong:** `Ctrl+Z` in the editor. Every operation is wrapped in an undo transaction.

### Step 7: STOP — Show the Results to the User and Ask Before Submitting

After the script runs, report the results to the user:
- What changed
- Any errors or warnings from the Output Log
- Confirmation that the editor shows the expected state

**Do not submit to Perforce until the user explicitly approves.**

### Step 8: Submit to Perforce (Only After Approval)

```bash
# Check out the modified files
p4 edit //depot/SOH_VR/Content/Maps/SL_Trailer_Logic.umap

# Submit with a descriptive changelist
p4 submit -d "[JARVIS] <description of what was changed and why>"
```

---

## The JarvisEditor C++ Plugin: When You Need It

Most edits (spawning actors, Level Sequences, materials, properties) work with the standard `unreal` Python module. No plugin needed.

**The plugin is ONLY required for Blueprint graph editing:**
- Adding nodes to an EventGraph (Branch, PrintString, Custom Event, etc.)
- Connecting or disconnecting pins between nodes
- Compiling a Blueprint from script
- Inspecting graph topology from Python

If the user's task involves any of those operations, the plugin must be installed first. See `PLUGIN_EXPLAINER.md` for the full explanation of why it exists.

**Plugin installation (one-time, requires user approval):**

1. Copy `ue56-level-editor/ue_plugin/JarvisEditor/` to `SOH_VR/Plugins/JarvisEditor/`
2. Regenerate project files
3. Build Development Editor
4. Verify: in UE Python console, `print(hasattr(unreal, 'JarvisBlueprintLibrary'))` should print `True`

**Ask the user before installing the plugin.** It modifies the project's plugin list and requires a rebuild.

---

## Quick Reference: Script Generation Domains

Query `GET /api/script/operations` for the full list with all parameters. Summary:

| Domain | Operations | Plugin Required? |
| :--- | :--- | :--- |
| `actors` | spawn, spawn_blueprint, delete, set_property, move, duplicate, list_all, batch_set_property | No |
| `assets` | load, duplicate, delete, rename, save_all_dirty, list_assets | No |
| `sequences` | create, add_track, add_keyframe, set_playback_range, bind_actor, list_bindings | No |
| `materials` | set_parameter, create_instance, assign_to_actor | No |
| `blueprints` | compile, add_variable, set_variable_default, create, reparent | No (but `compile` is more reliable through the plugin) |
| `animation` | retarget, import_fbx, set_anim_on_skeletal_mesh | No |
| `data_tables` | list_rows, get_row | No |
| `levels` | load_level, save_current_level, add_streaming_level, list_streaming_levels, set_level_visibility | No |
| `pcg` | execute_graph, set_pcg_parameter | No |
| `utility` | run_commandlet, build_lighting, take_screenshot, fix_redirectors, custom | No |

**Blueprint graph node/pin manipulation** (not listed above) requires the JarvisEditor plugin. Those operations are called directly via `unreal.JarvisBlueprintLibrary.*` in the generated scripts.
