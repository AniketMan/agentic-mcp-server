# UE5 Blueprint Paste Text Format Specification

> Reverse-engineered from SL_Trailer_Logic.umap (UE 5.6 Oculus fork).
> This is the clipboard format used when you Ctrl+C Blueprint nodes in the UE5 editor.
> Generated text in this format can be Ctrl+V'd directly into any Blueprint EventGraph.

## Overview

The paste text is a sequence of `Begin Object ... End Object` blocks. Each block represents one Blueprint node (K2Node). The format is **plain text**, not binary. Nodes reference each other by name within `LinkedTo` fields on their pins.

## Top-Level Structure

```
Begin Object Class=<NodeClassPath> Name="<NodeName>" ExportPath="<FullExportPath>"
   <NodeProperty>=<Value>
   <NodeProperty>=<Value>
   CustomProperties Pin (<PinDefinition>)
   CustomProperties Pin (<PinDefinition>)
   CustomProperties UserDefinedPin (<UserPinDefinition>)
End Object
```

### Fields

| Field | Description | Example |
|-------|-------------|---------|
| `Class` | Full script path to the K2Node class | `/Script/BlueprintGraph.K2Node_CallFunction` |
| `Name` | Unique node name within the graph | `K2Node_CallFunction_4` |
| `ExportPath` | Full path including level, graph, and node | `...'/Game/Maps/.../SL_Trailer_Logic.EventGraph.K2Node_CallFunction_4'` |

## Node Types Catalog

All node types observed in the working SL_Trailer_Logic level:

| Node Class | Blueprint Name | Purpose |
|------------|---------------|---------|
| `K2Node_Event` | Event BeginPlay | Lifecycle event — fires when level starts |
| `K2Node_CustomEvent` | Custom Event | User-defined event with custom name and pins |
| `K2Node_CallFunction` | Function Call | Calls a C++ or Blueprint function |
| `K2Node_AsyncAction_ListenForGameplayMessages` | Listen for Gameplay Messages | Async node — listens on a GameplayTag channel |
| `K2Node_BreakStruct` | Break Struct | Splits a struct into individual pin outputs |
| `K2Node_MakeStruct` | Make Struct | Constructs a struct from individual pin inputs |
| `K2Node_LoadAsset` | Async Load Asset | Asynchronously loads a soft object reference |
| `K2Node_DynamicCast` | Cast To | Dynamic cast to a specific class |
| `K2Node_VariableGet` | Get Variable | Reads a member variable from an object |
| `K2Node_GetSubsystem` | Get Subsystem | Gets a UGameInstanceSubsystem by class |
| `K2Node_AssignDelegate` | Assign Delegate | Binds a delegate to an event |
| `K2Node_CreateDelegate` | Create Delegate | Creates a delegate reference to a function |
| `K2Node_Literal` | Actor Reference | References a specific actor instance in the level |
| `K2Node_ConvertAsset` | Convert Asset | Converts object ref to soft object ref |
| `K2Node_MacroInstance` | Macro (IsValid) | Instance of a standard macro |
| `K2Node_PromotableOperator` | == (EqualEqual) | Comparison operator |
| `K2Node_IfThenElse` | Branch | Conditional branch (if/else) |
| `K2Node_MultiGate` | MultiGate | Routes execution to one of N outputs sequentially |
| `K2Node_Knot` | Reroute | Wire routing node (no logic, just visual cleanup) |

## Node Properties (Per Node Type)

### K2Node_Event
```
EventReference=(MemberParent="/Script/CoreUObject.Class'/Script/Engine.Actor'",MemberName="ReceiveBeginPlay")
bOverrideFunction=True
NodeGuid=<32-char hex>
```

### K2Node_CustomEvent
```
CustomFunctionName="OnPlayerTeleported_Event_0"
NodePosX=-240
NodePosY=1568
NodeGuid=<32-char hex>
```
- Can have `CustomProperties UserDefinedPin` entries for custom output pins.

### K2Node_CallFunction
```
FunctionReference=(MemberParent="/Script/CoreUObject.Class'/Script/SohVr.EnablerComponent'",MemberName="EnableActor")
NodePosX=1808
NodePosY=608
NodeGuid=<32-char hex>
```
- For self-context calls: `FunctionReference=(MemberName="ListenMessages",MemberGuid=<guid>,bSelfContext=True)`
- `ErrorType=1` appears on some nodes (indicates a compile warning, not a fatal error).

### K2Node_AsyncAction_ListenForGameplayMessages
```
ProxyFactoryFunctionName="ListenForGameplayMessages"
ProxyFactoryClass="/Script/CoreUObject.Class'/Script/GameplayMessageRuntime.AsyncAction_ListenForGameplayMessage'"
ProxyClass="/Script/CoreUObject.Class'/Script/GameplayMessageRuntime.AsyncAction_ListenForGameplayMessage'"
```

### K2Node_DynamicCast
```
TargetType="/Script/CoreUObject.Class'/Script/SohVr.TeleportPoint'"
```

### K2Node_VariableGet
```
VariableReference=(MemberParent="/Script/CoreUObject.Class'/Script/SohVr.TeleportPoint'",MemberName="EnablerComponent")
SelfContextInfo=NotSelfContext
```

### K2Node_GetSubsystem
```
CustomClass="/Script/CoreUObject.Class'/Script/GameplayMessageRuntime.GameplayMessageSubsystem'"
```

### K2Node_Literal (Actor Reference)
```
ObjectRef="/Game/Blueprints/Game/BP_TeleportPoint.BP_TeleportPoint_C'/Game/Maps/Game/Trailer/Levels/SLs/SL_Trailer_Logic.SL_Trailer_Logic:PersistentLevel.BP_TeleportPoint_C_0'"
```
- References a specific actor placed in the level by its full path.

### K2Node_MacroInstance (IsValid)
```
MacroGraphReference=(MacroGraph="/Script/Engine.EdGraph'/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:IsValid'",GraphBlueprint="/Script/Engine.Blueprint'/Engine/EditorBlueprintResources/StandardMacros.StandardMacros'",GraphGuid=64422BCD430703FF5CAEA8B79A32AA65)
```

### K2Node_PromotableOperator (==)
```
OperationName="EqualEqual"
bDefaultsToPureFunc=True
FunctionReference=(MemberParent="/Script/CoreUObject.Class'/Script/Engine.KismetMathLibrary'",MemberName="EqualEqual_ObjectObject")
```

### K2Node_MakeStruct / K2Node_BreakStruct
```
bMadeAfterOverridePinRemoval=True
ShowPinForProperties(0)=(PropertyName="Step_4_9162A20A46E747E6062EFAAD47D66DAA",PropertyFriendlyName="Step",bShowPin=True,bCanToggleVisibility=True)
StructType="/Script/CoreUObject.UserDefinedStruct'/Game/Blueprints/Data/Message/Msg_StoryStep.Msg_StoryStep'"
```

### K2Node_MultiGate
- Has `Out 0`, `Out 1`, ... output exec pins.
- Has `Reset`, `IsRandom`, `Loop`, `StartIndex` input pins.

### K2Node_AssignDelegate
```
DelegateReference=(MemberParent="/Script/CoreUObject.Class'/Script/SohVr.TeleportPoint'",MemberName="OnPlayerTeleported")
```

### K2Node_CreateDelegate
```
SelectedFunctionName="OnPlayerTeleported_Event_0"
SelectedFunctionGuid=B5599AA34CDCD8C76D8E2F92197E217C
```

## Pin Format

Every pin is a `CustomProperties Pin (...)` line with comma-separated key=value pairs inside parentheses.

### Pin Fields

| Field | Type | Description |
|-------|------|-------------|
| `PinId` | 32-char hex | Unique ID for this pin within the node |
| `PinName` | string | Internal name (e.g., `"execute"`, `"then"`, `"self"`) |
| `PinFriendlyName` | NSLOCTEXT/INVTEXT | Display name (optional) |
| `PinToolTip` | string | Tooltip text (optional) |
| `Direction` | enum | `"EGPD_Output"` for outputs. Omitted = input (EGPD_Input is default) |
| `PinType.PinCategory` | string | `"exec"`, `"object"`, `"struct"`, `"bool"`, `"int"`, `"delegate"`, `"softobject"`, `"byte"`, `""` |
| `PinType.PinSubCategory` | string | Usually `""`, can be `"self"` for self pins |
| `PinType.PinSubCategoryObject` | path/None | Class or struct path for typed pins |
| `PinType.PinSubCategoryMemberReference` | struct | `()` or `(MemberParent=...,MemberName=...)` for delegates |
| `PinType.PinValueType` | struct | Always `()` in observed data |
| `PinType.ContainerType` | enum | `None` (could be Array, Set, Map) |
| `PinType.bIsReference` | bool | `False` normally, `True` for ref params |
| `PinType.bIsConst` | bool | `False` normally, `True` for const ref |
| `PinType.bIsWeakPointer` | bool | Always `False` in observed data |
| `PinType.bIsUObjectWrapper` | bool | Always `False` in observed data |
| `PinType.bSerializeAsSinglePrecisionFloat` | bool | Always `False` in observed data |
| `DefaultValue` | string | Default value for the pin (e.g., `"true"`, `"3"`, `"(TagName=\"Message.Event.StoryStep\")"`) |
| `AutogeneratedDefaultValue` | string | Engine-generated default |
| `DefaultObject` | path | Default object reference |
| `LinkedTo` | list | Comma-separated `NodeName PinId` pairs for wired connections |
| `PersistentGuid` | 32-char hex | `00000000000000000000000000000000` for most pins, non-zero for user-defined struct member pins |
| `bHidden` | bool | Whether pin is hidden in the UI |
| `bNotConnectable` | bool | Whether pin can accept connections |
| `bDefaultValueIsReadOnly` | bool | |
| `bDefaultValueIsIgnored` | bool/True | Set to True for struct input pins |
| `bAdvancedView` | bool | Whether pin is in "Advanced" section |
| `bOrphanedPin` | bool | Whether pin is orphaned |

### LinkedTo Format

```
LinkedTo=(TargetNodeName TargetPinId,AnotherNodeName AnotherPinId,)
```

- Space-separated: `NodeName` then `PinId`
- Multiple connections separated by commas
- Trailing comma before closing paren

### Pin Direction

- **Input pins**: `Direction` field is omitted (EGPD_Input is default)
- **Output pins**: `Direction="EGPD_Output"`

### Pin Categories

| Category | Meaning | SubCategoryObject |
|----------|---------|-------------------|
| `exec` | Execution flow | None |
| `object` | UObject reference | Class path |
| `struct` | Struct value | Struct path |
| `bool` | Boolean | None |
| `int` | Integer | None |
| `delegate` | Delegate | None (MemberReference in PinSubCategoryMemberReference) |
| `softobject` | Soft object reference | Class path |
| `byte` | Byte/Enum | Enum path |
| `""` (empty) | Wildcard/unused | None |

## UserDefinedPin Format

Custom events can define their own output pins:

```
CustomProperties UserDefinedPin (PinName="TeleportPoint",PinType=(PinCategory="object",PinSubCategoryObject="/Script/CoreUObject.Class'/Script/SohVr.TeleportPoint'"),DesiredPinDirection=EGPD_Output)
```

## NodeGuid

Every node has a `NodeGuid` — a 32-character uppercase hex string. This must be **unique** across the entire graph. When generating paste text, generate random GUIDs.

## PinId

Every pin has a `PinId` — a 32-character uppercase hex string. This must be **unique** across the entire graph. When generating paste text, generate random PinIds. The format matches UE4/5 FGuid: 8-8-8-8 hex digits concatenated without dashes.

## ExportPath

The ExportPath encodes the full hierarchy:

```
/Script/BlueprintGraph.K2Node_CallFunction'/Game/Maps/Game/Trailer/Levels/SLs/SL_Trailer_Logic.SL_Trailer_Logic:PersistentLevel.SL_Trailer_Logic.EventGraph.K2Node_CallFunction_4'
```

Structure: `<ClassPath>'<PackagePath>.<AssetName>:<Outer>.<BlueprintName>.<GraphName>.<NodeName>'`

When pasting into a different level, the editor **rewrites** the ExportPath automatically. So the ExportPath in generated paste text can use a placeholder level name — the editor will fix it on paste.

## NodePosX / NodePosY

Integer coordinates for node placement in the graph editor. Nodes are laid out left-to-right (increasing X) and top-to-bottom (increasing Y). Typical spacing:

- Horizontal: 256-400 px between nodes
- Vertical: 128-256 px between parallel chains

## Key Observations

1. **PinIds can be reused across different nodes** — the same PinId string appears on pins of different nodes (e.g., `B6A767254BF982204B67A282263ABD2B` appears on multiple `execute` pins). This is valid because LinkedTo references use `NodeName PinId` pairs.

2. **LinkedTo is bidirectional** — if Node A's output links to Node B's input, Node B's input also links back to Node A's output.

3. **The editor auto-resolves** — when pasting, the editor resolves class paths, updates ExportPaths, and validates connections. Invalid connections are silently dropped.

4. **NSLOCTEXT format**: `NSLOCTEXT("Namespace", "Key", "DisplayText")` — used for localized pin names.

5. **INVTEXT format**: `INVTEXT("Text")` — invariant (non-localized) text.

6. **Struct default values**: GameplayTag defaults use `(TagName="Message.Event.TeleportPoint")` format.

7. **Soft object references**: Use `PinCategory="softobject"` with the class in PinSubCategoryObject.
