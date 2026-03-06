# ManusMCP

A custom UE5.6 editor plugin that exposes Blueprint manipulation, actor management, and level editing via HTTP and MCP (Model Context Protocol). Built for direct AI-driven Blueprint authoring inside the running editor.

## Overview

ManusMCP consists of two components:

1. **C++ Editor Plugin** — Runs inside the UE5 editor as an `UEditorSubsystem`. Starts an HTTP server on port 9847 that processes Blueprint graph mutations, actor operations, and level management on the game thread.

2. **TypeScript MCP Server** — Wraps the HTTP API as MCP tools. Connects to any MCP-compatible AI client (Claude Desktop, Cursor, etc.) via stdio.

## Installation

### 1. Copy the Plugin

Copy the `ManusMCP/` folder into your project's `Plugins/` directory:

```
YourProject/
  Plugins/
    ManusMCP/
      ManusMCP.uplugin
      Source/
        ManusMCP/
          ...
```

### 2. Regenerate Project Files

```bash
# Windows (from project root)
"C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\GenerateProjectFiles.bat" YourProject.uproject

# Or right-click the .uproject file -> Generate Visual Studio project files
```

### 3. Build

Open the `.sln` in Visual Studio / Rider and build (Development Editor). The plugin is editor-only — it will not be included in packaged builds.

### 4. Verify Plugin is Loaded

Open UE5 Editor. Go to **Edit -> Plugins**, search for "ManusMCP". It should be enabled by default. Check the Output Log for:

```
ManusMCP: HTTP server started on port 9847
```

### 5. Install MCP Tools

```bash
cd ManusMCP/Tools
npm install
npm run build
```

### 6. Configure MCP Client

Add to your MCP client config (e.g. `claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "manus-mcp": {
      "command": "node",
      "args": ["path/to/ManusMCP/Tools/dist/index.js"],
      "env": {}
    }
  }
}
```

Or for development:

```json
{
  "mcpServers": {
    "manus-mcp": {
      "command": "npx",
      "args": ["tsx", "path/to/ManusMCP/Tools/src/index.ts"],
      "env": {}
    }
  }
}
```

## HTTP API

All endpoints are at `http://localhost:9847/api/<endpoint>`. POST with JSON body.

### Health Check
```
GET /api/health
```

### Blueprint Read
| Endpoint | Method | Description |
|---|---|---|
| `/api/list` | GET | List Blueprints and Maps |
| `/api/blueprint` | GET | Get Blueprint details |
| `/api/graph` | GET | Get graph nodes and connections |
| `/api/search` | GET | Search assets by name |
| `/api/references` | GET | Find asset references |
| `/api/list-classes` | POST | List UClasses |
| `/api/list-functions` | POST | List UFunctions on a class |
| `/api/list-properties` | POST | List UProperties on a class |
| `/api/get-pin-info` | POST | Get pin details |
| `/api/rescan` | GET | Force asset registry refresh |

### Blueprint Mutation
| Endpoint | Method | Description |
|---|---|---|
| `/api/add-node` | POST | Add a node to a graph |
| `/api/delete-node` | POST | Delete a node |
| `/api/connect-pins` | POST | Connect two pins |
| `/api/disconnect-pin` | POST | Break pin connections |
| `/api/set-pin-default` | POST | Set pin default value |
| `/api/move-node` | POST | Move a node |
| `/api/refresh-all-nodes` | POST | Refresh all nodes |
| `/api/create-blueprint` | POST | Create new Blueprint |
| `/api/create-graph` | POST | Create function/macro graph |
| `/api/delete-graph` | POST | Delete a graph |
| `/api/add-variable` | POST | Add a variable |
| `/api/remove-variable` | POST | Remove a variable |
| `/api/compile-blueprint` | POST | Compile and save |
| `/api/set-node-comment` | POST | Set node comment |

### Actor Management
| Endpoint | Method | Description |
|---|---|---|
| `/api/list-actors` | POST | List actors in world |
| `/api/get-actor` | POST | Get actor details |
| `/api/spawn-actor` | POST | Spawn a new actor |
| `/api/delete-actor` | POST | Delete an actor |
| `/api/set-actor-property` | POST | Set actor property |
| `/api/set-actor-transform` | POST | Set actor transform |

### Level Management
| Endpoint | Method | Description |
|---|---|---|
| `/api/list-levels` | POST | List levels |
| `/api/load-level` | POST | Load a sublevel |
| `/api/get-level-blueprint` | POST | Get level blueprint |

### Validation
| Endpoint | Method | Description |
|---|---|---|
| `/api/validate-blueprint` | POST | Compile and validate |
| `/api/snapshot-graph` | POST | Take graph snapshot |
| `/api/restore-graph` | POST | Restore from snapshot |

## Configuration

The HTTP server port defaults to **9847**. To change it:

**C++ side**: Modify `DEFAULT_PORT` in `ManusMCPServer.h`

**TypeScript side**: Pass `--port <number>` when starting the MCP server

## Supported Node Types

The `add-node` endpoint supports:

| nodeType | Required Fields | Description |
|---|---|---|
| `CallFunction` | `functionName`, optional `className` | Call a UFunction |
| `VariableGet` | `variableName` | Get variable node |
| `VariableSet` | `variableName` | Set variable node |
| `BreakStruct` | `typeName` | Break a struct |
| `MakeStruct` | `typeName` | Make a struct |
| `DynamicCast` | `castTarget` | Cast to class |
| `OverrideEvent` | `functionName` | Override parent event |
| `CustomEvent` | `eventName` | Custom event |
| `Branch` | — | If/else |
| `Sequence` | — | Execution sequence |
| `ForLoop` | — | For loop |
| `ForEachLoop` | — | For each loop |
| `WhileLoop` | — | While loop |
| `ForLoopWithBreak` | — | For loop with break |
| `SpawnActorFromClass` | optional `actorClass` | Spawn actor |
| `Select` | — | Select node |
| `Comment` | optional `comment`, `width`, `height` | Comment box |
| `Reroute` | — | Reroute/knot |
| `MacroInstance` | `macroName`, optional `macroSource` | Generic macro |
| `CallParentFunction` | `functionName` | Call parent impl |

## Safety

- **Snapshots**: Always take a snapshot before making destructive changes
- **Validation**: Always compile after mutations to catch errors
- **SEH Protection**: On Windows, compilation is wrapped in SEH handlers to prevent editor crashes
- **Game Thread**: All mutations run on the game thread via `AsyncTask(ENamedThreads::GameThread, ...)`
- **Max Snapshots**: 50 snapshots stored in memory, oldest pruned automatically

## License

Proprietary. Built for SOH VR production pipeline.
