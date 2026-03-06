# ManusMCP - AI Agent Instructions

## What This Is

ManusMCP is a UE5.6 editor plugin that exposes Blueprint manipulation, actor management, and level editing via HTTP + MCP. When the UE5 editor is running with this plugin enabled, you can directly create and modify Blueprints, spawn actors, wire logic, and compile — all without the user touching the editor.

## Architecture

```
AI Client <-> MCP Protocol (stdio) <-> TypeScript Server <-> HTTP (localhost:9847) <-> C++ Plugin (inside UE5 editor)
```

All mutations happen on the UE5 game thread. Requests are queued and processed one per editor tick (up to 4/tick).

## Connection

The UE5 editor must be running with ManusMCP enabled. Check with `rescan_assets` or `list_blueprints`. If tools fail with connection errors, the editor is not running or the plugin is not loaded.

## Tool Categories

### Read (Non-destructive)
- `list_blueprints` - List all Blueprint and Map assets. Maps contain level blueprints.
- `get_blueprint` - Get Blueprint details (graphs, variables, parent class).
- `get_graph` - Get all nodes, pins, connections in a graph. This is your primary inspection tool.
- `search_blueprints` - Search by name pattern.
- `find_references` - Find what references an asset.
- `list_classes` - Discover UClasses (for CallFunction node types).
- `list_functions` - List UFunctions on a class (for knowing what to call).
- `list_properties` - List UProperties on a class.
- `get_pin_info` - Inspect pin types and connections on a node.
- `rescan_assets` - Force asset registry refresh.

### Mutation (Modifies Blueprints)
- `add_node` - Add a node to a graph. Returns nodeId and all pins.
- `delete_node` - Remove a node (breaks connections first).
- `connect_pins` - Wire two pins together (type-validated).
- `disconnect_pin` - Break all connections on a pin.
- `set_pin_default` - Set a pin's default value.
- `move_node` - Reposition a node.
- `refresh_all_nodes` - Refresh all nodes (after variable/class changes).
- `create_blueprint` - Create a new Blueprint asset.
- `create_graph` - Add a function or macro graph.
- `delete_graph` - Remove a graph.
- `add_variable` - Add a variable to a Blueprint.
- `remove_variable` - Remove a variable.
- `compile_blueprint` - Compile and save.
- `set_node_comment` - Set comment bubble on a node.

### World (Actors and Levels)
- `list_actors` - List actors with optional class/name/level filters.
- `get_actor` - Get actor details: transform, components, properties.
- `spawn_actor` - Spawn a new actor (Blueprint or native class).
- `delete_actor` - Remove an actor.
- `set_actor_property` - Set a property on an actor or its components.
- `set_actor_transform` - Move/rotate/scale an actor.
- `list_levels` - List persistent and streaming levels.
- `load_level` - Load a sublevel.
- `get_level_blueprint` - Get level blueprint details.

### Safety
- `validate_blueprint` - Compile and report errors/warnings.
- `snapshot_graph` - Take a graph snapshot before making changes.
- `restore_graph` - Clear graph and get snapshot data for reconstruction.

## Workflow: Building Blueprint Logic

### Step 1: Understand the target
```
get_blueprint("SL_Trailer_Logic")  // See what graphs/variables exist
get_graph("SL_Trailer_Logic", "EventGraph")  // See existing nodes
```

### Step 2: Snapshot before changes
```
snapshot_graph("SL_Trailer_Logic", "Before adding teleport logic")
```

### Step 3: Add nodes
```
add_node(blueprint="SL_Trailer_Logic", graph="EventGraph", nodeType="OverrideEvent", functionName="ReceiveBeginPlay", posX=0, posY=0)
// Returns: { nodeId: "ABC-123", pins: [...] }

add_node(blueprint="SL_Trailer_Logic", graph="EventGraph", nodeType="CallFunction", functionName="ListenForMessages", className="UStoryHelpers", posX=300, posY=0)
// Returns: { nodeId: "DEF-456", pins: [...] }
```

### Step 4: Wire pins
```
connect_pins(blueprint="SL_Trailer_Logic", sourceNodeId="ABC-123", sourcePinName="then", targetNodeId="DEF-456", targetPinName="execute")
```

### Step 5: Set defaults
```
set_pin_default(blueprint="SL_Trailer_Logic", nodeId="DEF-456", pinName="Channel", value="OnPlayerTeleported")
```

### Step 6: Validate
```
validate_blueprint("SL_Trailer_Logic")
```

## SOH Project Context

This plugin was built for the SOH VR experience. Key project-specific knowledge:

### Story Step System
- `DA_GameData` (Data Asset) contains `DT_StorySteps` (Data Table)
- Each story step has an index (0-35+), a level sequence, and scene metadata
- Steps are broadcast via `UStoryHelpers::BroadcastStoryStep(StepIndex)`
- Steps are listened for via `UStoryHelpers::ListenForMessages(Channel)`

### Interaction Architecture (Three Pillars)
1. **EnablerComponent** - Activates/deactivates interactables based on story state
2. **TriggerBoxComponent** - Spatial triggers for gaze/proximity/overlap events
3. **ActivatableActor** - Base class for all interactive objects

### Level Structure
- Master Level (ML_*) loads sublevel layers:
  - SL_*_Art (environment meshes)
  - SL_*_Lighting (lights, post-process)
  - SL_*_Blockout (greybox geometry)
  - SL_*_Logic (Blueprint logic — this is what we edit)

### Interaction Pattern (from paste text analysis)
The standard interaction chain in a logic sublevel:
1. BeginPlay -> ListenForMessages (async) -> BreakStruct
2. LoadAsset (soft reference) -> Cast -> EnableActor
3. AssignDelegate (OnPlayerTeleported) -> CustomEvent
4. MultiGate -> BroadcastStoryStep (per gate output)

### Existing Logic Levels
- SL_Trailer_Logic: 56 nodes, 7 functions, steps 3/6/3/7
- SL_Restaurant_Logic: 7 teleport points, 0 functions (actors placed, no logic)
- SL_Hospital_Logic: Interaction chains wired
- SL_Scene6_Logic: Interaction chains wired

## Important Rules

1. **Always snapshot before mutations** - Take a snapshot before any destructive operation.
2. **Always validate after mutations** - Compile and check for errors after adding/connecting nodes.
3. **Use get_pin_info when unsure** - Pin names vary by node type. Always check before connecting.
4. **Level blueprints use map names** - Pass the .umap name (e.g. "SL_Trailer_Logic"), not a Blueprint path.
5. **Save happens automatically** - add_node, connect_pins, etc. compile and save after each operation.
6. **Node IDs are GUIDs** - Store them after add_node returns; you need them for connect_pins.
7. **SEH protection on Windows** - Compilation and save are wrapped in SEH handlers to prevent editor crashes.
