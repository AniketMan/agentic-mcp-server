"""
inject_fun_level_v2.py -- Modify MoreLogicTests.umap to prove binary editing works.

CRITICAL FIX from v1:
  - DO NOT add or remove tagged properties. This changes the serialized property
    size, but UAssetAPI does not recalculate ScriptSerializationEndOffset, causing
    the engine to assert on load.
  - ONLY modify Extras blobs (pin wiring, default values) and change existing
    property VALUES in-place.

What this does:
  1. Change BeginPlay EnabledState from Disabled to Enabled (value change, not removal)
  2. Wire BeginPlay.then -> PrintString.execute (Extras blob modification)
  3. Wire PrintString.execute <- BeginPlay.then (Extras blob back-reference)
  4. Change PrintString message text (Extras blob DefaultValue)
  5. Change PrintString text color to green (Extras blob DefaultValue)
  6. Change PrintString EnabledState from DevelopmentOnly to Enabled (value change)
"""

import sys
import os
import shutil
import struct
import io

sys.path.insert(0, '/home/ubuntu/ue56-level-editor')

from core.uasset_bridge import (
    AssetFile, NormalExport, FName, FPackageIndex, FString,
    NetList, PropertyData, StructPropertyData, IntPropertyData,
    NamePropertyData, ObjectPropertyData, BoolPropertyData,
    StrPropertyData, GuidPropertyData, TextPropertyData,
    ArrayPropertyData, BytePropertyData, EnumPropertyData,
    _make_ptn, _make_ptn_struct,
    System,
)
from parse_extras import ExtrasParser


def build_name_map(af):
    """Build a Python list of FName strings from the asset's name map."""
    name_map = []
    for i in range(af.asset.GetNameMapIndexList().Count):
        name_map.append(str(af.asset.GetNameMapIndexList()[i]))
    return name_map


def set_extras_bytes(export, data: bytes):
    """Replace an export's Extras blob with new raw bytes."""
    from System import Array, Byte
    arr = Array.CreateInstance(Byte, len(data))
    for i, b in enumerate(data):
        arr[i] = b
    export.Extras = arr


def rebuild_extras_with_modifications(original_extras, name_map, modifications):
    """
    Rebuild an Extras blob with targeted modifications.
    
    This is a byte-perfect reconstruction of the pin array with optional changes:
      - linked_to_add: {pin_id_hex: [(target_node_pkg_idx, target_pin_guid_hex), ...]}
      - default_value_change: {pin_name_idx: new_default_value_string}
    
    All other fields are preserved exactly as-is.
    """
    parser = ExtrasParser(original_extras, name_map)
    parsed = parser.parse()
    
    linked_to_add = modifications.get('linked_to_add', {})
    default_value_change = modifications.get('default_value_change', {})
    
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
    
    # Write pin count
    write_int32(parsed['num_pins'])
    
    for pin in parsed['pins']:
        if pin is None:
            write_int32(1)  # bNullPtr
            continue
        
        # SerializePin wrapper
        write_int32(0)  # bNullPtr = false
        write_int32(pin['_wrapper_owning_node'])
        write_guid(pin['_wrapper_pin_guid'])
        
        # Pin.Serialize()
        write_int32(pin['OwningNode'])
        write_guid(pin['PinId'])
        
        # PinName
        write_int32(pin['PinName'][0])
        write_int32(pin['PinName'][1])
        
        # PinFriendlyName
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
        write_int32(pt['PinSubCategoryObject'])
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
        write_int32(pt['MemberParent'])
        write_int32(pt['MemberName'][0])
        write_int32(pt['MemberName'][1])
        write_guid(pt['MemberGuid'])
        write_int32(1 if pt['bIsConst'] else 0)
        write_int32(1 if pt['bIsUObjectWrapper'] else 0)
        write_int32(1 if pt['bSerializeAsSinglePrecisionFloat'] else 0)
        
        # DefaultValue -- possibly modified
        pin_name_idx = pin['PinName'][0]
        if pin_name_idx in default_value_change:
            write_fstring(default_value_change[pin_name_idx])
        else:
            write_fstring(pin['DefaultValue'])
        
        # AutogeneratedDefaultValue
        write_fstring(pin['AutogeneratedDefaultValue'])
        
        # DefaultObject
        write_int32(pin['DefaultObject'])
        
        # DefaultTextValue
        write_ftext(pin['DefaultTextValue'])
        
        # LinkedTo -- possibly with additions
        pin_id = pin['PinId']
        existing_linked = pin['LinkedTo']
        added_links = linked_to_add.get(pin_id, [])
        total_linked = existing_linked + [
            {'_is_null': False, 'OwningNode': node_pkg, 'PinGuid': pin_guid}
            for node_pkg, pin_guid in added_links
        ]
        write_int32(len(total_linked))
        for lt in total_linked:
            if lt.get('_is_null', False):
                write_int32(1)
            else:
                write_int32(0)
                write_int32(lt['OwningNode'])
                write_guid(lt['PinGuid'])
        
        # SubPins
        write_int32(len(pin['SubPins']))
        for sp in pin['SubPins']:
            if sp.get('_is_null', False):
                write_int32(1)
            else:
                write_int32(0)
                write_int32(sp['OwningNode'])
                write_guid(sp['PinGuid'])
        
        # ParentPin
        pp = pin['ParentPin']
        if pp.get('_is_null', False):
            write_int32(1)
        else:
            write_int32(0)
            write_int32(pp['OwningNode'])
            write_guid(pp['PinGuid'])
        
        # RefPassThrough
        rpt = pin['RefPassThrough']
        if rpt.get('_is_null', False):
            write_int32(1)
        else:
            write_int32(0)
            write_int32(rpt['OwningNode'])
            write_guid(rpt['PinGuid'])
        
        # PersistentGuid
        write_guid(pin['PersistentGuid'])
        
        # BitField
        write_uint32(pin['BitField'])
    
    # Append remaining bytes (UserDefinedPins etc.)
    remaining = original_extras[parsed['bytes_consumed']:]
    w.write(remaining)
    
    return w.getvalue()


def fix_trailer_offset(filepath):
    """
    Post-save fixup: UAssetAPI doesn't recalculate the PayloadTocOffset
    (trailer offset) in the package summary when export data changes size.
    This function finds the actual trailer magic position and patches the
    stored offset to match.
    
    The trailer offset is stored as int64 at byte offset 610 in the package summary.
    The trailer is identified by the magic value 0x9E2A83C1 near the end of the file.
    """
    with open(filepath, 'rb') as f:
        data = bytearray(f.read())
    
    file_size = len(data)
    magic = struct.pack('<I', 0x9E2A83C1)
    
    # Find the FIRST trailer magic (not the end-of-file one)
    # The trailer has magic at start and end. We want the start.
    # Search backwards from the end to find both, then take the earlier one.
    positions = []
    pos = 0
    while True:
        idx = data.find(magic, pos)
        if idx == -1 or idx == 0:  # skip the package header magic at offset 0
            break
        positions.append(idx)
        pos = idx + 1
    
    if len(positions) < 2:
        print(f"WARNING: Could not find trailer magic pair. Found {len(positions)} positions: {positions}")
        return
    
    # The trailer start is the second-to-last magic occurrence
    # (last is the end-of-trailer marker)
    trailer_start = positions[-2]
    
    # Read current stored offset
    current_offset = struct.unpack_from('<q', data, 610)[0]
    
    if current_offset != trailer_start:
        print(f"  Fixing trailer offset: {current_offset} -> {trailer_start}")
        struct.pack_into('<q', data, 610, trailer_start)
        with open(filepath, 'wb') as f:
            f.write(data)
        print(f"  Trailer offset patched. File size: {file_size}")
    else:
        print(f"  Trailer offset already correct: {trailer_start}")


def main():
    # Start from the ORIGINAL unmodified file
    src = '/tmp/MoreLogicTests_original.umap'
    dst = '/tmp/MoreLogicTests_v2.umap'
    shutil.copy2(src, dst)
    os.chmod(dst, 0o644)
    print(f"Copied original {src} -> {dst}")
    
    af = AssetFile(dst, '5.6')
    name_map = build_name_map(af)
    print(f"Loaded: {af.export_count} exports, {len(name_map)} names")
    print()
    
    # --- Current structure ---
    # [6] K2Node_CallFunction/K2Node_CallFunction_0 -- PrintString
    #     Props: FunctionReference, NodePosX, NodePosY, AdvancedPinDisplay, EnabledState=DevelopmentOnly, NodeGuid
    #     execute pin: id=B9FA15BB45CBBA16852ED8B2789B3699, owning=7 (pkg_idx)
    #     InString pin: default="Hello JARVIS THIS IS YOUR LEVEL"
    #     TextColor pin: default="(R=0.000000,G=0.660000,B=1.000000,A=1.000000)"
    #
    # [7] K2Node_Event/K2Node_Event_0 -- BeginPlay (DISABLED)
    #     Props: EventReference, bOverrideFunction, EnabledState=Disabled, bCommentBubblePinned,
    #            bCommentBubbleVisible, NodeComment, NodeGuid
    #     then pin: id=6ACB58F243F8641D69539C82D1AC6AB6, owning=8 (pkg_idx)
    #
    # [8] K2Node_Event/K2Node_Event_1 -- Tick (DISABLED)
    
    # Note: OwningNode values in Extras are 1-based FPackageIndex
    # Export[6] -> pkg_idx = 7
    # Export[7] -> pkg_idx = 8
    # Export[8] -> pkg_idx = 9
    
    # =========================================================================
    # Step 1: Change BeginPlay EnabledState from Disabled to Enabled
    # =========================================================================
    # CRITICAL: Do NOT remove the property. Change its VALUE in-place.
    # Removing properties changes serialized size but UAssetAPI doesn't update
    # ScriptSerializationEndOffset, causing engine assert on load.
    # =========================================================================
    print("Step 1: Changing BeginPlay EnabledState from Disabled to Enabled...")
    begin_play = af.get_export(7)
    
    for i in range(begin_play.Data.Count):
        prop = begin_play.Data[i]
        pname = str(prop.Name)
        if pname == 'EnabledState':
            # EnumPropertyData -- change value from Disabled to Enabled
            # In UE, the default EnabledState is "Enabled" which means the property
            # wouldn't normally exist. But we can't remove it (SSEO issue).
            # Instead, set it to the "Enabled" enum value.
            old_val = str(prop.Value)
            prop.Value = FName.FromString(af.asset, "ENodeEnabledState::Enabled")
            print(f"  EnabledState: {old_val} -> ENodeEnabledState::Enabled")
        elif pname == 'bCommentBubblePinned':
            prop.Value = False
            print(f"  bCommentBubblePinned: True -> False")
        elif pname == 'bCommentBubbleVisible':
            prop.Value = False
            print(f"  bCommentBubbleVisible: True -> False")
        elif pname == 'NodeComment':
            # Change the comment to something fun
            prop.Value = FString("Enabled by JARVIS binary injection")
            print(f"  NodeComment: changed")
    print()
    
    # =========================================================================
    # Step 2: Wire BeginPlay.then -> PrintString.execute (Extras modification)
    # =========================================================================
    print("Step 2: Wiring BeginPlay.then -> PrintString.execute...")
    
    bp_extras = bytes(begin_play.Extras) if begin_play.Extras else b''
    bp_mods = {
        'linked_to_add': {
            '6ACB58F243F8641D69539C82D1AC6AB6': [  # BeginPlay.then pin
                (7, 'B9FA15BB45CBBA16852ED8B2789B3699'),  # -> PrintString.execute (pkg_idx=7)
            ]
        }
    }
    new_bp_extras = rebuild_extras_with_modifications(bp_extras, name_map, bp_mods)
    set_extras_bytes(begin_play, new_bp_extras)
    print(f"  BeginPlay extras: {len(bp_extras)}B -> {len(new_bp_extras)}B")
    print()
    
    # =========================================================================
    # Step 3: Wire PrintString.execute <- BeginPlay.then (back-reference)
    #         + Change message + Change color
    # =========================================================================
    print("Step 3: Wiring PrintString + changing message + color...")
    
    instring_idx = name_map.index('InString')
    textcolor_idx = name_map.index('TextColor')
    
    print_string = af.get_export(6)
    ps_extras = bytes(print_string.Extras) if print_string.Extras else b''
    ps_mods = {
        'linked_to_add': {
            'B9FA15BB45CBBA16852ED8B2789B3699': [  # PrintString.execute pin
                (8, '6ACB58F243F8641D69539C82D1AC6AB6'),  # <- BeginPlay.then (pkg_idx=8)
            ]
        },
        'default_value_change': {
            instring_idx: "LEVEL EDITED BY BINARY INJECTION -- NO EDITOR NEEDED",
            textcolor_idx: "(R=0.000000,G=1.000000,B=0.000000,A=1.000000)",  # Bright green
        }
    }
    new_ps_extras = rebuild_extras_with_modifications(ps_extras, name_map, ps_mods)
    set_extras_bytes(print_string, new_ps_extras)
    print(f"  PrintString extras: {len(ps_extras)}B -> {len(new_ps_extras)}B")
    print()
    
    # =========================================================================
    # Step 4: Change PrintString EnabledState (value change, NOT removal)
    # =========================================================================
    print("Step 4: Changing PrintString EnabledState to Enabled...")
    for i in range(print_string.Data.Count):
        prop = print_string.Data[i]
        pname = str(prop.Name)
        if pname == 'EnabledState':
            old_val = str(prop.Value)
            prop.Value = FName.FromString(af.asset, "ENodeEnabledState::Enabled")
            print(f"  EnabledState: {old_val} -> ENodeEnabledState::Enabled")
    print()
    
    # =========================================================================
    # Step 5: Save
    # =========================================================================
    print("Saving...")
    output_path = '/tmp/MoreLogicTests_v2_out.umap'
    saved = af.save(output_path=output_path, create_backup=False, validate_first=False)
    print(f"Saved to: {saved}")
    print(f"File size: {os.path.getsize(str(saved))} bytes")
    print()
    
    # =========================================================================
    # Step 6: Fix trailer offset (UAssetAPI bug workaround)
    # =========================================================================
    print("Fixing trailer offset...")
    fix_trailer_offset(str(saved))
    print()
    
    # =========================================================================
    # Step 7: Verify round-trip
    # =========================================================================
    print("Verifying round-trip...")
    af2 = AssetFile(str(saved), '5.6')
    name_map2 = build_name_map(af2)
    
    for idx in [6, 7, 8]:
        exp = af2.get_export(idx)
        cname = af2.get_export_class_name(exp)
        oname = af2.get_export_name(exp)
        extras = bytes(exp.Extras) if exp.Extras else b''
        sseo = exp.ScriptSerializationEndOffset
        serial_size = exp.SerialSize
        print(f"  [{idx}] {cname}/{oname}  SSEO={sseo} SerialSize={serial_size} Extras={len(extras)}B")
        
        parser = ExtrasParser(extras, name_map2)
        parsed = parser.parse()
        for pin in parsed['pins']:
            if pin:
                pn = name_map2[pin['PinName'][0]] if 0 <= pin['PinName'][0] < len(name_map2) else '?'
                d = 'OUT' if pin['Direction'] == 1 else 'IN'
                linked_count = len([l for l in pin['LinkedTo'] if not l.get('_is_null', False)])
                defval = pin.get('DefaultValue', '')
                line = f"    {pn} ({d}) linked={linked_count}"
                if defval and defval != 'None':
                    line += f' default="{defval[:60]}"'
                print(line)
    
    # Check properties preserved
    print()
    print("Property check:")
    for idx in [6, 7]:
        exp = af2.get_export(idx)
        cname = af2.get_export_class_name(exp)
        oname = af2.get_export_name(exp)
        props = af2.get_export_properties(exp)
        print(f"  [{idx}] {cname}/{oname}: {len(props)} properties")
        for p in props:
            print(f"    {p['name']} = {p.get('value', '?')}")
    
    print()
    print("INJECTION COMPLETE!")
    print(f"Output: {saved}")
    print()
    print("Changes made (NO properties added or removed):")
    print("  - BeginPlay EnabledState: Disabled -> Enabled")
    print("  - BeginPlay.then -> PrintString.execute: WIRED")
    print("  - PrintString.execute <- BeginPlay.then: WIRED (back-ref)")
    print('  - Message: "LEVEL EDITED BY BINARY INJECTION -- NO EDITOR NEEDED"')
    print("  - Text color: Bright GREEN (was cyan)")
    print("  - PrintString EnabledState: DevelopmentOnly -> Enabled")


if __name__ == '__main__':
    main()
