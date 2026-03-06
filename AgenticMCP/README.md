# AgenticMCP

A dual-path MCP (Model Context Protocol) server for Unreal Engine 5. Gives AI agents direct access to Blueprint manipulation, actor management, and level editing — with an offline fallback when the editor is not running.

## What Makes This Different

Every other UE5 MCP tool requires the editor to be running. AgenticMCP has two execution paths:

1. **Live Editor** — C++ plugin runs inside UE5, exposes the full UEdGraph API over HTTP. Real-time compilation, validation, and the complete tool set.
2. **Offline Fallback** — Python binary injector reads and modifies `.umap` files directly. Works when the editor is closed. Supports reading level data, inspecting actors, and generating Blueprint paste text.

Additionally:
- **Snapshot/Rollback** — Save graph state before destructive operations, restore if something breaks.
- **Validation Endpoint** — Pre-compilation error checking with detailed diagnostics.
- **Auto-Discovery** — The MCP bridge dynamically discovers tools from the C++ plugin. No hardcoded tool lists.
- **Context Injection** — Optionally injects UE5 API documentation into tool responses so the AI agent has reference material.
- **Async Task Queue** — Long-running operations (compilation, batch edits) run asynchronously with progress reporting.

## Architecture

```
AI Agent (Claude, Cursor, Manus, etc.)
    |
    | MCP Protocol (stdio)
    |
Node.js MCP Bridge
    |                   |
    | HTTP              | Python subprocess
    | (editor running)  | (editor closed)
    |                   |
C++ Plugin          Binary Injector
(in-editor)         (offline .umap)
```

## Installation

### 1. C++ Plugin (UE5 Editor)

Copy the plugin into your project:

```
YourProject/
  Plugins/
    AgenticMCP/
      AgenticMCP.uplugin
      Source/
        AgenticMCP/
          ...
```

Regenerate project files and build. The plugin is editor-only and auto-starts when the editor opens.

```bash
# Windows (from project root)
"C:\Program Files\Epic Games\UE_5.x\Engine\Build\BatchFiles\GenerateProjectFiles.bat" YourProject.uproject
```

Verify in the Output Log:

```
AgenticMCP: HTTP server started on port 3000
```

### 2. MCP Bridge (Node.js)

```bash
cd AgenticMCP/Tools
npm install
```

### 3. Configure Your MCP Client

#### Claude Code / Claude Desktop

Add to your MCP config (`claude_desktop_config.json` or project `.mcp.json`):

```json
{
  "mcpServers": {
    "agentic-mcp": {
      "command": "node",
      "args": ["/path/to/AgenticMCP/Tools/index.js"],
      "env": {
        "UNREAL_MCP_URL": "http://localhost:3000",
        "AGENTIC_FALLBACK_ENABLED": "true"
      }
    }
  }
}
```

#### Cursor

Add to `.cursor/mcp.json`:

```json
{
  "mcpServers": {
    "agentic-mcp": {
      "command": "node",
      "args": ["/path/to/AgenticMCP/Tools/index.js"]
    }
  }
}
```

## Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `UNREAL_MCP_URL` | `http://localhost:3000` | C++ plugin HTTP endpoint |
| `MCP_REQUEST_TIMEOUT_MS` | `30000` | Per-request HTTP timeout (ms) |
| `MCP_ASYNC_ENABLED` | `true` | Enable async task queue |
| `MCP_ASYNC_TIMEOUT_MS` | `300000` | Async operation timeout (ms) |
| `MCP_POLL_INTERVAL_MS` | `2000` | Async poll interval (ms) |
| `INJECT_CONTEXT` | `false` | Auto-inject UE API docs in responses |
| `AGENTIC_FALLBACK_ENABLED` | `true` | Enable offline binary injector |
| `AGENTIC_PROJECT_ROOT` | (auto-detect) | UE project root for fallback |

## HTTP API

All live editor endpoints are at `http://localhost:3000/api/<endpoint>`.

### Health and Lifecycle

| Endpoint | Method | Description |
|---|---|---|
| `/api/health` | GET | Server status, asset counts |
| `/mcp/status` | GET | Connection status for MCP bridge |
| `/mcp/tools` | GET | Auto-discovery of all available tools |
| `/mcp/tool/{name}` | POST | Execute a tool by name |

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
| `/api/unload-level` | POST | Unload a sublevel |
| `/api/get-level-blueprint` | POST | Get level blueprint |

### Validation and Safety

| Endpoint | Method | Description |
|---|---|---|
| `/api/validate-blueprint` | POST | Compile and validate (no save) |
| `/api/snapshot-graph` | POST | Take graph snapshot |
| `/api/restore-graph` | POST | Restore from snapshot |

## Supported Node Types

The `add-node` endpoint supports:

| nodeType | Required Fields | Description |
|---|---|---|
| `CallFunction` | `functionName`, optional `className` | Call a UFunction |
| `CustomEvent` | `eventName` | Custom event |
| `OverrideEvent` | `functionName` | Override parent event (BeginPlay, Tick, etc.) |
| `BreakStruct` | `typeName` | Break a struct |
| `MakeStruct` | `typeName` | Make a struct |
| `VariableGet` | `variableName` | Get variable |
| `VariableSet` | `variableName` | Set variable |
| `DynamicCast` | `castTarget` | Cast to class |
| `Branch` | — | If/else |
| `Sequence` | — | Execution sequence |
| `MacroInstance` | `macroName`, optional `macroSource` | Generic macro (ForLoop, ForEachLoop, etc.) |
| `SpawnActorFromClass` | optional `actorClass` | Spawn actor |
| `Select` | — | Select node |
| `Comment` | optional `comment`, `width`, `height` | Comment box |
| `Reroute` | — | Reroute/knot |
| `MultiGate` | optional `numOutputs` | Multi-output execution gate |
| `Delay` | optional `duration` | Timed delay |
| `SetTimer` | `functionName`, optional `time` | Timer by function name |
| `LoadAsset` | — | Async asset loading |

## Offline Fallback Tools

When the editor is not running, these tools are available:

| Tool | Description |
|------|-------------|
| `offline_list_levels` | List all `.umap` files in project |
| `offline_list_actors` | List actors in a `.umap` file |
| `offline_get_actor` | Get actor properties |
| `offline_get_graph` | Get Blueprint graph data |
| `offline_level_info` | Get level summary |
| `offline_generate_paste_text` | Generate Blueprint paste text |

## Safety

- **Snapshots**: Always take a snapshot before making destructive changes. Max 50 snapshots in memory, oldest pruned automatically.
- **Validation**: Always compile after mutations to catch errors.
- **SEH Protection**: On Windows, compilation is wrapped in SEH handlers to prevent editor crashes.
- **Game Thread**: All mutations run on the game thread via `AsyncTask(ENamedThreads::GameThread, ...)`.
- **Dual-Path Resilience**: If the editor goes down, the fallback path keeps working.

## Testing

```bash
cd AgenticMCP/Tools
npm test              # Run all tests
npm run test:watch    # Watch mode
npm run test:coverage # Coverage report
```

## Compatibility

- Unreal Engine 5.4 through 5.7+
- Node.js 18+
- Python 3.11+ (for offline fallback)
- Works with: Claude Code, Claude Desktop, Cursor, Windsurf, Manus

## Credits

MCP bridge architecture based on [Natfii/ue5-mcp-bridge](https://github.com/Natfii/unrealclaude-mcp-bridge) (MIT).
UE API context documentation adapted from the same project.

## License

MIT
