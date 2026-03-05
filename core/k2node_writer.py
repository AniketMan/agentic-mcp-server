#!/usr/bin/env python3
"""
K2Node Extras Blob Writer and Paste Text Parser.

This module can:
1. Parse UE Blueprint paste text (T3D format) into structured node/pin data
2. Construct valid K2Node Extras blobs (binary) from that data
3. Inject K2Nodes into .umap files via UAssetAPI

The Extras blob format was reverse-engineered from UE 5.4.4 source:
  EdGraphPin.cpp, EdGraphNode.cpp, K2Node.cpp, K2Node_EditablePinBase.cpp,
  K2Node_Event.cpp, K2Node_CustomEvent.cpp

Confirmed byte-perfect on 56/56 K2Nodes from SOH_VR Oculus 5.6 branch assets.

Author: JARVIS
Date: 2026-03-05
"""

import struct
import io
import re
import uuid
from typing import List, Dict, Optional, Tuple, Any


# ---------------------------------------------------------------------------
# Section 1: Binary Writer Primitives
# ---------------------------------------------------------------------------

class ExtrasWriter:
    """
    Writes K2Node Extras blobs in the exact binary format expected by UE 5.4+.

    The writer needs access to the asset's name map to resolve FName indices.
    It also needs the export list to resolve FPackageIndex values for object
    references (OwningNode, DefaultObject, PinSubCategoryObject, LinkedTo).
    """

    def __init__(self, name_map: List[str], add_name_fn=None):
        """
        Args:
            name_map: List of strings in the asset's name map (index -> string).
            add_name_fn: Callable that adds a name to the asset's name map and
                         returns the index. If None, names must already exist.
        """
        self.stream = io.BytesIO()
        self.name_map = list(name_map)  # mutable copy
        self._name_to_idx = {n: i for i, n in enumerate(self.name_map)}
        self.add_name_fn = add_name_fn

    def get_bytes(self) -> bytes:
        """Return the constructed binary blob."""
        return self.stream.getvalue()

    # --- Primitive writers ---

    def write_int32(self, val: int):
        self.stream.write(struct.pack('<i', val))

    def write_uint32(self, val: int):
        self.stream.write(struct.pack('<I', val))

    def write_uint8(self, val: int):
        self.stream.write(struct.pack('<B', val))

    def write_bool(self, val: bool):
        """UE serializes bool as int32 (4 bytes)."""
        self.write_int32(1 if val else 0)

    def write_guid(self, guid_hex: str):
        """
        Write FGuid from a 32-char hex string (AABBCCDD format).
        FGuid is 4 x uint32: A, B, C, D.
        """
        # Ensure 32 hex chars
        guid_hex = guid_hex.replace('-', '').replace('{', '').replace('}', '')
        assert len(guid_hex) == 32, f"GUID must be 32 hex chars, got {len(guid_hex)}: {guid_hex}"
        a = int(guid_hex[0:8], 16)
        b = int(guid_hex[8:16], 16)
        c = int(guid_hex[16:24], 16)
        d = int(guid_hex[24:32], 16)
        self.stream.write(struct.pack('<IIII', a, b, c, d))

    def write_fname(self, name: str):
        """
        Write FName as int32 NameIndex + int32 Number.
        If the name ends with _N (where N is a digit), split into base + number.
        """
        # Check for FName number suffix (e.g., "K2Node_CustomEvent_0" -> base + number 1)
        # But be careful: some names naturally contain underscores + digits
        # The FName number is only used for instanced names, not for type names
        # For simplicity, we write number=0 for all names (the name map entry
        # contains the full string including any suffix)
        idx = self._resolve_name(name)
        self.write_int32(idx)
        self.write_int32(0)  # Number = 0

    def write_fname_with_number(self, name: str, number: int):
        """Write FName with an explicit number field."""
        idx = self._resolve_name(name)
        self.write_int32(idx)
        self.write_int32(number)

    def write_fstring(self, text):
        """
        Write FString: int32 length (including null terminator) + UTF-8 bytes + null.
        None = int32(0) (truly empty / no string).
        Empty string "" = int32(1) + null byte (UE convention).
        """
        if text is None:
            self.write_int32(0)
            return
        encoded = text.encode('utf-8') + b'\x00'
        self.write_int32(len(encoded))
        self.stream.write(encoded)

    def write_ftext_empty(self):
        """
        Write an empty FText.
        Format: uint32 flags=0, int8 historyType=-1, int32 hasCultureInvariant=0
        """
        self.write_uint32(0)          # Flags
        self.stream.write(struct.pack('<b', -1))  # HistoryType = NONE
        self.write_int32(0)           # HasCultureInvariantString = false

    def write_ftext_base(self, namespace: str, key: str, source: str):
        """
        Write an FText with Base history type.
        Format: uint32 flags, int8 historyType=0, FString namespace, FString key, FString source
        """
        self.write_uint32(0)          # Flags
        self.stream.write(struct.pack('<b', 0))  # HistoryType = BASE
        self.write_fstring(namespace)
        self.write_fstring(key)
        self.write_fstring(source)

    def write_package_index(self, idx: int):
        """
        Write FPackageIndex as int32.
        Positive = export index + 1 (1-based)
        Negative = -(import index + 1)
        Zero = null
        """
        self.write_int32(idx)

    def _resolve_name(self, name: str) -> int:
        """
        Resolve a name string to its index in the name map.
        If add_name_fn is provided, adds the name if it doesn't exist.
        """
        if name in self._name_to_idx:
            return self._name_to_idx[name]
        if self.add_name_fn:
            idx = self.add_name_fn(name)
            self._name_to_idx[name] = idx
            self.name_map.append(name)
            return idx
        raise ValueError(f"Name '{name}' not in name map and no add_name_fn provided")

    # --- Pin type writer ---

    def write_pin_type(self, pin_type: Dict):
        """
        Write FEdGraphPinType.
        Expected keys: PinCategory, PinSubCategory, PinSubCategoryObject,
                       ContainerType, bIsReference, bIsWeakPointer,
                       MemberParent, MemberName, MemberGuid,
                       bIsConst, bIsUObjectWrapper, bSerializeAsSinglePrecisionFloat
        """
        self.write_fname(pin_type.get('PinCategory', ''))
        self.write_fname(pin_type.get('PinSubCategory', ''))
        self.write_package_index(pin_type.get('PinSubCategoryObject', 0))

        container_type = pin_type.get('ContainerType', 0)
        self.write_uint8(container_type)

        if container_type == 4:  # Map
            vt = pin_type.get('PinValueType', {})
            self.write_fname(vt.get('TerminalCategory', ''))
            self.write_fname(vt.get('TerminalSubCategory', ''))
            self.write_package_index(vt.get('TerminalSubCategoryObject', 0))
            self.write_bool(vt.get('bIsWeakPointer', False))

        self.write_bool(pin_type.get('bIsReference', False))
        self.write_bool(pin_type.get('bIsWeakPointer', False))

        # FSimpleMemberReference
        self.write_package_index(pin_type.get('MemberParent', 0))
        self.write_fname(pin_type.get('MemberName', 'None'))
        self.write_guid(pin_type.get('MemberGuid', '00000000000000000000000000000000'))

        self.write_bool(pin_type.get('bIsConst', False))
        self.write_bool(pin_type.get('bIsUObjectWrapper', False))
        self.write_bool(pin_type.get('bSerializeAsSinglePrecisionFloat', False))

    # --- Full pin writer ---

    def write_pin(self, pin: Dict, owning_export_idx: int):
        """
        Write a complete pin (UEdGraphPin::Serialize format).

        Args:
            pin: Dict with pin data (from paste text parser).
            owning_export_idx: The 0-based export index of the owning K2Node.
        """
        # FPackageIndex for owning node (1-based positive for exports)
        pkg_idx = owning_export_idx + 1
        self.write_package_index(pkg_idx)

        # PinId (FGuid)
        self.write_guid(pin['PinId'])

        # PinName (FName)
        self.write_fname(pin['PinName'])

        # PinFriendlyName (FText, editor-only) - usually empty
        self.write_ftext_empty()

        # SourceIndex (int32, UE5 only) - default -1 (not set)
        self.write_int32(pin.get('SourceIndex', -1))

        # PinToolTip (FString)
        self.write_fstring(pin.get('PinToolTip', ''))

        # Direction (uint8): 0=Input, 1=Output
        direction = 1 if pin.get('Direction', 'EGPD_Input') == 'EGPD_Output' else 0
        self.write_uint8(direction)

        # PinType
        self.write_pin_type(pin.get('PinType', {}))

        # DefaultValue (FString)
        self.write_fstring(pin.get('DefaultValue', ''))

        # AutogeneratedDefaultValue (FString)
        self.write_fstring(pin.get('AutogeneratedDefaultValue', ''))

        # DefaultObject (FPackageIndex)
        self.write_package_index(pin.get('DefaultObject', 0))

        # DefaultTextValue (FText) - usually empty
        self.write_ftext_empty()

        # LinkedTo array
        linked_to = pin.get('LinkedTo', [])
        self.write_int32(len(linked_to))
        for lt in linked_to:
            self.write_bool(False)  # bNullPtr = false
            self.write_package_index(lt['OwningNodePkgIdx'])
            self.write_guid(lt['PinGuid'])

        # SubPins array (usually empty)
        self.write_int32(0)

        # ParentPin (usually null)
        self.write_bool(True)  # bNullPtr = true (no parent)

        # ReferencePassThroughConnection (usually null)
        self.write_bool(True)  # bNullPtr = true

        # PersistentGuid (FGuid, editor-only)
        self.write_guid(pin.get('PersistentGuid', '00000000000000000000000000000000'))

        # BitField (uint32, editor-only)
        bitfield = 0
        if pin.get('bHidden', False):
            bitfield |= (1 << 0)
        if pin.get('bNotConnectable', False):
            bitfield |= (1 << 1)
        if pin.get('bDefaultValueIsReadOnly', False):
            bitfield |= (1 << 2)
        if pin.get('bDefaultValueIsIgnored', False):
            bitfield |= (1 << 3)
        if pin.get('bAdvancedView', False):
            bitfield |= (1 << 4)
        if pin.get('bOrphanedPin', False):
            bitfield |= (1 << 5)
        self.write_uint32(bitfield)

    # --- Top-level Extras blob writer ---

    def write_extras(self, node: Dict, owning_export_idx: int) -> bytes:
        """
        Write the complete Extras blob for a K2Node.

        Args:
            node: Dict with node data including 'pins' list and 'node_class'.
            owning_export_idx: The 0-based export index of the owning K2Node.

        Returns:
            bytes: The complete Extras blob.
        """
        self.stream = io.BytesIO()  # Reset

        pins = node.get('pins', [])

        # --- Pin array (SerializePinArray, OwningNode path) ---
        self.write_int32(len(pins))
        for pin in pins:
            # SerializePin wrapper
            self.write_bool(False)  # bNullPtr = false (valid pin)
            self.write_package_index(owning_export_idx + 1)  # OwningNode
            self.write_guid(pin['PinId'])  # PinGuid

            # Pin.Serialize()
            self.write_pin(pin, owning_export_idx)

        # --- Subclass-specific extra fields ---
        node_class = node.get('node_class', '')

        if node_class in ('K2Node_CustomEvent', 'K2Node_Event'):
            # UK2Node_EditablePinBase: TArray<FUserPinInfo>
            user_pins = node.get('user_defined_pins', [])
            self.write_int32(len(user_pins))
            for up in user_pins:
                self.write_fname(up['PinName'])
                self.write_pin_type(up.get('PinType', {}))
                direction = 1 if up.get('Direction', 'EGPD_Output') == 'EGPD_Output' else 0
                self.write_uint8(direction)
                self.write_fstring(up.get('DefaultValue', ''))

        elif node_class == 'K2Node_DynamicCast':
            # EPureState (uint8)
            pure_state = node.get('PureState', 1)  # 0=Impure, 1=Pure
            self.write_uint8(pure_state)

        # Other K2Node types (CallFunction, GetSubsystem, MakeStruct, etc.)
        # have no extra serialization after the pin array.

        return self.get_bytes()


# ---------------------------------------------------------------------------
# Section 2: Paste Text Parser
# ---------------------------------------------------------------------------

class PasteTextParser:
    """
    Parse UE Blueprint paste text (T3D format) into structured node/pin data.

    The paste text format looks like:
        Begin Object Class=/Script/BlueprintGraph.K2Node_CustomEvent Name="K2Node_CustomEvent_Scene3_DoorKnobGrabbed" ...
           CustomFunctionName="Scene3_DoorKnobGrabbed"
           ...
           CustomProperties Pin (PinId=...,PinName="execute",...)
           CustomProperties Pin (PinId=...,PinName="then",...)
        End Object
    """

    # Regex to match the Begin Object line
    RE_BEGIN = re.compile(
        r'Begin Object\s+'
        r'Class=(?P<class>[^\s]+)\s+'
        r'Name="(?P<name>[^"]+)"'
    )

    # Regex to match a CustomProperties Pin line
    RE_PIN = re.compile(r'CustomProperties Pin \((.+)\)')

    # Regex to match key=value pairs in a pin definition
    RE_KV = re.compile(r'(\w+(?:\.\w+)*)=([^,\)]+(?:\([^)]*\))?)')

    def parse_file(self, filepath: str) -> List[Dict]:
        """Parse a paste text file into a list of node dicts."""
        with open(filepath, 'r', encoding='utf-8') as f:
            text = f.read()
        return self.parse_text(text)

    def parse_text(self, text: str) -> List[Dict]:
        """Parse paste text string into a list of node dicts."""
        nodes = []
        current_node = None

        for line in text.split('\n'):
            line = line.strip()

            # Check for Begin Object
            m = self.RE_BEGIN.search(line)
            if m:
                # Extract the full class path
                class_path = m.group('class')
                # Get short class name (e.g., "K2Node_CustomEvent" from "/Script/BlueprintGraph.K2Node_CustomEvent")
                short_class = class_path.split('.')[-1]

                current_node = {
                    'node_class': short_class,
                    'class_path': class_path,
                    'name': m.group('name'),
                    'pins': [],
                    'user_defined_pins': [],
                    'properties': {},
                }

                # Also extract ExportPath and NodeGuid from the same line or subsequent lines
                export_match = re.search(r'ExportPath="([^"]+)"', line)
                if export_match:
                    current_node['export_path'] = export_match.group(1)

                nodes.append(current_node)
                continue

            if current_node is None:
                continue

            # Check for End Object
            if line.startswith('End Object'):
                current_node = None
                continue

            # Check for CustomProperties Pin
            pin_match = self.RE_PIN.search(line)
            if pin_match:
                pin = self._parse_pin(pin_match.group(1))
                current_node['pins'].append(pin)
                continue

            # Check for property assignments (key=value)
            if '=' in line and not line.startswith('CustomProperties'):
                key, _, value = line.partition('=')
                key = key.strip()
                value = value.strip().strip('"')
                current_node['properties'][key] = value

                # Special handling for known properties
                if key == 'NodeGuid':
                    current_node['NodeGuid'] = value
                elif key == 'CustomFunctionName':
                    current_node['CustomFunctionName'] = value
                elif key == 'NodePosX':
                    current_node['NodePosX'] = int(value)
                elif key == 'NodePosY':
                    current_node['NodePosY'] = int(value)

        return nodes

    def _parse_pin(self, pin_str: str) -> Dict:
        """Parse a single pin definition string into a dict."""
        pin = {
            'PinType': {},
            'LinkedTo': [],
        }

        # Split by comma, but respect parentheses
        # Use a state machine to handle nested parens
        parts = self._split_pin_fields(pin_str)

        for part in parts:
            part = part.strip()
            if '=' not in part:
                continue

            key, _, value = part.partition('=')
            key = key.strip()
            value = value.strip()

            # Remove surrounding quotes if present
            if value.startswith('"') and value.endswith('"'):
                value = value[1:-1]

            # Route to the correct field
            if key == 'PinId':
                pin['PinId'] = value
            elif key == 'PinName':
                pin['PinName'] = value
            elif key == 'PinToolTip':
                pin['PinToolTip'] = value
            elif key == 'Direction':
                pin['Direction'] = value
            elif key == 'DefaultValue':
                pin['DefaultValue'] = value
            elif key == 'AutogeneratedDefaultValue':
                pin['AutogeneratedDefaultValue'] = value
            elif key == 'LinkedTo':
                pin['LinkedTo'] = self._parse_linked_to(value)
            elif key == 'PersistentGuid':
                pin['PersistentGuid'] = value
            elif key.startswith('PinType.'):
                subkey = key[len('PinType.'):]
                pin['PinType'][subkey] = self._parse_pin_type_value(subkey, value)
            elif key in ('bHidden', 'bNotConnectable', 'bDefaultValueIsReadOnly',
                         'bDefaultValueIsIgnored', 'bAdvancedView', 'bOrphanedPin'):
                pin[key] = value == 'True'

        # Default direction to Input if not specified
        if 'Direction' not in pin:
            pin['Direction'] = 'EGPD_Input'

        return pin

    def _split_pin_fields(self, s: str) -> List[str]:
        """
        Split pin definition by commas, respecting parentheses.
        E.g., "PinId=ABC,LinkedTo=(Node1 GUID1,Node2 GUID2,),bHidden=False"
        -> ["PinId=ABC", "LinkedTo=(Node1 GUID1,Node2 GUID2,)", "bHidden=False"]
        """
        parts = []
        depth = 0
        current = []
        for ch in s:
            if ch == '(':
                depth += 1
                current.append(ch)
            elif ch == ')':
                depth -= 1
                current.append(ch)
            elif ch == ',' and depth == 0:
                parts.append(''.join(current))
                current = []
            else:
                current.append(ch)
        if current:
            parts.append(''.join(current))
        return parts

    def _parse_linked_to(self, value: str) -> List[Dict]:
        """
        Parse LinkedTo value like "(K2Node_CustomEvent_Scene3 C6E50560F49C40DC9DFD376CD449CBDF,)"
        into a list of {NodeName, PinGuid} dicts.
        """
        links = []
        # Strip outer parens
        value = value.strip()
        if value.startswith('(') and value.endswith(')'):
            value = value[1:-1]

        # Split by comma
        for part in value.split(','):
            part = part.strip()
            if not part:
                continue
            # Format: "NodeName PinGuid"
            pieces = part.rsplit(' ', 1)
            if len(pieces) == 2:
                links.append({
                    'NodeName': pieces[0].strip(),
                    'PinGuid': pieces[1].strip(),
                })
        return links

    def _parse_pin_type_value(self, key: str, value: str):
        """Parse a PinType sub-field value to the correct Python type."""
        if key in ('bIsReference', 'bIsConst', 'bIsWeakPointer',
                   'bIsUObjectWrapper', 'bSerializeAsSinglePrecisionFloat'):
            return value == 'True'
        if key == 'ContainerType':
            # "None" = 0, "Array" = 1, "Set" = 2, "Map" = 4
            mapping = {'None': 0, 'Array': 1, 'Set': 2, 'Map': 4}
            return mapping.get(value, 0)
        if key == 'PinSubCategoryObject':
            # "None" or a class path like "/Script/CoreUObject.Class'/Script/Engine.Actor'"
            if value == 'None':
                return 0  # null package index
            return value  # Will be resolved later
        if key == 'PinSubCategoryMemberReference':
            # "()" or "(MemberParent=...,MemberName=...)"
            return value
        if key == 'PinValueType':
            # "()" or "(TerminalCategory=...,TerminalSubCategory=...,...)"
            return value
        return value


# ---------------------------------------------------------------------------
# Section 3: Node Injector (ties it all together)
# ---------------------------------------------------------------------------

class K2NodeInjector:
    """
    Injects K2Nodes from paste text into a .umap file via UAssetAPI.

    This is the main entry point. It:
    1. Parses paste text into node/pin data
    2. Creates exports for each node in the asset
    3. Constructs Extras blobs with correct pin wiring
    4. Validates the result
    """

    def __init__(self, asset_file):
        """
        Args:
            asset_file: An AssetFile instance from uasset_bridge.py
        """
        self.af = asset_file
        self._name_map_cache = None

    @property
    def name_map(self) -> List[str]:
        """Lazy-load the name map."""
        if self._name_map_cache is None:
            self._name_map_cache = []
            for i in range(self.af.asset.GetNameMapIndexList().Count):
                self._name_map_cache.append(str(self.af.asset.GetNameMapIndexList()[i]))
        return self._name_map_cache

    def add_name(self, name: str) -> int:
        """Add a name to the asset's name map and return its index."""
        from System import String as DotNetString
        from UAssetAPI import FString
        idx = self.af.asset.AddNameReference(FString(name))
        # Invalidate cache
        self._name_map_cache = None
        return idx

    def inject_from_paste_text(self, paste_text_path: str) -> Dict:
        """
        Parse a paste text file and inject all nodes into the asset.

        Returns a dict with:
            - nodes_added: int
            - exports_created: list of (export_idx, node_name, node_class)
            - errors: list of error strings
        """
        parser = PasteTextParser()
        nodes = parser.parse_file(paste_text_path)

        result = {
            'nodes_added': 0,
            'exports_created': [],
            'errors': [],
        }

        if not nodes:
            result['errors'].append("No nodes found in paste text file")
            return result

        # Phase 1: Create exports for all nodes and build a name -> export_idx map
        node_export_map = {}  # node_name -> export_idx
        for node in nodes:
            try:
                export_idx = self._create_node_export(node)
                node_export_map[node['name']] = export_idx
                result['exports_created'].append((export_idx, node['name'], node['node_class']))
            except Exception as e:
                result['errors'].append(f"Failed to create export for {node['name']}: {e}")

        # Phase 2: Resolve LinkedTo references and build Extras blobs
        for node in nodes:
            if node['name'] not in node_export_map:
                continue  # Skip nodes that failed to create

            export_idx = node_export_map[node['name']]

            try:
                # Resolve LinkedTo references from node names to export indices
                self._resolve_linked_to(node, node_export_map)

                # Build the Extras blob
                writer = ExtrasWriter(self.name_map, self.add_name)
                extras_bytes = writer.write_extras(node, export_idx)

                # Set the Extras on the export
                from System import Array, Byte
                extras_array = Array.CreateInstance(Byte, len(extras_bytes))
                for i, b in enumerate(extras_bytes):
                    extras_array[i] = b

                exp = self.af.get_export(export_idx)
                exp.Extras = extras_array

                result['nodes_added'] += 1

            except Exception as e:
                result['errors'].append(f"Failed to build Extras for {node['name']}: {e}")

        return result

    def _create_node_export(self, node: Dict) -> int:
        """
        Create a new export for a K2Node.

        Returns the 0-based export index.
        """
        # Determine the import for the node's class
        class_path = node['class_path']
        # e.g., "/Script/BlueprintGraph.K2Node_CustomEvent"
        parts = class_path.split('.')
        package_path = parts[0]  # "/Script/BlueprintGraph"
        class_name = parts[1]    # "K2Node_CustomEvent"

        # Find or add the class import
        class_import_idx = self.af.find_or_add_import(package_path, class_name)

        # Find the EventGraph export to use as outer
        # The EventGraph is typically a child of the LevelScriptBlueprint
        # For level blueprints, the outer is the PersistentLevel's blueprint
        # We need to find the correct outer - look for existing K2Nodes and use their outer
        outer_idx = self._find_event_graph_outer()

        # Create the export
        export_idx = self.af.add_export(
            class_import_idx=class_import_idx,
            outer_export_idx=outer_idx,
            object_name=node['name'],
        )

        # Set tagged properties (NodePosX, NodePosY, NodeGuid, etc.)
        # These go into the export's Data[] array (handled by UAssetAPI)
        exp = self.af.get_export(export_idx)

        # NodePosX and NodePosY
        if 'NodePosX' in node:
            self.af.add_property(export_idx, 'NodePosX', 'int', node['NodePosX'])
        if 'NodePosY' in node:
            self.af.add_property(export_idx, 'NodePosY', 'int', node['NodePosY'])

        # NodeGuid (StructProperty with FGuid)
        if 'NodeGuid' in node:
            # NodeGuid is stored as a tagged property, not in Extras
            # It's a StructProperty<Guid> with 4 int32 fields
            pass  # TODO: implement struct property creation

        # CustomFunctionName for CustomEvent nodes
        if 'CustomFunctionName' in node:
            self.af.add_property(export_idx, 'CustomFunctionName', 'name',
                                 node['CustomFunctionName'])

        # FunctionReference for CallFunction nodes
        if 'FunctionReference' in node.get('properties', {}):
            pass  # TODO: implement FMemberReference property

        return export_idx

    def _find_event_graph_outer(self) -> int:
        """
        Find the outer export index for new K2Nodes.
        This is typically the EventGraph UEdGraph export.
        """
        # Look for an existing K2Node and use its outer
        for i in range(self.af.export_count):
            exp = self.af.get_export(i)
            cname = self.af.get_export_class_name(exp)
            if 'K2Node' in cname:
                # Get this export's OuterIndex
                outer = exp.OuterIndex
                if hasattr(outer, 'Index'):
                    return outer.Index - 1  # Convert from 1-based to 0-based
                return 0

        # Fallback: look for an export named "EventGraph"
        for i in range(self.af.export_count):
            exp = self.af.get_export(i)
            name = self.af.get_export_name(exp)
            if name == 'EventGraph':
                return i

        # Last resort: use export 0
        return 0

    def _resolve_linked_to(self, node: Dict, node_export_map: Dict):
        """
        Resolve LinkedTo references from node names to FPackageIndex values.

        The paste text format uses "NodeName PinGuid" for LinkedTo.
        We need to convert NodeName to the export's FPackageIndex (export_idx + 1).
        """
        for pin in node.get('pins', []):
            resolved_links = []
            for link in pin.get('LinkedTo', []):
                target_name = link['NodeName']
                if target_name in node_export_map:
                    target_export_idx = node_export_map[target_name]
                    resolved_links.append({
                        'OwningNodePkgIdx': target_export_idx + 1,  # 1-based
                        'PinGuid': link['PinGuid'],
                    })
                else:
                    # Try to find the node in existing exports
                    found = False
                    for i in range(self.af.export_count):
                        exp = self.af.get_export(i)
                        if self.af.get_export_name(exp) == target_name:
                            resolved_links.append({
                                'OwningNodePkgIdx': i + 1,
                                'PinGuid': link['PinGuid'],
                            })
                            found = True
                            break
                    if not found:
                        # Node not found - this is a dangling reference
                        # Write it as null to avoid corruption
                        pass

            pin['LinkedTo'] = resolved_links

        # Also build user_defined_pins for CustomEvent nodes
        if node['node_class'] in ('K2Node_CustomEvent', 'K2Node_Event'):
            # User-defined pins are the output pins that aren't 'OutputDelegate' or 'then'
            builtin_pins = {'OutputDelegate', 'then', 'execute'}
            user_pins = []
            for pin in node.get('pins', []):
                if pin.get('PinName') not in builtin_pins and pin.get('Direction') == 'EGPD_Output':
                    user_pins.append({
                        'PinName': pin['PinName'],
                        'PinType': pin.get('PinType', {}),
                        'Direction': pin.get('Direction', 'EGPD_Output'),
                        'DefaultValue': pin.get('DefaultValue', ''),
                    })
            node['user_defined_pins'] = user_pins


def generate_guid() -> str:
    """Generate a new random GUID in the format expected by UE (32 hex chars)."""
    return uuid.uuid4().hex.upper()
