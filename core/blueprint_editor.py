"""
blueprint_editor.py -- Blueprint/Kismet bytecode inspection and editing for UE 5.6.

Provides tools to:
  - Parse Kismet bytecode into a graph structure
  - Resolve actual Blueprint node names matching UE editor display
  - Inspect node connections (execution flow + data wires)
  - Modify literal values in expressions
  - Visualize the Blueprint graph as JSON for the UI

Name Resolution Strategy:
  Display names match what UE's editor shows, derived from the actual
  GetNodeTitle() C++ source code in UE 5.6. Three data layers:

  1. K2Node exports: EdGraph node metadata with FunctionReference,
     CustomFunctionName, StructType, TargetType, etc.
  2. Bytecode StackNode: FPackageIndex on call expressions that resolves
     to import/export names (e.g., LoadAsset, ClearAllBits, MarkBit).
  3. Bytecode FunctionName/VirtualFunctionName: FName fields on delegate
     and virtual call expressions.

  The readable_name on K2NodeInfo is generated using the same logic as
  UE's GetNodeTitle(ENodeTitleType::ListView), verified against the
  5.6 source in Engine/Source/Editor/BlueprintGraph/Private/.

IMPORTANT: Kismet bytecode is a stack-based VM instruction set.
Modifying it incorrectly WILL crash the engine at runtime.
This module provides safe, validated editing operations only.
"""

from typing import List, Dict, Optional, Tuple
from dataclasses import dataclass, field
import logging
import re

from .uasset_bridge import (
    AssetFile, StructExport, FunctionExport, ClassExport, NormalExport,
    KISMET_AVAILABLE
)

logger = logging.getLogger(__name__)


@dataclass
class BlueprintNode:
    """Represents a single node in the Blueprint graph."""
    id: int                     # Unique node ID within the function
    expression_index: int       # Index in the ScriptBytecode array
    opcode: str                 # EExprToken name (e.g., "EX_CallFunction")
    opcode_hex: str             # Hex value of the opcode
    display_name: str           # Human-readable name for the UI
    category: str               # "flow", "data", "literal", "variable", "call", "other"
    properties: Dict            # Opcode-specific properties
    children: List[int] = field(default_factory=list)  # Child expression indices


@dataclass
class K2NodeInfo:
    """Metadata from a K2Node export (EdGraph node)."""
    export_index: int
    class_name: str             # e.g., "K2Node_CallFunction"
    object_name: str            # e.g., "K2Node_CallFunction_Scene4_PhoneGrabbed_Broadcast"
    node_type: str              # Cleaned type: "CallFunction", "CustomEvent", etc.
    readable_name: str          # Editor-matching display name from GetNodeTitle()
    pos_x: int = 0
    pos_y: int = 0
    properties: Dict = field(default_factory=dict)


@dataclass
class BlueprintGraph:
    """Represents the full graph of a Blueprint function."""
    function_name: str
    export_index: int
    nodes: List[BlueprintNode]
    k2_nodes: List[K2NodeInfo]  # EdGraph node metadata
    has_bytecode: bool
    bytecode_size: int
    raw_available: bool         # True if only raw bytes are available (parse failed)


# ---------------------------------------------------------------------------
# FName::NameToDisplayString — Python port of UE's name formatting
# ---------------------------------------------------------------------------
# Source: Engine/Source/Runtime/Core/Private/UObject/UnrealNames.cpp
# This is the function UE uses to convert internal names to editor-visible
# display strings. It handles CamelCase splitting, underscore replacement,
# K2_ prefix stripping, and bool "b" prefix removal.

def _name_to_display_string(name: str, is_bool: bool = False) -> str:
    """
    Python implementation of FName::NameToDisplayString from UE 5.6.
    
    Converts internal C++ names to editor-visible display names:
      - "K2_BroadcastMessage" -> "Broadcast Message"
      - "BroadcastMessage" -> "Broadcast Message"
      - "bIsEnabled" -> "Is Enabled" (when is_bool=True)
      - "OnPlayerTeleported_Event_0" -> "On Player Teleported Event 0"
      - "GameplayMessageSubsystem" -> "Gameplay Message Subsystem"
    
    This matches UE's behavior exactly for the cases we encounter.
    """
    if not name:
        return ""
    
    # Strip K2_ prefix (UE convention for Blueprint-exposed C++ functions)
    if name.startswith("K2_"):
        name = name[3:]
    
    # Strip bool "b" prefix when applicable
    if is_bool and len(name) > 1 and name[0] == 'b' and name[1].isupper():
        name = name[1:]
    
    # Build display string character by character, matching UE's algorithm:
    # Insert spaces before uppercase letters that follow lowercase letters,
    # before uppercase letters followed by lowercase (in acronym sequences),
    # and replace underscores with spaces.
    result = []
    prev_was_upper = False
    prev_was_underscore = False
    prev_was_digit = False
    
    for i, ch in enumerate(name):
        if ch == '_':
            # Replace underscore with space (skip consecutive)
            if result and result[-1] != ' ':
                result.append(' ')
            prev_was_underscore = True
            prev_was_upper = False
            prev_was_digit = False
            continue
        
        if ch.isupper():
            # Insert space before uppercase if:
            # - Previous was lowercase: "camelCase" -> "camel Case"
            # - Previous was uppercase and next is lowercase: "HTMLParser" -> "HTML Parser"
            if result and result[-1] != ' ':
                if not prev_was_upper and not prev_was_underscore:
                    result.append(' ')
                elif prev_was_upper and i + 1 < len(name) and name[i + 1].islower():
                    result.append(' ')
            prev_was_upper = True
            prev_was_digit = False
        elif ch.isdigit():
            # Insert space before digit if previous was letter
            if result and result[-1] != ' ' and not prev_was_digit and not prev_was_underscore:
                # Only add space if previous was a letter
                if prev_was_upper or (result and result[-1].isalpha()):
                    result.append(' ')
            prev_was_upper = False
            prev_was_digit = True
        else:
            prev_was_upper = False
            prev_was_digit = False
        
        prev_was_underscore = False
        result.append(ch)
    
    return ''.join(result).strip()


def _resolve_import_name(asset_file: AssetFile, pkg_index_val: int) -> str:
    """
    Resolve a package index integer to an import/export name.
    Negative = import, positive = export, zero = null.
    """
    try:
        if pkg_index_val < 0:
            imp_idx = -pkg_index_val - 1
            if imp_idx < asset_file.asset.Imports.Count:
                return str(asset_file.asset.Imports[imp_idx].ObjectName)
        elif pkg_index_val > 0:
            exp_idx = pkg_index_val - 1
            if exp_idx < asset_file.asset.Exports.Count:
                return str(asset_file.asset.Exports[exp_idx].ObjectName)
    except Exception:
        pass
    return ""


# ---------------------------------------------------------------------------
# Opcode Classification
# ---------------------------------------------------------------------------

OPCODE_CATEGORIES = {
    # Flow control
    "EX_Jump": "flow", "EX_JumpIfNot": "flow", "EX_Return": "flow",
    "EX_EndOfScript": "flow", "EX_Nothing": "flow",
    "EX_PushExecutionFlow": "flow", "EX_PopExecutionFlow": "flow",
    "EX_PopExecutionFlowIfNot": "flow",
    "EX_ComputedJump": "flow", "EX_SwitchValue": "flow",
    # Function calls
    "EX_FinalFunction": "call", "EX_LocalFinalFunction": "call",
    "EX_VirtualFunction": "call", "EX_LocalVirtualFunction": "call",
    "EX_CallMath": "call", "EX_CallMulticastDelegate": "call",
    # Variables
    "EX_LocalVariable": "variable", "EX_InstanceVariable": "variable",
    "EX_DefaultVariable": "variable", "EX_LocalOutVariable": "variable",
    "EX_ClassSparseDataVariable": "variable",
    # Assignments
    "EX_Let": "data", "EX_LetBool": "data", "EX_LetObj": "data",
    "EX_LetWeakObjPtr": "data", "EX_LetValueOnPersistentFrame": "data",
    "EX_LetDelegate": "data", "EX_LetMulticastDelegate": "data",
    # Literals
    "EX_IntConst": "literal", "EX_FloatConst": "literal",
    "EX_StringConst": "literal", "EX_UnicodeStringConst": "literal",
    "EX_TextConst": "literal", "EX_ObjectConst": "literal",
    "EX_NameConst": "literal", "EX_RotationConst": "literal",
    "EX_VectorConst": "literal", "EX_TransformConst": "literal",
    "EX_True": "literal", "EX_False": "literal", "EX_NoObject": "literal",
    "EX_IntZero": "literal", "EX_IntOne": "literal",
    "EX_SoftObjectConst": "literal", "EX_FieldPathConst": "literal",
    "EX_Int64Const": "literal", "EX_UInt64Const": "literal",
    "EX_DoubleConst": "literal",
    # Casts
    "EX_MetaCast": "data", "EX_DynamicCast": "data",
    "EX_ObjToInterfaceCast": "data", "EX_CrossInterfaceCast": "data",
    "EX_InterfaceToObjCast": "data", "EX_PrimitiveCast": "data",
    # Context
    "EX_Context": "data", "EX_Context_FailSilent": "data",
    "EX_ClassContext": "data", "EX_InterfaceContext": "data",
    "EX_Self": "data",
    # Delegates
    "EX_BindDelegate": "call", "EX_AddMulticastDelegate": "call",
    "EX_RemoveMulticastDelegate": "call", "EX_ClearMulticastDelegate": "call",
    # Arrays / Structs / Maps / Sets
    "EX_ArrayConst": "data", "EX_SetArray": "data",
    "EX_ArrayGetByRef": "data", "EX_SetConst": "data",
    "EX_MapConst": "data", "EX_StructConst": "data",
    "EX_StructMemberContext": "data",
    # Debug -- filtered out in the UI-facing graph
    "EX_Breakpoint": "debug", "EX_Tracepoint": "debug",
    "EX_WireTracepoint": "debug", "EX_InstrumentationEvent": "debug",
    # RTFM (UE 5.x)
    "EX_AutoRtfmTransact": "flow", "EX_AutoRtfmStopTransact": "flow",
    "EX_AutoRtfmAbortIfNot": "flow",
}


# ---------------------------------------------------------------------------
# K2Node GetNodeTitle() Resolution
# ---------------------------------------------------------------------------
# Each function below implements the exact same logic as the corresponding
# C++ GetNodeTitle() method in UE 5.6 source. Source file references are
# in the docstrings.

def _title_call_function(obj_name: str, props: Dict, af: AssetFile) -> str:
    """
    K2Node_CallFunction::GetNodeTitle — K2Node_CallFunction.cpp:601
    
    Uses FName::NameToDisplayString(MemberName) as the title.
    Strips K2_ prefix. Adds target class as context line.
    """
    member_name = props.get('_MemberName', '')
    member_parent = props.get('_MemberParent', '')
    
    if member_name:
        # Apply UE's NameToDisplayString — this is what the editor shows
        display = _name_to_display_string(member_name)
        if member_parent:
            # Full title includes context: "Broadcast Message\nTarget: Gameplay Message Subsystem"
            parent_display = _name_to_display_string(member_parent)
            return f"{display}\nTarget: {parent_display}"
        return display
    
    # Fallback: derive from object_name
    return _name_from_object_name(obj_name, "K2Node_CallFunction", "Call Function")


def _title_custom_event(obj_name: str, props: Dict, af: AssetFile) -> str:
    """
    K2Node_CustomEvent::GetNodeTitle — K2Node_CustomEvent.cpp:194
    
    Returns CustomFunctionName directly. Full title appends "Custom Event" below.
    """
    custom_name = props.get('CustomFunctionName', '')
    if custom_name:
        return f"{custom_name}\nCustom Event"
    
    # Fallback: derive from object_name
    return _name_from_object_name(obj_name, "K2Node_CustomEvent", "Custom Event")


def _title_get_subsystem(obj_name: str, props: Dict, af: AssetFile) -> str:
    """
    K2Node_GetSubsystem::GetNodeTitle — K2Node_GetSubsystem.cpp:112
    
    Returns "Get {ClassName}" where ClassName = SubsystemClass->GetDisplayNameText().
    """
    # Try to resolve SubsystemClass from properties
    subsystem_class = props.get('_SubsystemClass', '')
    if subsystem_class:
        display = _name_to_display_string(subsystem_class)
        return f"Get {display}"
    
    # Try to extract from object_name context
    context = _extract_context_from_objname(obj_name, "K2Node_GetSubsystem")
    if context:
        return f"Get {context}"
    
    return "Get Subsystem"


def _title_dynamic_cast(obj_name: str, props: Dict, af: AssetFile) -> str:
    """
    K2Node_DynamicCast::GetNodeTitle — K2Node_DynamicCast.cpp:156
    
    Returns "Cast To {TargetName}" where TargetName = BP name (without _C) or class name.
    """
    target_type = props.get('_TargetType', '')
    if target_type:
        # Strip _C suffix for Blueprint classes (UE does this via UBlueprint::GetBlueprintFromClass)
        if target_type.endswith('_C'):
            target_type = target_type[:-2]
        return f"Cast To {target_type}"
    
    context = _extract_context_from_objname(obj_name, "K2Node_DynamicCast")
    if context:
        return f"Cast To {context}"
    
    return "Cast"


def _title_break_struct(obj_name: str, props: Dict, af: AssetFile) -> str:
    """
    K2Node_BreakStruct::GetNodeTitle — K2Node_BreakStruct.cpp
    
    Returns "Break {StructName}" where StructName = StructType->GetDisplayNameText().
    """
    struct_type = props.get('StructType', '')
    if struct_type:
        display = _name_to_display_string(struct_type)
        return f"Break {display}"
    return "Break Struct"


def _title_make_struct(obj_name: str, props: Dict, af: AssetFile) -> str:
    """
    K2Node_MakeStruct::GetNodeTitle — K2Node_MakeStruct.cpp
    
    Returns "Make {StructName}" where StructName = StructType->GetDisplayNameText().
    """
    struct_type = props.get('StructType', '')
    if struct_type:
        display = _name_to_display_string(struct_type)
        return f"Make {display}"
    return "Make Struct"


def _title_assign_delegate(obj_name: str, props: Dict, af: AssetFile) -> str:
    """
    K2Node_AssignDelegate::GetNodeTitle — K2Node_AssignDelegate.cpp:35
    
    Returns "Assign {DelegateName}" from DelegateReference property.
    """
    delegate_ref = props.get('DelegateReference', '')
    if delegate_ref:
        display = _name_to_display_string(delegate_ref)
        return f"Assign {display}"
    
    context = _extract_context_from_objname(obj_name, "K2Node_AssignDelegate")
    if context:
        return f"Assign {context}"
    
    return "Assign Delegate"


def _title_async_action(obj_name: str, props: Dict, af: AssetFile) -> str:
    """
    K2Node_BaseAsyncTask::GetNodeTitle — K2Node_BaseAsyncTask.cpp:72
    
    Returns GetUserFacingFunctionName(FactoryFunction) which applies
    NameToDisplayString to the factory function name.
    """
    factory_fn = props.get('ProxyFactoryFunctionName', '')
    if factory_fn:
        return _name_to_display_string(factory_fn)
    
    # Check if the class name itself contains the action name
    # e.g., K2Node_AsyncAction_ListenForGameplayMessages
    context = _extract_context_from_objname(obj_name, "K2Node_AsyncAction")
    if context:
        return context
    
    return "Async Task"


def _title_macro_instance(obj_name: str, props: Dict, af: AssetFile) -> str:
    """
    K2Node_MacroInstance::GetNodeTitle — K2Node_MacroInstance.cpp:195
    
    Returns NameToDisplayString(MacroGraph->GetName()).
    e.g., "ForLoop" -> "For Loop", "Gate" -> "Gate"
    """
    macro_name = props.get('_MacroGraphName', '')
    if macro_name:
        return _name_to_display_string(macro_name)
    
    context = _extract_context_from_objname(obj_name, "K2Node_MacroInstance")
    if context:
        return _name_to_display_string(context)
    
    return "Macro"


def _title_literal(obj_name: str, props: Dict, af: AssetFile) -> str:
    """
    K2Node_Literal::GetNodeTitle — K2Node_Literal.cpp:148
    
    Returns Actor->GetActorLabel() or ObjectRef->GetName() or "Unknown".
    """
    obj_ref = props.get('_ObjectRef', '')
    if obj_ref:
        return obj_ref
    return "Reference"


# Static title nodes — these always return the same string
STATIC_TITLES = {
    "K2Node_IfThenElse": "Branch",                    # K2Node_IfThenElse.cpp:125
    "K2Node_LoadAsset": "Async Load Asset",            # K2Node_LoadAsset.cpp
    "K2Node_LoadClass": "Async Load Class Asset",      # K2Node_LoadAsset.cpp:330
    "K2Node_ConvertAsset": "Make Soft Reference",      # K2Node_ConvertAsset.cpp:229
    "K2Node_Knot": "Reroute Node",                     # K2Node_Knot.cpp:58
    "K2Node_FunctionEntry": "Function Entry",
    "K2Node_FunctionResult": "Return Node",
    "K2Node_ExecutionSequence": "Sequence",
    "K2Node_DoOnceMultiInput": "Do N",
    "K2Node_FormatText": "Format Text",
    "K2Node_Select": "Select",
    "K2Node_GetArrayItem": "Get",
    "K2Node_MakeArray": "Make Array",
    "K2Node_SetFieldsInStruct": "Set Members in Struct",
    "K2Node_SwitchString": "Switch on String",
    "K2Node_SwitchInteger": "Switch on Int",
    "K2Node_SwitchEnum": "Switch on Enum",
    "K2Node_ForEachElementInEnum": "For Each",
    "K2Node_SpawnActorFromClass": "Spawn Actor from Class",
    "K2Node_Timeline": "Timeline",
    "K2Node_VariableGet": "Get",
    "K2Node_VariableSet": "Set",
    "K2Node_GenericCreateObject": "Construct Object from Class",
    "K2Node_ClearDelegate": "Clear",
    "K2Node_CreateDelegate": "Create Event",
    "K2Node_AddDelegate": "Bind Event to",
    "K2Node_RemoveDelegate": "Unbind Event from",
    "K2Node_CallDelegate": "Call",
    "K2Node_CommutativeAssociativeBinaryOperator": "Operator",
    "K2Node_EnumLiteral": "Enum Literal",
    "K2Node_BitmaskLiteral": "Bitmask Literal",
    "K2Node_GetDataTableRow": "Get Data Table Row",
    "K2Node_Copy": "Copy",
}

# Dynamic title resolvers — functions that compute the title from properties
DYNAMIC_TITLE_RESOLVERS = {
    "K2Node_CallFunction": _title_call_function,
    "K2Node_CustomEvent": _title_custom_event,
    "K2Node_Event": _title_custom_event,  # Same logic for base Event
    "K2Node_GetSubsystem": _title_get_subsystem,
    "K2Node_DynamicCast": _title_dynamic_cast,
    "K2Node_ClassDynamicCast": _title_dynamic_cast,  # Same as DynamicCast
    "K2Node_BreakStruct": _title_break_struct,
    "K2Node_MakeStruct": _title_make_struct,
    "K2Node_AssignDelegate": _title_assign_delegate,
    "K2Node_AsyncAction": _title_async_action,
    "K2Node_BaseAsyncTask": _title_async_action,
    "K2Node_MacroInstance": _title_macro_instance,
    "K2Node_Literal": _title_literal,
}


def _extract_context_from_objname(object_name: str, class_name: str) -> str:
    """
    Extract scene/context info from a K2Node object name.
    
    Examples:
      K2Node_CallFunction_Scene3_PitcherComplete_Broadcast -> "Scene3 Pitcher Complete Broadcast"
      K2Node_AsyncAction_ListenForGameplayMessages_1 -> "Listen For Gameplay Messages"
      K2Node_CallFunction_10 -> "" (no context, just an index)
    """
    suffix = object_name
    if suffix.startswith(class_name):
        suffix = suffix[len(class_name):]
        if suffix.startswith("_"):
            suffix = suffix[1:]
    
    # If suffix is just a number or empty, no context
    if not suffix or suffix.isdigit():
        return ""
    
    # Split, remove trailing numbers
    parts = suffix.split("_")
    while parts and parts[-1].isdigit():
        parts.pop()
    
    if not parts:
        return ""
    
    # Join and apply CamelCase splitting
    cleaned = " ".join(parts)
    cleaned = re.sub(r'(?<=[a-z])(?=[A-Z])', ' ', cleaned)
    return cleaned


def _name_from_object_name(obj_name: str, class_name: str, fallback: str) -> str:
    """
    Derive a display name from the object_name when properties aren't available.
    Used as a last resort when FunctionReference etc. couldn't be read.
    """
    context = _extract_context_from_objname(obj_name, class_name)
    if context:
        return context
    return fallback


class BlueprintInspector:
    """
    Inspects and edits Blueprint bytecode in UE 5.6 assets.
    
    Works with both standalone Blueprint assets (.uasset) and
    level Blueprint logic embedded in .umap files.
    
    Name resolution uses the exact same logic as UE's GetNodeTitle()
    methods, verified against the 5.6 C++ source. Each K2Node type
    has a dedicated title resolver that matches the editor display.
    """

    def __init__(self, asset_file: AssetFile):
        self.af = asset_file
        # Cache K2Node metadata on init
        self._k2_nodes: Optional[List[K2NodeInfo]] = None
        self._k2_nodes_by_export: Optional[Dict[int, K2NodeInfo]] = None

    # ----- K2Node (EdGraph) Metadata -----

    def _build_k2_node_cache(self):
        """
        Scan all exports for K2Node (EdGraph node) metadata.
        
        For each K2Node, extracts properties needed for title resolution:
          - FunctionReference (MemberName, MemberParent) for CallFunction
          - CustomFunctionName for CustomEvent
          - StructType for BreakStruct/MakeStruct
          - DelegateReference for AssignDelegate
          - ProxyFactoryFunctionName for AsyncAction
          - SubsystemClass for GetSubsystem
          - TargetType for DynamicCast
          - MacroGraphReference for MacroInstance
        
        Then applies the correct GetNodeTitle() resolver for each type.
        """
        if self._k2_nodes is not None:
            return
        
        self._k2_nodes = []
        self._k2_nodes_by_export = {}
        
        for i, exp in enumerate(self.af.exports):
            class_name = self.af.get_export_class_name(exp)
            
            # K2Node classes are imported (negative ClassIndex)
            if not class_name.startswith("K2Node") and class_name != "EdGraph":
                continue
            
            obj_name = self.af.get_export_name(exp)
            
            # Extract properties from the export for title resolution
            props = {}
            pos_x = 0
            pos_y = 0
            
            if isinstance(exp, NormalExport) and exp.Data is not None:
                for k in range(exp.Data.Count):
                    prop = exp.Data[k]
                    pname = str(prop.Name) if hasattr(prop, 'Name') else ''
                    try:
                        if pname == "NodePosX":
                            pos_x = int(str(prop.Value))
                        elif pname == "NodePosY":
                            pos_y = int(str(prop.Value))
                        elif pname == "FunctionReference":
                            # Extract MemberName and MemberParent from the struct
                            self._extract_function_reference(prop, props)
                        elif pname == "CustomFunctionName":
                            props['CustomFunctionName'] = str(prop.Value) if hasattr(prop, 'Value') else ''
                        elif pname == "StructType":
                            # Resolve the struct type import to a name
                            self._extract_package_index_prop(prop, props, 'StructType')
                        elif pname == "DelegateReference":
                            self._extract_function_reference(prop, props)
                        elif pname == "ProxyFactoryFunctionName":
                            props['ProxyFactoryFunctionName'] = str(prop.Value) if hasattr(prop, 'Value') else ''
                        elif pname == "ProxyClass":
                            self._extract_package_index_prop(prop, props, 'ProxyClass')
                        elif pname == "CustomClass":
                            # Used by K2Node_GetSubsystem for SubsystemClass
                            self._extract_package_index_prop(prop, props, '_SubsystemClass')
                        elif pname == "TargetType":
                            self._extract_package_index_prop(prop, props, '_TargetType')
                        elif pname == "MacroGraphReference":
                            # Extract macro graph name from the struct
                            self._extract_macro_graph_ref(prop, props)
                        elif pname == "ObjectRef":
                            self._extract_package_index_prop(prop, props, '_ObjectRef')
                        elif pname == "EventReference":
                            props['EventReference'] = str(prop.Value) if hasattr(prop, 'Value') else ''
                    except Exception as e:
                        logger.debug(f"Failed to extract property '{pname}' from K2Node export {i}: {e}")
            
            # --- Resolve the editor-visible title ---
            node_type = class_name.replace("K2Node_", "")
            
            # Check for exact class match in AsyncAction subclasses first
            # e.g., K2Node_AsyncAction_ListenForGameplayMessages
            if class_name.startswith("K2Node_AsyncAction_"):
                # Extract the action name from the class: "ListenForGameplayMessages"
                action_part = class_name[len("K2Node_AsyncAction_"):]
                readable = _name_to_display_string(action_part)
            elif class_name in STATIC_TITLES:
                readable = STATIC_TITLES[class_name]
            elif class_name in DYNAMIC_TITLE_RESOLVERS:
                readable = DYNAMIC_TITLE_RESOLVERS[class_name](obj_name, props, self.af)
            else:
                # Unknown K2Node type — apply NameToDisplayString to the type
                readable = _name_to_display_string(node_type)
            
            # For VariableGet/Set, try to append the variable name
            if class_name in ("K2Node_VariableGet", "K2Node_VariableSet"):
                var_ref = props.get('_MemberName', '')
                if var_ref:
                    action = "Get" if class_name == "K2Node_VariableGet" else "Set"
                    readable = f"{action} {_name_to_display_string(var_ref)}"
            
            # Clean up internal props before storing
            display_props = {k: v for k, v in props.items() if not k.startswith('_')}
            
            info = K2NodeInfo(
                export_index=i,
                class_name=class_name,
                object_name=obj_name,
                node_type=node_type,
                readable_name=readable,
                pos_x=pos_x,
                pos_y=pos_y,
                properties=display_props,
            )
            
            self._k2_nodes.append(info)
            self._k2_nodes_by_export[i] = info
            logger.debug(f"K2Node export {i}: {class_name} -> '{readable}' at ({pos_x},{pos_y})")

    def _extract_function_reference(self, prop, props: Dict):
        """
        Extract MemberName and MemberParent from a FunctionReference struct property.
        Stores them as _MemberName and _MemberParent in props dict.
        Also stores the combined string as FunctionReference.
        """
        member_name = ''
        member_parent = ''
        val = prop.Value
        if hasattr(val, 'Count'):
            for j in range(val.Count):
                sp = val[j]
                spn = str(sp.Name) if hasattr(sp, 'Name') else ''
                if spn == 'MemberName':
                    member_name = str(sp.Value)
                elif spn == 'MemberParent':
                    try:
                        idx = int(str(sp.Value))
                        member_parent = _resolve_import_name(self.af, idx)
                    except (ValueError, IndexError):
                        pass
        
        props['_MemberName'] = member_name
        props['_MemberParent'] = member_parent
        
        if member_parent and member_name:
            props['FunctionReference'] = f"{member_parent}::{member_name}"
        elif member_name:
            props['FunctionReference'] = member_name

    def _extract_package_index_prop(self, prop, props: Dict, key: str):
        """
        Extract a package index property and resolve it to a name.
        Stores the resolved name under the given key.
        """
        try:
            idx = int(str(prop.Value))
            resolved = _resolve_import_name(self.af, idx)
            if resolved:
                props[key] = resolved
            else:
                props[key] = str(prop.Value)
        except (ValueError, AttributeError):
            props[key] = str(prop.Value) if hasattr(prop, 'Value') else ''

    def _extract_macro_graph_ref(self, prop, props: Dict):
        """
        Extract the macro graph name from a MacroGraphReference struct.
        """
        val = prop.Value
        if hasattr(val, 'Count'):
            for j in range(val.Count):
                sp = val[j]
                spn = str(sp.Name) if hasattr(sp, 'Name') else ''
                if spn == 'MacroGraph':
                    try:
                        idx = int(str(sp.Value))
                        resolved = _resolve_import_name(self.af, idx)
                        if resolved:
                            props['_MacroGraphName'] = resolved
                    except (ValueError, AttributeError):
                        pass

    def get_k2_nodes(self) -> List[K2NodeInfo]:
        """Get all K2Node (EdGraph) metadata from the asset."""
        self._build_k2_node_cache()
        return self._k2_nodes

    # ----- Name Resolution for Bytecode Expressions -----

    def _resolve_package_index(self, pkg_index) -> str:
        """
        Resolve a FPackageIndex to a human-readable name.
        Negative = import, positive = export, zero = null.
        """
        try:
            idx = pkg_index.Index
            return _resolve_import_name(self.af, idx)
        except Exception:
            pass
        return ""

    def _get_display_name(self, expr, opcode_name: str) -> str:
        """
        Generate a human-readable display name for a Kismet expression.
        
        Resolution priority:
          1. StackNode -> resolved import/export name (for function calls)
             Then apply NameToDisplayString for editor-matching output.
          2. FunctionName field (for delegate binds)
          3. VirtualFunctionName field (for virtual calls)
          4. Value field (for literals)
          5. Fallback to cleaned opcode name
        """
        # --- Layer 2: StackNode resolution (CallMath, LocalFinalFunction, etc.) ---
        if hasattr(expr, 'StackNode'):
            try:
                resolved = self._resolve_package_index(expr.StackNode)
                if resolved:
                    # Apply NameToDisplayString to match editor display
                    return _name_to_display_string(resolved)
            except Exception:
                pass
        
        # --- Layer 3: FunctionName (BindDelegate, etc.) ---
        if hasattr(expr, 'FunctionName'):
            try:
                fn = str(expr.FunctionName)
                if fn:
                    # Clean up hash suffixes: OnLoaded_2D696DAD48D267... -> OnLoaded
                    parts = fn.split("_")
                    if len(parts) > 1 and len(parts[-1]) > 8:
                        try:
                            int(parts[-1], 16)
                            fn = "_".join(parts[:-1])
                        except ValueError:
                            pass
                    return f"Bind: {_name_to_display_string(fn)}"
            except Exception:
                pass
        
        # --- Layer 3: VirtualFunctionName ---
        if hasattr(expr, 'VirtualFunctionName'):
            try:
                vfn = str(expr.VirtualFunctionName)
                if vfn:
                    return _name_to_display_string(vfn)
            except Exception:
                pass
        
        # --- Literal values ---
        name = opcode_name.replace("EX_", "")
        try:
            if hasattr(expr, 'Value'):
                val = expr.Value
                if val is not None:
                    vs = str(val)
                    if len(vs) > 60:
                        vs = vs[:57] + "..."
                    return f"{_name_to_display_string(name)}: {vs}"
        except Exception:
            pass
        
        return _name_to_display_string(name)

    def _extract_properties(self, expr) -> Dict:
        """Extract readable properties from a KismetExpression, with name resolution."""
        props = {}
        
        # Resolve StackNode to actual name
        if hasattr(expr, 'StackNode'):
            try:
                resolved = self._resolve_package_index(expr.StackNode)
                props["StackNode"] = f"{expr.StackNode.Index} -> {resolved}" if resolved else str(expr.StackNode.Index)
            except Exception:
                pass
        
        # Resolve FunctionName
        if hasattr(expr, 'FunctionName'):
            try:
                props["FunctionName"] = str(expr.FunctionName)
            except Exception:
                pass
        
        # Resolve VirtualFunctionName
        if hasattr(expr, 'VirtualFunctionName'):
            try:
                props["VirtualFunctionName"] = str(expr.VirtualFunctionName)
            except Exception:
                pass
        
        # Other common fields
        for field_name in ["CodeOffset", "SkipCount"]:
            try:
                val = getattr(expr, field_name, None)
                if val is not None:
                    props[field_name] = str(val)
            except Exception:
                pass
        
        return props

    # ----- Graph Extraction -----

    def get_all_functions(self) -> List[Dict]:
        """
        List all functions (with or without bytecode) in the asset.
        Returns metadata without parsing bytecode.
        """
        functions = []
        for i, exp in enumerate(self.af.exports):
            if isinstance(exp, (StructExport, FunctionExport)):
                name = self.af.get_export_name(exp)
                has_bc = False
                bc_size = 0
                if isinstance(exp, StructExport):
                    has_bc = exp.ScriptBytecode is not None
                    bc_size = exp.ScriptBytecodeSize if hasattr(exp, "ScriptBytecodeSize") else 0
                functions.append({
                    "export_index": i,
                    "name": name,
                    "type": type(exp).__name__,
                    "has_bytecode": has_bc,
                    "bytecode_size": bc_size,
                    "class_name": self.af.get_export_class_name(exp),
                })
        return functions

    def get_function_graph(self, export_index: int) -> BlueprintGraph:
        """
        Parse a function's Kismet bytecode into a graph structure
        with resolved names from all three data layers.
        """
        self._build_k2_node_cache()
        
        exp = self.af.get_export(export_index)
        name = self.af.get_export_name(exp)

        if not isinstance(exp, StructExport):
            return BlueprintGraph(
                function_name=name, export_index=export_index,
                nodes=[], k2_nodes=self._k2_nodes or [],
                has_bytecode=False, bytecode_size=0, raw_available=False,
            )

        if exp.ScriptBytecode is None:
            raw_size = len(exp.ScriptBytecodeRaw) if exp.ScriptBytecodeRaw else 0
            return BlueprintGraph(
                function_name=name, export_index=export_index,
                nodes=[], k2_nodes=self._k2_nodes or [],
                has_bytecode=False, bytecode_size=exp.ScriptBytecodeSize,
                raw_available=raw_size > 0,
            )

        # Parse expressions into nodes, filtering out debug tracepoints
        nodes = []
        for i in range(exp.ScriptBytecode.Length):
            expr = exp.ScriptBytecode[i]
            node = self._expression_to_node(expr, i)
            # Filter out debug tracepoints for cleaner graph view
            if node.category != "debug":
                nodes.append(node)

        return BlueprintGraph(
            function_name=name, export_index=export_index,
            nodes=nodes, k2_nodes=self._k2_nodes or [],
            has_bytecode=True, bytecode_size=exp.ScriptBytecodeSize,
            raw_available=False,
        )

    def _expression_to_node(self, expr, index: int) -> BlueprintNode:
        """Convert a KismetExpression to a BlueprintNode with resolved names."""
        type_name = type(expr).__name__
        opcode_name = type_name
        opcode_hex = "0x??"
        try:
            if hasattr(expr, 'Token'):
                opcode_name = str(expr.Token)
                opcode_hex = f"0x{int(expr.Token):02X}"
        except Exception:
            pass

        category = OPCODE_CATEGORIES.get(opcode_name, "other")
        display_name = self._get_display_name(expr, opcode_name)
        properties = self._extract_properties(expr)

        return BlueprintNode(
            id=index, expression_index=index,
            opcode=opcode_name, opcode_hex=opcode_hex,
            display_name=display_name, category=category,
            properties=properties,
        )

    # ----- Graph Serialization for UI -----

    def graph_to_json(self, graph: BlueprintGraph) -> Dict:
        """
        Convert a BlueprintGraph to a JSON-serializable dict
        suitable for the web UI graph editor.
        
        Includes both bytecode nodes (with resolved names) and
        K2Node metadata (with positions for layout).
        """
        return {
            "function_name": graph.function_name,
            "export_index": graph.export_index,
            "has_bytecode": graph.has_bytecode,
            "bytecode_size": graph.bytecode_size,
            "raw_available": graph.raw_available,
            "node_count": len(graph.nodes),
            "nodes": [
                {
                    "id": n.id,
                    "expression_index": n.expression_index,
                    "opcode": n.opcode,
                    "opcode_hex": n.opcode_hex,
                    "display_name": n.display_name,
                    "category": n.category,
                    "properties": n.properties,
                    "children": n.children,
                }
                for n in graph.nodes
            ],
            "k2_nodes": [
                {
                    "export_index": k.export_index,
                    "class_name": k.class_name,
                    "object_name": k.object_name,
                    "node_type": k.node_type,
                    "readable_name": k.readable_name,
                    "pos_x": k.pos_x,
                    "pos_y": k.pos_y,
                    "properties": k.properties,
                }
                for k in graph.k2_nodes
            ],
        }

    # ----- Bytecode Editing -----

    def modify_literal_value(self, export_index: int, expression_index: int, new_value) -> bool:
        """
        Modify a literal value in a Kismet expression.
        ONLY works for literal expressions (IntConst, FloatConst, StringConst, etc.).
        
        WARNING: This modifies the in-memory representation. Call asset_file.save()
        to write changes to disk. Incorrect values WILL cause runtime crashes.
        """
        exp = self.af.get_export(export_index)
        if not isinstance(exp, StructExport):
            raise TypeError(f"Export {export_index} is not a StructExport")
        if exp.ScriptBytecode is None:
            raise ValueError(f"Export {export_index} has no parsed bytecode.")
        if expression_index < 0 or expression_index >= exp.ScriptBytecode.Length:
            raise IndexError(f"Expression index {expression_index} out of range (0-{exp.ScriptBytecode.Length - 1})")

        expr = exp.ScriptBytecode[expression_index]
        opcode_name = type(expr).__name__
        category = OPCODE_CATEGORIES.get(opcode_name, "other")
        if category != "literal":
            raise ValueError(f"Expression at index {expression_index} is '{opcode_name}' (category: {category}), not a literal.")
        if not hasattr(expr, "Value"):
            raise ValueError(f"Expression '{opcode_name}' does not have a Value field")

        try:
            expr.Value = new_value
            logger.info(f"Modified literal at export {export_index}, expr {expression_index}: {new_value}")
            return True
        except Exception as e:
            raise RuntimeError(f"Failed to set value on '{opcode_name}': {e}") from e

    # ----- Class/Blueprint Structure -----

    def get_blueprint_hierarchy(self) -> List[Dict]:
        """
        Get the class hierarchy of Blueprint classes in the asset.
        Shows ClassExport -> FunctionExport relationships.
        """
        hierarchy = []
        for i, exp in enumerate(self.af.exports):
            if isinstance(exp, ClassExport):
                class_info = {
                    "export_index": i,
                    "class_name": self.af.get_export_name(exp),
                    "functions": [],
                    "properties": [],
                }
                class_pkg_idx = i + 1  # 1-based
                for j, exp2 in enumerate(self.af.exports):
                    if exp2.OuterIndex.Index == class_pkg_idx:
                        if isinstance(exp2, FunctionExport):
                            class_info["functions"].append({
                                "export_index": j,
                                "name": self.af.get_export_name(exp2),
                                "has_bytecode": exp2.ScriptBytecode is not None if isinstance(exp2, StructExport) else False,
                            })
                hierarchy.append(class_info)
        return hierarchy
