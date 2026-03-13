# Complete Node Type Reference (from C++ Source)

## addNode API
POST /api/add-node
Required: `blueprint`, `nodeType`, `posX`, `posY`
Optional: varies by nodeType

### Exact nodeType Strings (CASE SENSITIVE):

| nodeType | C++ Class | Required Extra Params | Optional Params |
|----------|-----------|----------------------|-----------------|
| `CallFunction` | UK2Node_CallFunction | `functionName` (e.g. "PrintString", "Delay") | `className` |
| `VariableGet` | UK2Node_VariableGet | `variableName` | `selfContext` (bool) |
| `VariableSet` | UK2Node_VariableSet | `variableName` | `selfContext` (bool) |
| `CustomEvent` | UK2Node_CustomEvent | `eventName` | - |
| `OverrideEvent` | UK2Node_Event (override) | `functionName` (e.g. "ReceiveBeginPlay", "ReceiveTick") | - |
| `Branch` | UK2Node_IfThenElse | - | - |
| `Sequence` | UK2Node_ExecutionSequence | - | - |
| `ForLoop` | UK2Node_ForLoop (macro) | - | - |
| `ForEachLoop` | UK2Node_ForEachLoop (macro) | - | - |
| `ForLoopWithBreak` | UK2Node_ForLoopWithBreak (macro) | - | - |
| `WhileLoop` | UK2Node_WhileLoop (macro) | - | - |
| `MakeStruct` | UK2Node_MakeStruct | `structType` (e.g. "StoryStep") | - |
| `BreakStruct` | UK2Node_BreakStruct | `structType` | - |
| `DynamicCast` | UK2Node_DynamicCast | `castTo` (class name) | - |
| `OverrideEvent` | UK2Node_Event (parent) | `eventName` | - |
| `CallParentFunction` | UK2Node_CallParentFunction | `functionName` | - |
| `SpawnActorFromClass` | UK2Node_SpawnActorFromClass | - | `actorClass` |
| `Select` | UK2Node_Select | - | - |
| `Comment` | UEdGraphNode_Comment | - | `comment`, `width`, `height` |
| `Reroute` | UK2Node_Knot | - | - |
| `MacroInstance` | UK2Node_MacroInstance | `macroName` | `macroSource` (defaults to StandardMacros) |
| `AddDelegate` | UK2Node_AddDelegate | `delegateName` | `ownerClass` |
| `RemoveDelegate` | UK2Node_RemoveDelegate | `delegateName` | `ownerClass` |
| `ClearDelegate` | UK2Node_ClearDelegate | `delegateName` | `ownerClass` |
| `CreateDelegate` | UK2Node_CreateDelegate | - | `functionName` |
| `Delay` | UK2Node_CallFunction(Delay) | - | `duration` (float) |

Total: 26 supported nodeTypes

### VERIFIED AGAINST C++ SOURCE (Handlers_Mutation.cpp)
Every nodeType string, every required param name, and every optional param name above has been verified against the live C++ handler `FAgenticMCPServer::HandleAddNode` in `Source/AgenticMCP/Private/Handlers_Mutation.cpp` (lines 100-932).

### Key Param Name Verification:
| nodeType | C++ Param Field | JSON Key to Use |
|----------|----------------|----------------|
| `CallFunction` | `Json->GetStringField(TEXT("functionName"))` | `"functionName"` |
| `CallFunction` | `Json->GetStringField(TEXT("className"))` | `"className"` (optional) |
| `VariableGet/Set` | `Json->GetStringField(TEXT("variableName"))` | `"variableName"` |
| `CustomEvent` | `Json->GetStringField(TEXT("eventName"))` | `"eventName"` |
| `OverrideEvent` | `Json->GetStringField(TEXT("functionName"))` | `"functionName"` |
| `CallParentFunction` | `Json->GetStringField(TEXT("functionName"))` | `"functionName"` |
| `BreakStruct/MakeStruct` | `Json->GetStringField(TEXT("typeName"))` | `"typeName"` |
| `DynamicCast` | `Json->GetStringField(TEXT("castTarget"))` | `"castTarget"` |
| `SpawnActorFromClass` | `Json->GetStringField(TEXT("actorClass"))` | `"actorClass"` (optional) |
| `MacroInstance` | `Json->GetStringField(TEXT("macroName"))` | `"macroName"` |
| `MacroInstance` | `Json->GetStringField(TEXT("macroSource"))` | `"macroSource"` (optional, defaults to StandardMacros) |
| `AddDelegate` | `Json->GetStringField(TEXT("delegateName"))` | `"delegateName"` |
| `AddDelegate` | `Json->GetStringField(TEXT("ownerClass"))` | `"ownerClass"` (optional) |
| `Comment` | `Json->GetStringField(TEXT("comment"))` | `"comment"` (optional) |
| `Comment` | `Json->GetIntegerField(TEXT("width"))` | `"width"` (optional, default 400) |
| `Comment` | `Json->GetIntegerField(TEXT("height"))` | `"height"` (optional, default 200) |
| `Delay` | `Json->GetNumberField(TEXT("duration"))` | `"duration"` (optional, float) |
| `CreateDelegate` | `Json->GetStringField(TEXT("functionName"))` | `"functionName"` (optional) |

### IMPORTANT: There is NO "Event" nodeType.
The C++ source does NOT have a standalone `Event` nodeType handler. To create event nodes:
- **BeginPlay/Tick/Overlap**: Use `OverrideEvent` with `functionName: "ReceiveBeginPlay"` (or `"ReceiveTick"`, `"ReceiveActorBeginOverlap"`).
- **Custom Events**: Use `CustomEvent` with `eventName: "YourName"`.
- **Delegate Events**: Use `AddDelegate` with `delegateName: "OnComponentBeginOverlap"` and `ownerClass: "PrimitiveComponent"`.

### Error on unknown nodeType:
Returns: "Unsupported nodeType 'X'. Supported: BreakStruct, MakeStruct, CallFunction, VariableGet, VariableSet, DynamicCast, OverrideEvent, CallParentFunction, CustomEvent, Branch, Sequence, ForLoop, ForEachLoop, ForLoopWithBreak, WhileLoop, SpawnActorFromClass, Select, Comment, Reroute, MacroInstance, AddDelegate, RemoveDelegate, ClearDelegate, CreateDelegate, Delay"

## connectPins API
POST /api/connect-pins
Required: `blueprint`, `sourceNodeId`, `sourcePinName`, `targetNodeId`, `targetPinName`
- sourceNodeId/targetNodeId = GUID returned by addNode
- Pin names come from the node's allocated pins (use get_pin_info to discover)
- On pin not found: returns `availablePins` array with all pin names and directions
- On type mismatch: returns error with pin categories

## setPinDefault API
POST /api/set-pin-default
Required: `blueprint`, `nodeId`, `pinName`
Optional: `value` (string), `defaultObject` (asset path for class/object refs)
- For simple values: use `value` (e.g. "5.0", "true", "Hello")
- For asset references: use `defaultObject` (e.g. "/Game/Sounds/MySound")
- On pin not found: returns `availableInputPins` array

## addComponent API
POST /api/add-component
Required: `blueprint`, `componentClass`
Optional: `componentName`
- componentClass examples: "StaticMeshComponent", "AudioComponent", "BoxComponent", "SphereComponent"
- If component already exists, returns `alreadyExists: true`
- Adds to SCS root

## loadLevel API
POST /api/load-level
Required: `levelPath` (e.g. "/Game/Maps/Game/Main/Levels/SLs/SL_Main_Logic")
- ADDS a streaming sublevel to the CURRENT persistent world
- Does NOT switch maps
- Does NOT open a new persistent level
- If already loaded, returns `alreadyLoaded: true`

## openAsset API
POST /api/open-asset
Required: `assetPath`
- Opens asset in its editor (Sequencer for LS, BP Editor for BP)
- For World assets: opens in level editor

## executePython API
POST /api/execute-python
Required: `script` (inline code) OR `file` (path to .py file)
- Runs via GEditor->Exec with "py -c" or "py"
- Output goes to Output Log, NOT returned in response
- Use for: switching maps, querying plugins, checking project settings

## CRITICAL: How to switch to the correct map
`load-level` does NOT switch maps. To switch to a different persistent level:
```python
# Via execute_python:
import unreal
unreal.EditorLevelLibrary.load_level('/Game/Maps/Game/Main/Levels/ML_Main')
```
This actually switches the persistent level. Then the sublevels that are already configured in the World Settings will load automatically.
