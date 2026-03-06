# AgenticMCP — AI Agent Instructions

## What This Is

AgenticMCP is a dual-path MCP server for Unreal Engine 5. It gives you (the AI agent) direct access to the running UE5 editor through Blueprint manipulation, actor management, and level editing tools. If the editor is not running, it falls back to an offline binary injector that can read and modify `.umap` files directly.

## Architecture

```
AI Client <-> MCP Protocol (stdio) <-> Node.js MCP Bridge <-> HTTP (localhost:3000) <-> C++ Plugin (inside UE5 editor)
                                            |
                                            +-> Python Binary Injector (offline fallback, .umap files)
```

All live mutations happen on the UE5 game thread. Requests are queued and processed safely. When the editor is not running, the fallback path reads/writes `.umap` files directly.

## Connection Modes

### Live Editor (Primary)
When the UE5 editor is running with the AgenticMCP C++ plugin loaded, all tools are available over HTTP. You get real-time compilation, validation, and the full tool set.

### Offline Fallback
When the editor is not running, a subset of tools is available through the Python binary injector. You can read level structure, inspect actors, and generate paste text. You cannot compile, validate, or spawn actors in real-time.

### Check Status First
Always start by calling `unreal_status` to determine which mode is active and what tools are available.

## Tool Categories

### Read (Non-destructive)
- `list_blueprints` — List all Blueprint and Map assets. Maps contain level blueprints.
- `get_blueprint` — Get Blueprint details (graphs, variables, parent class).
- `get_graph` — Get all nodes, pins, connections in a graph. Primary inspection tool.
- `search_blueprints` — Search by name pattern.
- `find_references` — Find what references an asset.
- `list_classes` — Discover UClasses (for CallFunction node types).
- `list_functions` — List UFunctions on a class (for knowing what to call).
- `list_properties` — List UProperties on a class.
- `get_pin_info` — Inspect pin types and connections on a node.
- `rescan_assets` — Force asset registry refresh.

### Mutation (Modifies Blueprints)
- `add_node` — Add a node to a graph. Returns nodeId and all pins.
- `delete_node` — Remove a node (breaks connections first).
- `connect_pins` — Wire two pins together (type-validated).
- `disconnect_pin` — Break all connections on a pin.
- `set_pin_default` — Set a pin's default value.
- `move_node` — Reposition a node.
- `refresh_all_nodes` — Refresh all nodes (after variable/class changes).
- `create_blueprint` — Create a new Blueprint asset.
- `create_graph` — Add a function or macro graph.
- `delete_graph` — Remove a graph.
- `add_variable` — Add a variable to a Blueprint.
- `remove_variable` — Remove a variable.
- `compile_blueprint` — Compile and save.
- `set_node_comment` — Set comment bubble on a node.

### World (Actors and Levels)
- `list_actors` — List actors with optional class/name/level filters.
- `get_actor` — Get actor details: transform, components, properties.
- `spawn_actor` — Spawn a new actor (Blueprint or native class).
- `delete_actor` — Remove an actor.
- `set_actor_property` — Set a property on an actor or its components.
- `set_actor_transform` — Move/rotate/scale an actor.
- `list_levels` — List persistent and streaming levels.
- `load_level` — Load a sublevel.
- `get_level_blueprint` — Get level blueprint details.

### Safety
- `validate_blueprint` — Compile and report errors/warnings without saving.
- `snapshot_graph` — Take a graph snapshot before making changes.
- `restore_graph` — Restore graph from snapshot.

### UE API Documentation
- `unreal_get_ue_context` — Load UE API reference by category or keyword search.

## Standard Workflow

```
1. unreal_status                              // Check connection
2. unreal_get_ue_context(category="blueprint") // Load relevant API docs
3. get_blueprint("MyBlueprint")               // See what exists
4. get_graph("MyBlueprint", "EventGraph")     // See existing nodes
5. snapshot_graph("MyBlueprint", "Before changes")  // Save rollback point
6. add_node(...)                              // Make changes
7. connect_pins(...)                          // Wire nodes
8. set_pin_default(...)                       // Set values
9. validate_blueprint("MyBlueprint")          // Check for errors
10. compile_blueprint("MyBlueprint")          // Compile and save
```

## Example: Add a BeginPlay -> PrintString Chain

```
// Step 1: Read the Blueprint
get_graph("MyBlueprint", "EventGraph")

// Step 2: Snapshot before changes
snapshot_graph("MyBlueprint", "Before adding print chain")

// Step 3: Add ReceiveBeginPlay event
add_node(blueprint="MyBlueprint", graph="EventGraph",
  nodeType="OverrideEvent", functionName="ReceiveBeginPlay", posX=0, posY=0)
// Returns: { nodeId: "ABC-123", pins: [...] }

// Step 4: Add PrintString call
add_node(blueprint="MyBlueprint", graph="EventGraph",
  nodeType="CallFunction", functionName="PrintString",
  className="UKismetSystemLibrary", posX=300, posY=0)
// Returns: { nodeId: "DEF-456", pins: [...] }

// Step 5: Wire them together
connect_pins(blueprint="MyBlueprint",
  sourceNodeId="ABC-123", sourcePinName="then",
  targetNodeId="DEF-456", targetPinName="execute")

// Step 6: Set the print text
set_pin_default(blueprint="MyBlueprint",
  nodeId="DEF-456", pinName="InString", value="Hello from AgenticMCP")

// Step 7: Validate
validate_blueprint("MyBlueprint")

// Step 8: Compile
compile_blueprint("MyBlueprint")
```

## Supported Node Types

| nodeType | Required Fields | Description |
|----------|----------------|-------------|
| CallFunction | functionName, className? | Call a UFunction |
| CustomEvent | eventName | Create a custom event |
| OverrideEvent | functionName | Override a parent class event (BeginPlay, Tick, etc.) |
| BreakStruct | typeName | Break a struct into individual pins |
| MakeStruct | typeName | Construct a struct from individual pins |
| VariableGet | variableName | Get a variable value |
| VariableSet | variableName | Set a variable value |
| DynamicCast | castTarget | Cast to a class |
| Branch | (none) | If/Then/Else |
| Sequence | (none) | Execution sequence (Then 0, Then 1, ...) |
| MacroInstance | macroName, macroSource? | Instantiate a macro (ForLoop, ForEachLoop, etc.) |
| SpawnActorFromClass | actorClass? | Spawn actor node |
| Select | (none) | Select node |
| Comment | comment?, width?, height? | Comment box |
| Reroute | (none) | Reroute/knot node |
| MultiGate | numOutputs? | MultiGate execution node |
| Delay | duration? | Delay node |
| SetTimer | functionName, time? | Set Timer by Function Name |
| LoadAsset | (none) | Async Load Asset |

## Available UE API Context Categories

The `unreal_get_ue_context` tool provides reference documentation for:
- `actor` — Actor spawning, components, transforms, attachment
- `animation` — Animation Blueprints, state machines, montages
- `assets` — Asset loading, references, soft/hard pointers
- `blueprint` — K2Node hierarchy, pin types, graph manipulation
- `character` — Character movement, capsule, mesh setup
- `enhanced_input` — Input actions, mappings, triggers
- `material` — Material expressions, instances, parameters
- `parallel_workflows` — Batch operations, parallel patterns
- `replication` — Network replication, RPCs, variable replication
- `slate` — Editor UI, widgets, panels

## Offline Mode Tools

When the editor is not running, these tools are available:
- `offline_list_levels` — List all `.umap` files in the project
- `offline_list_actors` — List actors in a `.umap` file
- `offline_get_actor` — Get actor properties
- `offline_get_graph` — Get Blueprint graph from a level
- `offline_level_info` — Get level summary
- `offline_generate_paste_text` — Generate Blueprint paste text for manual import

## Critical Rules

1. **Always snapshot before destructive operations.** The snapshot system exists for a reason — use it.
2. **Always validate before compiling.** Catch errors before they persist.
3. **Node IDs are GUIDs.** Every `add_node` returns a GUID. Store it for subsequent `connect_pins` and `set_pin_default` calls.
4. **Pin names are case-sensitive.** Use `get_pin_info` to discover exact pin names before connecting.
5. **Level blueprints use map names.** Pass the `.umap` name (e.g., `"MyLevel"`), not a Blueprint path.
6. **Compile after all changes.** Batch your mutations, then compile once at the end.
7. **Check status first.** If the editor is not connected, only offline tools are available.
8. **SEH protection on Windows.** Compilation and save are wrapped in SEH handlers to prevent editor crashes.

## Error Handling

- If a tool returns `isError: true`, read the error message carefully. It often includes available options (valid pin names, graph names, class names).
- If compilation fails, use `validate_blueprint` to get detailed error information.
- If you break something, use `restore_graph` to roll back to the last snapshot.
- If the editor disconnects mid-operation, the bridge will automatically switch to offline mode on the next call.
