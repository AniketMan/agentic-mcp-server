"""
UE5 Blueprint Paste Text Generator

Generates paste-ready text that can be Ctrl+V'd directly into the UE5 Blueprint editor.
Reverse-engineered from SL_Trailer_Logic.umap (UE 5.6 Oculus fork).

Usage:
    from core.paste_text_gen import Graph, Node, Pin, PinType, PinDirection

    g = Graph("SL_MyLevel")
    begin_play = g.add_event_begin_play()
    print_str = g.add_call_function("KismetSystemLibrary", "PrintString")
    g.connect(begin_play, "then", print_str, "execute")
    print(g.to_paste_text())
"""

import uuid
import textwrap
from dataclasses import dataclass, field
from typing import Optional, List, Dict, Tuple
from enum import Enum


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

# Standard macros
ISVALID_MACRO_GRAPH = "/Script/Engine.EdGraph'/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:IsValid'"
ISVALID_MACRO_BP = "/Script/Engine.Blueprint'/Engine/EditorBlueprintResources/StandardMacros.StandardMacros'"
ISVALID_MACRO_GUID = "64422BCD430703FF5CAEA8B79A32AA65"

# Project-specific paths
PATHS = {
    "TeleportPoint": "/Script/SohVr.TeleportPoint",
    "EnablerComponent": "/Script/SohVr.EnablerComponent",
    "ActivatableComponent": "/Script/SohVr.ActivatableComponent",
    "GameplayMessageSubsystem": "/Script/GameplayMessageRuntime.GameplayMessageSubsystem",
    "AsyncAction_ListenForGameplayMessage": "/Script/GameplayMessageRuntime.AsyncAction_ListenForGameplayMessage",
    "Msg_TeleportPoint": "/Game/Blueprints/Data/Message/Msg_TeleportPoint.Msg_TeleportPoint",
    "Msg_StoryStep": "/Game/Blueprints/Data/Message/Msg_StoryStep.Msg_StoryStep",
    "BP_TeleportPoint": "/Game/Blueprints/Game/BP_TeleportPoint",
    "BP_TeleportPoint_C": "/Game/Blueprints/Game/BP_TeleportPoint.BP_TeleportPoint_C",
    "KismetMathLibrary": "/Script/Engine.KismetMathLibrary",
    "KismetSystemLibrary": "/Script/Engine.KismetSystemLibrary",
    "GameplayStatics": "/Script/Engine.GameplayStatics",
    "Actor": "/Script/Engine.Actor",
    "Object": "/Script/CoreUObject.Object",
}

# Struct member GUIDs (from UserDefinedStructs)
STRUCT_MEMBER_GUIDS = {
    "Msg_TeleportPoint.Teleport": "9162A20A46E747E6062EFAAD47D66DAA",
    "Msg_StoryStep.Step": "9162A20A46E747E6062EFAAD47D66DAA",
}


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def make_guid() -> str:
    """Generate a 32-char uppercase hex GUID matching UE format."""
    return uuid.uuid4().hex.upper()[:32]


def class_path(module: str, class_name: str) -> str:
    """Build a /Script/Module.ClassName path."""
    return f"/Script/{module}.{class_name}"


def ue_class_ref(path: str) -> str:
    """Wrap a class path in /Script/CoreUObject.Class'...' format."""
    return f"/Script/CoreUObject.Class'{path}'"


def ue_struct_ref(path: str) -> str:
    """Wrap a struct path in /Script/CoreUObject.ScriptStruct'...' format."""
    return f"/Script/CoreUObject.ScriptStruct'{path}'"


def ue_user_struct_ref(path: str) -> str:
    """Wrap a user-defined struct path."""
    return f"/Script/CoreUObject.UserDefinedStruct'{path}'"


def ue_bp_class_ref(path: str) -> str:
    """Wrap a Blueprint generated class path."""
    return f"/Script/Engine.BlueprintGeneratedClass'{path}'"


# ---------------------------------------------------------------------------
# Pin Types
# ---------------------------------------------------------------------------

class PinDirection(Enum):
    INPUT = "EGPD_Input"
    OUTPUT = "EGPD_Output"


@dataclass
class PinType:
    """Represents FEdGraphPinType."""
    category: str = ""  # exec, object, struct, bool, int, float, string, delegate, softobject, byte, name, text
    sub_category: str = ""
    sub_category_object: str = "None"  # Class/Struct path or "None"
    member_reference: str = "()"  # For delegates
    value_type: str = "()"
    container_type: str = "None"
    is_reference: bool = False
    is_const: bool = False
    is_weak_pointer: bool = False
    is_uobject_wrapper: bool = False
    serialize_single_precision: bool = False

    def to_str(self) -> str:
        parts = [
            f'PinType.PinCategory="{self.category}"',
            f'PinType.PinSubCategory="{self.sub_category}"',
            f'PinType.PinSubCategoryObject={self.sub_category_object}',
            f'PinType.PinSubCategoryMemberReference={self.member_reference}',
            f'PinType.PinValueType={self.value_type}',
            f'PinType.ContainerType={self.container_type}',
            f'PinType.bIsReference={str(self.is_reference)}',
            f'PinType.bIsConst={str(self.is_const)}',
            f'PinType.bIsWeakPointer={str(self.is_weak_pointer)}',
            f'PinType.bIsUObjectWrapper={str(self.is_uobject_wrapper)}',
            f'PinType.bSerializeAsSinglePrecisionFloat={str(self.serialize_single_precision)}',
        ]
        return ",".join(parts)

    @staticmethod
    def exec() -> 'PinType':
        return PinType(category="exec")

    @staticmethod
    def object(class_path: str) -> 'PinType':
        return PinType(category="object", sub_category_object=ue_class_ref(class_path))

    @staticmethod
    def self_object() -> 'PinType':
        """Self pin — uses sub_category='self' instead of sub_category_object."""
        return PinType(category="object", sub_category="self")

    @staticmethod
    def struct(struct_path: str, is_user_defined: bool = False) -> 'PinType':
        ref = ue_user_struct_ref(struct_path) if is_user_defined else ue_struct_ref(struct_path)
        return PinType(category="struct", sub_category_object=ref)

    @staticmethod
    def bool_type() -> 'PinType':
        return PinType(category="bool")

    @staticmethod
    def int_type() -> 'PinType':
        return PinType(category="int")

    @staticmethod
    def float_type() -> 'PinType':
        return PinType(category="real", sub_category="double")

    @staticmethod
    def string_type() -> 'PinType':
        return PinType(category="string")

    @staticmethod
    def name_type() -> 'PinType':
        return PinType(category="name")

    @staticmethod
    def text_type() -> 'PinType':
        return PinType(category="text")

    @staticmethod
    def delegate(member_parent: str = "", member_name: str = "") -> 'PinType':
        if member_parent and member_name:
            mr = f'(MemberParent="{ue_class_ref(member_parent)}",MemberName="{member_name}")'
        elif member_parent:
            # Package-level delegate
            mr = f'(MemberParent="/Script/CoreUObject.Package\'{member_parent}\'",MemberName="{member_name}")'
        else:
            mr = "()"
        return PinType(category="delegate", member_reference=mr)

    @staticmethod
    def softobject(class_path: str) -> 'PinType':
        return PinType(category="softobject", sub_category_object=ue_class_ref(class_path))

    @staticmethod
    def byte_enum(enum_path: str) -> 'PinType':
        return PinType(category="byte", sub_category_object=f"/Script/CoreUObject.Enum'{enum_path}'")

    @staticmethod
    def wildcard() -> 'PinType':
        """Empty/wildcard pin type."""
        return PinType(category="")


# ---------------------------------------------------------------------------
# Pin
# ---------------------------------------------------------------------------

@dataclass
class Pin:
    """Represents a single pin on a Blueprint node."""
    pin_id: str = ""
    name: str = ""
    friendly_name: str = ""  # NSLOCTEXT or INVTEXT string, or empty
    tooltip: str = ""
    direction: PinDirection = PinDirection.INPUT
    pin_type: PinType = field(default_factory=PinType.exec)
    default_value: str = ""
    autogenerated_default: str = ""
    default_object: str = ""
    linked_to: List[Tuple[str, str]] = field(default_factory=list)  # [(NodeName, PinId), ...]
    persistent_guid: str = "00000000000000000000000000000000"
    hidden: bool = False
    not_connectable: bool = False
    default_read_only: bool = False
    default_ignored: bool = False
    advanced_view: bool = False
    orphaned: bool = False

    def __post_init__(self):
        if not self.pin_id:
            self.pin_id = make_guid()

    def to_str(self) -> str:
        parts = [f'PinId={self.pin_id}']
        parts.append(f'PinName="{self.name}"')

        if self.friendly_name:
            parts.append(f'PinFriendlyName={self.friendly_name}')

        if self.tooltip:
            parts.append(f'PinToolTip="{self.tooltip}"')

        if self.direction == PinDirection.OUTPUT:
            parts.append(f'Direction="EGPD_Output"')

        parts.append(self.pin_type.to_str())

        if self.default_value:
            parts.append(f'DefaultValue="{self.default_value}"')
        if self.autogenerated_default:
            parts.append(f'AutogeneratedDefaultValue="{self.autogenerated_default}"')
        if self.default_object:
            parts.append(f'DefaultObject="{self.default_object}"')

        if self.linked_to:
            links = ",".join(f"{node_name} {pin_id}" for node_name, pin_id in self.linked_to)
            parts.append(f'LinkedTo=({links},)')

        parts.append(f'PersistentGuid={self.persistent_guid}')
        parts.append(f'bHidden={str(self.hidden)}')
        parts.append(f'bNotConnectable={str(self.not_connectable)}')
        parts.append(f'bDefaultValueIsReadOnly={str(self.default_read_only)}')
        parts.append(f'bDefaultValueIsIgnored={str(self.default_ignored)}')
        parts.append(f'bAdvancedView={str(self.advanced_view)}')
        parts.append(f'bOrphanedPin={str(self.orphaned)}')

        return f'CustomProperties Pin ({",".join(parts)},)'


# ---------------------------------------------------------------------------
# Node
# ---------------------------------------------------------------------------

@dataclass
class Node:
    """Represents a single K2Node in the Blueprint graph."""
    node_class: str = ""  # e.g., "/Script/BlueprintGraph.K2Node_CallFunction"
    name: str = ""  # e.g., "K2Node_CallFunction_4"
    properties: Dict[str, str] = field(default_factory=dict)
    pins: List[Pin] = field(default_factory=list)
    user_defined_pins: List[str] = field(default_factory=list)  # Raw UserDefinedPin strings
    node_pos_x: int = 0
    node_pos_y: int = 0
    node_guid: str = ""

    def __post_init__(self):
        if not self.node_guid:
            self.node_guid = make_guid()

    def get_pin(self, name: str, direction: PinDirection = None) -> Optional[Pin]:
        """Find a pin by name, optionally filtered by direction."""
        for p in self.pins:
            if p.name == name:
                if direction is None or p.direction == direction:
                    return p
        return None

    def get_pin_id(self, name: str, direction: PinDirection = None) -> str:
        """Get pin ID by name."""
        pin = self.get_pin(name, direction)
        return pin.pin_id if pin else ""

    def to_paste_text(self, level_path: str = "/Game/Maps/Level.Level") -> str:
        """Generate paste text for this node."""
        export_path = (
            f"{self.node_class}'{level_path}:PersistentLevel."
            f"{level_path.split('.')[-1]}.EventGraph.{self.name}'"
        )

        lines = []
        lines.append(
            f'Begin Object Class={self.node_class} Name="{self.name}" '
            f'ExportPath="{export_path}"'
        )

        # Node-specific properties
        for key, val in self.properties.items():
            lines.append(f'   {key}={val}')

        # Position
        if self.node_pos_x != 0:
            lines.append(f'   NodePosX={self.node_pos_x}')
        if self.node_pos_y != 0:
            lines.append(f'   NodePosY={self.node_pos_y}')

        # GUID
        lines.append(f'   NodeGuid={self.node_guid}')

        # Pins
        for pin in self.pins:
            lines.append(f'   {pin.to_str()}')

        # User-defined pins
        for udp in self.user_defined_pins:
            lines.append(f'   {udp}')

        lines.append('End Object')
        return "\n".join(lines)


# ---------------------------------------------------------------------------
# Graph — High-Level Builder
# ---------------------------------------------------------------------------

class Graph:
    """
    Blueprint graph builder. Creates nodes and wires them together,
    then emits paste-ready text.
    """

    def __init__(self, level_name: str = "MyLevel", level_path: str = ""):
        self.level_name = level_name
        self.level_path = level_path or f"/Game/Maps/{level_name}.{level_name}"
        self.nodes: List[Node] = []
        self._counters: Dict[str, int] = {}
        self._next_x = 0
        self._next_y = 0

    def _next_name(self, prefix: str) -> str:
        """Generate next unique node name like K2Node_CallFunction_3."""
        idx = self._counters.get(prefix, 0)
        self._counters[prefix] = idx + 1
        return f"{prefix}_{idx}"

    def _auto_pos(self) -> Tuple[int, int]:
        """Auto-assign position (simple left-to-right layout)."""
        x, y = self._next_x, self._next_y
        self._next_x += 320
        return x, y

    # ------------------------------------------------------------------
    # Node Factories
    # ------------------------------------------------------------------

    def add_event_begin_play(self, x: int = None, y: int = None) -> Node:
        """Add a ReceiveBeginPlay event node."""
        px, py = (x if x is not None else 0), (y if y is not None else 0)
        name = self._next_name("K2Node_Event")
        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_Event",
            name=name,
            properties={
                'EventReference': f'(MemberParent="{ue_class_ref(PATHS["Actor"])}",MemberName="ReceiveBeginPlay")',
                'bOverrideFunction': 'True',
            },
            node_pos_x=px, node_pos_y=py,
            pins=[
                Pin(name="OutputDelegate", direction=PinDirection.OUTPUT,
                    pin_type=PinType.delegate(PATHS["Actor"], "ReceiveBeginPlay")),
                Pin(name="then", direction=PinDirection.OUTPUT,
                    pin_type=PinType.exec()),
            ]
        )
        self.nodes.append(node)
        return node

    def add_custom_event(self, event_name: str, output_pins: List[Tuple[str, PinType]] = None,
                         x: int = None, y: int = None) -> Node:
        """Add a custom event node with optional typed output pins."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_CustomEvent")
        node_guid = make_guid()

        # Build delegate member reference for the OutputDelegate pin
        bp_class = f"/Script/Engine.BlueprintGeneratedClass'{self.level_path}_C'"
        delegate_mr = f'(MemberParent="{bp_class}",MemberName="{event_name}",MemberGuid={node_guid})'

        pins = [
            Pin(name="OutputDelegate", direction=PinDirection.OUTPUT,
                pin_type=PinType(category="delegate", member_reference=delegate_mr)),
            Pin(name="then", direction=PinDirection.OUTPUT,
                pin_type=PinType.exec()),
        ]

        user_defined = []
        if output_pins:
            for pin_name, pin_type in output_pins:
                pins.append(Pin(
                    name=pin_name, direction=PinDirection.OUTPUT,
                    pin_type=pin_type
                ))
                # Build UserDefinedPin entry
                udp_parts = [f'PinName="{pin_name}"']
                udp_type_parts = [f'PinCategory="{pin_type.category}"']
                if pin_type.sub_category_object != "None":
                    udp_type_parts.append(f'PinSubCategoryObject="{pin_type.sub_category_object}"')
                udp_parts.append(f'PinType=({",".join(udp_type_parts)})')
                udp_parts.append('DesiredPinDirection=EGPD_Output')
                user_defined.append(f'CustomProperties UserDefinedPin ({",".join(udp_parts)})')

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_CustomEvent",
            name=name,
            properties={'CustomFunctionName': f'"{event_name}"'},
            node_pos_x=px, node_pos_y=py,
            node_guid=node_guid,
            pins=pins,
            user_defined_pins=user_defined,
        )
        self.nodes.append(node)
        return node

    def add_call_function(self, class_path_key: str, function_name: str,
                          extra_pins: List[Pin] = None,
                          self_context: bool = False, member_guid: str = "",
                          x: int = None, y: int = None) -> Node:
        """Add a function call node."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_CallFunction")

        if self_context:
            func_ref = f'(MemberName="{function_name}"'
            if member_guid:
                func_ref += f',MemberGuid={member_guid}'
            func_ref += ',bSelfContext=True)'
        else:
            path = PATHS.get(class_path_key, class_path_key)
            func_ref = f'(MemberParent="{ue_class_ref(path)}",MemberName="{function_name}")'

        pins = [
            Pin(name="execute", pin_type=PinType.exec()),
            Pin(name="then", direction=PinDirection.OUTPUT, pin_type=PinType.exec()),
        ]

        # Add self/target pin for non-self-context calls
        if not self_context:
            path = PATHS.get(class_path_key, class_path_key)
            pins.append(Pin(
                name="self",
                friendly_name='NSLOCTEXT("K2Node", "Target", "Target")',
                pin_type=PinType.object(path),
            ))

        if extra_pins:
            pins.extend(extra_pins)

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_CallFunction",
            name=name,
            properties={'FunctionReference': func_ref},
            node_pos_x=px, node_pos_y=py,
            pins=pins,
        )
        self.nodes.append(node)
        return node

    def add_variable_get(self, owner_class: str, var_name: str,
                         var_type: PinType, friendly_name: str = "",
                         x: int = None, y: int = None) -> Node:
        """Add a variable getter node."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_VariableGet")

        owner_path = PATHS.get(owner_class, owner_class)

        pins = [
            Pin(name=var_name, direction=PinDirection.OUTPUT,
                friendly_name=friendly_name if friendly_name else "",
                pin_type=var_type),
            Pin(name="self",
                friendly_name='NSLOCTEXT("K2Node", "Target", "Target")',
                pin_type=PinType.object(owner_path)),
        ]

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_VariableGet",
            name=name,
            properties={
                'VariableReference': f'(MemberParent="{ue_class_ref(owner_path)}",MemberName="{var_name}")',
                'SelfContextInfo': 'NotSelfContext',
            },
            node_pos_x=px, node_pos_y=py,
            pins=pins,
        )
        self.nodes.append(node)
        return node

    def add_get_subsystem(self, subsystem_class: str,
                          x: int = None, y: int = None) -> Node:
        """Add a Get Subsystem node."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_GetSubsystem")

        path = PATHS.get(subsystem_class, subsystem_class)

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_GetSubsystem",
            name=name,
            properties={'CustomClass': f'"{ue_class_ref(path)}"'},
            node_pos_x=px, node_pos_y=py,
            pins=[
                Pin(name="ReturnValue", direction=PinDirection.OUTPUT,
                    pin_type=PinType.object(path)),
            ],
        )
        self.nodes.append(node)
        return node

    def add_branch(self, x: int = None, y: int = None) -> Node:
        """Add a Branch (if/then/else) node."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_IfThenElse")

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_IfThenElse",
            name=name,
            node_pos_x=px, node_pos_y=py,
            pins=[
                Pin(name="execute", pin_type=PinType.exec()),
                Pin(name="Condition", pin_type=PinType.bool_type(),
                    default_value="true", autogenerated_default="true"),
                Pin(name="then", direction=PinDirection.OUTPUT,
                    friendly_name='NSLOCTEXT("K2Node", "true", "true")',
                    pin_type=PinType.exec()),
                Pin(name="else", direction=PinDirection.OUTPUT,
                    friendly_name='NSLOCTEXT("K2Node", "false", "false")',
                    pin_type=PinType.exec()),
            ],
        )
        self.nodes.append(node)
        return node

    def add_multigate(self, num_outputs: int = 2,
                      x: int = None, y: int = None) -> Node:
        """Add a MultiGate node with N output pins."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_MultiGate")

        pins = [
            Pin(name="execute", pin_type=PinType.exec()),
        ]
        for i in range(num_outputs):
            pins.append(Pin(
                name=f"Out {i}", direction=PinDirection.OUTPUT,
                pin_type=PinType.exec(),
            ))
        pins.extend([
            Pin(name="Reset", pin_type=PinType.exec()),
            Pin(name="IsRandom", pin_type=PinType.bool_type()),
            Pin(name="Loop", pin_type=PinType.bool_type()),
            Pin(name="StartIndex", pin_type=PinType.int_type(),
                default_value="-1", autogenerated_default="-1"),
        ])

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_MultiGate",
            name=name,
            node_pos_x=px, node_pos_y=py,
            pins=pins,
        )
        self.nodes.append(node)
        return node

    def add_knot(self, pin_type: PinType,
                 x: int = None, y: int = None) -> Node:
        """Add a Reroute (Knot) node."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_Knot")

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_Knot",
            name=name,
            node_pos_x=px, node_pos_y=py,
            pins=[
                Pin(name="InputPin", pin_type=pin_type, default_ignored=True),
                Pin(name="OutputPin", direction=PinDirection.OUTPUT, pin_type=pin_type),
            ],
        )
        self.nodes.append(node)
        return node

    def add_isvalid_macro(self, x: int = None, y: int = None) -> Node:
        """Add an IsValid macro instance."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_MacroInstance")

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_MacroInstance",
            name=name,
            properties={
                'MacroGraphReference': (
                    f'(MacroGraph="{ISVALID_MACRO_GRAPH}",'
                    f'GraphBlueprint="{ISVALID_MACRO_BP}",'
                    f'GraphGuid={ISVALID_MACRO_GUID})'
                ),
            },
            node_pos_x=px, node_pos_y=py,
            pins=[
                Pin(name="exec", pin_type=PinType.exec()),
                Pin(name="InputObject", pin_type=PinType.object(PATHS["Object"])),
                Pin(name="Is Valid", direction=PinDirection.OUTPUT, pin_type=PinType.exec()),
                Pin(name="Is Not Valid", direction=PinDirection.OUTPUT, pin_type=PinType.exec()),
            ],
        )
        self.nodes.append(node)
        return node

    def add_dynamic_cast(self, target_class: str,
                         x: int = None, y: int = None) -> Node:
        """Add a Cast To node."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_DynamicCast")

        path = PATHS.get(target_class, target_class)
        # Derive friendly cast name
        short_name = path.split(".")[-1]

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_DynamicCast",
            name=name,
            properties={'TargetType': f'"{ue_class_ref(path)}"'},
            node_pos_x=px, node_pos_y=py,
            pins=[
                Pin(name="execute", pin_type=PinType.exec()),
                Pin(name="then", direction=PinDirection.OUTPUT, pin_type=PinType.exec()),
                Pin(name="CastFailed", direction=PinDirection.OUTPUT, pin_type=PinType.exec()),
                Pin(name="Object", pin_type=PinType.object(PATHS["Object"])),
                Pin(name=f"As{short_name}", direction=PinDirection.OUTPUT,
                    pin_type=PinType.object(path)),
                Pin(name="bSuccess", direction=PinDirection.OUTPUT,
                    pin_type=PinType.bool_type(), hidden=True),
            ],
        )
        self.nodes.append(node)
        return node

    def add_literal(self, actor_ref: str, pin_name: str, pin_friendly: str,
                    pin_type: PinType, x: int = None, y: int = None) -> Node:
        """Add an actor reference literal node."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_Literal")

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_Literal",
            name=name,
            properties={'ObjectRef': f'"{actor_ref}"'},
            node_pos_x=px, node_pos_y=py,
            pins=[
                Pin(name=pin_name, friendly_name=f'"{pin_friendly}"',
                    direction=PinDirection.OUTPUT, pin_type=pin_type),
            ],
        )
        self.nodes.append(node)
        return node

    def add_make_struct(self, struct_path: str, struct_name: str,
                        pin_properties: List[Dict],
                        x: int = None, y: int = None) -> Node:
        """
        Add a MakeStruct node.

        pin_properties: list of dicts with keys:
            property_name, friendly_name, pin_type, persistent_guid, default_value (optional)
        """
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_MakeStruct")

        # Build ShowPinForProperties
        show_pins = []
        for i, pp in enumerate(pin_properties):
            show_pins.append(
                f'ShowPinForProperties({i})='
                f'(PropertyName="{pp["property_name"]}",'
                f'PropertyFriendlyName="{pp["friendly_name"]}",'
                f'bShowPin=True,bCanToggleVisibility=True)'
            )

        props = {
            'bMadeAfterOverridePinRemoval': 'True',
            'StructType': f'"{ue_user_struct_ref(struct_path)}"',
        }
        for sp in show_pins:
            # Add each as a separate property line
            key, val = sp.split("=", 1)
            props[key] = val

        # Output struct pin
        pins = [
            Pin(name=struct_name, direction=PinDirection.OUTPUT,
                pin_type=PinType.struct(struct_path, is_user_defined=True)),
        ]

        # Member pins
        for pp in pin_properties:
            pins.append(Pin(
                name=pp["property_name"],
                friendly_name=f'INVTEXT("{pp["friendly_name"]}")',
                tooltip=f'{pp["friendly_name"]}\\n{pp.get("tooltip", "")}',
                pin_type=pp["pin_type"],
                default_value=pp.get("default_value", ""),
                autogenerated_default=pp.get("autogenerated_default", "0"),
                persistent_guid=pp.get("persistent_guid", "00000000000000000000000000000000"),
            ))

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_MakeStruct",
            name=name,
            properties=props,
            node_pos_x=px, node_pos_y=py,
            pins=pins,
        )
        self.nodes.append(node)
        return node

    def add_break_struct(self, struct_path: str, struct_name: str,
                         pin_properties: List[Dict],
                         x: int = None, y: int = None) -> Node:
        """Add a BreakStruct node. Similar to MakeStruct but pins are outputs."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_BreakStruct")

        show_pins = []
        for i, pp in enumerate(pin_properties):
            show_pins.append(
                f'ShowPinForProperties({i})='
                f'(PropertyName="{pp["property_name"]}",'
                f'PropertyFriendlyName="{pp["friendly_name"]}",'
                f'bShowPin=True,bCanToggleVisibility=True)'
            )

        props = {
            'bMadeAfterOverridePinRemoval': 'True',
            'StructType': f'"{ue_user_struct_ref(struct_path)}"',
        }
        for sp in show_pins:
            key, val = sp.split("=", 1)
            props[key] = val

        # Input struct pin
        pins = [
            Pin(name=struct_name,
                pin_type=PinType.struct(struct_path, is_user_defined=True)),
        ]

        # Member output pins
        for pp in pin_properties:
            pins.append(Pin(
                name=pp["property_name"],
                friendly_name=f'INVTEXT("{pp["friendly_name"]}")',
                tooltip=f'{pp["friendly_name"]}\\n{pp.get("tooltip", "")}',
                direction=PinDirection.OUTPUT,
                pin_type=pp["pin_type"],
                persistent_guid=pp.get("persistent_guid", "00000000000000000000000000000000"),
            ))

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_BreakStruct",
            name=name,
            properties=props,
            node_pos_x=px, node_pos_y=py,
            pins=pins,
        )
        self.nodes.append(node)
        return node

    def add_load_asset(self, x: int = None, y: int = None) -> Node:
        """Add an Async Load Asset node."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_LoadAsset")

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_LoadAsset",
            name=name,
            node_pos_x=px, node_pos_y=py,
            pins=[
                Pin(name="execute", pin_type=PinType.exec()),
                Pin(name="then", direction=PinDirection.OUTPUT, pin_type=PinType.exec()),
                Pin(name="Completed", direction=PinDirection.OUTPUT, pin_type=PinType.exec()),
                Pin(name="Asset", pin_type=PinType.softobject(PATHS["Object"])),
                Pin(name="Object", direction=PinDirection.OUTPUT,
                    pin_type=PinType.object(PATHS["Object"])),
            ],
        )
        self.nodes.append(node)
        return node

    def add_convert_asset(self, source_class: str,
                          x: int = None, y: int = None) -> Node:
        """Add a Convert Asset (hard ref -> soft ref) node."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_ConvertAsset")

        path = PATHS.get(source_class, source_class)

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_ConvertAsset",
            name=name,
            node_pos_x=px, node_pos_y=py,
            pins=[
                Pin(name="Input", pin_type=PinType.object(path)),
                Pin(name="Output", direction=PinDirection.OUTPUT,
                    pin_type=PinType.softobject(path)),
            ],
        )
        self.nodes.append(node)
        return node

    def add_equal_equal_object(self, x: int = None, y: int = None) -> Node:
        """Add an == comparison node for objects."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_PromotableOperator")

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_PromotableOperator",
            name=name,
            properties={
                'OperationName': '"EqualEqual"',
                'bDefaultsToPureFunc': 'True',
                'FunctionReference': f'(MemberParent="{ue_class_ref(PATHS["KismetMathLibrary"])}",MemberName="EqualEqual_ObjectObject")',
            },
            node_pos_x=px, node_pos_y=py,
            pins=[
                Pin(name="A", pin_type=PinType.object(PATHS["Object"])),
                Pin(name="B", pin_type=PinType.object(PATHS["Object"])),
                Pin(name="ReturnValue", direction=PinDirection.OUTPUT,
                    pin_type=PinType.bool_type()),
                Pin(name="ErrorTolerance", pin_type=PinType.wildcard(), hidden=True),
            ],
        )
        self.nodes.append(node)
        return node

    def add_assign_delegate(self, delegate_class: str, delegate_name: str,
                            x: int = None, y: int = None) -> Node:
        """Add an Assign Delegate (Bind Event) node."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_AssignDelegate")

        path = PATHS.get(delegate_class, delegate_class)

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_AssignDelegate",
            name=name,
            properties={
                'DelegateReference': f'(MemberParent="{ue_class_ref(path)}",MemberName="{delegate_name}")',
            },
            node_pos_x=px, node_pos_y=py,
            pins=[
                Pin(name="execute", pin_type=PinType.exec()),
                Pin(name="then", direction=PinDirection.OUTPUT, pin_type=PinType.exec()),
                Pin(name="self",
                    friendly_name='NSLOCTEXT("K2Node", "BaseMCDelegateSelfPinName", "Target")',
                    pin_type=PinType.object(path)),
                Pin(name="Delegate",
                    friendly_name='NSLOCTEXT("K2Node", "PinFriendlyDelegatetName", "Event")',
                    pin_type=PinType(
                        category="delegate",
                        is_reference=True, is_const=True,
                        member_reference=f'(MemberParent="/Script/CoreUObject.Package\'/Script/SohVr\'",MemberName="{delegate_name}__DelegateSignature")',
                    )),
            ],
        )
        self.nodes.append(node)
        return node

    def add_create_delegate(self, function_name: str, function_guid: str,
                            x: int = None, y: int = None) -> Node:
        """Add a Create Delegate node."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_CreateDelegate")

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_CreateDelegate",
            name=name,
            properties={
                'SelectedFunctionName': f'"{function_name}"',
                'SelectedFunctionGuid': function_guid,
            },
            node_pos_x=px, node_pos_y=py,
            pins=[
                Pin(name="self",
                    friendly_name='NSLOCTEXT("K2Node", "CreateDelegate_ObjectInputName", "Object")',
                    pin_type=PinType.object(PATHS["Object"])),
                Pin(name="OutputDelegate",
                    friendly_name='NSLOCTEXT("K2Node", "CreateDelegate_DelegateOutName", "Event")',
                    direction=PinDirection.OUTPUT,
                    pin_type=PinType(category="delegate")),
            ],
        )
        self.nodes.append(node)
        return node

    def add_listen_gameplay_messages(self, channel_tag: str, payload_struct: str,
                                     payload_type_path: str,
                                     x: int = None, y: int = None) -> Node:
        """Add a ListenForGameplayMessages async action node."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_AsyncAction_ListenForGameplayMessages")

        async_class = PATHS["AsyncAction_ListenForGameplayMessage"]

        node = Node(
            node_class="/Script/GameplayMessageNodes.K2Node_AsyncAction_ListenForGameplayMessages",
            name=name,
            properties={
                'ProxyFactoryFunctionName': '"ListenForGameplayMessages"',
                'ProxyFactoryClass': f'"{ue_class_ref(async_class)}"',
                'ProxyClass': f'"{ue_class_ref(async_class)}"',
            },
            node_pos_x=px, node_pos_y=py,
            pins=[
                Pin(name="execute", pin_type=PinType.exec()),
                Pin(name="then", direction=PinDirection.OUTPUT, pin_type=PinType.exec()),
                Pin(name="AsyncTaskProxy", friendly_name='"AsyncAction"',
                    direction=PinDirection.OUTPUT,
                    pin_type=PinType.object(async_class)),
                Pin(name="OnMessageReceived",
                    friendly_name=f'NSLOCTEXT("UObjectDisplayNames", "AsyncAction_ListenForGameplayMessage:OnMessageReceived", "On Message Received")',
                    tooltip="Called when a message is broadcast on the specified channel.",
                    direction=PinDirection.OUTPUT,
                    pin_type=PinType.exec()),
                Pin(name="ProxyObject", direction=PinDirection.OUTPUT,
                    pin_type=PinType.object(async_class), hidden=True),
                Pin(name="ActualChannel", direction=PinDirection.OUTPUT,
                    pin_type=PinType.struct("/Script/GameplayTags.GameplayTag")),
                Pin(name="WorldContextObject",
                    pin_type=PinType.object(PATHS["Object"]), hidden=True),
                Pin(name="Channel",
                    pin_type=PinType.struct("/Script/GameplayTags.GameplayTag"),
                    default_value=f'(TagName=\\"{channel_tag}\\")'),
                Pin(name="PayloadType",
                    pin_type=PinType.object("/Script/CoreUObject.ScriptStruct"),
                    default_object=payload_type_path),
                Pin(name="MatchType",
                    pin_type=PinType.byte_enum("/Script/GameplayMessageRuntime.EGameplayMessageMatch"),
                    default_value="ExactMatch", autogenerated_default="ExactMatch"),
                Pin(name="Payload", direction=PinDirection.OUTPUT,
                    pin_type=PinType.struct(payload_struct, is_user_defined=True)),
            ],
        )
        self.nodes.append(node)
        return node

    def add_print_string(self, default_text: str = "Hello",
                         x: int = None, y: int = None) -> Node:
        """Add a PrintString node (useful for debugging)."""
        px, py = self._auto_pos() if x is None else (x, y or 0)
        name = self._next_name("K2Node_CallFunction")

        node = Node(
            node_class="/Script/BlueprintGraph.K2Node_CallFunction",
            name=name,
            properties={
                'FunctionReference': f'(MemberParent="{ue_class_ref(PATHS["KismetSystemLibrary"])}",MemberName="PrintString")',
            },
            node_pos_x=px, node_pos_y=py,
            pins=[
                Pin(name="execute", pin_type=PinType.exec()),
                Pin(name="then", direction=PinDirection.OUTPUT, pin_type=PinType.exec()),
                Pin(name="self",
                    friendly_name='NSLOCTEXT("K2Node", "Target", "Target")',
                    pin_type=PinType.object(PATHS["KismetSystemLibrary"])),
                Pin(name="WorldContextObject",
                    pin_type=PinType.object(PATHS["Object"]), hidden=True),
                Pin(name="InString",
                    pin_type=PinType.string_type(),
                    default_value=default_text, autogenerated_default="Hello"),
                Pin(name="bPrintToScreen",
                    pin_type=PinType.bool_type(),
                    default_value="true", autogenerated_default="true"),
                Pin(name="bPrintToLog",
                    pin_type=PinType.bool_type(),
                    default_value="true", autogenerated_default="true"),
                Pin(name="TextColor",
                    pin_type=PinType.struct("/Script/CoreUObject.LinearColor"),
                    default_value="(R=0.000000,G=0.660000,B=1.000000,A=1.000000)"),
                Pin(name="Duration",
                    pin_type=PinType.float_type(),
                    default_value="2.000000", autogenerated_default="2.000000"),
            ],
        )
        self.nodes.append(node)
        return node

    # ------------------------------------------------------------------
    # Wiring
    # ------------------------------------------------------------------

    def connect(self, source_node: Node, source_pin_name: str,
                target_node: Node, target_pin_name: str):
        """
        Wire two pins together. Adds LinkedTo entries on both sides.
        Source pin must be output, target pin must be input.
        """
        src_pin = source_node.get_pin(source_pin_name, PinDirection.OUTPUT)
        if not src_pin:
            # Try without direction filter (some pins like Knot OutputPin)
            src_pin = source_node.get_pin(source_pin_name)
        tgt_pin = target_node.get_pin(target_pin_name, PinDirection.INPUT)
        if not tgt_pin:
            tgt_pin = target_node.get_pin(target_pin_name)

        if not src_pin:
            raise ValueError(f"Source pin '{source_pin_name}' not found on {source_node.name}")
        if not tgt_pin:
            raise ValueError(f"Target pin '{target_pin_name}' not found on {target_node.name}")

        # Add bidirectional links
        src_pin.linked_to.append((target_node.name, tgt_pin.pin_id))
        tgt_pin.linked_to.append((source_node.name, src_pin.pin_id))

    # ------------------------------------------------------------------
    # Output
    # ------------------------------------------------------------------

    def to_paste_text(self) -> str:
        """Generate the complete paste text for all nodes."""
        blocks = []
        for node in self.nodes:
            blocks.append(node.to_paste_text(self.level_path))
        return "\n".join(blocks) + "\n"


# ---------------------------------------------------------------------------
# Convenience: Pre-built interaction patterns
# ---------------------------------------------------------------------------

def build_teleport_listener_pattern(
    graph: Graph,
    actor_ref: str,
    actor_pin_name: str = "BP_TeleportPoint_0",
    step_index: int = 1,
    base_x: int = 0,
    base_y: int = 0,
) -> Dict[str, Node]:
    """
    Build the complete teleport point interaction pattern:
    BeginPlay -> ListenMessages -> ListenForGameplayMessages -> BreakStruct ->
    LoadAsset -> Cast -> EnableActor -> AssignDelegate

    Plus the OnPlayerTeleported handler with MultiGate -> IsValid -> DisableActor ->
    Branch -> BroadcastMessage(StoryStep)

    Returns dict of all created nodes keyed by role name.
    """
    nodes = {}

    # --- Setup chain ---
    bp = graph.add_event_begin_play(x=base_x, y=base_y)
    nodes["begin_play"] = bp

    # Call ListenMessages (self)
    listen_call = graph.add_call_function(
        "", "ListenMessages", self_context=True,
        x=base_x + 256, y=base_y
    )
    # Add self pin for self-context
    listen_call.pins.append(Pin(
        name="self",
        friendly_name='NSLOCTEXT("K2Node", "Target", "Target")',
        pin_type=PinType.self_object(),
    ))
    nodes["listen_call"] = listen_call
    graph.connect(bp, "then", listen_call, "execute")

    # BroadcastMessage for TeleportPoint
    subsys1 = graph.add_get_subsystem("GameplayMessageSubsystem",
                                       x=base_x + 640, y=base_y - 128)
    nodes["subsys_broadcast"] = subsys1

    broadcast1 = graph.add_call_function(
        "GameplayMessageSubsystem", "K2_BroadcastMessage",
        x=base_x + 592, y=base_y,
    )
    # Add Channel and Message pins
    broadcast1.pins.append(Pin(
        name="Channel",
        pin_type=PinType.struct("/Script/GameplayTags.GameplayTag"),
        default_value='(TagName=\\"Message.Event.TeleportPoint\\")',
    ))
    broadcast1.pins.append(Pin(
        name="Message",
        pin_type=PinType.struct(PATHS["Msg_TeleportPoint"], is_user_defined=True),
        default_ignored=True,
    ))
    nodes["broadcast_tp"] = broadcast1
    graph.connect(listen_call, "then", broadcast1, "execute")
    graph.connect(subsys1, "ReturnValue", broadcast1, "self")

    # Literal + ConvertAsset + MakeStruct for the message payload
    literal = graph.add_literal(
        actor_ref, actor_pin_name, actor_pin_name,
        PinType.object(PATHS["BP_TeleportPoint_C"]),
        x=base_x + 640, y=base_y + 320,
    )
    nodes["literal"] = literal

    convert = graph.add_convert_asset("BP_TeleportPoint_C",
                                       x=base_x + 672, y=base_y + 272)
    nodes["convert"] = convert
    graph.connect(literal, actor_pin_name, convert, "Input")

    make_tp = graph.add_make_struct(
        PATHS["Msg_TeleportPoint"], "Msg_TeleportPoint",
        [{
            "property_name": f"Teleport_2_{STRUCT_MEMBER_GUIDS['Msg_TeleportPoint.Teleport']}",
            "friendly_name": "Teleport",
            "tooltip": "Teleport Point Soft Object Reference",
            "pin_type": PinType.softobject(PATHS["TeleportPoint"]),
            "persistent_guid": STRUCT_MEMBER_GUIDS["Msg_TeleportPoint.Teleport"],
        }],
        x=base_x + 608, y=base_y + 192,
    )
    nodes["make_tp_struct"] = make_tp
    graph.connect(convert, "Output", make_tp,
                  f"Teleport_2_{STRUCT_MEMBER_GUIDS['Msg_TeleportPoint.Teleport']}")
    graph.connect(make_tp, "Msg_TeleportPoint", broadcast1, "Message")

    # --- Listener chain ---
    listen_event = graph.add_custom_event("ListenMessages",
                                           x=base_x, y=base_y + 512)
    nodes["listen_event"] = listen_event

    # Update listen_call's member guid to match
    listen_call.properties['FunctionReference'] = (
        f'(MemberName="ListenMessages",MemberGuid={listen_event.node_guid},bSelfContext=True)'
    )

    async_listen = graph.add_listen_gameplay_messages(
        channel_tag="Message.Event.TeleportPoint",
        payload_struct=PATHS["Msg_TeleportPoint"],
        payload_type_path=PATHS["Msg_TeleportPoint"],
        x=base_x + 256, y=base_y + 528,
    )
    nodes["async_listen"] = async_listen
    graph.connect(listen_event, "then", async_listen, "execute")

    # BreakStruct
    break_tp = graph.add_break_struct(
        PATHS["Msg_TeleportPoint"], "Msg_TeleportPoint",
        [{
            "property_name": f"Teleport_2_{STRUCT_MEMBER_GUIDS['Msg_TeleportPoint.Teleport']}",
            "friendly_name": "Teleport",
            "tooltip": "Teleport Point Soft Object Reference",
            "pin_type": PinType.softobject(PATHS["TeleportPoint"]),
            "persistent_guid": STRUCT_MEMBER_GUIDS["Msg_TeleportPoint.Teleport"],
        }],
        x=base_x + 720, y=base_y + 688,
    )
    nodes["break_tp"] = break_tp
    graph.connect(async_listen, "Payload", break_tp, "Msg_TeleportPoint")

    # LoadAsset
    load_asset = graph.add_load_asset(x=base_x + 1024, y=base_y + 592)
    nodes["load_asset"] = load_asset
    graph.connect(async_listen, "OnMessageReceived", load_asset, "execute")
    graph.connect(break_tp,
                  f"Teleport_2_{STRUCT_MEMBER_GUIDS['Msg_TeleportPoint.Teleport']}",
                  load_asset, "Asset")

    # Cast to TeleportPoint
    cast_tp = graph.add_dynamic_cast("TeleportPoint",
                                      x=base_x + 1392, y=base_y + 624)
    nodes["cast_tp"] = cast_tp
    graph.connect(load_asset, "Completed", cast_tp, "execute")
    graph.connect(load_asset, "Object", cast_tp, "Object")

    # EnableActor
    enable_fn = graph.add_call_function(
        "EnablerComponent", "EnableActor",
        x=base_x + 1808, y=base_y + 608,
    )
    nodes["enable_actor"] = enable_fn
    graph.connect(cast_tp, "then", enable_fn, "execute")

    # Get EnablerComponent from TeleportPoint
    get_enabler = graph.add_variable_get(
        "TeleportPoint", "EnablerComponent",
        PinType.object(PATHS["EnablerComponent"]),
        friendly_name='NSLOCTEXT("UObjectDisplayNames", "TeleportPoint:EnablerComponent", "Enabler Component")',
        x=base_x + 1792, y=base_y + 736,
    )
    nodes["get_enabler"] = get_enabler
    graph.connect(cast_tp, "AsTeleportPoint", get_enabler, "self")
    graph.connect(get_enabler, "EnablerComponent", enable_fn, "self")

    # AssignDelegate + CreateDelegate
    assign_del = graph.add_assign_delegate(
        "TeleportPoint", "OnPlayerTeleported",
        x=base_x + 2144, y=base_y + 624,
    )
    nodes["assign_delegate"] = assign_del
    graph.connect(enable_fn, "then", assign_del, "execute")
    graph.connect(cast_tp, "AsTeleportPoint", assign_del, "self")

    # Custom event for OnPlayerTeleported
    tp_event = graph.add_custom_event(
        "OnPlayerTeleported_Event_0",
        output_pins=[("TeleportPoint", PinType.object(PATHS["TeleportPoint"]))],
        x=base_x - 240, y=base_y + 1568,
    )
    nodes["tp_event"] = tp_event

    create_del = graph.add_create_delegate(
        "OnPlayerTeleported_Event_0", tp_event.node_guid,
        x=base_x + 2144, y=base_y + 768,
    )
    nodes["create_delegate"] = create_del
    graph.connect(create_del, "OutputDelegate", assign_del, "Delegate")

    return nodes


# ---------------------------------------------------------------------------
# Self-test
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    # Quick smoke test: BeginPlay -> PrintString
    g = Graph("TestLevel")
    bp = g.add_event_begin_play()
    ps = g.add_print_string("Paste text works!", x=320, y=0)
    g.connect(bp, "then", ps, "execute")
    print(g.to_paste_text())
    print(f"\n--- Generated {len(g.nodes)} nodes ---")
