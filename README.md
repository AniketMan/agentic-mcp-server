# UE 5.6 Level Logic Editor

An AI-operated inspection and editing tool for Unreal Engine 5.6 `.uasset` and `.umap` files. Built on top of [UAssetAPI](https://github.com/atenfyr/UAssetGUI) for binary asset parsing, with a Flask-based dashboard for read-only visualization and a Python API for programmatic editing.

## Architecture

The tool has two operational modes:

**Binary Inspection (Read-Only)** — Parses `.uasset`/`.umap` files outside the engine using UAssetAPI (.NET) via pythonnet. Extracts exports, imports, actors, Blueprint graphs, K2 nodes, Kismet bytecode, and reference chains. No engine installation required.

**Script Generation (Write)** — Generates `unreal` Python scripts that run inside UE5.6's editor for safe editing. The engine handles all serialization, validation, and dependency tracking natively. Zero risk of file corruption.

## Components

| Module | Purpose |
|--------|---------|
| `core/uasset_bridge.py` | Python wrapper around UAssetAPI via .NET interop |
| `core/level_logic.py` | Level actor enumeration, property reading, component trees |
| `core/blueprint_editor.py` | Kismet bytecode parsing, K2 node resolution, graph visualization |
| `core/integrity.py` | Reference integrity validation (FPackageIndex, FName, circular refs) |
| `core/plugin_validator.py` | `.uplugin` descriptor validation, module structure checks, dependency analysis |
| `ui/server.py` | Flask REST API and static file server |
| `ui/static/index.html` | Single-page dashboard (dark theme, OKLCH design tokens) |
| `run_dashboard.py` | Server launcher |

## Dashboard Views

- **Overview** — Stats grid, asset details, engine version
- **Actors** — Full actor table with class, Blueprint flag, components, properties
- **Exports** — Complete export table with type, class, serial size
- **Imports** — Import table with class, package, name
- **Functions** — Blueprint function list with bytecode status
- **Validation** — Integrity check results (0 false positives)
- **Level Logic** — Multi-map view with K2 node graphs using editor-matching display names
- **Plugins** — `.uplugin` validation with score, modules, dependencies, issues

## K2 Node Name Resolution

Node names match what you see in the UE Blueprint editor. Resolution logic is derived directly from the UE 5.6 source `GetNodeTitle()` implementations:

- **CallFunction** — Resolves `FunctionReference.MemberName` via `FName::NameToDisplayString` with target class context
- **GetSubsystem** — Resolves `SubsystemClass` import to "Get {ClassName}"
- **DynamicCast** — Resolves `TargetType` to "Cast To {Name}" (strips `_C` for BP classes)
- **CustomEvent** — Uses `CustomFunctionName` directly
- **BreakStruct/MakeStruct** — Resolves `StructType` to "Break/Make {Name}"
- **IfThenElse** — Static title "Branch"
- **LoadAsset** — Static title "Async Load Asset"
- **Knot** — Static title "Reroute Node"

## Setup

```bash
# Prerequisites: Python 3.11+, .NET 8 SDK

# Install Python dependencies
pip3 install flask pythonnet

# Build UAssetAPI (if lib/publish is empty)
cd UAssetGUI/UAssetAPI
dotnet publish UAssetAPI/UAssetAPI.csproj -c Release -o ../../ue56-level-editor/lib/publish

# Run
cd ue56-level-editor
python3 run_dashboard.py
```

## API Reference

### Asset Operations
| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/load` | Load a `.uasset`/`.umap` file |
| POST | `/api/load-multi` | Load multiple `.umap` files for Level Logic view |
| GET | `/api/status` | Check if an asset is loaded |
| GET | `/api/summary` | Get asset summary |
| POST | `/api/save` | Save with validation and backup |

### Inspection
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/exports` | List all exports |
| GET | `/api/imports` | List all imports |
| GET | `/api/names` | List the name map |
| GET | `/api/export/{index}` | Export detail with properties |
| GET | `/api/actors` | List level actors |
| GET | `/api/actor/{name}` | Actor detail with components |
| GET | `/api/functions` | List Blueprint functions |
| GET | `/api/graph/{export_index}` | Get function graph |
| GET | `/api/validate` | Run integrity validation |

### Level Logic
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/levels` | List all loaded levels with K2 nodes |
| GET | `/api/level/{filename}/graph/{index}` | Get function graph for a specific level |
| GET | `/api/level/{filename}/actors` | Get actors for a specific level |
| GET | `/api/level/{filename}/validate` | Validate a specific level |

### Plugin Validation
| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/plugin/validate` | Validate a single `.uplugin` file or directory |
| POST | `/api/plugin/validate-multiple` | Validate multiple plugins |
| POST | `/api/plugin/scan` | Scan a directory tree for all `.uplugin` files |
| GET | `/api/plugins` | List all validated plugins |

## Design System

The dashboard follows a custom design rules document with:
- **Typography**: Space Grotesk (display) + DM Sans (body) + JetBrains Mono (data)
- **Color**: OKLCH token system, 60-30-10 rule, tungsten amber accent
- **Layout**: Sidebar + content grid, no horizontal scroll, responsive
- **Interaction**: 3D press states, hover lifts, staggered animations, `prefers-reduced-motion` respected
- **CSS**: BEM naming, flat specificity, custom properties, `rem` units

## UE 5.6 Compatibility

Verified against the `dev-5.6` branch (Engine version 5.6.1):
- ObjectVersionUE5 enums up to 1017
- VERSE_CELLS, PACKAGE_SAVED_HASH, METADATA_SERIALIZATION_OFFSET header fields
- All Kismet bytecode opcodes
- K2Node GetNodeTitle() logic from UE 5.6 source

## Next Steps

- [ ] `unreal` Python script generator for all editor scripting domains
- [ ] C++ plugin stub for Blueprint graph manipulation methods not exposed to Python
- [ ] Full project mode (load `.uproject` + all assets for complete reference resolution)
- [ ] Level Sequence track/keyframe inspection
- [ ] Batch operations across multiple assets
