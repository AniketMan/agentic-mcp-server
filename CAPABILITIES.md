# AgenticMCP Capabilities

This document is the **permanent capabilities manifest** for the AgenticMCP plugin. It is loaded automatically on every MCP connection. Any AI agent connected to this plugin **must read this file first** and treat it as the authoritative source of what operations are available.

Do not guess. Do not hallucinate capabilities. If it is not listed here, it is not available.

---

## 1. Blueprint Manipulation

These tools operate on any Blueprint asset in the project — Actor Blueprints, Level Blueprints, Widget Blueprints, Animation Blueprints.

| Tool | What It Does | Key Parameters |
|------|-------------|----------------|
| `list_blueprints` | List all Blueprint assets in the project or a subfolder | `path` (optional filter) |
| `get_blueprint` | Get full metadata for a Blueprint — class, parent, variables, graphs | `name` or `path` |
| `get_graph` | Get the node graph for a specific function/event graph | `blueprint`, `graph_name` |
| `search_blueprints` | Full-text search across Blueprint names, variables, nodes | `query` |
| `get_references` | Find all assets that reference or are referenced by a Blueprint | `blueprint` |
| `get_classes` | List all available classes that can be used as node types | `filter` (optional) |
| `get_functions` | List all functions available on a class | `class_name` |
| `get_properties` | List all properties/pins on a class or node | `class_name` or `node_id` |
| `get_pins` | Get detailed pin information for a specific node | `blueprint`, `graph`, `node_id` |
| `add_node` | Add a node to a Blueprint graph | `blueprint`, `graph`, `node_type`, `position`, `params` |
| `delete_node` | Remove a node from a graph | `blueprint`, `graph`, `node_id` |
| `connect_pins` | Wire two pins together | `blueprint`, `graph`, `source_node`, `source_pin`, `target_node`, `target_pin` |
| `disconnect_pin` | Break a pin connection | `blueprint`, `graph`, `node_id`, `pin_name` |
| `set_pin_default` | Set the default value of an input pin | `blueprint`, `graph`, `node_id`, `pin_name`, `value` |
| `move_node` | Reposition a node in the graph | `blueprint`, `graph`, `node_id`, `x`, `y` |
| `compile_blueprint` | Compile a Blueprint and return errors/warnings | `blueprint` |
| `create_blueprint` | Create a new Blueprint asset | `name`, `path`, `parent_class` |
| `create_graph` | Add a new function or macro graph | `blueprint`, `graph_name`, `graph_type` |
| `delete_graph` | Remove a function or macro graph | `blueprint`, `graph_name` |
| `add_variable` | Add a variable to a Blueprint | `blueprint`, `name`, `type`, `default_value` |
| `remove_variable` | Remove a variable | `blueprint`, `name` |

### Supported Node Types

The `add_node` tool supports these node types via the `node_type` parameter:

| Node Type | Description | Example Use |
|-----------|-------------|-------------|
| `function_call` | Call any function on any class | `PrintString`, `SetActorLocation`, `BroadcastStoryStep` |
| `event` | Event nodes (BeginPlay, Tick, Custom) | Entry points for logic |
| `custom_event` | Named custom events | Delegates, callbacks |
| `branch` | If/else conditional | Boolean logic |
| `sequence` | Execute multiple outputs in order | Sequential operations |
| `multigate` | Execute outputs one at a time per trigger | State machines |
| `gate` | Open/close gate for execution flow | Flow control |
| `delay` | Wait for duration then continue | Timed sequences |
| `timeline` | Animate values over time | Smooth transitions |
| `for_each` | Loop over array elements | Iteration |
| `cast` | Cast to specific class | Type conversion |
| `make_struct` | Construct a struct value | Data construction |
| `break_struct` | Decompose a struct into fields | Data extraction |
| `get_variable` | Read a variable | Variable access |
| `set_variable` | Write a variable | Variable mutation |
| `literal` | Constant value node | Hardcoded values |
| `spawn_actor` | Spawn actor from class | Runtime actor creation |
| `create_widget` | Create UI widget | HUD elements |
| `macro` | Macro instance | Reusable logic blocks |

---

## 2. Actor Management

These tools operate on actors placed in the currently loaded level(s).

| Tool | What It Does | Key Parameters |
|------|-------------|----------------|
| `list_actors` | List all actors in the current level with class, name, transform | `class_filter` (optional) |
| `get_actor` | Get full details for a specific actor — components, properties, transform | `name` or `label` |
| `spawn_actor` | Place a new actor in the level | `class`, `location`, `rotation`, `label` |
| `delete_actor` | Remove an actor from the level | `name` or `label` |
| `set_actor_property` | Set any UPROPERTY on an actor | `actor`, `property`, `value` |
| `set_actor_transform` | Move/rotate/scale an actor | `actor`, `location`, `rotation`, `scale` |

---

## 3. Level Management

| Tool | What It Does | Key Parameters |
|------|-------------|----------------|
| `list_levels` | List all loaded and available levels | none |
| `load_level` | Load a sublevel into the world | `path`, `streaming_class` |
| `get_level_blueprint` | Get the Level Blueprint for a loaded level | `level_name` |

---

## 4. Safety and Validation

| Tool | What It Does | Key Parameters |
|------|-------------|----------------|
| `validate` | Run validation checks on a Blueprint or level | `target` |
| `snapshot` | Save the current state of a Blueprint for rollback | `blueprint` |
| `restore` | Restore a Blueprint to a previous snapshot | `blueprint`, `snapshot_id` |

---

## 5. Python Script Execution

The plugin can execute arbitrary Python scripts inside the UE5 editor via `unreal.PythonScriptLibrary` or the editor console. This is the **most powerful capability** — anything the editor can do, Python can automate.

| Tool | What It Does | Key Parameters |
|------|-------------|----------------|
| `execute_python` | Run a Python script in the editor's Python environment | `script` (string or file path) |

### What Python Can Do That Other Tools Cannot

The Python execution path gives access to the **full `unreal` module**, which includes every editor subsystem. Key capabilities:

**Level Sequence Operations:**
- Create, open, and modify Level Sequences
- Add actor bindings (possessable or spawnable)
- Add tracks (Transform, Animation, Audio, Event, Camera Cut, Visibility, Fade)
- Add sections to tracks with frame ranges
- Set keyframes on channels with interpolation modes
- Copy/paste bindings, tracks, sections, folders between sequences
- Set playback range and frame rate
- Focus on sub-sequences

**Asset Operations:**
- `unreal.EditorAssetLibrary` — find, load, rename, delete, duplicate assets
- `unreal.AssetToolsHelpers.get_asset_tools()` — create new assets with factories
- Import FBX, textures, audio files

**Actor Operations (via Python):**
- `unreal.EditorActorSubsystem` — get all actors, get selected actors, spawn, destroy, duplicate, set transforms
- Select actors programmatically
- Get/set any UPROPERTY via `actor.get_editor_property()` / `set_editor_property()`

**Editor Utility:**
- `unreal.EditorLevelLibrary` — load/save levels, get editor world
- `unreal.SystemLibrary` — execute console commands
- `unreal.EditorDialog` — show message boxes

---

## 6. Scene State Awareness

The plugin provides mechanisms to understand what exists in the current scene.

| Capability | How to Access |
|------------|---------------|
| All actors with transforms | `list_actors` tool — returns name, class, location, rotation, scale |
| Actor hierarchy | `get_actor` tool — returns components and child actors |
| Level structure | `list_levels` tool — shows loaded/unloaded sublevels |
| Blueprint graph state | `get_graph` tool — full node/pin/wire dump |
| Asset references | `get_references` tool — dependency graph |

---

## 7. Offline Fallback (Binary Injector)

When the UE5 editor is **not running**, the binary injector provides a subset of capabilities by directly reading and modifying `.umap` files on disk.

| Capability | Available Offline |
|------------|-------------------|
| Read actor list from `.umap` | Yes |
| Read actor properties | Yes |
| Read Blueprint graphs | Yes (bytecode level) |
| Modify actor properties | Yes |
| Add/remove actors | Yes |
| Modify Blueprint graphs | No (use paste text generation instead) |
| Level Sequence editing | No |
| Python execution | No |

---

## 8. What This Plugin CANNOT Do

Be explicit about limitations so the AI does not waste time attempting impossible operations.

| Cannot Do | Why | Workaround |
|-----------|-----|------------|
| Compile C++ code | Requires full build toolchain | Use Blueprint or Python instead |
| Run gameplay | PIE must be started manually | Can trigger PIE via Python console command |
| Access runtime-only state | Editor operates in edit mode | Use PIE delegates for runtime testing |
| Modify engine source | Read-only access to engine | Modify project source only |
| Create Niagara systems from scratch | Complex internal format | Create in editor, modify via Python |
| Edit Material graphs | Different graph system | Use Material Instance parameters via Python |
| Record motion capture | Requires external hardware | Import recorded data via FBX |
| Generate 3D assets | Not a generative tool | Import pre-made assets |

---

## 9. Connection Protocol

On every MCP connection, the following happens automatically:

1. This `CAPABILITIES.md` file is loaded and injected into the AI context
2. The `CLAUDE.md` file is loaded with workflow instructions
3. All context docs from `Tools/contexts/` are available on demand
4. The persistent project state file (if it exists) at `Content/AgenticMCP/project_state.json` is read
5. The recent actions buffer is read from `Content/AgenticMCP/recent_actions.log`

The AI agent should:
1. Read this capabilities list first
2. Read the project state to understand what exists
3. Read the recent actions to understand what the user just did
4. Only then begin executing the requested task
