# VisualAgent Architecture

> Playwright-style visual automation for Unreal Engine 5

VisualAgent is a unified CLI subsystem that enables AI agents to visually understand and manipulate Unreal Engine scenes. It provides accessibility-tree-style scene snapshots, on-demand viewport screenshots, debug visualization, and comprehensive scene state management.

---

## Table of Contents

1. [Design Philosophy](#design-philosophy)
2. [System Architecture](#system-architecture)
3. [Component Overview](#component-overview)
4. [Data Flow](#data-flow)
5. [C++ Backend Handlers](#c-backend-handlers)
6. [Node.js CLI Layer](#nodejs-cli-layer)
7. [Reference System](#reference-system)
8. [Feature Deep Dives](#feature-deep-dives)
9. [API Reference](#api-reference)
10. [Performance Considerations](#performance-considerations)

---

## Design Philosophy

VisualAgent is inspired by Playwright's approach to web automation:

| Playwright (Web) | VisualAgent (UE5) |
|------------------|-------------------|
| Accessibility tree | Scene snapshot with refs |
| `page.screenshot()` | `screenshot` command |
| CSS selectors | Short refs (`a0`, `a1.c0`) |
| `page.click()`, `page.fill()` | `spawn`, `move`, `rotate`, `delete` |
| Trace viewer | Debug visualization |
| Network HAR | Diff/Compare mode |

### Core Principles

1. **GPU-Friendly**: Screenshots are off by default, captured only when explicitly requested
2. **Accessibility-Tree Style**: Scene hierarchy uses short refs like `a0`, `a1.c0` for efficient AI context
3. **Single Tool Interface**: One unified `cli` tool with rich command vocabulary (not dozens of separate tools)
4. **Dual Feedback**: Both structured JSON and human-readable YAML snapshots
5. **State Awareness**: Diff/Compare mode tracks scene changes over time
6. **Undo Safety**: Transaction grouping for atomic operations

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        AI Agent (Claude, Cursor, Manus)                  │
└────────────────────────────────────┬────────────────────────────────────┘
                                     │ MCP Protocol (stdio)
                                     │
┌────────────────────────────────────▼────────────────────────────────────┐
│                     Layer 1: MCP Bridge (Node.js)                        │
│                                                                          │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                    visual-agent.js                               │    │
│  │                                                                  │    │
│  │  • Command Parser (flags, quoted args, short refs)              │    │
│  │  • Auto-Screenshot Mode (toggle for visual actions)             │    │
│  │  • Recording System (capture/replay sessions)                   │    │
│  │  • 40+ Commands (snapshot, spawn, move, draw-sphere, etc.)      │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                     │                                    │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                      index.js                                    │    │
│  │                                                                  │    │
│  │  • Tool Registration (unreal_cli)                               │    │
│  │  • HTTP Client Wrapper                                          │    │
│  │  • Response Formatting (text + image + snapshot)                │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└────────────────────────────────────┬────────────────────────────────────┘
                                     │ HTTP (localhost:3000)
                                     │
┌────────────────────────────────────▼────────────────────────────────────┐
│                   Layer 2: C++ Plugin (Unreal Engine)                    │
│                                                                          │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                  Handlers_VisualAgent.cpp                        │    │
│  │                                                                  │    │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │    │
│  │  │ Scene       │  │ Screenshot  │  │ Debug Visualization     │  │    │
│  │  │ Snapshot    │  │ Capture     │  │ (sphere, line, text)    │  │    │
│  │  └─────────────┘  └─────────────┘  └─────────────────────────┘  │    │
│  │                                                                  │    │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │    │
│  │  │ Blueprint   │  │ Undo/Redo   │  │ Diff/Compare            │  │    │
│  │  │ Snapshot    │  │ Transactions│  │ (save, diff, restore)   │  │    │
│  │  └─────────────┘  └─────────────┘  └─────────────────────────┘  │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                          │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                    Ref Registry (Static)                         │    │
│  │                                                                  │    │
│  │  TMap<FString, TWeakObjectPtr<UObject>> RefRegistry              │    │
│  │  • Persists across requests within session                       │    │
│  │  • Cleared on each sceneSnapshot call                            │    │
│  │  • Maps "a0" → Actor, "a0.c0" → Component                        │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Component Overview

### Node.js Layer (`AgenticMCP/Tools/`)

| File | Purpose |
|------|---------|
| `visual-agent.js` | Unified CLI with 40+ commands, command parser, recording system |
| `index.js` | MCP server, tool registration, HTTP routing |
| `lib.js` | HTTP client, timeout handling, logging |

### C++ Layer (`AgenticMCP/Source/AgenticMCP/`)

| File | Purpose |
|------|---------|
| `Handlers_VisualAgent.cpp` | All VisualAgent endpoint handlers (~1300 lines) |
| `AgenticMCPServer.h` | Handler declarations |
| `AgenticMCPServer.cpp` | Handler registration in `HandlerMap` |

---

## Data Flow

### Scene Snapshot Flow

```
1. AI sends: "snapshot"
              │
              ▼
2. visual-agent.js parses command
              │
              ▼
3. HTTP POST to localhost:3000/api/sceneSnapshot
              │
              ▼
4. HandleSceneSnapshot() in C++:
   a. Clear RefRegistry
   b. Iterate all actors in world
   c. Build parent→children hierarchy map
   d. Assign refs: actors get a0..aN, components get a0.c0..a0.cN
   e. Build JSON + YAML snapshot
              │
              ▼
5. Response contains:
   {
     "success": true,
     "actorCount": 47,
     "actors": [...],          // Full JSON hierarchy
     "yamlSnapshot": "..."     // Human-readable YAML
   }
              │
              ▼
6. AI receives both structured data and readable snapshot
```

### Command Execution Flow

```
1. AI sends: "move a0 100 200 300"
              │
              ▼
2. parseCommand() extracts:
   { action: "move", args: ["a0", "100", "200", "300"], flags: {} }
              │
              ▼
3. executeMove() builds HTTP request:
   POST /api/setActorTransform
   { "name": "a0", "locationX": 100, "locationY": 200, "locationZ": 300 }
              │
              ▼
4. C++ handler resolves "a0" via RefRegistry → AActor*
              │
              ▼
5. Actor->SetActorLocation(FVector(100, 200, 300))
              │
              ▼
6. If auto-screenshot enabled: capture viewport
              │
              ▼
7. Append scene snapshot for context
              │
              ▼
8. Return combined response to AI
```

---

## C++ Backend Handlers

All handlers are in `Handlers_VisualAgent.cpp`:

### Core Handlers

| Handler | Endpoint | Purpose |
|---------|----------|---------|
| `HandleSceneSnapshot` | `/api/scene-snapshot` | Build hierarchical scene tree with refs |
| `HandleScreenshot` | `/api/screenshot` | Capture viewport using UE's HighRes system |
| `HandleFocusActor` | `/api/focus-actor` | Move editor camera to focus on actor |
| `HandleSelectActor` | `/api/select-actor` | Select actor(s) in outliner |
| `HandleSetViewport` | `/api/set-viewport` | Set camera position/rotation |
| `HandleWaitReady` | `/api/wait-ready` | Wait for assets/compile/render |
| `HandleResolveRef` | `/api/resolve-ref` | Resolve short ref to actor name |

### Debug Visualization Handlers

| Handler | Endpoint | Purpose |
|---------|----------|---------|
| `HandleDrawDebug` | `/api/draw-debug` | Draw sphere/line/text/box |
| `HandleClearDebug` | `/api/clear-debug` | Clear all debug draws |

### Blueprint Inspection

| Handler | Endpoint | Purpose |
|---------|----------|---------|
| `HandleBlueprintSnapshot` | `/api/blueprint-snapshot` | Get BP graph as YAML |

### Undo/Redo Transactions

| Handler | Endpoint | Purpose |
|---------|----------|---------|
| `HandleBeginTransaction` | `/api/begin-transaction` | Start undo group |
| `HandleEndTransaction` | `/api/end-transaction` | Commit transaction |
| `HandleUndo` | `/api/undo` | Undo operation(s) |
| `HandleRedo` | `/api/redo` | Redo operation(s) |

### Diff/Compare Mode

| Handler | Endpoint | Purpose |
|---------|----------|---------|
| `HandleSaveState` | `/api/save-state` | Snapshot scene state |
| `HandleDiffState` | `/api/diff-state` | Compare current to saved |
| `HandleRestoreState` | `/api/restore-state` | Restore to saved state |
| `HandleListStates` | `/api/list-states` | List all snapshots |

---

## Node.js CLI Layer

### Command Parser

The parser in `visual-agent.js` handles:

```javascript
// Simple command
parseCommand("screenshot")
// → { action: "screenshot", args: [], flags: {} }

// Command with args
parseCommand("move a0 100 200 300")
// → { action: "move", args: ["a0", "100", "200", "300"], flags: {} }

// Command with flags
parseCommand("screenshot --format=png --width=1920")
// → { action: "screenshot", args: [], flags: { format: "png", width: "1920" } }

// Quoted arguments
parseCommand('spawn StaticMeshActor 0 0 0 --label="My Actor"')
// → { action: "spawn", args: ["StaticMeshActor", "0", "0", "0"], flags: { label: "My Actor" } }
```

### Auto-Screenshot Mode

```javascript
let autoScreenshotEnabled = false;
const VISUAL_ACTIONS = new Set(["spawn", "move", "rotate", "delete", "camera", "focus", "navigate"]);

// When enabled, visual actions automatically capture viewport
if (autoScreenshotEnabled && VISUAL_ACTIONS.has(action)) {
  result._screenshot = await captureScreenshot();
}
```

### Recording System

```javascript
const recording = {
  active: false,
  actions: [],           // Array of { command, timestamp, success }
  startTime: null,
  savedSessions: Map(),  // name → session
};

// Commands: record start|stop|save|load|list|play
```

---

## Reference System

### Ref Assignment

During `sceneSnapshot`:

```cpp
// Actors get sequential refs: a0, a1, a2, ...
FString RegisterRef(UObject* Obj, const FString& ParentRef = TEXT(""))
{
    if (ParentRef.IsEmpty()) {
        // Root actor
        return FString::Printf(TEXT("a%d"), RefCounter++);  // "a0", "a1", ...
    } else {
        // Component of actor
        return FString::Printf(TEXT("%s.c%d"), *ParentRef, ComponentCounter++);  // "a0.c0", "a0.c1", ...
    }
}
```

### Ref Resolution

```cpp
// FindActorByNameOrRef checks RefRegistry first, then falls back to name search
AActor* FindActorByNameOrRef(UWorld* World, const FString& NameOrRef)
{
    // First try ref registry
    UObject* Obj = ResolveRef(NameOrRef);
    if (AActor* Actor = Cast<AActor>(Obj)) {
        return Actor;
    }

    // Fall back to name/label search
    for (TActorIterator<AActor> It(World); It; ++It) {
        if (Actor->GetName() == NameOrRef || Actor->GetActorLabel() == NameOrRef) {
            return Actor;
        }
    }
    return nullptr;
}
```

### Ref Lifecycle

| Event | RefRegistry State |
|-------|-------------------|
| Server starts | Empty |
| `sceneSnapshot` called | Cleared, then rebuilt |
| Actor spawned | Not auto-added (run `snapshot` again) |
| Actor deleted | WeakObjectPtr becomes invalid |
| Editor closed | Lost (server shuts down) |

---

## Feature Deep Dives

### Debug Visualization

Draw debug shapes to help AI understand spatial relationships:

```cpp
// Supported types: sphere, line, text, box
FString HandleDrawDebug(const FString& Body)
{
    FString Type = Json->GetStringField(TEXT("type"));

    if (Type == TEXT("sphere")) {
        DrawDebugSphere(World, Location, Radius, 16, Color, false, Duration);
    }
    else if (Type == TEXT("line")) {
        DrawDebugLine(World, Start, End, Color, false, Duration);
    }
    else if (Type == TEXT("text")) {
        DrawDebugString(World, Location, Text, nullptr, Color, Duration);
    }
    else if (Type == TEXT("box")) {
        DrawDebugBox(World, Location, Extent, Color, false, Duration);
    }
}
```

### Blueprint Graph Snapshot

Returns Blueprint structure as YAML:

```yaml
Graph: EventGraph
  - [n0] Event: BeginPlay
  - [n1] Function: PrintString
  - [n2] GetVariable: PlayerHealth
Function: CalculateDamage (12 nodes)
Function: OnHit (8 nodes)
```

### Diff/Compare Mode

Track scene changes:

```cpp
struct FSceneStateSnapshot
{
    FString Name;
    FDateTime Timestamp;
    TMap<FString, FTransform> ActorTransforms;
    TMap<FString, FString> ActorClasses;
    TSet<FString> ActorNames;
};

// Diff output:
// + ADDED: BP_Enemy_3
// - REMOVED: StaticMeshActor_5
// ~ MOVED: BP_Player (0,0,0) -> (100,200,0)
// ~ ROTATED: DirectionalLight (0,0,0) -> (0,45,0)
```

---

## API Reference

### Core Commands

| Command | Description | Example |
|---------|-------------|---------|
| `snapshot` | Get scene hierarchy | `snapshot --classFilter=Light` |
| `screenshot` | Capture viewport | `screenshot --format=png --width=1920` |
| `focus <ref>` | Move camera to actor | `focus a0` |
| `select <ref>` | Select actor | `select a1 --add` |
| `spawn <class> <x> <y> <z>` | Spawn actor | `spawn PointLight 100 200 300` |
| `move <ref> <x> <y> <z>` | Move actor | `move a0 500 0 100` |
| `rotate <ref> <p> <y> <r>` | Rotate actor | `rotate a0 0 45 0` |
| `delete <ref>` | Delete actor | `delete a3` |
| `camera <x> <y> <z> <p> <y> <r>` | Set viewport | `camera 0 0 500 -45 0 0` |

### Debug Visualization

| Command | Description | Example |
|---------|-------------|---------|
| `draw-sphere <target> [radius]` | Draw sphere | `draw-sphere a0 100 --color=red` |
| `draw-line <start> <end>` | Draw line | `draw-line a0 a1 --color=green` |
| `draw-text <target> <text>` | Draw label | `draw-text a0 "Player"` |
| `draw-box <target>` | Draw bounds | `draw-box a0 --color=yellow` |
| `clear-debug` | Clear all | `clear-debug` |

### Undo/Redo

| Command | Description | Example |
|---------|-------------|---------|
| `begin-transaction [name]` | Start group | `begin-transaction "Move furniture"` |
| `end-transaction` | Commit | `end-transaction` |
| `undo [count]` | Undo | `undo 3` |
| `redo [count]` | Redo | `redo` |

### Diff/Compare

| Command | Description | Example |
|---------|-------------|---------|
| `save-state [name]` | Snapshot | `save-state before` |
| `diff [name]` | Compare | `diff before` |
| `restore [name]` | Restore | `restore before` |
| `list-states` | List all | `list-states` |

---

## Performance Considerations

### Screenshot Capture

| Setting | Impact |
|---------|--------|
| Default resolution | 1280x720 (fast) |
| Rate limiting | 0.5s cooldown between captures |
| Format | JPEG @ 75% quality (smaller than PNG) |
| GPU sync | `FlushRenderingCommands()` before capture |

### Scene Snapshots

| Optimization | Description |
|--------------|-------------|
| Class filter | `--classFilter=Light` reduces iteration |
| Component toggle | `--noComponents` skips component enumeration |
| WeakObjectPtr | Refs don't prevent garbage collection |
| Single pass | Build JSON + YAML in one iteration |

### Memory Management

| Resource | Lifecycle |
|----------|-----------|
| RefRegistry | Static, cleared on each snapshot |
| SavedSnapshots (diff) | Static map, persists until server restart |
| Debug draws | Auto-expire after `duration` seconds |
| Recording sessions | In-memory, lost on server restart |

---

## Future Enhancements

- [ ] Property Inspector (`inspect a0` → show all editable properties)
- [ ] PIE Control (`play`, `pause`, `step`, `stop`)
- [ ] Content Browser (`assets`, `import`)
- [ ] Material preview
- [ ] Async screenshot with callback
- [ ] Persistent recording to disk
- [ ] WebSocket for real-time updates
