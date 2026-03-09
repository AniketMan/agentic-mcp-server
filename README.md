# UE 5.6 Level Editor — AI-Controlled Project Editor

> An autonomous toolchain for inspecting, validating, and editing Unreal Engine 5.6 projects. Designed to be operated by an AI agent (JARVIS) with a read-only web dashboard for human visualization.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Quick Start](#quick-start)
3. [VisualAgent CLI](#visualagent-cli)
4. [Component Reference](#component-reference)
5. [API Reference](#api-reference)
6. [Script Generator Reference](#script-generator-reference)
7. [JarvisEditor C++ Plugin](#jarviseditor-c-plugin)
8. [Project Scanner Reference](#project-scanner-reference)
9. [Plugin Validator Reference](#plugin-validator-reference)
10. [Dashboard Views](#dashboard-views)
11. [K2 Node Name Resolution](#k2-node-name-resolution)
12. [UE 5.6 Compatibility](#ue-56-compatibility)
13. [Troubleshooting](#troubleshooting)

---

## Architecture Overview

The system has three layers:

```
+-------------------------------------------------------------------+
|  LAYER 1: BINARY INSPECTION (runs in sandbox / any Linux/macOS)   |
|  Reads .uasset/.umap files via UAssetAPI (.NET) through Python    |
|  bridge. Parses actors, K2 nodes, Kismet bytecode, imports,       |
|  exports, name maps. Read-only — never writes to asset files.     |
+-------------------------------------------------------------------+
|  LAYER 2: SCRIPT GENERATION (runs in sandbox)                     |
|  Generates runnable Python scripts that use UE5.6's built-in      |
|  `unreal` module. Scripts execute INSIDE the UE editor via        |
|  Python console or -ExecutePythonScript command line.              |
+-------------------------------------------------------------------+
|  LAYER 3: C++ PLUGIN (compiles inside UE5.6 project)              |
|  JarvisEditor plugin exposes Blueprint graph manipulation          |
|  functions (node spawning, pin connections, compilation) to        |
|  Python via UFUNCTION(BlueprintCallable). Required for operations  |
|  that UE's Python module does not natively expose.                 |
+-------------------------------------------------------------------+
```

**Why this architecture?**

Writing directly to `.uasset`/`.umap` binary files outside the engine is dangerous. UAssetAPI round-trips are not byte-perfect, Kismet bytecode `Extras` blobs contain serialized pin references that go stale when nodes change, and the engine's internal validation is the only reliable way to ensure file integrity. By generating scripts that run inside the editor, the engine handles all serialization, reference tracking, and validation natively. Zero risk of file corruption, memory leaks, or broken references.

**Data flow:**

```
  You describe the edit
        |
        v
  JARVIS inspects the asset (Layer 1)
        |
        v
  JARVIS generates a Python script (Layer 2)
        |
        v
  Script runs inside UE5.6 editor (Layer 2 + Layer 3)
        |
        v
  Engine handles serialization, validation, undo history
```

---

## Quick Start

### Prerequisites

- Linux (Ubuntu 22.04+ recommended) or macOS
- Python 3.11+
- Internet access (for .NET SDK download during setup)

### Installation

```bash
# Clone the repo
gh repo clone AniketMan/ue56-level-editor
cd ue56-level-editor

# Run the bootstrap script (installs .NET SDK, builds UAssetAPI, installs Python deps)
chmod +x setup.sh
./setup.sh

# Start the dashboard server
python3 run_dashboard.py
# Dashboard available at http://localhost:8080
```

### What setup.sh Does

The bootstrap script performs these steps in order:

1. **Checks for Python 3.11+** — exits with error if not found
2. **Installs .NET SDK 8.0** — downloads to `~/.dotnet` via Microsoft's install script
3. **Clones UAssetGUI** — `https://github.com/atenfyr/UAssetGUI` with `--recurse-submodules` to get UAssetAPI
4. **Publishes UAssetAPI** — `dotnet publish -c Release` to `lib/publish/` with all dependencies (UAssetAPI.dll, Newtonsoft.Json.dll, ZstdSharp.dll)
5. **Creates runtimeconfig.json** — required for pythonnet to initialize .NET CoreCLR
6. **Installs Python dependencies** — `flask`, `pythonnet` via pip
7. **Verifies the bridge** — imports pythonnet, loads UAssetAPI.dll, confirms Kismet bytecode types are accessible

If any step fails, the script exits with a descriptive error message.

### Loading Assets

Once the server is running on `http://localhost:8080`:

```bash
# Load a single .umap file (sets it as the primary asset for all views)
curl -X POST http://localhost:8080/api/load \
  -H "Content-Type: application/json" \
  -d '{"path": "/absolute/path/to/YourLevel.umap"}'

# Load multiple .umap files (for the Levels content browser view)
curl -X POST http://localhost:8080/api/load-multiple \
  -H "Content-Type: application/json" \
  -d '{"paths": ["/path/to/Level1.umap", "/path/to/Level2.umap"]}'
```

**Important:** All paths must be absolute. Relative paths will fail.

---

## VisualAgent CLI

VisualAgent is a unified CLI tool for visual automation of Unreal Engine 5, similar to Playwright for web browsers. It provides scene hierarchy snapshots with short refs (like accessibility trees), on-demand viewport screenshots, and a rich command vocabulary for AI agents.

### Key Features

- **Scene Snapshots**: Hierarchical actor tree with short refs (`a0`, `a1.c0`) like accessibility trees
- **On-Demand Screenshots**: GPU-friendly design - screenshots only when explicitly requested
- **Auto-Screenshot Mode**: Toggle automatic screenshots after visual actions (spawn, move, rotate, delete, camera, focus)
- **Recording System**: Record and replay automation sessions
- **Short Refs**: Use `a0`, `a1.c2` instead of full actor names

### Commands

| Command | Description | Example |
|---------|-------------|---------|
| `snapshot` | Get scene hierarchy with short refs | `snapshot --classFilter=Light` |
| `screenshot` | Capture viewport screenshot | `screenshot --format=png --width=1920` |
| `auto-screenshot` | Toggle auto-screenshot mode | `auto-screenshot on` |
| `focus` | Move camera to actor | `focus a0` |
| `select` | Select actor in editor | `select a1 --add` |
| `spawn` | Spawn new actor | `spawn PointLight 100 200 300 --label="MyLight"` |
| `move` | Move actor to position | `move a0 500 0 100` |
| `rotate` | Rotate actor | `rotate a0 0 45 0` |
| `delete` | Delete actor | `delete a3` |
| `camera` | Set viewport camera | `camera 0 0 500 -45 0 0` |
| `query` | Query actors by class | `query StaticMeshActor` |
| `wait` | Wait for condition | `wait assets` |
| `ref` | Resolve short ref to name | `ref a0` |
| `record` | Recording control | `record start`, `record stop`, `record play` |
| `help` | Show help | `help` |

### Screenshot Behavior

Screenshots are **off by default** to be GPU-friendly:

1. **Explicit**: Use `screenshot` command to capture viewport
2. **Per-Command**: Add `--screenshot` flag to any command
3. **Auto Mode**: Enable with `auto-screenshot on` - captures after visual actions (spawn, move, rotate, delete, camera, focus, navigate)

### C++ Backend Endpoints

VisualAgent is powered by these C++ handlers in `Handlers_VisualAgent.cpp`:

| Endpoint | Purpose |
|----------|---------|
| `/api/scene-snapshot` | Get hierarchical scene tree with short refs |
| `/api/screenshot` | Capture viewport using UE's built-in screenshot tools |
| `/api/focus-actor` | Move editor camera to focus on actor |
| `/api/select-actor` | Select actor(s) in editor |
| `/api/set-viewport` | Set camera position and rotation |
| `/api/wait-ready` | Wait for assets/compile/render to complete |
| `/api/resolve-ref` | Resolve short ref to actor/component name |

---

## Component Reference

### File Inventory

| File | Lines | Purpose |
|------|-------|---------|
| `core/uasset_bridge.py` | ~350 | Python-to-.NET bridge wrapping UAssetAPI. Loads `.uasset`/`.umap` files, exposes exports, imports, names, actors, properties, components. |
| `core/blueprint_editor.py` | ~550 | K2 node resolution (matches UE editor `GetNodeTitle()` logic), Kismet bytecode parsing, graph visualization data. |
| `core/level_logic.py` | ~300 | Level actor enumeration, property reading, component tree building, actor class resolution. |
| `core/integrity.py` | ~380 | Reference integrity validation — checks import/export indices, name map consistency, orphan detection, circular reference detection. |
| `core/plugin_validator.py` | ~710 | `.uplugin` descriptor validation with 6-phase checks, scoring, and actionable suggestions. |
| `core/project_scanner.py` | ~815 | Full project scanning — `.uproject` parsing, asset registry, level scanning, dependency resolution, source code analysis, config extraction. |
| `core/script_generator.py` | ~1410 | Generates runnable `unreal` Python scripts across 10 domains and 45+ operations. |
| `core/security.py` | ~250 | Input validation, path traversal prevention, XSS sanitization for server endpoints. |
| `core/cache.py` | ~200 | LRU caching with TTL support for asset parsing results. |
| `core/perforce.py` | ~300 | Perforce integration for source control operations. |
| `core/cli.py` | ~150 | Command-line interface for direct operation. |
| `ui/server.py` | ~600 | Flask REST API server with 25+ endpoints. |
| `ui/static/index.html` | ~2200 | Single-page dashboard application with 10 views. |
| `AgenticMCP/Tools/visual-agent.js` | ~590 | VisualAgent unified CLI tool for visual automation. |
| `AgenticMCP/Tools/index.js` | ~300 | MCP server with tool registration and routing. |
| `AgenticMCP/Source/.../Handlers_VisualAgent.cpp` | ~770 | C++ handlers for VisualAgent scene snapshot, screenshot, camera control. |
| `ue_plugin/JarvisEditor/` | ~900 | C++ editor plugin for Blueprint graph manipulation (12 functions). |
| `setup.sh` | ~80 | Bootstrap script for automated environment setup. |
| `run_dashboard.py` | ~30 | Server launcher script. |

### Module Dependencies

```
core/uasset_bridge.py
  └── pythonnet (clr_loader + clr)
      └── UAssetAPI.dll (.NET 8.0)

core/blueprint_editor.py
  └── core/uasset_bridge.py (AssetFile)

core/level_logic.py
  └── core/uasset_bridge.py (AssetFile)

core/integrity.py
  └── core/uasset_bridge.py (AssetFile)

core/plugin_validator.py
  └── (standalone — reads JSON and filesystem only)

core/project_scanner.py
  └── core/uasset_bridge.py (optional — for deep asset inspection)
  └── core/plugin_validator.py (for plugin validation)

core/script_generator.py
  └── (standalone — generates Python text files)

ui/server.py
  └── core/uasset_bridge.py
  └── core/blueprint_editor.py
  └── core/level_logic.py
  └── core/integrity.py
  └── core/plugin_validator.py
  └── core/project_scanner.py
  └── core/script_generator.py
```

---

## API Reference

All endpoints are served at `http://localhost:8080`. All POST endpoints accept `Content-Type: application/json` bodies. All responses are JSON.

### Asset Loading

| Method | Endpoint | Body | Response | Description |
|--------|----------|------|----------|-------------|
| POST | `/api/load` | `{"path": "/abs/path.umap"}` | `{"status": "ok", "file": "name.umap", "exports": 67, "imports": 86}` | Load a single asset as the primary file. Populates Overview, Actors, Exports, Imports, Functions, Validation views. |
| POST | `/api/load-multiple` | `{"paths": ["/path1.umap", "/path2.umap"]}` | `{"status": "ok", "loaded": 4, "levels": ["name1", "name2"]}` | Load multiple assets for the Levels content browser view. |

### Inspection Endpoints

| Method | Endpoint | Response Fields | Description |
|--------|----------|-----------------|-------------|
| GET | `/api/status` | `loaded`, `file`, `level_count` | Server status, whether a file is loaded, how many levels are loaded. |
| GET | `/api/overview` | `export_count`, `import_count`, `name_count`, `actor_count`, `class_count`, `function_count`, `engine_version`, `asset_type`, `has_level_export` | Asset overview — counts and metadata. |
| GET | `/api/actors` | Array of `{class_name, object_name, is_blueprint, component_count, property_count, properties, components}` | Full actor list with nested property and component data. |
| GET | `/api/exports` | Array of `{index, type, class_name, object_name, serial_size}` | Complete export table. |
| GET | `/api/imports` | Array of `{index, class_name, class_package, object_name}` | Complete import table. |
| GET | `/api/functions` | Array of `{name, has_bytecode, bytecode_size, script_flags}` | Blueprint function list with bytecode status. |
| GET | `/api/validation` | `{passed, error_count, warning_count, items: [{severity, category, message, location, suggestion}]}` | Integrity check results with actionable suggestions. |
| GET | `/api/levels` | Array of `{name, export_count, import_count, actor_count, function_count, compiled_count, has_logic, actors, functions, k2_nodes}` | All loaded levels with full data including K2 nodes with editor-matching names. |
| GET | `/api/level/<name>/graph` | `{name, k2_nodes, functions: [{name, bytecode_size, nodes}]}` | Detailed graph data for a specific level. |

### Script Generation Endpoints

| Method | Endpoint | Body | Response | Description |
|--------|----------|------|----------|-------------|
| GET | `/api/script/operations` | — | `{domains: [{name, methods: [{name, params: [{name, type, required, default}]}]}]}` | List all 45 operations with full parameter specs. |
| POST | `/api/script/generate` | `{"domain": "actors", "method": "spawn", "params": {"class_name": "StaticMeshActor"}}` | `{"status": "ok", "script": "import unreal\n..."}` | Generate a runnable UE Python script. Returns the script text. |
| POST | `/api/script/save` | `{"domain": "actors", "method": "spawn", "params": {...}, "output_path": "/path/to/script.py"}` | `{"status": "ok", "path": "/path/to/script.py"}` | Generate and save script to a file. |

### Plugin Validation Endpoints

| Method | Endpoint | Body | Response | Description |
|--------|----------|------|----------|-------------|
| POST | `/api/plugin/validate` | `{"path": "/abs/path/to/PluginDir"}` | `{plugin_name, passed, error_count, warning_count, items: [...]}` | Validate a `.uplugin` and its directory structure. |
| GET | `/api/plugins` | — | Array of plugin reports | List all validated plugins and their status. |

### Project Scanning Endpoints

| Method | Endpoint | Body | Response | Description |
|--------|----------|------|----------|-------------|
| POST | `/api/project/scan` | `{"project_path": "/abs/path/to/ProjectRoot"}` | Full project report (see Project Scanner Reference) | Full project scan. |
| GET | `/api/project` | — | Last scan result or `{"status": "no_project"}` | Return the last scan result. |

---

## Script Generator Reference

The script generator produces complete, runnable Python scripts that execute inside UE5.6's editor. Every generated script includes:

- Standard header with `[JARVIS]` prefix, timestamp, domain, and operation name
- `import unreal` and logging setup
- Full error handling with try/except
- Print statements for every operation (visible in UE's Output Log)
- Comments explaining each step
- Validation of inputs before execution

### How to Run Generated Scripts

**Option A — UE Python Console (interactive):**
1. Open UE Editor
2. Go to `Window > Developer Tools > Output Log`
3. Change the command bar dropdown from "Cmd" to "Python"
4. Paste the generated script and press Enter

**Option B — Command Line (headless):**
```bash
UE5Editor.exe "YourProject.uproject" -ExecutePythonScript="/path/to/generated_script.py"
```

**Option C — Startup Script (auto-run on editor open):**
Place the script in `YourProject/Content/Python/init_unreal.py`

**Option D — Editor Utility Widget:**
Create an Editor Utility Widget with a "Run Script" button that calls `unreal.PythonScriptLibrary.execute_python_command(script_text)`

### Domain: actors (8 operations)

Generates scripts for spawning, modifying, and managing actors in the current level.

| Method | Required Parameters | Optional Parameters | Description |
|--------|-------------------|---------------------|-------------|
| `spawn` | `class_name` (str) | `label` (str), `location` (tuple of 3 floats), `rotation` (tuple of 3 floats), `scale` (tuple of 3 floats) | Spawn an actor of the given class at the specified transform. Uses `unreal.EditorLevelLibrary.spawn_actor_from_class()`. |
| `spawn_blueprint` | `blueprint_path` (str) | `label` (str), `location` (tuple), `rotation` (tuple) | Spawn a Blueprint actor. Loads the Blueprint asset, gets the generated class, spawns it. |
| `delete` | `label` (str) | — | Delete an actor by its editor label. Uses `unreal.EditorLevelLibrary.destroy_actor()`. |
| `set_property` | `label` (str), `property_name` (str), `value` (any) | — | Set a property value on an actor. Uses reflection to find the property and set it. |
| `move` | `label` (str) | `location` (tuple), `rotation` (tuple), `scale` (tuple) | Set an actor's world transform. At least one of location/rotation/scale must be provided. |
| `duplicate` | `label` (str) | `offset` (tuple of 3 floats, default (100,0,0)) | Duplicate an actor with an offset. |
| `list_all` | — | `class_filter` (str) | List all actors in the current level. Prints name, class, location for each. |
| `batch_set_property` | `labels` (list of str), `property_name` (str), `value` (any) | — | Set the same property on multiple actors at once. |

**Example — spawn a Blueprint actor:**
```bash
curl -X POST http://localhost:8080/api/script/generate \
  -H "Content-Type: application/json" \
  -d '{
    "domain": "actors",
    "method": "spawn_blueprint",
    "params": {
      "blueprint_path": "/Game/Blueprints/BP_TeleportPoint",
      "label": "TeleportPoint_New",
      "location": [100, 200, 0]
    }
  }'
```

### Domain: assets (6 operations)

| Method | Required Parameters | Optional Parameters | Description |
|--------|-------------------|---------------------|-------------|
| `load` | `asset_path` (str) | — | Load an asset into memory. Returns the loaded object. |
| `duplicate` | `source_path` (str), `dest_path` (str), `dest_name` (str) | — | Duplicate an asset to a new location. |
| `delete` | `asset_path` (str) | — | Delete an asset. Checks for references first. |
| `rename` | `asset_path` (str), `new_name` (str) | — | Rename an asset (creates a redirector). |
| `save_all_dirty` | — | — | Save all modified assets. |
| `list_assets` | `directory` (str) | `class_filter` (str), `recursive` (bool, default true) | List assets in a content directory. |

### Domain: sequences (6 operations)

| Method | Required Parameters | Optional Parameters | Description |
|--------|-------------------|---------------------|-------------|
| `create` | `sequence_name` (str), `save_path` (str) | — | Create a new empty Level Sequence asset. |
| `add_track` | `sequence_path` (str), `actor_label` (str), `track_type` (str) | — | Add a track to a sequence. `track_type`: "Transform", "Visibility", "SkeletalAnimation", "Audio", "Event", "Float", "Bool". |
| `add_keyframe` | `sequence_path` (str), `actor_label` (str), `track_type` (str), `frame` (int), `value` (any) | — | Add a keyframe to a track at a specific frame. |
| `set_playback_range` | `sequence_path` (str), `start_frame` (int), `end_frame` (int) | — | Set the playback range of a sequence. |
| `bind_actor` | `sequence_path` (str), `actor_label` (str) | — | Bind an actor to a Level Sequence (creates a possessable). |
| `list_bindings` | `sequence_path` (str) | — | List all actor bindings in a sequence with their tracks. |

### Domain: materials (3 operations)

| Method | Required Parameters | Optional Parameters | Description |
|--------|-------------------|---------------------|-------------|
| `set_parameter` | `material_path` (str), `param_name` (str), `value` (float or tuple) | — | Set a scalar or vector parameter on a Material Instance. Detects type from value (float = scalar, tuple = vector). |
| `create_instance` | `parent_path` (str), `instance_name` (str), `save_path` (str) | — | Create a Material Instance from a parent material. |
| `assign_to_actor` | `actor_label` (str), `material_path` (str) | `slot_index` (int, default 0) | Assign a material to an actor's mesh component at the given slot. |

### Domain: blueprints (5 operations)

| Method | Required Parameters | Optional Parameters | Description |
|--------|-------------------|---------------------|-------------|
| `compile` | `blueprint_path` (str) | — | Compile a Blueprint and report success/failure. |
| `add_variable` | `blueprint_path` (str), `var_name` (str), `var_type` (str) | `default_value` (any) | Add a variable. `var_type`: "Bool", "Int", "Float", "String", "Vector", "Rotator", "Transform", "Object", "Class". |
| `set_variable_default` | `blueprint_path` (str), `var_name` (str), `value` (any) | — | Set the default value of an existing variable. |
| `create` | `blueprint_name` (str), `save_path` (str) | `parent_class` (str, default "Actor") | Create a new Blueprint class. |
| `reparent` | `blueprint_path` (str), `new_parent_class` (str) | — | Change a Blueprint's parent class. |

**Note:** For graph manipulation (adding nodes, connecting pins), use the JarvisEditor C++ plugin functions. See [JarvisEditor C++ Plugin](#jarviseditor-c-plugin).

### Domain: animation (3 operations)

| Method | Required Parameters | Optional Parameters | Description |
|--------|-------------------|---------------------|-------------|
| `retarget` | `source_anim_path` (str), `target_skeleton_path` (str), `output_path` (str) | — | Retarget an animation to a different skeleton. |
| `import_fbx` | `fbx_path` (str), `skeleton_path` (str), `dest_path` (str), `dest_name` (str) | — | Import an FBX animation file. |
| `set_anim_on_skeletal_mesh` | `actor_label` (str), `anim_path` (str) | — | Set an animation asset on a Skeletal Mesh actor's AnimInstance. |

### Domain: data_tables (2 operations)

| Method | Required Parameters | Optional Parameters | Description |
|--------|-------------------|---------------------|-------------|
| `list_rows` | `table_path` (str) | — | List all row names in a Data Table. |
| `get_row` | `table_path` (str), `row_name` (str) | — | Get a specific row's data as a dict. |

### Domain: levels (5 operations)

| Method | Required Parameters | Optional Parameters | Description |
|--------|-------------------|---------------------|-------------|
| `load_level` | `level_path` (str) | — | Load a level in the editor. |
| `save_current_level` | — | — | Save the currently open level. |
| `add_streaming_level` | `sub_level_path` (str) | `streaming_method` (str: "Blueprint" or "AlwaysLoaded", default "Blueprint") | Add a streaming sub-level to the current persistent level. |
| `list_streaming_levels` | — | — | List all streaming levels in the current world. |
| `set_level_visibility` | `level_name` (str), `visible` (bool) | — | Set a streaming level's visibility. |

### Domain: pcg (2 operations)

| Method | Required Parameters | Optional Parameters | Description |
|--------|-------------------|---------------------|-------------|
| `execute_graph` | `actor_label` (str) | — | Execute a PCG graph on a PCG Volume actor. |
| `set_pcg_parameter` | `actor_label` (str), `param_name` (str), `value` (any) | — | Set an override parameter on a PCG component. |

### Domain: utility (5 operations)

| Method | Required Parameters | Optional Parameters | Description |
|--------|-------------------|---------------------|-------------|
| `run_commandlet` | `commandlet_name` (str) | `args` (str) | Run an editor commandlet. |
| `build_lighting` | — | `quality` (str: "Preview", "Medium", "High", "Production", default "Preview") | Build lighting for the current level. |
| `take_screenshot` | `filename` (str) | `resolution_x` (int, default 1920), `resolution_y` (int, default 1080) | Take a viewport screenshot. |
| `fix_redirectors` | — | `directory` (str, default "/Game") | Fix up all redirectors in a directory. |
| `custom` | `title` (str), `code` (str) | — | Generate a custom script with standard `[JARVIS]` header and error handling wrapping your code. |

---

## JarvisEditor C++ Plugin

### What It Is

A UE5.6 Editor plugin that exposes Blueprint graph manipulation functions to Python. These are operations that UE's built-in Python module does **not** natively support:

- Spawning K2 nodes (UK2Node_CallFunction, UK2Node_CustomEvent, etc.)
- Connecting/disconnecting pins (UEdGraphPin::MakeLinkTo)
- Triggering Blueprint compilation (FKismetEditorUtilities::CompileBlueprint)
- Scoped undo transactions (FScopedTransaction)
- Graph state inspection with full topology

### Installation — REQUIRED BEFORE USE

The JarvisEditor plugin **must be compiled into your UE5.6 project** before any Blueprint graph manipulation scripts will work. It is NOT pre-compiled — it ships as C++ source that the engine compiles alongside your project.

**Step-by-step installation:**

```bash
# 1. Copy the plugin to your project's Plugins directory
cp -r ue56-level-editor/ue_plugin/JarvisEditor /path/to/YourProject/Plugins/

# 2. Your project structure should now look like:
# YourProject/
#   Plugins/
#     JarvisEditor/
#       JarvisEditor.uplugin
#       Source/
#         JarvisEditor/
#           JarvisEditor.Build.cs
#           Public/
#             JarvisEditorModule.h
#             JarvisBlueprintLibrary.h
#           Private/
#             JarvisEditorModule.cpp
#             JarvisBlueprintLibrary.cpp

# 3. Regenerate project files
#    Windows: Right-click YourProject.uproject > "Generate Visual Studio project files"
#    Linux:   ./GenerateProjectFiles.sh
#    Mac:     ./GenerateProjectFiles.command

# 4. Build the project
#    Visual Studio/Rider: Open .sln, build Development Editor
#    Command line: UnrealBuildTool YourProject Win64 Development -Project="YourProject.uproject"

# 5. Open the editor — the plugin auto-loads (Type: Editor, LoadingPhase: Default)
```

**Verification — confirm the plugin loaded:**

Check the Output Log for:
```
LogJarvis: JarvisEditor module loaded — Blueprint graph manipulation ready
```

Verify from the Python console:
```python
import unreal
print(hasattr(unreal, 'JarvisBlueprintLibrary'))  # Must print True
print(dir(unreal.JarvisBlueprintLibrary))          # Lists all 12 functions
```

### Function Reference

All functions are static and callable from Python as:
```python
unreal.JarvisBlueprintLibrary.function_name(args)
```

#### Node Operations

| Function | Parameters | Returns | Description |
|----------|-----------|---------|-------------|
| `add_node_to_graph` | `blueprint` (UBlueprint), `graph_name` (FString), `node_class` (FString), `node_pos_x` (int32), `node_pos_y` (int32) | UEdGraphNode* | Add a K2Node to a Blueprint graph. `node_class` is the C++ class name: `"K2Node_CallFunction"`, `"K2Node_IfThenElse"`, `"K2Node_CustomEvent"`, etc. Returns the created node. |
| `remove_node_from_graph` | `blueprint` (UBlueprint), `graph_name` (FString), `node_name` (FString) | bool | Remove a node by its name/title. Disconnects all pins first to prevent dangling references. Returns true if found and removed. |
| `add_custom_event` | `blueprint` (UBlueprint), `graph_name` (FString), `event_name` (FString), `node_pos_x` (int32), `node_pos_y` (int32) | UEdGraphNode* | Convenience: add a UK2Node_CustomEvent with the given event name. |
| `add_function_call` | `blueprint` (UBlueprint), `graph_name` (FString), `function_name` (FString), `node_pos_x` (int32), `node_pos_y` (int32) | UEdGraphNode* | Convenience: add a UK2Node_CallFunction. `function_name` is fully qualified: `"KismetSystemLibrary.PrintString"`, `"GameplayMessageSubsystem.K2_BroadcastMessage"`. |
| `set_node_position` | `blueprint` (UBlueprint), `graph_name` (FString), `node_name` (FString), `pos_x` (int32), `pos_y` (int32) | bool | Move a node to a new position in the graph. |

#### Pin Operations

| Function | Parameters | Returns | Description |
|----------|-----------|---------|-------------|
| `connect_pins` | `blueprint` (UBlueprint), `graph_name` (FString), `source_node_name` (FString), `source_pin_name` (FString), `target_node_name` (FString), `target_pin_name` (FString) | bool | Connect an output pin to an input pin. Handles both execution pins (`"execute"`, `"then"`) and data pins. Uses `MakeLinkTo()` which manages bidirectional references correctly. |
| `disconnect_pin` | `blueprint` (UBlueprint), `graph_name` (FString), `node_name` (FString), `pin_name` (FString) | bool | Disconnect a specific pin from all its connections. |
| `disconnect_all_pins` | `blueprint` (UBlueprint), `graph_name` (FString), `node_name` (FString) | int32 | Disconnect ALL pins on a node. Returns count of broken connections. |

#### Compilation

| Function | Parameters | Returns | Description |
|----------|-----------|---------|-------------|
| `compile_blueprint` | `blueprint` (UBlueprint) | bool | Compile a Blueprint. Returns true if compilation succeeds with no errors. Logs all errors/warnings to `LogJarvis`. |

#### Inspection

| Function | Parameters | Returns | Description |
|----------|-----------|---------|-------------|
| `get_all_graph_nodes` | `blueprint` (UBlueprint), `graph_name` (FString) | FString (JSON) | Returns JSON array of all nodes: `[{"name": "...", "class": "...", "guid": "...", "pos_x": 0, "pos_y": 0, "pins": [...]}]` |
| `get_node_connections` | `blueprint` (UBlueprint), `graph_name` (FString), `node_name` (FString) | FString (JSON) | Returns JSON array of all connections for a node: `[{"pin": "then", "direction": "output", "connected_to": [{"node": "...", "pin": "execute"}]}]` |
| `validate_all_node_connections` | `blueprint` (UBlueprint), `graph_name` (FString) | FString (JSON) | Validates all connections in a graph. Returns JSON with any broken/invalid connections found. |

### Safety Guarantees

1. **Every graph modification is wrapped in `FScopedTransaction`** — all changes are undoable via Ctrl+Z in the editor
2. **Every function logs entry, exit, and errors** to `LogJarvis` category — full traceability in Output Log
3. **Pin connections use the engine's `MakeLinkTo`/`BreakLinkTo`** — bidirectional reference management is handled correctly (no stale LinkedTo references, which was the root cause of the original crash)
4. **Node removal disconnects all pins first** — no dangling pin references
5. **Compilation uses `FKismetEditorUtilities::CompileBlueprint`** — the same code path as clicking "Compile" in the editor toolbar

### Build Dependencies

The plugin's `.Build.cs` declares these module dependencies (all engine modules, no external deps):

```csharp
PublicDependencyModuleNames:  Core, CoreUObject, Engine, InputCore
PrivateDependencyModuleNames: UnrealEd, BlueprintGraph, KismetCompiler,
                               Kismet, GraphEditor, Slate, SlateCore,
                               EditorFramework, ToolMenus
```

### Usage Pattern — Complete Example

```python
import unreal

lib = unreal.JarvisBlueprintLibrary

# Get the Level Script Blueprint for the current level
bp = lib.get_level_script_blueprint()

# Add a custom event
event_node = lib.add_custom_event(bp, "EventGraph", "OnTeleportActivated", 0, 0)

# Add a PrintString call
print_node = lib.add_function_call(bp, "EventGraph",
    "KismetSystemLibrary.PrintString", 300, 0)

# Connect the event's "then" pin to PrintString's "execute" pin
lib.connect_pins(bp, "EventGraph",
    "OnTeleportActivated", "then",
    "PrintString", "execute")

# Compile
success = lib.compile_blueprint(bp)
if success:
    print("[JARVIS] Blueprint compiled successfully")
else:
    errors = lib.get_compile_errors(bp)
    print(f"[JARVIS] Compilation failed: {errors}")
```

---

## Project Scanner Reference

The project scanner provides full project analysis when given the project root directory (the directory containing the `.uproject` file).

### Usage via API

```bash
curl -X POST http://localhost:8080/api/project/scan \
  -H "Content-Type: application/json" \
  -d '{"project_path": "/absolute/path/to/YourProject"}'
```

### What It Scans

| Phase | Method | What It Reads | What It Returns |
|-------|--------|---------------|-----------------|
| 1 | `parse_uproject()` | The `.uproject` JSON file | `engine_version`, `modules` (name, host_type, loading_phase), `plugins` (name, enabled, optional, platform), `target_platforms` |
| 2 | `scan_plugins()` | All `.uplugin` files in `Plugins/` | Array of `{name, version, friendly_name, modules, dependencies, is_beta, is_experimental}` |
| 3 | `scan_assets()` | All `.uasset`/`.umap` files in `Content/` | `{total_count, total_size_mb, maps, blueprints, materials, textures, meshes, sounds, other}` with per-type counts |
| 4 | `scan_levels(bridge)` | All `.umap` files (deep inspection with UAssetAPI if bridge provided) | Array of `{name, path, size, actor_count, function_count, has_logic, is_persistent, referenced_blueprints}` |
| 5 | `scan_source()` | All `.cpp`/`.h` files in `Source/` | `{total_files, total_lines, cpp_files, header_files, modules: [{name, files, lines, dependencies}], build_targets}` |
| 6 | `scan_config()` | All `.ini` files in `Config/` | `{files: [name, size], key_settings: {rendering, physics, input, networking}}` |
| 7 | `resolve_dependencies(bridge)` | Import tables of all assets (requires UAssetAPI bridge) | `{nodes, edges, orphaned_assets, most_referenced}` — full cross-asset dependency graph |

### Requirements

- **Phases 1-3, 5-6**: No special requirements — reads JSON and filesystem only
- **Phase 4** (deep level scan): Requires UAssetAPI bridge to be initialized (run `setup.sh` first)
- **Phase 7** (dependency resolution): Requires UAssetAPI bridge

---

## Plugin Validator Reference

Validates `.uplugin` descriptors and plugin directory structure against UE 5.6 requirements.

### Validation Phases

| Phase | Category | What It Checks |
|-------|----------|----------------|
| 1 | `descriptor` | JSON parsing, file existence, UTF-8 BOM handling |
| 2 | `descriptor` | `FileVersion` (must be 3 for UE 5.x), `FriendlyName` presence, `VersionName` format (semver recommended), `Description` length, `Category` validity, `CreatedBy`, `DocsURL`, `MarketplaceURL` |
| 3 | `module` | Module `Name` validity (alphanumeric + underscore, no spaces), `Type` (must be in valid set), `LoadingPhase` (must be in valid set), editor-only module type consistency with plugin's `CanContainContent` |
| 4 | `filesystem` | `Source/` directory exists for each module, `Content/` matches `CanContainContent`, `Resources/Icon128.png` exists, `Binaries/` presence |
| 5 | `build` | `.Build.cs` exists for each module, module name in `.Build.cs` matches descriptor, dependencies reference known engine modules |
| 6 | `dependency` | Plugin dependencies exist in known plugin list, no circular references, `optional` flag usage |

### Valid Module Types (UE 5.6)

Source: `Engine/Source/Runtime/Projects/Public/ModuleDescriptor.h`

```
Runtime              RuntimeNoCommandlet    RuntimeAndProgram
CookedOnly           UncookedOnly
Developer            DeveloperTool
Editor               EditorNoCommandlet     EditorAndProgram
Program
ServerOnly           ClientOnly             ClientOnlyNoCommandlet
```

### Valid Loading Phases (UE 5.6)

```
EarliestPossible     PostConfigInit         PostSplashScreen
PreEarlyLoadingScreen PreLoadingScreen
PreDefault           Default                PostDefault
PostEngineInit       None
```

### Report Format

```json
{
  "plugin_name": "MyPlugin",
  "plugin_path": "/path/to/MyPlugin",
  "passed": true,
  "error_count": 0,
  "warning_count": 2,
  "info_count": 1,
  "module_count": 1,
  "dependency_count": 0,
  "has_content": false,
  "has_source": true,
  "has_binaries": false,
  "has_icon": false,
  "descriptor": { ... },
  "items": [
    {
      "severity": "warning",
      "category": "filesystem",
      "message": "No Resources/Icon128.png found",
      "location": "MyPlugin/Resources/",
      "suggestion": "Add a 128x128 PNG icon for the plugin browser"
    }
  ]
}
```

---

## Dashboard Views

The web dashboard at `http://localhost:8080` provides 10 read-only views. All views are populated from the API — the dashboard makes no modifications.

| View | Sidebar Label | Data Source | Description |
|------|--------------|-------------|-------------|
| Overview | Overview | `/api/overview` | Stats grid (exports, imports, names, actors, classes, functions) + asset details table showing file path, engine version, asset type |
| Actors | Actors | `/api/actors` | Full actor table with class, name, BP flag, component count. Click any actor for detail panel with properties and components. |
| Exports | Exports | `/api/exports` | Complete export table — index, type (NormalExport, ClassExport, LevelExport), class, name, serial size |
| Imports | Imports | `/api/imports` | Import table — index, class, package, name |
| Functions | Functions | `/api/functions` | Blueprint function list with bytecode status (has bytecode, size in bytes) |
| Validation | Validation | `/api/validation` | Integrity check results — overall pass/fail, error count, warning count, per-issue detail with severity, category, message, suggestion |
| Levels | Levels | `/api/levels` | **Two-panel content browser layout.** Bottom panel: persistent grid of square tiles for each loaded level (shows actor count, function count, compiled count). Top panel: scrollable detail view that populates when you click a tile — stats, actors, K2 nodes with editor-matching names, collapsible compiled bytecode graphs per function. |
| Plugins | Plugins | `/api/plugins` | Plugin validation results — pass/fail badge, error/warning counts, module list, dependency list, per-issue detail |
| Scripts | Scripts | `/api/script/operations` | Browse all 45 script operations organized by domain. Select a domain and method, fill in parameters, preview the generated Python script. |
| Project | Project | `/api/project` | Full project scan results — engine version, module list, plugin list, asset counts by type, source code stats, config file list |

### Design System

- **Typography**: Space Grotesk (display/headings) + DM Sans (body) + JetBrains Mono (data/code)
- **Color**: OKLCH token system, dark theme (not pure black), tungsten amber accent, 60-30-10 rule
- **Layout**: Sidebar (260px) + content grid, no horizontal scroll, responsive at 768px breakpoint
- **Interaction**: 3D press states on stat cards, hover lifts with shadow, staggered fade-in animations
- **CSS**: BEM naming, flat specificity, custom properties, rem units, `prefers-reduced-motion` respected

---

## K2 Node Name Resolution

Node names in the dashboard match what you see in the UE Blueprint editor. The resolution logic is derived directly from UE 5.6 source code `GetNodeTitle()` implementations:

| K2Node Type | Source File | Resolution Rule | Example |
|-------------|------------|-----------------|---------|
| `K2Node_CallFunction` | `K2Node_CallFunction.cpp` | `FunctionReference.MemberName` passed through `FName::NameToDisplayString()`, with target class on second line | "Broadcast Message" / "Target: Gameplay Message Subsystem" |
| `K2Node_GetSubsystem` | `K2Node_GetSubsystem.cpp` | `SubsystemClass` import resolved, display name extracted | "Get Gameplay Message Subsystem" |
| `K2Node_DynamicCast` | `K2Node_DynamicCast.cpp` | `TargetType` import resolved, `_C` suffix stripped for BP classes | "Cast To BP_PlayerPawn" |
| `K2Node_CustomEvent` | `K2Node_CustomEvent.cpp` | `CustomFunctionName` property used directly | "Scene4 Phone Grabbed" |
| `K2Node_BreakStruct` | `K2Node_BreakStruct.cpp` | `StructType` import resolved | "Break Msg Teleport Point" |
| `K2Node_MakeStruct` | `K2Node_MakeStruct.cpp` | `StructType` import resolved | "Make Soft Object Path" |
| `K2Node_IfThenElse` | `K2Node_IfThenElse.cpp` | Static title | "Branch" |
| `K2Node_LoadAsset` | `K2Node_LoadAsset.cpp` | Static title | "Async Load Asset" |
| `K2Node_Knot` | `K2Node_Knot.cpp` | Static title | "Reroute Node" |
| `K2Node_ConvertAsset` | `K2Node_ConvertAsset.cpp` | Static title | "Make Soft Reference" |
| `K2Node_Literal` | `K2Node_Literal.cpp` | Object reference resolved | "Literal (ObjectName)" |
| `K2Node_MacroInstance` | `K2Node_MacroInstance.cpp` | Macro name from object_name | "Macro: ForEachLoop" |
| `K2Node_AssignDelegate` | `K2Node_AssignDelegate.cpp` | Delegate property name | "Assign OnLoaded" |
| `K2Node_BaseAsyncTask` | `K2Node_BaseAsyncTask.cpp` | Factory function name via NameToDisplayString | "Listen For Gameplay Messages" |

### FName::NameToDisplayString Implementation

The Python implementation matches UE's C++ logic:

1. Strip `K2_` prefix if present
2. Insert spaces before uppercase letters following lowercase letters (`CamelCase` -> `Camel Case`)
3. Replace underscores with spaces
4. Collapse multiple spaces
5. Title-case the result

---

## UE 5.6 Compatibility

Verified against the `dev-5.6` branch of `AniketMan/UnrealEngine` (Engine version 5.6.1).

### Version Enums

| Enum | Range | Source |
|------|-------|--------|
| `ObjectVersionUE5` | 1000-1017 | `Engine/Source/Runtime/Core/Public/UObject/ObjectVersion.h` |
| `EngineVersion` | `VER_UE5_6` | `UAssetAPI/UnrealTypes/EngineVersion.cs` |

### Header Fields Verified

| Field | Version Gate | Status |
|-------|-------------|--------|
| `VERSE_CELLS` | `ObjectVersionUE5 >= 1015` | Handled |
| `PACKAGE_SAVED_HASH` | `ObjectVersionUE5 >= 1012` | Handled |
| `METADATA_SERIALIZATION_OFFSET` | `ObjectVersionUE5 >= 1016` | Handled |
| `FilterEditorOnly` flag | All versions | Handled |
| `bUseWorldPartition` | UE5+ | Handled |

### Kismet Bytecode

All opcodes through UE 5.6 are supported, including:
- `EX_InstrumentationEvent` (added in 5.x)
- `EX_ArrayGetByRef` (added in 5.x)
- `EX_GetCoroutine` (added in 5.x)
- All RTFM transact opcodes

---

## Troubleshooting

### "No .NET runtime found" or "coreclr not found"

Run `./setup.sh` again. If it fails, manually install:
```bash
wget https://dot.net/v1/dotnet-install.sh -O /tmp/dotnet-install.sh
chmod +x /tmp/dotnet-install.sh
/tmp/dotnet-install.sh --channel 8.0
export PATH="$HOME/.dotnet:$PATH"
export DOTNET_ROOT="$HOME/.dotnet"
```

### "UAssetAPI.dll not found" or "lib/publish/ is empty"

The `lib/publish/` directory is gitignored (binaries are not committed). Run `./setup.sh` to build it, or manually:
```bash
git clone --recurse-submodules https://github.com/atenfyr/UAssetGUI /tmp/UAssetGUI
cd /tmp/UAssetGUI/UAssetAPI
dotnet publish UAssetAPI/UAssetAPI.csproj -c Release -o /path/to/ue56-level-editor/lib/publish/
```

### "pythonnet import error" or "clr_loader not found"

```bash
sudo pip3 install pythonnet
# If that fails:
sudo pip3 install clr-loader
sudo pip3 install pythonnet --no-build-isolation
```

### "JarvisEditor plugin not found in Python" or "unreal.JarvisBlueprintLibrary does not exist"

The plugin must be compiled into your UE project first. It ships as C++ source, not pre-compiled binaries. Follow the installation steps in [JarvisEditor C++ Plugin](#jarviseditor-c-plugin). After building, verify:
```python
import unreal
print(hasattr(unreal, 'JarvisBlueprintLibrary'))  # Must print True
```

If it prints `False`, check:
1. The plugin directory is at `YourProject/Plugins/JarvisEditor/`
2. The `.uplugin` file has `"Type": "Editor"` (not "Runtime")
3. You regenerated project files after copying
4. The build succeeded without errors
5. The Output Log shows `LogJarvis: JarvisEditor module loaded`

### "Validation shows errors that aren't real"

The integrity validator checks against UAssetAPI's name map resolution. FName numeric suffixes (e.g., `AmbientSound_0` stored as `AmbientSound` + number `0`) are handled correctly in the current version. If you see false positives, ensure you're on the latest commit.

### Dashboard shows "No Asset Loaded"

Load an asset via the API:
```bash
curl -X POST http://localhost:8080/api/load \
  -H "Content-Type: application/json" \
  -d '{"path": "/absolute/path/to/your.umap"}'
```

### Server won't start — "Address already in use"

```bash
# Find and kill the existing process on port 8080
lsof -ti :8080 | xargs kill -9
# Then restart
python3 run_dashboard.py
```

### "Project scan failed — no .uproject found"

The project scanner expects the root directory containing the `.uproject` file. Not a subdirectory, not the Content folder, not the Plugins folder. Example:
```bash
# Correct:
curl -X POST http://localhost:8080/api/project/scan \
  -d '{"project_path": "/home/user/MyProject"}'
# Where /home/user/MyProject/MyProject.uproject exists

# Wrong:
curl -X POST http://localhost:8080/api/project/scan \
  -d '{"project_path": "/home/user/MyProject/Content"}'
```

---

## File Structure

```
ue56-level-editor/
  core/
    __init__.py                # Module exports
    uasset_bridge.py           # Python <-> UAssetAPI .NET bridge
    blueprint_editor.py        # K2 node resolution, Kismet bytecode
    level_logic.py             # Actor enumeration, properties
    integrity.py               # Reference validation
    plugin_validator.py        # .uplugin validation
    project_scanner.py         # Full project scanning
    script_generator.py        # UE Python script generation (45 ops)
    cli.py                     # CLI interface
  ui/
    __init__.py                # Module init
    server.py                  # Flask API (25+ endpoints)
    static/
      index.html               # Dashboard SPA (10 views)
  ue_plugin/
    JarvisEditor/
      JarvisEditor.uplugin     # Plugin descriptor
      Source/
        JarvisEditor/
          JarvisEditor.Build.cs        # Build rules
          Public/
            JarvisEditorModule.h       # Module header
            JarvisBlueprintLibrary.h   # 12 function declarations
          Private/
            JarvisEditorModule.cpp     # Module implementation
            JarvisBlueprintLibrary.cpp # 12 function implementations
  lib/
    publish/                   # UAssetAPI .NET binaries (built by setup.sh, gitignored)
  setup.sh                     # Bootstrap script
  run_dashboard.py             # Server launcher
  requirements.txt             # Python dependencies
  .gitignore
  README.md                    # This file
```

---

## License

This tool uses [UAssetAPI](https://github.com/atenfyr/UAssetGUI) (MIT License) for binary asset parsing. The JarvisEditor plugin and all Python code are proprietary to Aniket Bhatt.
