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
AgenticMCP: HTTP server started on port 9847 (editor mode)
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
        "UNREAL_MCP_URL": "http://localhost:9847",
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
| `UNREAL_MCP_URL` | `http://localhost:9847` | C++ plugin HTTP endpoint |
| `MCP_REQUEST_TIMEOUT_MS` | `30000` | Per-request HTTP timeout (ms) |
| `MCP_ASYNC_ENABLED` | `true` | Enable async task queue |
| `MCP_ASYNC_TIMEOUT_MS` | `300000` | Async operation timeout (ms) |
| `MCP_POLL_INTERVAL_MS` | `2000` | Async poll interval (ms) |
| `INJECT_CONTEXT` | `false` | Auto-inject UE API docs in responses |
| `AGENTIC_FALLBACK_ENABLED` | `true` | Enable offline binary injector |
| `AGENTIC_PROJECT_ROOT` | (auto-detect) | UE project root for fallback |

## HTTP API

All live editor endpoints are at `http://localhost:9847/api/<endpoint>`.

> **Note**: Port 9847 is the default for the editor plugin. Port 3000 is reserved for DevmateMCP.

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

### Visual Agent / Automation

| Endpoint | Method | Description |
|---|---|---|
| `/api/screenshot` | POST | Capture viewport screenshot (base64 JPEG) |
| `/api/scene-snapshot` | POST | Get hierarchical scene tree with short refs |
| `/api/focus-actor` | POST | Move editor camera to focus on an actor |
| `/api/select-actor` | POST | Select actor(s) in the editor |
| `/api/set-viewport` | POST | Set camera position and rotation |
| `/api/move-actor` | POST | Move/transform an actor |
| `/api/get-camera` | POST | Get current viewport camera position, rotation, FOV |
| `/api/list-viewports` | POST | List all editor viewports with positions and types |
| `/api/get-selection` | POST | Get currently selected actors with transforms |
| `/api/wait-ready` | POST | Wait for assets/compile/render to complete |
| `/api/resolve-ref` | POST | Resolve a short ref (a0, a1.c0) to actor/component |
| `/api/draw-debug` | POST | Draw debug shapes in viewport |
| `/api/clear-debug` | POST | Clear debug drawings |

#### get-camera

Returns the current editor viewport camera position, rotation, and settings.

**Request:** `POST /api/get-camera`
```json
{}
```

**Response:**
```json
{
  "success": true,
  "x": -888.39,
  "y": 583.01,
  "z": 237.47,
  "pitch": -3.2,
  "yaw": -20.2,
  "roll": 0,
  "fov": 90,
  "viewMode": "Static"
}
```

#### list-viewports

Returns all available editor viewports with their positions and view types.

**Request:** `POST /api/list-viewports`
```json
{}
```

**Response:**
```json
{
  "success": true,
  "count": 4,
  "viewports": [
    {
      "index": 0,
      "isActive": false,
      "isRealtime": false,
      "x": -1215.38,
      "y": 18.71,
      "z": 285.17,
      "pitch": 0,
      "yaw": 0,
      "roll": 0,
      "fov": 90,
      "viewType": "Right"
    },
    {
      "index": 1,
      "isActive": true,
      "isRealtime": false,
      "viewType": "Perspective"
    }
  ]
}
```

#### get-selection

Returns the currently selected actors in the editor with their transforms.

**Request:** `POST /api/get-selection`
```json
{}
```

**Response:**
```json
{
  "success": true,
  "count": 1,
  "selected": [
    {
      "name": "1M_Cube10_0",
      "label": "Table",
      "class": "StaticMeshActor",
      "x": 193.41,
      "y": 262.10,
      "z": 0,
      "pitch": 0,
      "yaw": 50.0,
      "roll": 0
    }
  ]
}
```

### Validation and Safety

| Endpoint | Method | Description |
|---|---|---|
| `/api/validate-blueprint` | POST | Compile and validate (no save) |
| `/api/snapshot-graph` | POST | Take graph snapshot |
| `/api/restore-graph` | POST | Restore from snapshot |

### PIE Control (Play-In-Editor)

| Endpoint | Method | Description |
|---|---|---|
| `/api/start-pie` | POST | Start a PIE session (modes: SelectedViewport, NewEditorWindow, VR, MobilePreview) |
| `/api/stop-pie` | POST | Stop the current PIE session |
| `/api/pause-pie` | POST | Pause/resume the PIE session |
| `/api/step-pie` | POST | Single-step the paused PIE session |
| `/api/get-pie-state` | POST | Get PIE state (isRunning, isPaused, timeSeconds) |

### Console Commands

| Endpoint | Method | Description |
|---|---|---|
| `/api/execute-console` | POST | Execute a console command |
| `/api/get-cvar` | POST | Get a console variable value |
| `/api/set-cvar` | POST | Set a console variable value |

### Audio System

| Endpoint | Method | Description |
|---|---|---|
| `/api/audio/status` | GET | Get audio system status |
| `/api/audio/active-sounds` | GET | List currently playing sounds |
| `/api/audio/device-info` | GET | Get audio device information |
| `/api/audio/sound-classes` | GET | List sound classes |
| `/api/audio/set-volume` | POST | Set volume for a sound class |
| `/api/audio/stats` | GET | Get audio statistics |
| `/api/audio/play` | POST | Play a sound at location |
| `/api/audio/stop` | POST | Stop a playing sound |
| `/api/audio/set-listener` | POST | Set listener position/rotation |
| `/api/audio/debug-visualize` | POST | Toggle audio debug visualization |

### Niagara Particle Systems

| Endpoint | Method | Description |
|---|---|---|
| `/api/niagara/status` | GET | Get Niagara system status |
| `/api/niagara/systems` | GET | List active Niagara systems in the world |
| `/api/niagara/system-info` | POST | Get detailed info about a Niagara system |
| `/api/niagara/emitters` | POST | Get emitters for a Niagara component |
| `/api/niagara/set-parameter` | POST | Set a Niagara parameter value |
| `/api/niagara/get-parameters` | POST | Get all parameters for a Niagara component |
| `/api/niagara/activate` | POST | Activate a Niagara system |
| `/api/niagara/set-emitter-enable` | POST | Enable/disable a specific emitter |
| `/api/niagara/reset` | POST | Reset a Niagara system |
| `/api/niagara/stats` | GET | Get Niagara performance statistics |
| `/api/niagara/debug-hud` | POST | Toggle Niagara debug HUD |

### Pixel Streaming

| Endpoint | Method | Description |
|---|---|---|
| `/api/pixelstreaming/status` | GET | Get Pixel Streaming status |
| `/api/pixelstreaming/start` | POST | Start Pixel Streaming |
| `/api/pixelstreaming/stop` | POST | Stop Pixel Streaming |
| `/api/pixelstreaming/streamers` | GET | List active streamers |
| `/api/pixelstreaming/codec` | GET | Get current codec settings |
| `/api/pixelstreaming/set-codec` | POST | Set codec settings |
| `/api/pixelstreaming/players` | GET | List connected players |

### Level Sequences

### PIE Control (Play-In-Editor)

| Endpoint | Method | Description |
|---|---|---|
| `/api/start-pie` | POST | Start a PIE session (modes: SelectedViewport, NewEditorWindow, VR, MobilePreview) |
| `/api/stop-pie` | POST | Stop the current PIE session |
| `/api/pause-pie` | POST | Pause/resume the PIE session |
| `/api/step-pie` | POST | Single-step the paused PIE session |
| `/api/get-pie-state` | POST | Get PIE state (isRunning, isPaused, timeSeconds) |

### Console Commands

| Endpoint | Method | Description |
|---|---|---|
| `/api/execute-console` | POST | Execute a console command |
| `/api/get-cvar` | POST | Get a console variable value |
| `/api/set-cvar` | POST | Set a console variable value |

### Audio System

| Endpoint | Method | Description |
|---|---|---|
| `/api/audio/status` | GET | Get audio system status |
| `/api/audio/active-sounds` | GET | List currently playing sounds |
| `/api/audio/device-info` | GET | Get audio device information |
| `/api/audio/sound-classes` | GET | List sound classes |
| `/api/audio/set-volume` | POST | Set volume for a sound class |
| `/api/audio/stats` | GET | Get audio statistics |
| `/api/audio/play` | POST | Play a sound at location |
| `/api/audio/stop` | POST | Stop a playing sound |
| `/api/audio/set-listener` | POST | Set listener position/rotation |
| `/api/audio/debug-visualize` | POST | Toggle audio debug visualization |

### Niagara Particle Systems

| Endpoint | Method | Description |
|---|---|---|
| `/api/niagara/status` | GET | Get Niagara system status |
| `/api/niagara/systems` | GET | List active Niagara systems in the world |
| `/api/niagara/system-info` | POST | Get detailed info about a Niagara system |
| `/api/niagara/emitters` | POST | Get emitters for a Niagara component |
| `/api/niagara/set-parameter` | POST | Set a Niagara parameter value |
| `/api/niagara/get-parameters` | POST | Get all parameters for a Niagara component |
| `/api/niagara/activate` | POST | Activate a Niagara system |
| `/api/niagara/set-emitter-enable` | POST | Enable/disable a specific emitter |
| `/api/niagara/reset` | POST | Reset a Niagara system |
| `/api/niagara/stats` | GET | Get Niagara performance statistics |
| `/api/niagara/debug-hud` | POST | Toggle Niagara debug HUD |

### Pixel Streaming

| Endpoint | Method | Description |
|---|---|---|
| `/api/pixelstreaming/status` | GET | Get Pixel Streaming status |
| `/api/pixelstreaming/start` | POST | Start Pixel Streaming |
| `/api/pixelstreaming/stop` | POST | Stop Pixel Streaming |
| `/api/pixelstreaming/streamers` | GET | List active streamers |
| `/api/pixelstreaming/codec` | GET | Get current codec settings |
| `/api/pixelstreaming/set-codec` | POST | Set codec settings |
| `/api/pixelstreaming/players` | GET | List connected players |

### Level Sequences

| Endpoint | Method | Description |
|---|---|---|
| `/api/list-sequences` | POST | List all LevelSequenceActors in loaded levels |
| `/api/read-sequence` | POST | Read sequence tracks, audio cues, timing data |
| `/api/remove-audio-tracks` | POST | Remove audio tracks from sequences (music cleanup) |

#### list-sequences

Lists all Level Sequence actors across all loaded levels with timing information.

**Request:** `POST /api/list-sequences`
```json
{}
```

**Response:**
```json
{
  "success": true,
  "count": 3,
  "sequences": [
    {
      "actorName": "LevelSequenceActor_0",
      "actorLabel": "LS_1_1",
      "level": "SL_Trailer_Logic",
      "sequenceName": "LS_1_1",
      "sequencePath": "/Game/Sequences/Scene1/LS_1_1.LS_1_1",
      "startTime": 0,
      "endTime": 45.5,
      "duration": 45.5,
      "frameRate": 30,
      "trackCount": 5
    }
  ]
}
```

#### read-sequence

Reads detailed track information from a Level Sequence including audio cues and bindings.

**Request:** `POST /api/read-sequence`
```json
{
  "sequencePath": "/Game/Sequences/Scene1/LS_1_1",
  // OR
  "actorName": "LS_1_1"
}
```

**Response:**
```json
{
  "success": true,
  "sequenceName": "LS_1_1",
  "duration": 45.5,
  "frameRate": 30,
  "masterTracks": [
    {
      "trackName": "Audio",
      "trackClass": "MovieSceneAudioTrack",
      "sections": [
        {
          "type": "Audio",
          "startTime": 0,
          "endTime": 12.5,
          "soundName": "VO_Susan_Intro",
          "soundPath": "/Game/Sounds/VO/VO_Susan_Intro",
          "soundDuration": 12.5
        }
      ]
    }
  ],
  "objectBindings": [
    {
      "name": "BP_HeatherChild",
      "trackCount": 3
    }
  ]
}
```

#### remove-audio-tracks

Removes audio tracks from sequences. Useful for cleaning up music so it can be handled separately.

**Request:** `POST /api/remove-audio-tracks`
```json
{
  "sequencePath": "/Game/Sequences/Scene1/LS_1_1",
  // OR remove from all sequences:
  "all": true,
  // Optional: only remove music, keep VO
  "musicOnly": true
}
```

**Response:**
```json
{
  "success": true,
  "totalTracksRemoved": 15,
  "sequencesModified": 8,
  "modifiedSequences": [
    {"name": "LS_1_1", "tracksRemoved": 2}
  ],
  "note": "Changes are in memory. Save modified sequences to persist."
}
```

### Audio Analysis (External Service)

A separate Python service provides audio transcription for subtitle generation.

**Setup:**
```bash
cd AgenticMCP/Tools/AudioAnalysis
pip install -r requirements.txt
python transcribe.py --serve --port 9848
```

| Endpoint | Method | Description |
|---|---|---|
| `http://localhost:9848/health` | GET | Service health check |
| `http://localhost:9848/transcribe` | POST | Transcribe single audio file |
| `http://localhost:9848/batch-transcribe` | POST | Transcribe multiple audio files |

#### Transcribe Single File

**Request:** `POST http://localhost:9848/transcribe`
```json
{
  "audioPath": "C:/Project/Content/Sounds/VO/VO_Scene1.wav",
  "model": "base",
  "outputFormat": "ue_datatable",
  "sequenceName": "LS_1_1"
}
```

**Response:**
```json
{
  "success": true,
  "format": "ue_datatable",
  "rows": [
    {
      "RowName": "Line_001",
      "Time": 0.0,
      "Duration": 3.5,
      "Speaker": "",
      "Text": "Welcome to Susan's home.",
      "SequenceName": "LS_1_1"
    }
  ]
}
```

#### Batch Transcribe (All VO Files)

**Request:** `POST http://localhost:9848/batch-transcribe`
```json
{
  "audioFiles": [
    {"path": "C:/Project/Content/Sounds/VO/VO_1_1.wav", "sequenceName": "LS_1_1"},
    {"path": "C:/Project/Content/Sounds/VO/VO_1_2.wav", "sequenceName": "LS_1_2"}
  ],
  "model": "base",
  "outputFormat": "ue_datatable"
}
```

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
