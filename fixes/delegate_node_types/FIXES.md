# Fix: Delegate Node Types for AddNode Handler

## Date
2025-03-12

## Problem
The `addNode` handler in `Handlers_Mutation.cpp` supported 20 node types but was missing delegate-related nodes. This meant the AI agent could not programmatically bind events like `OnGrabbed`, `OnComponentBeginOverlap`, or any other multicast delegate without falling back to `execute-python`. The SOH VR project's trigger wiring (connecting in-world actors to CustomEvents) required these nodes.

## Changes

### 1. Handlers_Mutation.cpp
Added 5 new node types to the `HandleAddNode` function:

| Node Type | K2Node Class | Purpose |
|-----------|-------------|---------|
| `AddDelegate` | `UK2Node_AddDelegate` | "Bind Event to ..." -- binds a function to a multicast delegate |
| `RemoveDelegate` | `UK2Node_RemoveDelegate` | "Unbind Event from ..." -- removes a binding |
| `ClearDelegate` | `UK2Node_ClearDelegate` | "Unbind All Events from ..." -- clears all bindings |
| `CreateDelegate` | `UK2Node_CreateDelegate` | Creates a delegate reference (Assign pattern) |
| `Delay` | `UK2Node_CallFunction` (UGameplayStatics::Delay) | Convenience wrapper for the Delay node |

**New includes added:**
- `K2Node_AddDelegate.h`
- `K2Node_CreateDelegate.h`
- `K2Node_BaseMCDelegate.h`
- `K2Node_ClearDelegate.h`
- `K2Node_RemoveDelegate.h`
- `GameFramework/GameplayStatics.h`

**AddDelegate error handling:** If the delegate name is not found on the specified class, the error response includes an `availableDelegates` array listing all multicast delegate properties on that class. This allows the AI agent to self-correct without guessing.

**Auto-search:** If `ownerClass` is not specified, the handler searches all loaded UClasses for the delegate property. This is slower but allows the AI to bind delegates without knowing the exact owner class.

### 2. tool-registry.json
Updated the `addNode` tool entry:
- Expanded `nodeType` description to list all 27 supported types
- Added parameters: `delegateName`, `ownerClass`, `duration`, `actorClass`, `castTarget`

### 3. CLAUDE.md
- Added delegate binding example (OnGrabbed -> CustomEvent pattern)
- Added all new node types to the Supported Node Types table

## Testing
These changes require compilation inside UE5 with the AgenticMCP plugin. The new node types follow the exact same pattern as all existing types (NewObject, SetFromProperty/SetFromFunction, AddNode, AllocateDefaultPins). The delegate property lookup uses the same `TFieldIterator` pattern used elsewhere in the codebase.
