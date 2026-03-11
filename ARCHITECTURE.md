# AgenticMCP Architecture

## Overview

AgenticMCP is a dual-path UE5 editor manipulation system. It provides AI agents with full access to Blueprints, actors, levels, and assets through two independent execution paths: a live editor connection and an offline binary injector fallback. No other tool in the ecosystem has both.

## Three-Layer Architecture

```
+-------------------------------------------------------------------+
|                   AI Agent (Manus, Claude, Cursor)                 |
+-------------------------------------------------------------------+
                              |
                    MCP Protocol (stdio)
                              |
+-------------------------------------------------------------------+
|                   Layer 2: MCP Bridge (Node.js)                   |
|                                                                   |
|   index.js          - MCP server, tool routing, auto-async        |
|   lib.js            - HTTP client, timeout, schema conversion     |
|   context-loader.js - UE API docs injection per tool category     |
|   fallback.js       - Offline injector bridge (Python subprocess) |
|   contexts/*.md     - UE 5.x API reference per domain            |
+-------------------------------------------------------------------+
          |                                         |
     HTTP (port 9847)                        Python subprocess
     (editor running)                        (editor closed)
          |                                         |
+--------------------------+    +----------------------------------+
| Layer 1: C++ Plugin     |    | Layer 3: Binary Injector         |
| (in-editor HTTP server) |    | (offline .umap manipulation)     |
|                          |    |                                  |
| Runs inside UE5 editor   |    | Parses .umap via UAssetAPI       |
| Exposes tools over HTTP  |    | Reads/writes K2Node graphs       |
| Full UEdGraph API access |    | Patches SSEO bytecode            |
| Real-time compilation    |    | No editor required               |
+--------------------------+    +----------------------------------+
```

## Path Selection Logic

The MCP bridge automatically selects the execution path:

1. **Primary (Live):** Attempt HTTP connection to the C++ plugin. If the editor is running and the plugin is loaded, all tools execute through the live UEdGraph API. This is the preferred path — it supports compilation, real-time validation, and the full tool set.

2. **Fallback (Offline):** If the HTTP connection fails (editor not running), the bridge routes supported operations to the Python binary injector via subprocess. This path supports a subset of operations — reading level data, adding/modifying nodes in .umap files, and patching Blueprint graphs. It cannot compile or validate in real-time, but it can prepare levels for the next editor session.

3. **Status reporting:** The `unreal_status` tool reports which path is active, what tools are available in the current mode, and any degraded capabilities.

## Layer 1: C++ Editor Plugin

The C++ plugin is a `UEditorSubsystem` that starts an HTTP server when the editor loads. It processes requests on the game thread via `FTickableEditorObject` to ensure thread safety with the UEdGraph API.

### Two Serving Modes

1. **Editor Subsystem** (preferred): Auto-starts when UE5 editor opens. Zero overhead. Uses `UEditorSubsystem` + `FTickableEditorObject` to process requests on the game thread.
2. **Standalone Commandlet**: Spawns headless `UnrealEditor-Cmd.exe` for CI/CD or when editor is not open.

### HTTP Endpoints

#### Health and Lifecycle

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | /api/health | Server status, asset counts |
| GET | /mcp/status | Connection status for MCP bridge |
| GET | /mcp/tools | Auto-discovery of all available tools |
| POST | /mcp/tool/{name} | Execute a tool by name |
| POST | /api/shutdown | Graceful shutdown (commandlet only) |
| POST | /api/rescan | Re-scan asset registry |

#### Blueprint Read

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | /api/list | List all Blueprint and Map assets |
| GET | /api/blueprint | Get Blueprint details (graphs, variables, parent class) |
| GET | /api/graph | Get graph nodes and connections |
| GET | /api/search | Search Blueprints by name pattern |
| GET | /api/references | Find references to an asset |
| POST | /api/list-classes | List UClasses matching a filter |
| POST | /api/list-functions | List UFunctions on a class |
| POST | /api/list-properties | List UProperties on a class |

#### Blueprint Mutation

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | /api/add-node | Create a new node in a graph |
| POST | /api/delete-node | Remove a node from a graph |
| POST | /api/connect-pins | Wire two pins together |
| POST | /api/disconnect-pin | Break pin connections |
| POST | /api/set-pin-default | Set a pin's default value |
| POST | /api/move-node | Change node position |
| POST | /api/refresh-all-nodes | Refresh all nodes in a Blueprint |
| POST | /api/create-blueprint | Create a new Blueprint asset |
| POST | /api/create-graph | Create a new function/macro graph |
| POST | /api/add-variable | Add a variable to a Blueprint |
| POST | /api/remove-variable | Remove a variable |
| POST | /api/compile-blueprint | Compile and save a Blueprint |

#### Actor Management

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | /api/list-actors | List actors in a level |
| POST | /api/get-actor | Get actor properties |
| POST | /api/spawn-actor | Spawn an actor in a level |
| POST | /api/delete-actor | Remove an actor from a level |
| POST | /api/set-actor-property | Set a property on an actor |
| POST | /api/set-actor-transform | Set actor location/rotation/scale |

#### Level Management

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | /api/list-levels | List all loaded levels/sublevels |
| POST | /api/load-level | Load a sublevel |
| POST | /api/unload-level | Unload a sublevel |
| POST | /api/get-level-blueprint | Get the level script Blueprint |

#### Validation and Safety

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | /api/validate-blueprint | Compile without saving, report errors |
| POST | /api/snapshot-graph | Save a snapshot of graph state |
| POST | /api/restore-graph | Restore graph from snapshot |
| POST | /api/get-pin-info | Introspect pin types and connections |

### Supported Node Types for add-node

| nodeType | Required Fields | Description |
|----------|----------------|-------------|
| CallFunction | functionName, className? | Call a UFunction |
| CustomEvent | eventName | Create a custom event |
| OverrideEvent | functionName | Override a parent class event |
| BreakStruct | typeName | Break a struct into pins |
| MakeStruct | typeName | Construct a struct from pins |
| VariableGet | variableName | Get a variable |
| VariableSet | variableName | Set a variable |
| DynamicCast | castTarget | Cast to a class |
| Branch | (none) | If/Then/Else |
| Sequence | (none) | Execution sequence |
| MacroInstance | macroName, macroSource? | Instantiate a macro |
| SpawnActorFromClass | actorClass? | Spawn actor node |
| Select | (none) | Select node |
| Comment | comment?, width?, height? | Comment box |
| Reroute | (none) | Reroute/knot node |
| MultiGate | numOutputs? | MultiGate execution node |
| Delay | duration? | Delay node |
| SetTimer | functionName, time? | Set Timer by Function Name |
| LoadAsset | (none) | Async Load Asset |

### Snapshot/Rollback System (Unique to AgenticMCP)

Before any destructive operation, the system can snapshot the current graph state:
- Stores all node positions, pin connections, and default values
- Supports named snapshots with descriptions
- Restore reverts to a previous snapshot atomically
- Maximum 10 snapshots in circular buffer

### Validation Endpoint (Unique to AgenticMCP)

The validate tool performs pre-compilation checks:
- Detects orphaned nodes (no exec connections)
- Finds broken pin connections (type mismatches)
- Identifies missing required pin values
- Reports warnings vs errors with node IDs

## Layer 2: MCP Bridge (Node.js)

Based on Natfii/ue5-mcp-bridge (MIT). Key capabilities:

### Auto-Discovery
The bridge does not hardcode tools. It queries the C++ plugin's `/mcp/tools` endpoint at startup and dynamically registers all available tools. This means the C++ plugin defines the tool set — the bridge just wraps it.

### Async Task Queue
Long-running operations (compilation, batch operations) use the async path:
1. `task_submit` — queues the operation
2. `task_status` — polls for completion with progress
3. `task_result` — retrieves the result

Falls back to synchronous execution if the plugin doesn't support async.

### Context Injection
Each tool call can optionally inject relevant UE API documentation from `contexts/*.md`. This gives the AI agent reference material for the domain it's working in (Blueprint API, Actor API, Animation API, etc.).

### Fallback Routing
When the editor is not connected, the bridge routes supported operations to the Python binary injector via subprocess. The fallback supports:
- Reading level structure (actors, graphs, properties)
- Adding nodes to Blueprint graphs
- Modifying pin connections and default values
- Patching SSEO bytecode

Operations that require the live editor (compilation, validation, real-time actor manipulation) return a clear error indicating the editor must be running.

## Layer 3: Binary Injector (Python)

The existing level editor tool from the `core/` directory. It uses UAssetAPI (via .NET/Mono) to parse and modify `.umap` and `.uasset` files directly.

### Capabilities (Offline)
- Parse all actors, components, and properties from .umap files
- Read K2Node graphs (Ubergraph bytecode)
- Write new nodes and pin connections
- Patch SSEO (Script Serialized Expression Objects)
- Generate paste text for manual import

### Limitations (vs Live Path)
- No compilation — changes are written to disk but not validated until editor loads
- No real-time feedback — errors surface only when the editor next loads the file
- No actor spawning — can only modify existing actors
- No viewport control — headless operation only

## File Structure

```
AgenticMCP/
  AgenticMCP.uplugin              # Plugin descriptor
  Source/AgenticMCP/
    AgenticMCP.Build.cs           # Module build rules
    Public/
      AgenticMCPEditorSubsystem.h # Editor subsystem (auto-start)
      AgenticMCPServer.h          # HTTP server + all handlers
      AgenticMCPCommandlet.h      # Headless mode
    Private/
      AgenticMCPModule.cpp        # Module startup/shutdown
      AgenticMCPModule.h          # Module header
      AgenticMCPEditorSubsystem.cpp
      AgenticMCPServer.cpp        # Server core + helpers
      Handlers_Read.cpp           # Blueprint query handlers
      Handlers_Mutation.cpp       # Blueprint mutation handlers
      Handlers_Actors.cpp         # Actor management handlers
      Handlers_Level.cpp          # Level management handlers
      Handlers_Validation.cpp     # Validation + snapshot handlers
  Tools/
    package.json                  # Node.js MCP server
    index.js                      # MCP entry point + tool routing
    lib.js                        # HTTP client + schema conversion
    context-loader.js             # UE API docs injection
    fallback.js                   # Offline injector bridge
    contexts/
      actor.md                    # UE Actor API reference
      animation.md                # Animation Blueprint API
      assets.md                   # Asset management API
      blueprint.md                # Blueprint graph API
      character.md                # Character API
      enhanced_input.md           # Enhanced Input API
      material.md                 # Material API
      parallel_workflows.md       # Parallel workflow patterns
      replication.md              # Replication API
      slate.md                    # Slate UI API
  CLAUDE.md                       # AI agent instructions
  README.md                       # Setup and usage guide
  ARCHITECTURE.md                 # This file
```

## Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `UNREAL_MCP_URL` | `http://localhost:9847` | C++ plugin HTTP endpoint |
| `MCP_REQUEST_TIMEOUT_MS` | `30000` | Per-request timeout |
| `MCP_ASYNC_ENABLED` | `true` | Enable async task queue |
| `MCP_ASYNC_TIMEOUT_MS` | `300000` | Async operation timeout |
| `MCP_POLL_INTERVAL_MS` | `2000` | Async poll interval |
| `INJECT_CONTEXT` | `false` | Auto-inject UE API docs |
| `AGENTIC_FALLBACK_ENABLED` | `true` | Enable offline fallback |
| `AGENTIC_PROJECT_ROOT` | (auto-detect) | UE project root for fallback |

## Design Principles

1. **Game thread safety**: All asset manipulation queued to game thread via `TQueue<FPendingRequest>`
2. **Crash safety**: SEH wrappers on Windows for compile/save operations
3. **Idempotent reads**: GET endpoints can run concurrently
4. **Atomic mutations**: Each POST does one logical operation, compiles, and saves
5. **Rich error reporting**: On failure, return available options (pin names, graph names, etc.)
6. **Node GUID tracking**: All node operations return GUIDs for subsequent pin wiring
7. **Dual-path resilience**: If the editor is down, the fallback path keeps working
8. **Auto-discovery**: The bridge adapts to whatever tools the plugin exposes

## Compatibility

- Unreal Engine 5.4 through 5.7+
- Node.js 18+
- Python 3.11+ (for fallback path)
- Works with any MCP client: Claude Code, Claude Desktop, Cursor, Windsurf, Manus
