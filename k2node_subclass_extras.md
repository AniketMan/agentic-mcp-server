# K2Node Subclass Extra Serialization Fields

After the base UEdGraphNode pin array, each K2Node subclass may serialize additional data.

## Inheritance Chain and Extra Fields

### UEdGraphNode
- Pin array (SerializeAsOwningNode)

### UK2Node (extends UEdGraphNode)
- No extra serialization after Super

### UK2Node_EditablePinBase (extends UK2Node)
- `TArray<FUserPinInfo> SerializedItems` (int32 count + per-item data)
  - Each FUserPinInfo: FName PinName + FEdGraphPinType + uint8 Direction + FString DefaultValue

### UK2Node_Event (extends UK2Node_EditablePinBase)
- No extra serialization (just version fixups on load)

### UK2Node_CustomEvent (extends UK2Node_Event)
- No extra serialization (just version fixups on load)

### UK2Node_DynamicCast (extends UK2Node)
- `EPureState PureState` (uint8) - for UE >= FFortniteMainBranchObjectVersion::DynamicCastNodesUsePureStateEnum
- OR `bIsPureCast_DEPRECATED` (bool) for older versions

### UK2Node_MacroInstance (extends UK2Node)
- No extra serialization (version fixup only on very old assets)

### UK2Node_CallFunction (extends UK2Node)
- No extra serialization (version fixups only on very old assets)

### UK2Node_GetSubsystem (extends UK2Node)
- No extra serialization

### UK2Node_IfThenElse (extends UK2Node)
- No Serialize override found -> no extra fields

### UK2Node_Knot (extends UK2Node)
- No Serialize override found -> no extra fields

### UK2Node_Literal (extends UK2Node)
- No Serialize override found -> no extra fields

### UK2Node_LoadAsset (extends UK2Node)
- No Serialize override found -> no extra fields

### UK2Node_MakeStruct (extends UK2Node)
- No Serialize override found -> no extra fields

### UK2Node_MultiGate (extends UK2Node)
- No Serialize override found -> no extra fields

### UK2Node_PromotableOperator (extends UK2Node)
- No Serialize override found -> no extra fields

### UK2Node_VariableGet (extends UK2Node)
- No Serialize override found -> no extra fields

## Summary: Types Used in Project Paste Text

For the VR project, the node types in the paste text files are:
- K2Node_CustomEvent -> pins + TArray<FUserPinInfo>
- K2Node_CallFunction -> pins only
- K2Node_GetSubsystem -> pins only
- K2Node_MakeStruct -> pins only
- K2Node_IfThenElse -> pins only
- K2Node_DynamicCast -> pins + uint8 PureState
- K2Node_VariableGet -> pins only

Most node types need ONLY the pin array. The exceptions are:
1. K2Node_CustomEvent/K2Node_Event: need TArray<FUserPinInfo> after pins
2. K2Node_DynamicCast: need uint8 PureState after pins
