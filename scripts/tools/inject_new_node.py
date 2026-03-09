#!/usr/bin/env python3
"""
inject_new_node.py -- Add a brand new K2Node_CallFunction export to MoreLogicTests.
Tests whether the engine accepts entirely new K2Node exports created via binary editing.

Strategy:
1. Clone export [6] (existing PrintString) as a new export
2. Remap the clone's Extras blob: new OwningNode refs, new PinGuids
3. Add the new export to EventGraph's Nodes array
4. Wire: existing PrintString 'then' -> new PrintString 'execute'
5. Run post-save patcher for SSEO + trailer
"""

import sys
import os
import struct
import uuid
import random

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from core.uasset_bridge import AssetFile
from parse_extras import ExtrasParser
from core.post_save_patcher import patch_uasset_post_save

import clr
clr.AddReference(os.path.join(os.path.dirname(__file__), 'lib', 'publish', 'UAssetAPI'))
from UAssetAPI.ExportTypes import NormalExport
from UAssetAPI.UnrealTypes import FName, FPackageIndex
from UAssetAPI.PropertyTypes.Objects import ObjectPropertyData
from System import Array, Byte
from System.Collections.Generic import List
from UAssetAPI.PropertyTypes.Objects import PropertyData


def generate_ue_guid_bytes():
    """Generate 16 random bytes for a UE GUID."""
    return bytes([random.randint(0, 255) for _ in range(16)])


def remap_extras_owning_and_guids(original_extras, name_map, new_owning_node_1based):
    """
    Patch an Extras blob to change all OwningNode references and PinGuids.
    Also clears all LinkedTo arrays (sets count to 0 without removing entries).
    
    Uses the parser to get exact pin byte offsets, then patches in-place.
    
    Layout per pin:
      wrapper_start + 0:  is_null (int32, 4 bytes)
      wrapper_start + 4:  wrapper_OwningNode (int32, 4 bytes)
      wrapper_start + 8:  wrapper_PinGuid (16 bytes)
      wrapper_start + 24: Pin.Serialize() starts
        Pin.Serialize() + 0: OwningNode (int32, 4 bytes)
        Pin.Serialize() + 4: PinGuid (16 bytes)
        ... rest of pin data
    """
    parser = ExtrasParser(original_extras, name_map)
    parsed = parser.parse()
    
    data = bytearray(original_extras)
    new_pin_guids = []  # Store for wiring later
    
    offset = 4  # skip num_pins
    
    for i, pin in enumerate(parsed['pins']):
        if pin is None:
            offset += 4  # just the is_null int32
            new_pin_guids.append(None)
            continue
        
        wrapper_start = offset
        pin_serialize_start = wrapper_start + 24  # is_null(4) + OwningNode(4) + PinGuid(16)
        pin_bytes = pin['_bytes']
        
        # Generate new PinGuid
        new_guid = generate_ue_guid_bytes()
        new_pin_guids.append(new_guid)
        
        # Patch wrapper OwningNode (offset +4)
        struct.pack_into('<i', data, wrapper_start + 4, new_owning_node_1based)
        
        # Patch wrapper PinGuid (offset +8)
        data[wrapper_start + 8 : wrapper_start + 24] = new_guid
        
        # Patch Pin.Serialize() OwningNode (offset +24)
        struct.pack_into('<i', data, pin_serialize_start, new_owning_node_1based)
        
        # Patch Pin.Serialize() PinGuid (offset +28)
        data[pin_serialize_start + 4 : pin_serialize_start + 20] = new_guid
        
        # Move to next pin
        offset = wrapper_start + 24 + pin_bytes
    
    return bytes(data), new_pin_guids


def clear_all_linked_to(extras_blob, name_map):
    """
    Clear all LinkedTo entries from an Extras blob.
    For each pin, finds the LinkedTo array, sets count to 0, and removes entry bytes.
    Returns the cleaned blob (may be smaller than original).
    """
    parser = ExtrasParser(extras_blob, name_map)
    parsed = parser.parse()
    
    data = bytearray(extras_blob)
    # Process pins in reverse order so byte removals don't shift earlier offsets
    removals = []  # (start, length) of bytes to remove
    
    offset = 4  # skip num_pins
    for i, pin in enumerate(parsed['pins']):
        if pin is None:
            offset += 4
            continue
        
        wrapper_start = offset
        pin_serialize_start = wrapper_start + 24
        pin_bytes = pin['_bytes']
        
        # Find LinkedTo within this pin using sub-parser
        pin_data = extras_blob[pin_serialize_start : pin_serialize_start + pin_bytes]
        sub = ExtrasParser(pin_data, name_map)
        sub.read_package_index_raw()  # OwningNode
        sub.read_guid()  # PinId
        sub.read_fname_raw()  # PinName
        sub.read_ftext()  # FriendlyName
        sub.read_int32()  # SourceIndex
        sub.read_fstring()  # ToolTip
        sub.read_uint8()  # Direction
        sub.read_pin_type()  # PinType
        sub.read_fstring()  # DefaultValue
        sub.read_fstring()  # AutogenDefault
        sub.read_package_index_raw()  # DefaultObject
        sub.read_ftext()  # DefaultTextValue
        
        linked_to_offset = pin_serialize_start + sub.pos()
        linked_count = struct.unpack_from('<i', data, linked_to_offset)[0]
        
        if linked_count > 0:
            # Remove the entries (each 24 bytes) and set count to 0
            entry_start = linked_to_offset + 4
            entry_bytes = linked_count * 24
            removals.append((entry_start, entry_bytes, linked_to_offset))
        
        offset = wrapper_start + 24 + pin_bytes
    
    # Apply removals in reverse order
    for entry_start, entry_bytes, count_offset in reversed(removals):
        struct.pack_into('<i', data, count_offset, 0)  # Set count to 0
        del data[entry_start : entry_start + entry_bytes]
    
    return bytes(data)


def add_linked_to_entry(extras_blob, name_map, pin_index, target_owning_node, target_pin_guid):
    """
    Add a LinkedTo entry to a specific pin in an Extras blob.
    This INSERTS bytes into the blob (changes size).
    
    The LinkedTo array format is:
      [count: int32]
      for each entry:
        [is_null: int32] (0 = not null)
        [OwningNode: int32]
        [PinGuid: 16 bytes]
    
    Adding one entry means: increment count by 1, insert 24 bytes after count.
    """
    parser = ExtrasParser(extras_blob, name_map)
    parsed = parser.parse()
    
    data = bytearray(extras_blob)
    
    # Walk to the target pin's LinkedTo count
    offset = 4  # skip num_pins
    
    for i, pin in enumerate(parsed['pins']):
        if pin is None:
            offset += 4
            continue
        
        wrapper_start = offset
        pin_serialize_start = wrapper_start + 24
        pin_bytes = pin['_bytes']
        
        if i == pin_index:
            # Found the target pin. Now we need to find the LinkedTo count
            # within Pin.Serialize(). The LinkedTo array comes after:
            # OwningNode(4) + PinGuid(16) + PinName(8) + FriendlyName(var) +
            # SourceIndex(4) + ToolTip(var) + Direction(1) + PinType(var) +
            # DefaultValue(var) + AutogenDefault(var) + DefaultObject(4) +
            # DefaultTextValue(var)
            
            # The parser gives us the LinkedTo data. We need the byte offset.
            # Let's use a sub-parser on just this pin's data.
            pin_data = extras_blob[pin_serialize_start : pin_serialize_start + pin_bytes]
            sub_parser = ExtrasParser(pin_data, name_map)
            
            # Read through the pin fields to find LinkedTo offset
            sub_parser.read_package_index_raw()  # OwningNode
            sub_parser.read_guid()  # PinId
            sub_parser.read_fname_raw()  # PinName
            sub_parser.read_ftext()  # FriendlyName
            sub_parser.read_int32()  # SourceIndex
            sub_parser.read_fstring()  # ToolTip
            sub_parser.read_uint8()  # Direction
            sub_parser.read_pin_type()  # PinType
            sub_parser.read_fstring()  # DefaultValue
            sub_parser.read_fstring()  # AutogenDefault
            sub_parser.read_package_index_raw()  # DefaultObject
            sub_parser.read_ftext()  # DefaultTextValue
            
            # Now we're at the LinkedTo array
            linked_to_offset_in_pin = sub_parser.pos()
            linked_to_abs_offset = pin_serialize_start + linked_to_offset_in_pin
            
            # Read current count
            current_count = struct.unpack_from('<i', data, linked_to_abs_offset)[0]
            
            # Increment count
            struct.pack_into('<i', data, linked_to_abs_offset, current_count + 1)
            
            # Insert new entry after existing entries
            # Each existing entry is: is_null(4) + OwningNode(4) + PinGuid(16) = 24 bytes
            insert_offset = linked_to_abs_offset + 4 + (current_count * 24)
            
            new_entry = bytearray(24)
            struct.pack_into('<i', new_entry, 0, 0)  # is_null = 0 (not null)
            struct.pack_into('<i', new_entry, 4, target_owning_node)
            new_entry[8:24] = target_pin_guid
            
            data[insert_offset:insert_offset] = new_entry
            
            return bytes(data)
        
        offset = wrapper_start + 24 + pin_bytes
    
    raise ValueError(f"Pin index {pin_index} not found")


def main():
    src_path = '/home/ubuntu/p4_workspace/Content/Maps/Game/Restaurant/Levels/MoreLogicTests.umap'
    dst_path = '/tmp/MoreLogicTests_with_new_node.umap'
    
    af = AssetFile(src_path)
    asset = af.asset
    name_map = [str(n) for n in asset.GetNameMapIndexList()]
    
    print(f'Original: {len(asset.Exports)} exports, {len(name_map)} names')
    
    # ---------------------------------------------------------------
    # Step 1: Clone PrintString export (index 6)
    # ---------------------------------------------------------------
    original_exp = asset.Exports[6]
    original_extras = bytes(original_exp.Extras)
    
    # New export index will be 21 (0-based), 22 (1-based)
    new_export_1based = len(asset.Exports) + 1  # 22
    
    # Remap Extras: change OwningNode and PinGuids
    new_extras, new_pin_guids = remap_extras_owning_and_guids(
        original_extras, name_map, new_export_1based
    )
    
    # Clear all stale LinkedTo entries from the cloned blob
    new_extras = clear_all_linked_to(new_extras, name_map)
    print(f'Cleared stale LinkedTo entries from cloned extras')
    
    # Create new export
    new_exp = NormalExport(asset, Array[Byte](new_extras))
    new_exp.ClassIndex = FPackageIndex(original_exp.ClassIndex.Index)  # K2Node_CallFunction
    new_exp.OuterIndex = FPackageIndex(original_exp.OuterIndex.Index)  # EventGraph (4)
    new_exp.ObjectFlags = original_exp.ObjectFlags
    new_exp.ObjectName = FName(asset, "K2Node_CallFunction", 1)  # _1 suffix
    
    # Initialize Data list and copy tagged properties from original
    new_exp.Data = List[PropertyData]()
    for prop in original_exp.Data:
        new_exp.Data.Add(prop)
    
    # Change NodePosX to offset the node visually
    for prop in new_exp.Data:
        pname = str(prop.Name.Value)
        if pname == 'NodePosX':
            prop.Value = 736  # 400px to the right of original (336)
        elif pname == 'NodeGuid':
            # New GUID for the node
            for sub in prop.Value:
                sub_name = str(sub.Name.Value)
                if sub_name in ('A', 'B', 'C', 'D'):
                    sub.Value = random.randint(-2147483648, 2147483647)
    
    # Add to asset
    asset.Exports.Add(new_exp)
    print(f'Added new export at index {asset.Exports.Count - 1} (1-based: {new_export_1based})')
    
    # ---------------------------------------------------------------
    # Step 2: Add to EventGraph's Nodes array
    # ---------------------------------------------------------------
    event_graph = asset.Exports[3]
    for prop in event_graph.Data:
        if str(prop.Name.Value) == 'Nodes':
            # C# array - need to create a new larger array
            old_arr = prop.Value
            old_len = len(old_arr)
            new_arr = Array.CreateInstance(PropertyData, old_len + 1)
            for j in range(old_len):
                new_arr[j] = old_arr[j]
            new_ref = ObjectPropertyData(FName(asset, "Nodes"))
            new_ref.Value = FPackageIndex(new_export_1based)
            new_arr[old_len] = new_ref
            prop.Value = new_arr
            print(f'EventGraph Nodes: {len(new_arr)} entries')
            break
    
    # ---------------------------------------------------------------
    # Step 3: Wire PrintString1 'then' -> PrintString2 'execute'
    # ---------------------------------------------------------------
    # PrintString1 is export[6] (1-based: 7)
    # PrintString2 is export[21] (1-based: 22)
    # PrintString1's pin 1 = 'then' (output exec)
    # PrintString2's pin 0 = 'execute' (input exec)
    
    # Add LinkedTo on PrintString1's 'then' pin -> PrintString2's 'execute' pin
    ps1_extras = bytes(original_exp.Extras)
    ps1_extras = add_linked_to_entry(
        ps1_extras, name_map,
        pin_index=1,  # 'then' pin
        target_owning_node=new_export_1based,  # 22
        target_pin_guid=new_pin_guids[0]  # execute pin of new node
    )
    original_exp.Extras = Array[Byte](ps1_extras)
    print(f'Wired PrintString1 then -> PrintString2 execute')
    
    # Add LinkedTo on PrintString2's 'execute' pin -> PrintString1's 'then' pin
    # First, get PrintString1's 'then' pin GUID from the original extras
    orig_parser = ExtrasParser(original_extras, name_map)
    orig_parsed = orig_parser.parse()
    # Pin 1 is 'then' - get its wrapper PinGuid
    ps1_then_guid_hex = orig_parsed['pins'][1]['_wrapper_pin_guid']
    ps1_then_guid_bytes = bytes.fromhex(ps1_then_guid_hex)
    
    ps2_extras = add_linked_to_entry(
        new_extras, name_map,
        pin_index=0,  # 'execute' pin
        target_owning_node=7,  # PrintString1 (1-based)
        target_pin_guid=ps1_then_guid_bytes
    )
    new_exp.Extras = Array[Byte](ps2_extras)
    print(f'Wired PrintString2 execute -> PrintString1 then')
    
    # ---------------------------------------------------------------
    # Step 4: Save
    # ---------------------------------------------------------------
    asset.Write(dst_path)
    print(f'Saved to {dst_path}')
    
    # ---------------------------------------------------------------
    # Step 5: Post-save patch (SSEO + trailer)
    # ---------------------------------------------------------------
    patch_uasset_post_save(dst_path)
    print('Post-save patch applied')
    
    # ---------------------------------------------------------------
    # Step 6: Verify
    # ---------------------------------------------------------------
    verify = AssetFile(dst_path)
    vname_map = [str(n) for n in verify.asset.GetNameMapIndexList()]
    print(f'\nVerification: {len(verify.asset.Exports)} exports')
    
    for idx in [6, 21]:
        exp = verify.asset.Exports[idx]
        extras = bytes(exp.Extras)
        parser = ExtrasParser(extras, vname_map)
        parsed = parser.parse()
        
        cls = str(exp.GetExportClassType().Value)
        name = str(exp.ObjectName.Value)
        print(f'\n  [{idx}] {cls}::{name} SerialSize={exp.SerialSize} Extras={len(extras)}')
        
        if parsed['pins']:
            for pi, p in enumerate(parsed['pins']):
                if p is None:
                    print(f'    Pin {pi}: NULL')
                    continue
                pn_idx = p.get('PinName', '?')
                if isinstance(pn_idx, tuple):
                    pn = vname_map[pn_idx[0]] if pn_idx[0] < len(vname_map) else str(pn_idx)
                else:
                    pn = str(pn_idx)
                linked = p.get('LinkedTo', [])
                owning = p.get('OwningNode', '?')
                if pi < 2:  # Only show first 2 pins (execute, then)
                    print(f'    Pin {pi}: {pn} OwningNode={owning} LinkedTo={len(linked)}')
                    for lt in linked:
                        print(f'      -> OwningNode={lt["OwningNode"]} PinGuid={lt["PinGuid"]}')


if __name__ == '__main__':
    main()
