# ManusMCP — Architecture Design

## Overview

ManusMCP is a UE5.6 editor plugin that exposes Blueprint manipulation, actor management, and level editing capabilities over HTTP, wrapped in an MCP (Model Context Protocol) server for AI-driven automation.

## Architecture

```
MCP Client (Manus / Claude / any AI)
    ↓ stdio (MCP protocol)
TypeScript MCP Server (Tools/dist/index.js)
    ↓ HTTP calls to localhost:9847
C++ HTTP Backend (ManusMCPServer.cpp inside UE5 editor)
    ↓ UE5 Engine APIs (UEdGraph, UBlueprint, UWorld, etc.)
.uasset / .umap files
```

## Two Serving Modes

1. **Editor Subsystem** (preferred): Auto-starts when UE5 editor opens. Zero overhead. Uses `UEditorSubsystem` + `FTickableEditorObject` to process requests on the game thread.
2. **Standalone Commandlet**: Spawns headless `UnrealEditor-Cmd.exe` for CI/CD or when editor is not open.

## C++ Plugin Structure

```
ManusMCP/
├── ManusMCP.uplugin
├── Source/
│   └── ManusMCP/
│       ├── ManusMCP.Build.cs
│       ├── Public/
│       │   ├── ManusMCPServer.h
│       │   ├── ManusMCPEditorSubsystem.h
│       │   └── ManusMCPCommandlet.h
│       └── Private/
│           ├── ManusMCPModule.cpp
│           ├── ManusMCPModule.h
│           ├── ManusMCPServer.cpp          // HTTP server + route dispatch + helpers
│           ├── ManusMCPEditorSubsystem.cpp
│           ├── ManusMCPCommandlet.cpp
│           ├── Handlers_Read.cpp           // Blueprint read operations
│           ├── Handlers_Mutation.cpp       // Blueprint write operations (add node, connect, etc.)
│           ├── Handlers_Actors.cpp         // Actor placement and property manipulation
│           ├── Handlers_Level.cpp          // Level/sublevel management
│           └── Handlers_Validation.cpp     // Compile, validate, snapshot
├── Tools/
│   ├── package.json
│   ├── tsconfig.json
│   └── src/
│       ├── index.ts                        // MCP server entry point
│       ├── ue-bridge.ts                    // HTTP bridge to C++ backend
│       ├── helpers.ts                      // Formatting utilities
│       └── tools/
│           ├── read.ts                     // Blueprint read tools
│           ├── mutation.ts                 // Blueprint mutation tools
│           ├── actors.ts                   // Actor management tools
│           ├── level.ts                    // Level management tools
│           └── validation.ts              // Validation tools
├── CLAUDE.md
└── README.md
```

## HTTP Endpoints

### Health and Lifecycle
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | /api/health | Server status, asset counts |
| POST | /api/shutdown | Graceful shutdown (commandlet only) |
| POST | /api/rescan | Re-scan asset registry |

### Blueprint Read (GET, game thread queued)
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

### Blueprint Mutation (POST, game thread queued)
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

### Actor Management (POST, game thread queued)
| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | /api/list-actors | List actors in a level |
| POST | /api/get-actor | Get actor properties |
| POST | /api/spawn-actor | Spawn an actor in a level |
| POST | /api/delete-actor | Remove an actor from a level |
| POST | /api/set-actor-property | Set a property on an actor |
| POST | /api/set-actor-transform | Set actor location/rotation/scale |

### Level Management (POST, game thread queued)
| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | /api/list-levels | List all loaded levels/sublevels |
| POST | /api/load-level | Load a sublevel |
| POST | /api/unload-level | Unload a sublevel |
| POST | /api/get-level-blueprint | Get the level script Blueprint |

### Validation and Safety (POST, game thread queued)
| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | /api/validate-blueprint | Compile without saving, report errors |
| POST | /api/snapshot-graph | Save a snapshot of graph state |
| POST | /api/restore-graph | Restore graph from snapshot |
| POST | /api/get-pin-info | Introspect pin types and connections |

## Supported Node Types for add-node

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
| MacroInstance | macroName, macroSource? | Instantiate a macro (ForLoop, ForEachLoop, etc.) |
| SpawnActorFromClass | actorClass? | Spawn actor node |
| Select | (none) | Select node |
| Comment | comment?, width?, height? | Comment box |
| Reroute | (none) | Reroute/knot node |
| MultiGate | numOutputs? | MultiGate execution node |
| Delay | duration? | Delay node |
| SetTimer | functionName, time? | Set Timer by Function Name |
| LoadAsset | (none) | Async Load Asset |

## Design Principles

1. **Game thread safety**: All asset manipulation queued to game thread via `TQueue<FPendingRequest>`
2. **Crash safety**: SEH wrappers on Windows for compile/save operations
3. **Idempotent reads**: GET endpoints can run concurrently
4. **Atomic mutations**: Each POST does one logical operation, compiles, and saves
5. **Rich error reporting**: On failure, return available options (pin names, graph names, etc.)
6. **Node GUID tracking**: All node operations return GUIDs for subsequent pin wiring
