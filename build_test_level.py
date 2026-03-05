#!/usr/bin/env python3
"""
build_test_level.py -- Build a test level by injecting new K2Nodes into an existing .umap.

Strategy:
  1. Copy SL_Restaurant_Logic.umap as the base
  2. Clone existing K2Node exports (Chain 1: HandPlaced) to create Chain 2 (TeleportToHeather)
  3. Modify tagged properties (names, positions, GUIDs)
  4. Reconstruct Extras blobs with correct pin wiring
  5. Add a comment node
  6. Validate and save

This proves the full injection pipeline works end-to-end.
"""

import sys
import os
import shutil
import struct
import io
import uuid

sys.path.insert(0, '/home/ubuntu/ue56-level-editor')

from core.uasset_bridge import (
    AssetFile, NormalExport, FName, FPackageIndex, FString,
    NetList, PropertyData, StructPropertyData, IntPropertyData,
    NamePropertyData, ObjectPropertyData, BoolPropertyData,
    StrPropertyData, GuidPropertyData, TextPropertyData,
    ArrayPropertyData,
    _make_ptn, _make_ptn_struct,
    System,
)
from parse_extras import ExtrasParser

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def build_name_map(af):
    """Build the name map list from the asset."""
    name_map = []
    for i in range(af.asset.GetNameMapIndexList().Count):
        name_map.append(str(af.asset.GetNameMapIndexList()[i]))
    return name_map


def add_name(af, name):
    """Add a name to the asset's name map and return its index."""
    idx = af.asset.AddNameReference(FString(name))
    return idx


def ensure_name(af, name_map, name):
    """Ensure a name exists in the name map. Returns its index."""
    if name in name_map:
        return name_map.index(name)
    idx = add_name(af, name)
    # Refresh name map
    while len(name_map) <= idx:
        name_map.append(None)
    name_map[idx] = name
    return idx


def make_guid_bytes(guid_hex):
    """Convert 32-char hex GUID to 16 bytes (4 x uint32 LE)."""
    guid_hex = guid_hex.replace('-', '')
    a = int(guid_hex[0:8], 16)
    b = int(guid_hex[8:16], 16)
    c = int(guid_hex[16:24], 16)
    d = int(guid_hex[24:32], 16)
    return struct.pack('<IIII', a, b, c, d)


def new_guid():
    """Generate a new random GUID as 32-char hex string."""
    return uuid.uuid4().hex.upper()


def set_extras_bytes(export, data: bytes):
    """Set the Extras blob on an export from Python bytes."""
    from System import Array, Byte
    arr = Array.CreateInstance(Byte, len(data))
    for i, b in enumerate(data):
        arr[i] = b
    export.Extras = arr


# ---------------------------------------------------------------------------
# Clone an existing export's tagged properties (Data list)
# ---------------------------------------------------------------------------

def clone_export_data(source_export, target_export):
    """
    Deep-clone all tagged properties from source to target export.
    This copies the .NET PropertyData objects by re-serializing them.
    """
    if source_export.Data is None:
        target_export.Data = NetList[PropertyData]()
        return

    # The simplest approach: just copy the reference
    # UAssetAPI will serialize each property independently
    target_export.Data = NetList[PropertyData]()
    for i in range(source_export.Data.Count):
        target_export.Data.Add(source_export.Data[i])


# ---------------------------------------------------------------------------
# Extras Blob Manipulation
# ---------------------------------------------------------------------------

def remap_extras_blob(original_extras: bytes, name_map: list,
                      owning_pkg_idx_old: int, owning_pkg_idx_new: int,
                      linked_to_remap: dict, new_pin_guids: dict = None) -> bytes:
    """
    Take an existing Extras blob and remap:
    - OwningNode references (both wrapper and pin-level)
    - LinkedTo target references (via linked_to_remap dict)
    - Optionally replace pin GUIDs (via new_pin_guids dict)

    This is a binary-level rewrite that preserves all other data exactly.

    Args:
        original_extras: The source Extras blob bytes
        name_map: Asset name map
        owning_pkg_idx_old: Old owning node FPackageIndex (1-based)
        owning_pkg_idx_new: New owning node FPackageIndex (1-based)
        linked_to_remap: Dict mapping old target pkg_idx -> new target pkg_idx
        new_pin_guids: Dict mapping old pin GUID (hex) -> new pin GUID (hex)

    Returns:
        Modified Extras blob bytes
    """
    parser = ExtrasParser(original_extras, name_map)
    parsed = parser.parse()

    # Reconstruct with remapped values
    w = io.BytesIO()

    def write_int32(val):
        w.write(struct.pack('<i', val))

    def write_uint32(val):
        w.write(struct.pack('<I', val))

    def write_uint8(val):
        w.write(struct.pack('<B', val))

    def write_guid(hex_str):
        a = int(hex_str[0:8], 16)
        b = int(hex_str[8:16], 16)
        c = int(hex_str[16:24], 16)
        d = int(hex_str[24:32], 16)
        w.write(struct.pack('<IIII', a, b, c, d))

    def write_fstring(text):
        if text is None:
            write_int32(0)
            return
        encoded = text.encode('utf-8') + b'\x00'
        write_int32(len(encoded))
        w.write(encoded)

    def write_ftext(ft):
        write_uint32(ft['_flags'])
        w.write(struct.pack('<b', ft['_history_type']))
        ht = ft['_history_type']
        if ht == -1:
            write_int32(ft.get('_has_culture_invariant', 0))
            if ft.get('_has_culture_invariant', 0):
                write_fstring(ft['_text'])
        elif ht == 0:
            write_fstring(ft['_namespace'])
            write_fstring(ft['_key'])
            write_fstring(ft['_source'])
        elif ht == 11:
            tid_idx, tid_num = ft['_table_id']
            write_int32(tid_idx)
            write_int32(tid_num)
            write_fstring(ft['_table_key'])

    def remap_pkg_idx(idx):
        """Remap a package index if it matches the old owning node."""
        if idx == owning_pkg_idx_old:
            return owning_pkg_idx_new
        return linked_to_remap.get(idx, idx)

    def maybe_remap_guid(guid_hex):
        """Remap a pin GUID if specified."""
        if new_pin_guids and guid_hex in new_pin_guids:
            return new_pin_guids[guid_hex]
        return guid_hex

    # --- Write pin array ---
    write_int32(parsed['num_pins'])

    for pin in parsed['pins']:
        if pin is None:
            write_int32(1)  # bNullPtr = true
            continue

        # SerializePin wrapper
        write_int32(0)  # bNullPtr = false
        write_int32(remap_pkg_idx(pin['_wrapper_owning_node']))
        write_guid(maybe_remap_guid(pin['_wrapper_pin_guid']))

        # Pin.Serialize()
        write_int32(remap_pkg_idx(pin['OwningNode']))
        write_guid(maybe_remap_guid(pin['PinId']))

        # PinName (FName raw)
        write_int32(pin['PinName'][0])
        write_int32(pin['PinName'][1])

        # PinFriendlyName (FText)
        write_ftext(pin['PinFriendlyName'])

        # SourceIndex
        write_int32(pin['SourceIndex'])

        # PinToolTip
        write_fstring(pin['PinToolTip'])

        # Direction
        write_uint8(pin['Direction'])

        # PinType
        pt = pin['PinType']
        write_int32(pt['PinCategory'][0])
        write_int32(pt['PinCategory'][1])
        write_int32(pt['PinSubCategory'][0])
        write_int32(pt['PinSubCategory'][1])
        write_int32(pt['PinSubCategoryObject'])  # Don't remap - these are class refs
        write_uint8(pt['ContainerType'])
        if pt['ContainerType'] == 4:
            vt = pt['PinValueType']
            write_int32(vt['Category'][0])
            write_int32(vt['Category'][1])
            write_int32(vt['SubCategory'][0])
            write_int32(vt['SubCategory'][1])
            write_int32(vt['Object'])
            write_int32(1 if vt['bIsWeakPointer'] else 0)
        write_int32(1 if pt['bIsReference'] else 0)
        write_int32(1 if pt['bIsWeakPointer'] else 0)
        # MemberParent - remap if it points to the BPGC
        write_int32(pt['MemberParent'])  # Keep as-is (class reference, not node)
        write_int32(pt['MemberName'][0])
        write_int32(pt['MemberName'][1])
        write_guid(pt['MemberGuid'])
        write_int32(1 if pt['bIsConst'] else 0)
        write_int32(1 if pt['bIsUObjectWrapper'] else 0)
        write_int32(1 if pt['bSerializeAsSinglePrecisionFloat'] else 0)

        # DefaultValue
        write_fstring(pin['DefaultValue'])

        # AutogeneratedDefaultValue
        write_fstring(pin['AutogeneratedDefaultValue'])

        # DefaultObject
        write_int32(pin['DefaultObject'])

        # DefaultTextValue
        write_ftext(pin['DefaultTextValue'])

        # LinkedTo
        linked = pin['LinkedTo']
        write_int32(len(linked))
        for lt in linked:
            if lt.get('_is_null', False):
                write_int32(1)
            else:
                write_int32(0)
                write_int32(remap_pkg_idx(lt['OwningNode']))
                write_guid(maybe_remap_guid(lt['PinGuid']))

        # SubPins
        sub_pins = pin['SubPins']
        write_int32(len(sub_pins))
        for sp in sub_pins:
            if sp.get('_is_null', False):
                write_int32(1)
            else:
                write_int32(0)
                write_int32(remap_pkg_idx(sp['OwningNode']))
                write_guid(maybe_remap_guid(sp['PinGuid']))

        # ParentPin
        pp = pin['ParentPin']
        if pp.get('_is_null', False):
            write_int32(1)
        else:
            write_int32(0)
            write_int32(remap_pkg_idx(pp['OwningNode']))
            write_guid(maybe_remap_guid(pp['PinGuid']))

        # RefPassThrough
        rpt = pin['RefPassThrough']
        if rpt.get('_is_null', False):
            write_int32(1)
        else:
            write_int32(0)
            write_int32(remap_pkg_idx(rpt['OwningNode']))
            write_guid(maybe_remap_guid(rpt['PinGuid']))

        # PersistentGuid
        write_guid(pin['PersistentGuid'])

        # BitField
        write_uint32(pin['BitField'])

    # Append any remaining bytes (UserDefinedPins for CustomEvent/Event)
    remaining = original_extras[parsed['bytes_consumed']:]
    w.write(remaining)

    return w.getvalue()


# ---------------------------------------------------------------------------
# Main Build Script
# ---------------------------------------------------------------------------

def main():
    # --- Step 1: Copy the base .umap ---
    src = '/home/ubuntu/test_assets/SL_Restaurant_Logic.umap'
    dst = '/home/ubuntu/test_assets/SL_Restaurant_Logic_TestLevel.umap'
    shutil.copy2(src, dst)
    print(f"Copied {src} -> {dst}")

    # --- Step 2: Load the copy ---
    af = AssetFile(dst, '5.6')
    name_map = build_name_map(af)
    print(f"Loaded: {af.summary()}")
    print()

    # --- Step 3: Understand existing Chain 1 structure ---
    # Chain 1 exports (0-based indices):
    #   50: K2Node_CustomEvent / K2Node_CustomEvent_Scene5_HandPlaced
    #   49: K2Node_CallFunction / K2Node_DisableActor_Scene5_HandPlaced
    #   48: K2Node_CallFunction / K2Node_CallFunction_Broadcast_Scene5_HandPlaced
    #   51: K2Node_GetSubsystem / K2Node_GetSubsystem_Scene5_HandPlaced
    #   52: K2Node_MakeStruct / K2Node_MakeStruct_Scene5_HandPlaced
    #   38: EdGraphNode_Comment / EdGraphNode_Comment_Scene5_HandPlaced

    # Chain 2 design (TeleportToHeather):
    #   CustomEvent "Scene5_TeleportToHeather" -> DisableActor -> BroadcastMessage + GetSubsystem + MakeStruct
    #   Comment: "Scene 5b: TeleportToHeather -> DisableActor -> Broadcast Step 16 (LS_5_2)"

    # We'll clone each Chain 1 node and remap it for Chain 2.

    # --- Step 4: Generate new GUIDs for all new pins ---
    # We need to track old pin GUIDs -> new pin GUIDs for LinkedTo remapping

    # First, parse all Chain 1 Extras to get their pin GUIDs
    chain1_exports = {
        'custom_event': 50,
        'disable_actor': 49,
        'broadcast': 48,
        'get_subsystem': 51,
        'make_struct': 52,
        'comment': 38,
    }

    chain1_pin_guids = {}  # export_idx -> [pin_guid, ...]
    for role, idx in chain1_exports.items():
        exp = af.get_export(idx)
        extras = bytes(exp.Extras) if exp.Extras else b''
        if len(extras) > 4:
            parser = ExtrasParser(extras, name_map)
            parsed = parser.parse()
            guids = []
            for pin in parsed['pins']:
                if pin:
                    guids.append(pin['PinId'])
            chain1_pin_guids[idx] = guids
        else:
            chain1_pin_guids[idx] = []

    print("Chain 1 pin GUIDs:")
    for idx, guids in chain1_pin_guids.items():
        exp = af.get_export(idx)
        print(f"  Export[{idx}] {af.get_export_name(exp)}: {len(guids)} pins")
        for g in guids:
            print(f"    {g}")
    print()

    # Generate new GUIDs for Chain 2
    pin_guid_remap = {}  # old_guid -> new_guid
    for idx, guids in chain1_pin_guids.items():
        for g in guids:
            pin_guid_remap[g] = new_guid()

    # --- Step 5: Create new exports by cloning Chain 1 ---
    # We need to add new exports to the asset. The approach:
    # 1. Create NormalExport with same ClassIndex and OuterIndex
    # 2. Copy tagged properties, modifying names/values
    # 3. Build new Extras blobs with remapped references

    event_graph_pkg = 38  # Export[37] = EventGraph, pkg_idx = 38

    # Track new export indices (0-based)
    new_exports = {}

    # --- 5a: Clone CustomEvent ---
    print("Creating Chain 2 exports...")

    src_exp = af.get_export(50)  # K2Node_CustomEvent_Scene5_HandPlaced
    new_name = "K2Node_CustomEvent_Scene5_TeleportToHeather"
    ensure_name(af, name_map, new_name)
    ensure_name(af, name_map, "Scene5_TeleportToHeather")

    new_idx = af.add_export(
        object_name=new_name,
        class_index=src_exp.ClassIndex.Index,
        outer_index=event_graph_pkg,
        template_index=src_exp.TemplateIndex.Index,
        object_flags=0x00000001,
    )
    new_export_idx = new_idx - 1  # 0-based
    new_exports['custom_event'] = new_export_idx
    print(f"  Created CustomEvent: export[{new_export_idx}] (pkg={new_idx})")

    # Set tagged properties
    new_exp = af.get_export(new_export_idx)
    new_exp.Data = NetList[PropertyData]()

    # CustomFunctionName
    cfn = NamePropertyData(FName.FromString(af.asset, "CustomFunctionName"))
    cfn.Value = FName.FromString(af.asset, "Scene5_TeleportToHeather")
    cfn.PropertyTypeName = _make_ptn(af.asset, "NameProperty")
    new_exp.Data.Add(cfn)

    # NodePosX
    npx = IntPropertyData(FName.FromString(af.asset, "NodePosX"))
    npx.Value = 1712
    npx.PropertyTypeName = _make_ptn(af.asset, "IntProperty")
    new_exp.Data.Add(npx)

    # NodePosY (offset down from Chain 1)
    npy = IntPropertyData(FName.FromString(af.asset, "NodePosY"))
    npy.Value = 1700
    npy.PropertyTypeName = _make_ptn(af.asset, "IntProperty")
    new_exp.Data.Add(npy)

    # NodeGuid - clone from source and modify
    for i in range(src_exp.Data.Count):
        prop = src_exp.Data[i]
        if str(prop.Name) == "NodeGuid":
            new_exp.Data.Add(prop)  # Reuse the struct (it'll get a new GUID on recompile)
            break

    # --- 5b: Clone DisableActor ---
    src_exp = af.get_export(49)
    new_name = "K2Node_DisableActor_Scene5_TeleportToHeather"
    ensure_name(af, name_map, new_name)

    new_idx = af.add_export(
        object_name=new_name,
        class_index=src_exp.ClassIndex.Index,
        outer_index=event_graph_pkg,
        template_index=src_exp.TemplateIndex.Index,
        object_flags=0x00000001,
    )
    new_export_idx = new_idx - 1
    new_exports['disable_actor'] = new_export_idx
    print(f"  Created DisableActor: export[{new_export_idx}] (pkg={new_idx})")

    new_exp = af.get_export(new_export_idx)
    new_exp.Data = NetList[PropertyData]()

    # Clone FunctionReference from source
    for i in range(src_exp.Data.Count):
        prop = src_exp.Data[i]
        pname = str(prop.Name)
        if pname == "FunctionReference":
            new_exp.Data.Add(prop)
        elif pname == "NodePosX":
            npx = IntPropertyData(FName.FromString(af.asset, "NodePosX"))
            npx.Value = 2064
            npx.PropertyTypeName = _make_ptn(af.asset, "IntProperty")
            new_exp.Data.Add(npx)
        elif pname == "NodePosY":
            npy = IntPropertyData(FName.FromString(af.asset, "NodePosY"))
            npy.Value = 1668
            npy.PropertyTypeName = _make_ptn(af.asset, "IntProperty")
            new_exp.Data.Add(npy)
        elif pname == "NodeGuid":
            new_exp.Data.Add(prop)

    # --- 5c: Clone BroadcastMessage ---
    src_exp = af.get_export(48)
    new_name = "K2Node_CallFunction_Broadcast_Scene5_TeleportToHeather"
    ensure_name(af, name_map, new_name)

    new_idx = af.add_export(
        object_name=new_name,
        class_index=src_exp.ClassIndex.Index,
        outer_index=event_graph_pkg,
        template_index=src_exp.TemplateIndex.Index,
        object_flags=0x00000001,
    )
    new_export_idx = new_idx - 1
    new_exports['broadcast'] = new_export_idx
    print(f"  Created Broadcast: export[{new_export_idx}] (pkg={new_idx})")

    new_exp = af.get_export(new_export_idx)
    new_exp.Data = NetList[PropertyData]()

    for i in range(src_exp.Data.Count):
        prop = src_exp.Data[i]
        pname = str(prop.Name)
        if pname == "FunctionReference":
            new_exp.Data.Add(prop)
        elif pname == "NodePosX":
            npx = IntPropertyData(FName.FromString(af.asset, "NodePosX"))
            npx.Value = 2480
            npx.PropertyTypeName = _make_ptn(af.asset, "IntProperty")
            new_exp.Data.Add(npx)
        elif pname == "NodePosY":
            npy = IntPropertyData(FName.FromString(af.asset, "NodePosY"))
            npy.Value = 1748
            npy.PropertyTypeName = _make_ptn(af.asset, "IntProperty")
            new_exp.Data.Add(npy)
        elif pname == "NodeGuid":
            new_exp.Data.Add(prop)

    # --- 5d: Clone GetSubsystem ---
    src_exp = af.get_export(51)
    new_name = "K2Node_GetSubsystem_Scene5_TeleportToHeather"
    ensure_name(af, name_map, new_name)

    new_idx = af.add_export(
        object_name=new_name,
        class_index=src_exp.ClassIndex.Index,
        outer_index=event_graph_pkg,
        template_index=src_exp.TemplateIndex.Index,
        object_flags=0x00000001,
    )
    new_export_idx = new_idx - 1
    new_exports['get_subsystem'] = new_export_idx
    print(f"  Created GetSubsystem: export[{new_export_idx}] (pkg={new_idx})")

    new_exp = af.get_export(new_export_idx)
    new_exp.Data = NetList[PropertyData]()

    for i in range(src_exp.Data.Count):
        prop = src_exp.Data[i]
        pname = str(prop.Name)
        if pname == "CustomClass":
            new_exp.Data.Add(prop)
        elif pname == "NodePosX":
            npx = IntPropertyData(FName.FromString(af.asset, "NodePosX"))
            npx.Value = 2064
            npx.PropertyTypeName = _make_ptn(af.asset, "IntProperty")
            new_exp.Data.Add(npx)
        elif pname == "NodePosY":
            npy = IntPropertyData(FName.FromString(af.asset, "NodePosY"))
            npy.Value = 1900
            npy.PropertyTypeName = _make_ptn(af.asset, "IntProperty")
            new_exp.Data.Add(npy)
        elif pname == "NodeGuid":
            new_exp.Data.Add(prop)

    # --- 5e: Clone MakeStruct ---
    src_exp = af.get_export(52)
    new_name = "K2Node_MakeStruct_Scene5_TeleportToHeather"
    ensure_name(af, name_map, new_name)

    new_idx = af.add_export(
        object_name=new_name,
        class_index=src_exp.ClassIndex.Index,
        outer_index=event_graph_pkg,
        template_index=src_exp.TemplateIndex.Index,
        object_flags=0x00000001,
    )
    new_export_idx = new_idx - 1
    new_exports['make_struct'] = new_export_idx
    print(f"  Created MakeStruct: export[{new_export_idx}] (pkg={new_idx})")

    new_exp = af.get_export(new_export_idx)
    new_exp.Data = NetList[PropertyData]()

    for i in range(src_exp.Data.Count):
        prop = src_exp.Data[i]
        pname = str(prop.Name)
        if pname in ("bMadeAfterOverridePinRemoval", "ShowPinForProperties", "StructType"):
            new_exp.Data.Add(prop)
        elif pname == "NodePosX":
            npx = IntPropertyData(FName.FromString(af.asset, "NodePosX"))
            npx.Value = 2064
            npx.PropertyTypeName = _make_ptn(af.asset, "IntProperty")
            new_exp.Data.Add(npx)
        elif pname == "NodePosY":
            npy = IntPropertyData(FName.FromString(af.asset, "NodePosY"))
            npy.Value = 1932
            npy.PropertyTypeName = _make_ptn(af.asset, "IntProperty")
            new_exp.Data.Add(npy)
        elif pname == "NodeGuid":
            new_exp.Data.Add(prop)

    # --- 5f: Create Comment node ---
    src_exp = af.get_export(38)  # EdGraphNode_Comment_Scene5_HandPlaced
    new_name = "EdGraphNode_Comment_Scene5_TeleportToHeather"
    ensure_name(af, name_map, new_name)

    new_idx = af.add_export(
        object_name=new_name,
        class_index=src_exp.ClassIndex.Index,
        outer_index=event_graph_pkg,
        template_index=src_exp.TemplateIndex.Index,
        object_flags=0x00000001,
    )
    new_export_idx = new_idx - 1
    new_exports['comment'] = new_export_idx
    print(f"  Created Comment: export[{new_export_idx}] (pkg={new_idx})")

    new_exp = af.get_export(new_export_idx)
    new_exp.Data = NetList[PropertyData]()

    ensure_name(af, name_map, "NodeWidth")
    ensure_name(af, name_map, "NodeHeight")
    ensure_name(af, name_map, "NodeComment")

    npx = IntPropertyData(FName.FromString(af.asset, "NodePosX"))
    npx.Value = 1632
    npx.PropertyTypeName = _make_ptn(af.asset, "IntProperty")
    new_exp.Data.Add(npx)

    npy = IntPropertyData(FName.FromString(af.asset, "NodePosY"))
    npy.Value = 1590
    npy.PropertyTypeName = _make_ptn(af.asset, "IntProperty")
    new_exp.Data.Add(npy)

    nw = IntPropertyData(FName.FromString(af.asset, "NodeWidth"))
    nw.Value = 1200
    nw.PropertyTypeName = _make_ptn(af.asset, "IntProperty")
    new_exp.Data.Add(nw)

    nh = IntPropertyData(FName.FromString(af.asset, "NodeHeight"))
    nh.Value = 432
    nh.PropertyTypeName = _make_ptn(af.asset, "IntProperty")
    new_exp.Data.Add(nh)

    ensure_name(af, name_map, "StrProperty")
    nc = StrPropertyData(FName.FromString(af.asset, "NodeComment"))
    nc.Value = FString("Scene 5b: TeleportToHeather -> DisableActor -> Broadcast Step 16 (LS_5_2)")
    nc.PropertyTypeName = _make_ptn(af.asset, "StrProperty")
    new_exp.Data.Add(nc)

    # Clone NodeGuid from source
    for i in range(src_exp.Data.Count):
        prop = src_exp.Data[i]
        if str(prop.Name) == "NodeGuid":
            new_exp.Data.Add(prop)
            break

    # Comment has 4-byte Extras (just int32 count=0 for empty pin array)
    set_extras_bytes(af.get_export(new_exports['comment']), struct.pack('<i', 0))

    print()
    print("New export indices:")
    for role, idx in new_exports.items():
        print(f"  {role}: export[{idx}] (pkg={idx+1})")
    print()

    # --- Step 6: Build Extras blobs for new K2Nodes ---
    # Refresh name map after adding names
    name_map = build_name_map(af)

    # Build the linked_to remap: old Chain 1 pkg_idx -> new Chain 2 pkg_idx
    linked_to_remap = {}
    for role in ['custom_event', 'disable_actor', 'broadcast', 'get_subsystem', 'make_struct']:
        old_idx = chain1_exports[role]
        new_idx = new_exports[role]
        linked_to_remap[old_idx + 1] = new_idx + 1  # pkg_idx is 1-based

    print("LinkedTo remap (pkg_idx):")
    for old, new in linked_to_remap.items():
        print(f"  {old} -> {new}")
    print()

    # For each Chain 2 node, take Chain 1's Extras and remap
    for role in ['custom_event', 'disable_actor', 'broadcast', 'get_subsystem', 'make_struct']:
        old_export_idx = chain1_exports[role]
        new_export_idx = new_exports[role]

        old_exp = af.get_export(old_export_idx)
        old_extras = bytes(old_exp.Extras) if old_exp.Extras else b''

        if len(old_extras) <= 4:
            # Just copy as-is (e.g., comment nodes with 4-byte empty pin array)
            set_extras_bytes(af.get_export(new_export_idx), old_extras)
            print(f"  {role}: copied {len(old_extras)}B extras as-is")
            continue

        old_pkg = old_export_idx + 1
        new_pkg = new_export_idx + 1

        new_extras = remap_extras_blob(
            old_extras, name_map,
            owning_pkg_idx_old=old_pkg,
            owning_pkg_idx_new=new_pkg,
            linked_to_remap=linked_to_remap,
            new_pin_guids=pin_guid_remap,
        )

        set_extras_bytes(af.get_export(new_export_idx), new_extras)
        print(f"  {role}: remapped {len(old_extras)}B -> {len(new_extras)}B extras")

    print()

    # --- Step 7: Validate ---
    print("Validating...")
    issues = af.validate()
    if issues:
        for issue in issues:
            print(f"  {issue}")
    else:
        print("  No issues found!")
    print()

    # --- Step 8: Save ---
    output_path = dst
    saved = af.save(output_path=output_path, create_backup=False, validate_first=True)
    print(f"Saved to: {saved}")

    # --- Step 9: Verify round-trip ---
    print()
    print("Verifying round-trip...")
    af2 = AssetFile(str(saved), '5.6')
    print(f"  Reloaded: {af2.summary()}")

    # Check new exports exist
    for role, idx in new_exports.items():
        exp = af2.get_export(idx)
        cname = af2.get_export_class_name(exp)
        oname = af2.get_export_name(exp)
        extras_len = len(bytes(exp.Extras)) if exp.Extras else 0
        props = af2.get_export_properties(exp)
        print(f"  {role}: [{idx}] {cname}/{oname} extras={extras_len}B props={len(props)}")

    # Verify the new Extras blobs parse correctly
    name_map2 = build_name_map(af2)
    print()
    print("Parsing new K2Node Extras:")
    for role in ['custom_event', 'disable_actor', 'broadcast', 'get_subsystem', 'make_struct']:
        idx = new_exports[role]
        exp = af2.get_export(idx)
        extras = bytes(exp.Extras) if exp.Extras else b''
        if len(extras) <= 4:
            print(f"  {role}: {len(extras)}B (no pins)")
            continue
        parser = ExtrasParser(extras, name_map2)
        try:
            parsed = parser.parse()
            print(f"  {role}: {parsed['num_pins']} pins, "
                  f"{parsed['bytes_consumed']}/{len(extras)}B consumed, "
                  f"{parsed['bytes_remaining']}B remaining")
            for pin in parsed['pins']:
                if pin:
                    pn = name_map2[pin['PinName'][0]] if 0 <= pin['PinName'][0] < len(name_map2) else '?'
                    linked_count = len([l for l in pin['LinkedTo'] if not l.get('_is_null', False)])
                    print(f"    {pn} dir={pin['Direction']} linked={linked_count}")
        except Exception as e:
            print(f"  {role}: PARSE ERROR: {e}")

    print()
    print("BUILD COMPLETE!")
    print(f"Test level: {saved}")
    print(f"New exports: {len(new_exports)}")
    print(f"Total exports: {af2.export_count}")


if __name__ == '__main__':
    main()
