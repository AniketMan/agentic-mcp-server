# UE 5.6 Level Editor

AI-operated Unreal Engine 5.6 project editor. Inspects `.uasset`/`.umap` files outside the engine, generates `unreal` Python scripts for in-editor execution, and validates plugins — all controlled by JARVIS.

## Architecture

```
+---------------------------+      +---------------------------+
|   Inspection Layer        |      |   Script Generation       |
|   (Read-Only)             |      |   (Write via UE Editor)   |
|                           |      |                           |
|   UAssetAPI (.NET)        |      |   unreal Python scripts   |
|   via pythonnet bridge    |      |   + JarvisEditor C++ plugin|
|                           |      |                           |
|   Reads: .uasset, .umap  |      |   Runs inside UE5.6       |
|   Parses: actors, K2      |      |   Full engine API access  |
|   nodes, bytecode, refs   |      |                           |
+---------------------------+      +---------------------------+
            |                                  |
            v                                  v
+-----------------------------------------------------------+
|                   Flask API Server                         |
|   22+ REST endpoints | Dashboard UI | Script Preview       |
+-----------------------------------------------------------+
```

## Quick Start

```bash
# 1. Bootstrap (installs .NET, builds UAssetAPI, installs Python deps)
./setup.sh

# 2. Run the dashboard
python3 run_dashboard.py

# 3. Open http://localhost:8080
```

## Dashboard Views

| View | Purpose |
|------|---------|
| **Overview** | Asset stats, file details, engine version |
| **Actors** | Actor table with class, components, properties |
| **Exports** | Full export table with types and serial sizes |
| **Imports** | Import table with class packages |
| **Functions** | Blueprint functions with bytecode status |
| **Validation** | Reference integrity checks (0 false positives) |
| **Levels** | Content browser grid — click a level to see full breakdown with K2 nodes |
| **Plugins** | .uplugin validation with scoring and issue detection |
| **Scripts** | Browse all 45 script operations across 10 domains, preview generated code |
| **Project** | Full project scan — .uproject, plugins, source, configs, assets |

## Script Generator — 10 Domains, 45 Operations

All scripts are complete, runnable Python files with `[JARVIS]` logging, error handling, and validation. Run them in UE5.6 via Python console or `-ExecutePythonScript`.

### Actors (8 ops)
`spawn` `spawn_blueprint` `delete` `set_property` `move` `duplicate` `list_all` `batch_set_property`

### Assets (6 ops)
`load` `duplicate` `delete` `rename` `save_all_dirty` `list_assets`

### Sequences (6 ops)
`create` `add_track` `add_keyframe` `set_playback_range` `bind_actor` `list_bindings`

### Materials (3 ops)
`set_parameter` `create_instance` `assign_to_actor`

### Blueprints (5 ops)
`compile` `add_variable` `set_variable_default` `create` `reparent`

### Animation (3 ops)
`retarget` `import_fbx` `set_anim_on_skeletal_mesh`

### Data Tables (2 ops)
`list_rows` `get_row`

### Levels (5 ops)
`load_level` `save_current_level` `add_streaming_level` `list_streaming_levels` `set_level_visibility`

### PCG (2 ops)
`execute_graph` `set_pcg_parameter`

### Utility (5 ops)
`run_commandlet` `build_lighting` `take_screenshot` `fix_redirectors` `custom`

## C++ Plugin — JarvisEditor

For Blueprint graph manipulation methods not exposed to Python. Drop into your project's `Plugins/` directory.

**Functions exposed to Python via `unreal.JarvisBlueprintLibrary`:**

| Function | Purpose |
|----------|---------|
| `add_node_to_graph` | Add a K2Node to a Blueprint event graph |
| `remove_node_from_graph` | Remove a node by name |
| `connect_pins` | Connect two pins (handles bidirectional linking) |
| `disconnect_pin` | Disconnect a specific pin |
| `disconnect_all_pins` | Disconnect all pins on a node |
| `compile_blueprint` | Compile with full status reporting |
| `get_all_graph_nodes` | List all nodes in a Blueprint's graphs |
| `get_node_connections` | Get all connections for a specific node |
| `add_custom_event` | Add a custom event node |
| `add_function_call` | Add a function call node |
| `set_node_position` | Set a node's position in the graph |
| `validate_all_node_connections` | Validate all connections in a Blueprint |

## K2 Node Name Resolution

Node names match the UE Blueprint editor. Resolution logic derived from UE 5.6 source `GetNodeTitle()`:

- **CallFunction** — `FunctionReference.MemberName` via `FName::NameToDisplayString` with target class
- **GetSubsystem** — `SubsystemClass` import resolved to "Get {ClassName}"
- **DynamicCast** — `TargetType` resolved to "Cast To {Name}"
- **CustomEvent** — `CustomFunctionName` directly
- **BreakStruct/MakeStruct** — `StructType` resolved to "Break/Make {Name}"
- **IfThenElse** — "Branch"
- **LoadAsset** — "Async Load Asset"
- **Knot** — "Reroute Node"

## API Reference

### Asset Inspection
| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/load` | Load a .uasset/.umap file |
| POST | `/api/load-multiple` | Load multiple files for Levels view |
| GET | `/api/status` | Current load status |
| GET | `/api/overview` | Asset overview stats |
| GET | `/api/actors` | Actor list |
| GET | `/api/exports` | Export table |
| GET | `/api/imports` | Import table |
| GET | `/api/functions` | Function list with bytecode |
| GET | `/api/validation` | Integrity check results |
| GET | `/api/levels` | All loaded levels with K2 nodes |

### Script Generation
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/script/operations` | List all 45 operations with params |
| POST | `/api/script/generate` | Generate script from domain.method + params |
| POST | `/api/script/save` | Generate and save script to file |

### Plugin Validation
| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/plugin/validate` | Validate a .uplugin file |
| GET | `/api/plugins` | List all validated plugins |

### Project Scanning
| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/project/scan` | Full project scan from .uproject root |
| GET | `/api/project` | Current project scan results |

## Design System

- **Typography**: Space Grotesk (display) + DM Sans (body) + JetBrains Mono (data)
- **Color**: OKLCH token system, 60-30-10 rule, tungsten amber accent
- **Layout**: Sidebar + content grid, no horizontal scroll, responsive
- **Interaction**: 3D press states, hover lifts, staggered animations
- **CSS**: BEM naming, flat specificity, custom properties, rem units

## UE 5.6 Compatibility

Verified against `dev-5.6` branch (Engine version 5.6.1):
- ObjectVersionUE5 enums up to 1017
- VERSE_CELLS, PACKAGE_SAVED_HASH, METADATA_SERIALIZATION_OFFSET header fields
- All Kismet bytecode opcodes
- K2Node GetNodeTitle() logic from UE 5.6 source

## File Structure

```
ue56-level-editor/
  core/
    uasset_bridge.py       # Python <-> UAssetAPI .NET bridge
    blueprint_editor.py    # K2 node resolution, Kismet bytecode
    level_logic.py         # Actor enumeration, properties
    integrity.py           # Reference validation
    plugin_validator.py    # .uplugin validation
    project_scanner.py     # Full project scanning
    script_generator.py    # UE Python script generation (45 ops)
    cli.py                 # CLI interface
  ui/
    server.py              # Flask API (22+ endpoints)
    static/index.html      # Dashboard SPA
  ue_plugin/
    JarvisEditor/          # C++ editor plugin for UE5.6
  setup.sh                 # Bootstrap script
  run_dashboard.py         # Server launcher
```
