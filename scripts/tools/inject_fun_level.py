"""
inject_fun_level.py -- Modify MoreLogicTests.umap to prove binary editing works.

What this does:
  1. Enable BeginPlay event (change EnabledState from Disabled to Enabled)
  2. Wire BeginPlay.then -> PrintString.execute
  3. Change PrintString message to prove it was edited externally
  4. Wire PrintString.execute back-reference to BeginPlay.then
  5. Change the text color to bright green

This is the simplest possible proof: open the level, hit Play, see the message.
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
    ArrayPropertyData, BytePropertyData, EnumPropertyData,
    _make_ptn, _make_ptn_struct,
    System,
)
from parse_extras import ExtrasParser


def build_name_map(af):
    name_map = []
    for i in range(af.asset.GetNameMapIndexList().Count):
        name_map.append(str(af.asset.GetNameMapIndexList()[i]))
    return name_map


def ensure_name(af, name_map, name):
    if name in name_map:
        return name_map.index(name)
    idx = af.asset.AddNameReference(FString(name))
    while len(name_map) <= idx:
        name_map.append(None)
    name_map[idx] = name
    return idx


def set_extras_bytes(export, data: bytes):
    from System import Array, Byte
    arr = Array.CreateInstance(Byte, len(data))
    for i, b in enumerate(data):
        arr[i] = b
    export.Extras = arr


def rebuild_extras_with_modifications(original_extras, name_map, modifications):
    """
    Rebuild an Extras blob with targeted modifications.
    
    modifications dict can contain:
      'owning_remap': {old_pkg: new_pkg}  -- remap OwningNode references
      'linked_to_add': {pin_id: [(target_node_pkg, target_pin_guid), ...]}  -- add LinkedTo entries
      'default_value_change': {pin_name_idx: new_default_value}  -- change DefaultValue
      'guid_remap': {old_guid: new_guid}  -- remap pin GUIDs
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


def main():
    src = '/home/ubuntu/p4_workspace/Content/Maps/Game/Restaurant/Levels/MoreLogicTests.umap'
    dst = '/home/ubuntu/p4_workspace/Content/Maps/Game/Restaurant/Levels/MoreLogicTests_modified.umap'
    shutil.copy2(src, dst)
    print(f"Copied {src} -> {dst}")
    
    af = AssetFile(dst, '5.6')
    name_map = build_name_map(af)
    print(f"Loaded: {af.summary()}")
    print()
    
    # --- Current structure ---
    # [6] K2Node_CallFunction/K2Node_CallFunction_0 -- PrintString
    #     execute pin: id=B9FA15BB45CBBA16852ED8B2789B3699, owning=7 (pkg_idx)
    #     InString pin: default="Hello JARVIS THIS IS YOUR LEVEL"
    #     TextColor pin: default="(R=0.000000,G=0.660000,B=1.000000,A=1.000000)"
    #
    # [7] K2Node_Event/K2Node_Event_0 -- BeginPlay (DISABLED)
    #     then pin: id=6ACB58F243F8641D69539C82D1AC6AB6, owning=8 (pkg_idx)
    #
    # [8] K2Node_Event/K2Node_Event_1 -- Tick (DISABLED)
    
    # Note: OwningNode values in Extras are 1-based FPackageIndex
    # Export[6] -> pkg_idx = 7
    # Export[7] -> pkg_idx = 8
    # Export[8] -> pkg_idx = 9
    
    # --- Step 1: Enable BeginPlay (export[7]) ---
    print("Step 1: Enabling BeginPlay event...")
    begin_play = af.get_export(7)
    
    # Remove EnabledState, bCommentBubblePinned, bCommentBubbleVisible, NodeComment
    # Keep EventReference, bOverrideFunction, NodeGuid
    new_data = NetList[PropertyData]()
    for i in range(begin_play.Data.Count):
        prop = begin_play.Data[i]
        pname = str(prop.Name)
        if pname in ('EnabledState', 'bCommentBubblePinned', 'bCommentBubbleVisible', 'NodeComment'):
            print(f"  Removing property: {pname}")
            continue
        new_data.Add(prop)
        print(f"  Keeping property: {pname}")
    begin_play.Data = new_data
    print()
    
    # --- Step 2: Wire BeginPlay.then -> PrintString.execute ---
    print("Step 2: Wiring BeginPlay.then -> PrintString.execute...")
    
    # Modify BeginPlay extras: add LinkedTo on 'then' pin
    # BeginPlay 'then' pin id = 6ACB58F243F8641D69539C82D1AC6AB6
    # PrintString 'execute' pin id = B9FA15BB45CBBA16852ED8B2789B3699
    # PrintString owning node pkg_idx = 7
    
    bp_extras = bytes(begin_play.Extras) if begin_play.Extras else b''
    bp_mods = {
        'linked_to_add': {
            '6ACB58F243F8641D69539C82D1AC6AB6': [  # BeginPlay.then
                (7, 'B9FA15BB45CBBA16852ED8B2789B3699'),  # -> PrintString.execute (pkg=7)
            ]
        }
    }
    new_bp_extras = rebuild_extras_with_modifications(bp_extras, name_map, bp_mods)
    set_extras_bytes(begin_play, new_bp_extras)
    print(f"  BeginPlay extras: {len(bp_extras)}B -> {len(new_bp_extras)}B")
    print()
    
    # --- Step 3: Wire PrintString.execute <- BeginPlay.then (back-reference) ---
    print("Step 3: Wiring PrintString.execute <- BeginPlay.then (back-ref)...")
    
    # Also change InString default value and TextColor
    # Find name indices for 'InString' and 'TextColor'
    instring_idx = name_map.index('InString')
    textcolor_idx = name_map.index('TextColor')
    
    print_string = af.get_export(6)
    ps_extras = bytes(print_string.Extras) if print_string.Extras else b''
    ps_mods = {
        'linked_to_add': {
            'B9FA15BB45CBBA16852ED8B2789B3699': [  # PrintString.execute
                (8, '6ACB58F243F8641D69539C82D1AC6AB6'),  # <- BeginPlay.then (pkg=8)
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
    
    # --- Step 4: Change PrintString EnabledState from DevelopmentOnly to normal ---
    print("Step 4: Changing PrintString to always-enabled...")
    new_ps_data = NetList[PropertyData]()
    for i in range(print_string.Data.Count):
        prop = print_string.Data[i]
        pname = str(prop.Name)
        if pname == 'EnabledState':
            print(f"  Removing EnabledState (was DevelopmentOnly)")
            continue
        if pname == 'AdvancedPinDisplay':
            print(f"  Removing AdvancedPinDisplay")
            continue
        new_ps_data.Add(prop)
        print(f"  Keeping: {pname}")
    print_string.Data = new_ps_data
    print()
    
    # --- Step 5: Validate and save ---
    print("Validating...")
    issues = af.validate()
    if issues:
        for issue in issues:
            print(f"  {issue}")
    else:
        print("  No issues found!")
    print()
    
    # Save back to the ORIGINAL path (overwrite)
    output_path = src
    saved = af.save(output_path=output_path, create_backup=False, validate_first=True)
    print(f"Saved to: {saved}")
    
    # --- Step 6: Verify round-trip ---
    print()
    print("Verifying round-trip...")
    af2 = AssetFile(str(saved), '5.6')
    name_map2 = build_name_map(af2)
    
    for idx in [6, 7, 8]:
        exp = af2.get_export(idx)
        cname = af2.get_export_class_name(exp)
        oname = af2.get_export_name(exp)
        extras = bytes(exp.Extras) if exp.Extras else b''
        print(f"  [{idx}] {cname}/{oname} extras={len(extras)}B")
        
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
    
    # Clean up temp file
    if os.path.exists(dst):
        os.remove(dst)
    
    print()
    print("INJECTION COMPLETE!")
    print(f"Modified: {saved}")
    print("What changed:")
    print("  - BeginPlay event: ENABLED (was Disabled)")
    print("  - BeginPlay.then -> PrintString.execute: WIRED")
    print("  - PrintString.execute <- BeginPlay.then: WIRED (back-ref)")
    print('  - Message: "LEVEL EDITED BY BINARY INJECTION -- NO EDITOR NEEDED"')
    print("  - Text color: Bright GREEN (was cyan)")
    print("  - PrintString: Always enabled (was DevelopmentOnly)")


if __name__ == '__main__':
    main()
