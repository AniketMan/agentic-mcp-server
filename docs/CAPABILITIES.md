# VisualAgent Capabilities

> Complete command reference for the VisualAgent CLI

This document lists all commands available through the `unreal_cli` tool. Commands can be combined with flags and use short refs (`a0`, `a1.c0`) from scene snapshots.

---

## Quick Reference

| Category | Commands |
|----------|----------|
| **Visual** | `screenshot`, `snapshot`, `auto-screenshot` |
| **Actor** | `focus`, `select`, `spawn`, `move`, `rotate`, `delete`, `set`, `query`, `ref` |
| **Viewport** | `camera`, `navigate`, `wait` |
| **Debug Visualization** | `draw-sphere`, `draw-line`, `draw-text`, `draw-box`, `clear-debug` |
| **Blueprint** | `blueprint` |
| **Undo/Redo** | `begin-transaction`, `end-transaction`, `undo`, `redo` |
| **Diff/Compare** | `save-state`, `diff`, `restore`, `list-states` |
| **Recording** | `record start`, `record stop`, `record save`, `record load`, `record list`, `record play` |
| **Help** | `help` |

**Total: 42 commands**

---

## Visual Commands

### `snapshot`

Get hierarchical scene tree with short refs (like accessibility tree).

```
snapshot
snapshot --classFilter=Light
snapshot --noComponents
```

**Flags:**
- `--classFilter=<class>` - Filter actors by class name
- `--noComponents` - Exclude component enumeration

**Returns:**
- `actorCount` - Number of actors
- `actors[]` - JSON array with full hierarchy
- `yamlSnapshot` - Human-readable YAML format

---

### `screenshot`

Capture viewport screenshot using UE's built-in system.

```
screenshot
screenshot --format=png
screenshot --width=1920 --height=1080
screenshot --format=jpeg --quality=85
```

**Flags:**
- `--format=<png|jpeg>` - Image format (default: jpeg)
- `--width=<N>` - Target width (default: 1280)
- `--height=<N>` - Target height (default: 720)
- `--quality=<1-100>` - JPEG quality (default: 75)

**Returns:**
- `data` - Base64-encoded image
- `mimeType` - image/png or image/jpeg
- `width`, `height` - Actual dimensions
- `sizeBytes` - Compressed size

---

### `auto-screenshot`

Toggle automatic screenshot capture for visual actions.

```
auto-screenshot on
auto-screenshot off
auto-screenshot         # Check current status
```

**Visual actions that trigger auto-screenshot:**
- `spawn`, `move`, `rotate`, `delete`, `camera`, `focus`, `navigate`

---

## Actor Commands

### `focus`

Move editor camera to focus on an actor.

```
focus a0
focus BP_Player_C_0
focus PlayerStart
```

**Args:**
- `<ref|name>` - Actor ref or name (required)

---

### `select`

Select actor(s) in the editor outliner.

```
select a0
select a1 --add
select BP_Enemy_3
```

**Args:**
- `<ref|name>` - Actor ref or name (required)

**Flags:**
- `--add` - Add to current selection (don't clear)

---

### `spawn`

Spawn a new actor in the world.

```
spawn StaticMeshActor 100 200 300
spawn PointLight 0 0 500 --label="MainLight"
spawn BP_Enemy_C 1000 0 0
```

**Args:**
- `<className>` - Actor class to spawn (required)
- `<x> <y> <z>` - World location (required)

**Flags:**
- `--label=<name>` - Set actor label

---

### `move`

Set actor world location.

```
move a0 100 200 300
move BP_Player 500 0 0
```

**Args:**
- `<ref|name>` - Actor ref or name (required)
- `<x> <y> <z>` - New world location (required)

---

### `rotate`

Set actor world rotation.

```
rotate a0 0 45 0
rotate DirectionalLight -45 0 0
```

**Args:**
- `<ref|name>` - Actor ref or name (required)
- `<pitch> <yaw> <roll>` - Rotation in degrees (required)

---

### `delete`

Delete an actor from the world.

```
delete a0
delete StaticMeshActor_5
```

**Args:**
- `<ref|name>` - Actor ref or name (required)

---

### `set`

Set a property on an actor.

```
set a0 Mobility Movable
set BP_Player Health 100
```

**Args:**
- `<ref|name>` - Actor ref or name (required)
- `<property>` - Property name (required)
- `<value>` - New value (required)

---

### `query`

Query actors by class or name filter.

```
query
query StaticMeshActor
query Light --nameFilter=Main
```

**Args:**
- `[classFilter]` - Filter by class name (optional)

**Flags:**
- `--nameFilter=<pattern>` - Filter by name pattern

---

### `ref`

Resolve a short ref to actor/component details.

```
ref a0
ref a1.c0
```

**Args:**
- `<refId>` - Short ref to resolve (required)

**Returns:**
- `name` - Full object name
- `class` - Object class
- `type` - "Actor" or "Component"
- `pathName` - Full path

---

## Viewport Commands

### `camera`

Set viewport camera position and rotation.

```
camera 0 0 500 -45 0 0
camera 1000 2000 800 -30 45 0
```

**Args:**
- `<x> <y> <z>` - Camera location (required)
- `<pitch> <yaw> <roll>` - Camera rotation (required)

---

### `navigate`

Load a level/map.

```
navigate MainMenu
navigate /Game/Maps/Level_01
```

**Args:**
- `<levelName>` - Level name or path (required)

---

### `wait`

Wait for a condition to be ready.

```
wait assets      # Wait for asset registry scan
wait compile     # Wait for Blueprint compilation
wait render      # Flush rendering commands
```

**Args:**
- `<assets|compile|render>` - Condition to wait for (required)

---

## Debug Visualization Commands

### `draw-sphere`

Draw a debug sphere in the viewport.

```
draw-sphere a0
draw-sphere a0 100
draw-sphere a0 100 --color=red --duration=10
draw-sphere 500 0 200 50 --color=blue
```

**Args:**
- `<target>` - Actor ref OR `<x> <y> <z>` coordinates
- `[radius]` - Sphere radius (default: 50)

**Flags:**
- `--color=<name|#hex>` - Color (red, green, blue, yellow, cyan, magenta, white, black, orange, purple, or #RRGGBB)
- `--duration=<seconds>` - How long to display (default: 5)

---

### `draw-line`

Draw a debug line between two points.

```
draw-line a0 a1
draw-line a0 a1 --color=green --duration=10
draw-line 0 0 0 100 200 300 --color=#FF00FF
```

**Args:**
- `<start> <end>` - Two actor refs OR six coordinates

**Flags:**
- `--color=<name|#hex>` - Line color (default: green)
- `--duration=<seconds>` - How long to display (default: 5)
- `--thickness=<N>` - Line thickness (default: 2)

---

### `draw-text`

Draw a debug text label at an actor's location.

```
draw-text a0 "Player Start"
draw-text a1 "Enemy Spawn" --color=red
draw-text a0 "Health: 100" --duration=10
```

**Args:**
- `<target>` - Actor ref (required)
- `<text>` - Text to display (required)

**Flags:**
- `--color=<name|#hex>` - Text color (default: white)
- `--duration=<seconds>` - How long to display (default: 5)
- `--scale=<N>` - Text scale (default: 1.5)

---

### `draw-box`

Draw a debug bounding box around an actor.

```
draw-box a0
draw-box BP_Player --color=yellow --duration=10
```

**Args:**
- `<target>` - Actor ref (required)

**Flags:**
- `--color=<name|#hex>` - Box color (default: yellow)
- `--duration=<seconds>` - How long to display (default: 5)

---

### `clear-debug`

Clear all debug draws from the viewport.

```
clear-debug
```

---

## Blueprint Inspection Commands

### `blueprint`

Get Blueprint graph structure as YAML.

```
blueprint /Game/Blueprints/BP_Character
blueprint /Game/Blueprints/BP_GameMode
```

**Args:**
- `<assetPath>` - Blueprint asset path (required)

**Returns:**
- `blueprintName` - Blueprint name
- `blueprintClass` - Generated class
- `parentClass` - Parent class
- `graphs[]` - Array of graphs with nodes
- `yamlSnapshot` - Human-readable graph structure

**YAML format:**
```yaml
Graph: EventGraph
  - [n0] Event: BeginPlay
  - [n1] Function: PrintString
  - [n2] GetVariable: Health
Function: CalculateDamage (12 nodes)
```

---

## Undo/Redo Commands

### `begin-transaction`

Start an undo transaction group.

```
begin-transaction
begin-transaction "Move furniture"
begin-transaction "Reorganize level"
```

**Args:**
- `[name]` - Transaction name (optional, default: "VisualAgent Operation")

---

### `end-transaction`

End and commit the current transaction.

```
end-transaction
```

Alias: `commit`

---

### `undo`

Undo the last operation(s).

```
undo
undo 3
undo 10
```

**Args:**
- `[count]` - Number of operations to undo (default: 1)

---

### `redo`

Redo undone operation(s).

```
redo
redo 2
```

**Args:**
- `[count]` - Number of operations to redo (default: 1)

---

## Diff/Compare Commands

### `save-state`

Save a snapshot of current scene state.

```
save-state
save-state before
save-state checkpoint1
```

**Args:**
- `[name]` - Snapshot name (default: "default")

**Captures:**
- All actor names
- Actor transforms (location, rotation, scale)
- Actor classes

---

### `diff`

Compare current scene to a saved state.

```
diff
diff before
diff checkpoint1
```

**Args:**
- `[name]` - Snapshot to compare against (default: "default")

**Returns:**
- `addedCount`, `removedCount`, `modifiedCount`
- `added[]` - Actors added since snapshot
- `removed[]` - Actors removed since snapshot
- `modified[]` - Actors with changed transforms
- `diffYaml` - Human-readable diff

**Diff format:**
```
+ ADDED: BP_Enemy_3
- REMOVED: StaticMeshActor_5
~ MOVED: BP_Player (0,0,0) -> (100,200,0)
~ ROTATED: DirectionalLight (0,0,0) -> (0,45,0)
```

---

### `restore`

Restore scene to a saved state.

```
restore
restore before
restore checkpoint1
```

**Args:**
- `[name]` - Snapshot to restore (default: "default")

**Note:** Creates an undo transaction automatically.

---

### `list-states`

List all saved state snapshots.

```
list-states
```

**Returns:**
- `count` - Number of saved states
- `states[]` - Array with name, actorCount, timestamp

---

## Recording Commands

### `record start`

Begin recording commands.

```
record start
```

---

### `record stop`

Stop recording and return session.

```
record stop
```

**Returns:**
- `session.actions[]` - Recorded commands with timestamps
- `session.totalDuration` - Total recording time

---

### `record save`

Save current recording to memory.

```
record save mysession
record save level_setup
```

**Args:**
- `<name>` - Session name (required)

---

### `record load`

Load a saved recording.

```
record load mysession
```

**Args:**
- `<name>` - Session name (required)

---

### `record list`

List all saved recordings.

```
record list
```

---

### `record play`

Replay loaded recording.

```
record play
```

**Returns:**
- `replay[]` - Array of commands to execute

---

## Help Command

### `help`

Show full command documentation.

```
help
```

---

## Global Flags

These flags work with any command:

| Flag | Description |
|------|-------------|
| `--screenshot` | Capture screenshot after command completes |

**Example:**
```
move a0 100 0 0 --screenshot
spawn PointLight 0 0 500 --screenshot
```

---

## Short Ref Format

Refs are assigned during `snapshot`:

| Ref | Type | Example |
|-----|------|---------|
| `a0` | First root actor | `a0`, `a1`, `a2`, ... |
| `a0.c0` | First component of a0 | `a0.c0`, `a0.c1`, ... |
| `a1.c2` | Third component of a1 | |

**Usage:**
```
snapshot                    # Get refs
focus a0                    # Use ref instead of full name
move a1.c0 100 0 0         # Reference component
draw-line a0 a3            # Line between two actors
```

---

## Command Chaining Examples

```bash
# Setup scene and take screenshot
snapshot
spawn PointLight 0 0 500 --label="MainLight"
focus a0
screenshot --format=png

# Track changes
save-state before
move a0 100 0 0
move a1 200 0 0
diff before

# Grouped undo
begin-transaction "Move all enemies"
move a0 100 0 0
move a1 200 0 0
move a2 300 0 0
end-transaction
undo   # Undoes all 3 moves at once

# Debug visualization
draw-sphere a0 100 --color=green
draw-line a0 a1 --color=yellow
draw-text a0 "Start"
draw-text a1 "End"
# ... analyze spatial relationship ...
clear-debug
```
