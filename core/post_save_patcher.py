"""
post_save_patcher.py -- Binary post-save fixup for UAssetAPI output files.

UAssetAPI has two bugs when writing modified .umap/.uasset files:

  1. ScriptSerializationEndOffset (SSEO) is NOT recalculated when tagged
     property sizes change. SSEO marks where tagged properties end and
     Extras begin within an export's serial data. If you change a string
     property value to a different length, the tagged section shrinks/grows,
     but SSEO stays at the old value. The engine asserts on load.

  2. PayloadTocOffset (trailer offset) is NOT recalculated when total
     export data size changes. The trailer magic (0x9E2A83C1) moves to
     a new position, but the header still points to the old one. The
     engine seeks past EOF.

This module fixes both by:
  - Reading the export table from the raw binary (stride=112, known field offsets)
  - Recalculating SSEO for each export as: SerialSize - ExtrasSize - 4
  - Finding the actual trailer magic position and patching the header

IMPORTANT: This patcher depends on the export table binary layout which is
stable across UE 5.4-5.6. If Epic changes FObjectExport serialization,
this will need updating.

Usage:
    from core.post_save_patcher import patch_uasset_post_save
    patch_uasset_post_save(filepath, engine_version='5.6')
"""

import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _find_export_table_start(data, export_count, af):
    """
    Find the export table start offset by locating the first export's
    known SerialSize/SerialOffset pair in the binary.
    
    The export table has a fixed stride of 112 bytes per entry.
    Within each entry:
      +24: SerialSize (int64)
      +32: SerialOffset (int64)
      +100: ScriptSerializationEndOffset (int64)
    
    We find the table by searching for export[0]'s SerialSize+SerialOffset
    pair, then derive the table start.
    """
    exp0 = af.get_export(0)
    ss0 = exp0.SerialSize
    so0 = exp0.SerialOffset
    
    # Pack the expected pattern: SerialSize(int64) + SerialOffset(int64) at +24,+32
    target_ss = struct.pack('<q', ss0)
    
    pos = 0
    while pos < len(data) // 2:  # Only search first half (export table is before data)
        idx = data.find(target_ss, pos)
        if idx == -1:
            break
        # Check if SerialOffset follows at +8
        so_at = struct.unpack_from('<q', data, idx + 8)[0]
        if so_at == so0:
            # This is the SerialSize field of export[0]
            # SerialSize is at field offset +24 within the entry
            entry_start = idx - 24
            
            # Verify with export[1] if it exists
            if export_count > 1:
                exp1 = af.get_export(1)
                entry1_start = entry_start + 112
                ss1 = struct.unpack_from('<q', data, entry1_start + 24)[0]
                so1 = struct.unpack_from('<q', data, entry1_start + 32)[0]
                if ss1 == exp1.SerialSize and so1 == exp1.SerialOffset:
                    return entry_start
            else:
                return entry_start
        pos = idx + 1
    
    return None


# Export classes where the simple SSEO formula does NOT apply.
# These have additional class-specific serialization after SSEO that
# makes the relationship between SerialSize, SSEO, and Extras non-trivial.
# We skip these and only patch exports where the formula is proven correct.
SSEO_SKIP_CLASSES = {
    'BlueprintGeneratedClass',  # Has class-specific data after SSEO
    'Level',                     # Has level-specific data after SSEO
}


def _is_cdo(af, exp, idx):
    """
    Check if an export is a Class Default Object (CDO).
    CDOs have names starting with 'Default__' and have a different
    SSEO relationship.
    """
    oname = af.get_export_name(exp)
    return oname.startswith('Default__')


def _fix_sseo(data, export_table_start, export_count, af):
    """
    Recalculate and patch ScriptSerializationEndOffset for exports where
    the formula is known to be correct.
    
    The formula: SSEO = SerialSize - ExtrasSize - 4
    
    This holds for most export types including all K2Node variants,
    EdGraph, Brush, BrushComponent, World, WorldSettings, etc.
    
    It does NOT hold for:
      - BlueprintGeneratedClass (has class-specific post-SSEO data)
      - Level (has level-specific post-SSEO data)
      - Class Default Objects (CDOs with Default__ prefix)
    
    These are skipped.
    """
    STRIDE = 112
    SSEO_OFFSET = 100
    SS_OFFSET = 24
    
    fixes = []
    
    for idx in range(export_count):
        entry_off = export_table_start + idx * STRIDE
        
        exp = af.get_export(idx)
        cname = af.get_export_class_name(exp)
        
        # Skip classes where the formula doesn't apply
        if cname in SSEO_SKIP_CLASSES or _is_cdo(af, exp, idx):
            continue
        
        # Read current values from binary
        current_ss = struct.unpack_from('<q', data, entry_off + SS_OFFSET)[0]
        current_sseo = struct.unpack_from('<q', data, entry_off + SSEO_OFFSET)[0]
        
        # Get extras size from UAssetAPI (it correctly tracks Extras blob size)
        extras = bytes(exp.Extras) if exp.Extras else b''
        extras_size = len(extras)
        
        # The formula: SSEO = SerialSize - ExtrasSize - 4
        # The 4 bytes account for the trailing footer after Extras
        if extras_size > 0:
            correct_sseo = current_ss - extras_size - 4
        else:
            correct_sseo = current_ss - 4
        
        if correct_sseo != current_sseo:
            oname = af.get_export_name(exp)
            fixes.append((idx, cname, oname, current_sseo, correct_sseo))
            struct.pack_into('<q', data, entry_off + SSEO_OFFSET, correct_sseo)
    
    return fixes


def _fix_trailer_offset(data):
    """
    Find the actual Package Trailer position and patch the stored offset.
    
    The trailer offset is stored as int64 at byte 610 in the package summary.
    The trailer is identified by magic 0x9E2A83C1 appearing twice near EOF
    (start and end markers).
    """
    magic = struct.pack('<I', 0x9E2A83C1)
    
    # Find all magic positions, excluding offset 0 (package header magic)
    positions = []
    pos = 4  # Skip the header magic at offset 0
    while True:
        idx = data.find(magic, pos)
        if idx == -1:
            break
        positions.append(idx)
        pos = idx + 1
    
    if len(positions) < 2:
        return None, None  # No trailer found or incomplete
    
    # The trailer start is the second-to-last magic (start marker)
    # The last magic is the end marker
    trailer_start = positions[-2]
    
    # Read current stored offset
    current_offset = struct.unpack_from('<q', data, 610)[0]
    
    if current_offset != trailer_start:
        struct.pack_into('<q', data, 610, trailer_start)
        return current_offset, trailer_start
    
    return current_offset, current_offset  # Already correct


def patch_uasset_post_save(filepath, engine_version='5.6'):
    """
    Apply all post-save binary fixups to a .umap/.uasset file.
    
    This must be called AFTER UAssetAPI's Write() and BEFORE the file
    is used by the engine.
    
    Args:
        filepath: Path to the saved .umap/.uasset file
        engine_version: UE version string (currently only '5.6' tested)
    
    Returns:
        dict with 'sseo_fixes' and 'trailer_fix' details
    """
    from core.uasset_bridge import AssetFile
    
    # Load via UAssetAPI to get export metadata
    af = AssetFile(filepath, engine_version)
    
    # Read raw binary
    with open(filepath, 'rb') as f:
        data = bytearray(f.read())
    
    result = {
        'sseo_fixes': [],
        'trailer_fix': None,
        'file_size': len(data),
    }
    
    # Step 1: Find export table
    export_table_start = _find_export_table_start(data, af.export_count, af)
    if export_table_start is None:
        raise RuntimeError(f"Could not locate export table in {filepath}")
    
    result['export_table_start'] = export_table_start
    
    # Step 2: Fix SSEO for all exports
    sseo_fixes = _fix_sseo(data, export_table_start, af.export_count, af)
    result['sseo_fixes'] = sseo_fixes
    
    # Step 3: Fix trailer offset
    old_trailer, new_trailer = _fix_trailer_offset(data)
    if old_trailer is not None:
        result['trailer_fix'] = {
            'old': old_trailer,
            'new': new_trailer,
            'changed': old_trailer != new_trailer,
        }
    
    # Write patched binary
    with open(filepath, 'wb') as f:
        f.write(data)
    
    return result


def verify_export_table(filepath, engine_version='5.6'):
    """
    Verify that all export table entries are consistent.
    Returns a list of issues found, or empty list if all OK.
    """
    from core.uasset_bridge import AssetFile
    
    af = AssetFile(filepath, engine_version)
    
    with open(filepath, 'rb') as f:
        data = bytearray(f.read())
    
    export_table_start = _find_export_table_start(data, af.export_count, af)
    if export_table_start is None:
        return ["Could not locate export table"]
    
    STRIDE = 112
    issues = []
    
    for idx in range(af.export_count):
        entry_off = export_table_start + idx * STRIDE
        
        ss = struct.unpack_from('<q', data, entry_off + 24)[0]
        so = struct.unpack_from('<q', data, entry_off + 32)[0]
        sseo = struct.unpack_from('<q', data, entry_off + 100)[0]
        
        exp = af.get_export(idx)
        cname = af.get_export_class_name(exp)
        extras = bytes(exp.Extras) if exp.Extras else b''
        
        # Skip classes where the formula doesn't apply
        if cname in SSEO_SKIP_CLASSES or _is_cdo(af, exp, idx):
            continue
        
        # Check SSEO consistency
        if len(extras) > 0:
            expected_sseo = ss - len(extras) - 4
        else:
            expected_sseo = ss - 4
        
        if sseo != expected_sseo:
            issues.append(f"[{idx}] {cname}: SSEO={sseo} expected={expected_sseo} (SS={ss}, Extras={len(extras)}B)")
    
    # Check trailer
    magic = struct.pack('<I', 0x9E2A83C1)
    positions = []
    pos = 4
    while True:
        idx = data.find(magic, pos)
        if idx == -1:
            break
        positions.append(idx)
        pos = idx + 1
    
    if len(positions) >= 2:
        trailer_start = positions[-2]
        stored_offset = struct.unpack_from('<q', data, 610)[0]
        if stored_offset != trailer_start:
            issues.append(f"Trailer offset: stored={stored_offset} actual={trailer_start}")
    
    return issues


if __name__ == '__main__':
    """Command-line usage: python post_save_patcher.py <filepath> [engine_version]"""
    if len(sys.argv) < 2:
        print("Usage: python post_save_patcher.py <filepath> [engine_version]")
        sys.exit(1)
    
    filepath = sys.argv[1]
    version = sys.argv[2] if len(sys.argv) > 2 else '5.6'
    
    print(f"Patching: {filepath}")
    print(f"Engine version: {version}")
    print()
    
    result = patch_uasset_post_save(filepath, version)
    
    if result['sseo_fixes']:
        print(f"SSEO fixes ({len(result['sseo_fixes'])}):")
        for idx, cname, oname, old, new in result['sseo_fixes']:
            print(f"  [{idx}] {cname}/{oname}: {old} -> {new}")
    else:
        print("SSEO: all correct")
    
    if result['trailer_fix']:
        tf = result['trailer_fix']
        if tf['changed']:
            print(f"Trailer: {tf['old']} -> {tf['new']}")
        else:
            print(f"Trailer: correct at {tf['new']}")
    
    print()
    
    # Verify
    issues = verify_export_table(filepath, version)
    if issues:
        print("VERIFICATION FAILED:")
        for issue in issues:
            print(f"  {issue}")
    else:
        print("VERIFICATION PASSED: all exports consistent")
