#!/usr/bin/env python3
"""
Test the high-level write_extras() method against known-good K2Nodes.
This validates the abstraction layer produces byte-perfect output.
"""
import sys
import struct
import io

sys.path.insert(0, '.')
from core.uasset_bridge import AssetFile
from core.k2node_writer import ExtrasWriter
from parse_extras import ExtrasParser


def build_name_map(af):
    """Build the name map list from the asset."""
    name_map = []
    for i in range(af.asset.GetNameMapIndexList().Count):
        name_map.append(str(af.asset.GetNameMapIndexList()[i]))
    return name_map


def parse_user_defined_pins(remaining_bytes, name_map):
    """Parse TArray<FUserPinInfo> from remaining bytes after pin array."""
    stream = io.BytesIO(remaining_bytes)
    count = struct.unpack('<i', stream.read(4))[0]
    user_pins = []

    for _ in range(count):
        # FName PinName
        idx = struct.unpack('<i', stream.read(4))[0]
        num = struct.unpack('<i', stream.read(4))[0]
        up_name = name_map[idx] if 0 <= idx < len(name_map) else str(idx)

        # FEdGraphPinType fields
        cat_idx = struct.unpack('<i', stream.read(4))[0]
        cat_num = struct.unpack('<i', stream.read(4))[0]
        cat = name_map[cat_idx] if 0 <= cat_idx < len(name_map) else ''

        subcat_idx = struct.unpack('<i', stream.read(4))[0]
        subcat_num = struct.unpack('<i', stream.read(4))[0]
        subcat = name_map[subcat_idx] if 0 <= subcat_idx < len(name_map) else ''

        subcat_obj = struct.unpack('<i', stream.read(4))[0]
        container = struct.unpack('<B', stream.read(1))[0]
        is_ref = struct.unpack('<i', stream.read(4))[0]
        is_weak = struct.unpack('<i', stream.read(4))[0]
        member_parent = struct.unpack('<i', stream.read(4))[0]
        mn_idx = struct.unpack('<i', stream.read(4))[0]
        mn_num = struct.unpack('<i', stream.read(4))[0]
        mn = name_map[mn_idx] if 0 <= mn_idx < len(name_map) else 'None'
        mg = stream.read(16).hex().upper().ljust(32, '0')
        is_const = struct.unpack('<i', stream.read(4))[0]
        is_uobj = struct.unpack('<i', stream.read(4))[0]
        is_sp = struct.unpack('<i', stream.read(4))[0]

        direction_byte = struct.unpack('<B', stream.read(1))[0]

        # DefaultValue FString
        dv_len = struct.unpack('<i', stream.read(4))[0]
        dv = ''
        if dv_len > 0:
            dv = stream.read(dv_len).decode('utf-8', errors='replace').rstrip('\0')
        elif dv_len < 0:
            dv = stream.read(-dv_len * 2).decode('utf-16-le').rstrip('\0')

        user_pins.append({
            'PinName': up_name,
            'PinType': {
                'PinCategory': cat,
                'PinSubCategory': subcat,
                'PinSubCategoryObject': subcat_obj,
                'ContainerType': container,
                'bIsReference': bool(is_ref),
                'bIsWeakPointer': bool(is_weak),
                'MemberParent': member_parent,
                'MemberName': mn,
                'MemberGuid': mg,
                'bIsConst': bool(is_const),
                'bIsUObjectWrapper': bool(is_uobj),
                'bSerializeAsSinglePrecisionFloat': bool(is_sp),
            },
            'Direction': 'EGPD_Output' if direction_byte == 1 else 'EGPD_Input',
            'DefaultValue': dv,
        })

    return user_pins


def test_node(af, name_map, export_idx, description):
    """Parse a K2Node's Extras, reconstruct via write_extras(), compare."""
    exp = af.get_export(export_idx)
    cname = af.get_export_class_name(exp)
    oname = af.get_export_name(exp)
    original = bytes(exp.Extras)

    if len(original) == 0:
        print(f"  SKIP {description}: {oname} ({cname}) - 0 bytes Extras")
        return None

    # Parse the original
    parser = ExtrasParser(original, name_map, af)
    parsed = parser.parse()

    # Build the node dict from parsed data
    node = {
        'node_class': cname,
        'pins': parsed['pins'],
        'user_defined_pins': [],
    }

    # For CustomEvent/Event, parse the UserDefinedPins from remaining bytes
    if cname in ('K2Node_CustomEvent', 'K2Node_Event') and parsed['bytes_remaining'] > 0:
        remaining = original[parsed['bytes_consumed']:]
        node['user_defined_pins'] = parse_user_defined_pins(remaining, name_map)

    # Reconstruct using the high-level writer
    writer = ExtrasWriter(name_map)
    reconstructed = writer.write_extras(node, export_idx)

    if original == reconstructed:
        print(f"  PASS {description}: {oname} ({cname}) - {len(original)}b")
        return True
    else:
        min_len = min(len(original), len(reconstructed))
        diff_count = sum(1 for i in range(min_len) if original[i] != reconstructed[i])
        diff_count += abs(len(original) - len(reconstructed))

        for i in range(min_len):
            if original[i] != reconstructed[i]:
                print(f"  FAIL {description}: {oname} ({cname}) - "
                      f"{len(original)}b vs {len(reconstructed)}b, "
                      f"{diff_count} bytes differ, first at {i}")
                print(f"    orig[{i}:]: {original[i:i+16].hex(' ')}")
                print(f"    recon[{i}:]: {reconstructed[i:i+16].hex(' ')}")
                return False

        print(f"  FAIL {description}: size mismatch {len(original)} vs {len(reconstructed)}")
        return False


def main():
    af = AssetFile("/home/ubuntu/test_assets/SL_Trailer_Logic.umap", "5.6")
    name_map = build_name_map(af)

    # Test a variety of node types
    tests = [
        (72, "K2Node_GetSubsystem (pins only)"),
        (77, "K2Node_IfThenElse (4 pins)"),
        (85, "K2Node_Literal (1 pin)"),
        (98, "K2Node_MultiGate (7 pins)"),
        (100, "K2Node_PromotableOperator (4 pins)"),
        (102, "K2Node_VariableGet (2 pins)"),
        (93, "K2Node_MakeStruct (2 pins)"),
        (88, "K2Node_LoadAsset (5 pins)"),
        (79, "K2Node_Knot (2 pins)"),
        (67, "K2Node_CustomEvent (3 pins + UserPins)"),
        (69, "K2Node_CustomEvent (2 pins + empty UserPins)"),
        (71, "K2Node_Event (2 pins + empty UserPins)"),
    ]

    print("Testing high-level write_extras() against known-good K2Nodes:")
    print("=" * 70)
    passed = 0
    failed = 0
    for idx, desc in tests:
        result = test_node(af, name_map, idx, desc)
        if result is True:
            passed += 1
        elif result is False:
            failed += 1

    print(f"\n{'=' * 70}")
    print(f"Results: {passed} passed, {failed} failed out of {len(tests)} tests")

    # Also test ALL K2Nodes in the file (not just the curated list)
    print(f"\n\nFull sweep of ALL K2Nodes in SL_Trailer_Logic.umap:")
    print("=" * 70)
    all_passed = 0
    all_failed = 0
    all_skipped = 0
    for i in range(af.export_count):
        exp = af.get_export(i)
        cname = af.get_export_class_name(exp)
        if 'K2Node' not in cname:
            continue
        extras = bytes(exp.Extras) if exp.Extras else b''
        if len(extras) == 0:
            all_skipped += 1
            continue

        # Skip DynamicCast (has PureState byte we handle separately)
        # and MacroInstance (has extra 4 bytes we haven't mapped yet)
        if cname in ('K2Node_DynamicCast', 'K2Node_MacroInstance'):
            all_skipped += 1
            continue

        result = test_node(af, name_map, i, f"Export[{i}]")
        if result is True:
            all_passed += 1
        elif result is False:
            all_failed += 1

    print(f"\n{'=' * 70}")
    print(f"Full sweep: {all_passed} passed, {all_failed} failed, "
          f"{all_skipped} skipped")


if __name__ == '__main__':
    main()
