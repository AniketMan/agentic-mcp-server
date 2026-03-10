"""
Check character skeletal meshes for facial blend shapes (morph targets).
Updated with correct asset paths.
"""
import unreal

# Character skeletal mesh paths - corrected
character_meshes = [
    ("/Game/Assets/Characters/Heathers/HeatherAdult/Heather_Adult", "Heather Adult"),
    ("/Game/Assets/Characters/Heathers/HeatherChild/SKM_HeatherChild", "Heather Child"),
    ("/Game/Assets/Characters/Heathers/HeatherPreTeen/HeatherPreTeen2", "Heather PreTeen"),
    ("/Game/Assets/Characters/Heathers/HeatherTeen/Heather_Teen_Rig", "Heather Teen"),
    ("/Game/Assets/Characters/Detective/Detective", "Detective"),
    ("/Game/Assets/Characters/Friend1/FriendMale", "Friend Male"),
    ("/Game/Assets/Characters/Friend2/FriendFemale", "Friend Female"),
    ("/Game/Assets/Characters/Officer2/Officer2", "Officer 2"),
    ("/Game/Assets/Characters/Receptionist/Receptionist", "Receptionist"),
]

# ARKit blend shape names (52 shapes)
arkit_shapes = [
    "eyeBlinkLeft", "eyeLookDownLeft", "eyeLookInLeft", "eyeLookOutLeft", "eyeLookUpLeft",
    "eyeSquintLeft", "eyeWideLeft", "eyeBlinkRight", "eyeLookDownRight", "eyeLookInRight",
    "eyeLookOutRight", "eyeLookUpRight", "eyeSquintRight", "eyeWideRight", "jawForward",
    "jawLeft", "jawRight", "jawOpen", "mouthClose", "mouthFunnel", "mouthPucker",
    "mouthLeft", "mouthRight", "mouthSmileLeft", "mouthSmileRight", "mouthFrownLeft",
    "mouthFrownRight", "mouthDimpleLeft", "mouthDimpleRight", "mouthStretchLeft",
    "mouthStretchRight", "mouthRollLower", "mouthRollUpper", "mouthShrugLower",
    "mouthShrugUpper", "mouthPressLeft", "mouthPressRight", "mouthLowerDownLeft",
    "mouthLowerDownRight", "mouthUpperUpLeft", "mouthUpperUpRight", "browDownLeft",
    "browDownRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight", "cheekPuff",
    "cheekSquintLeft", "cheekSquintRight", "noseSneerLeft", "noseSneerRight", "tongueOut"
]

unreal.log("=" * 60)
unreal.log("FACIAL BLEND SHAPE ANALYSIS FOR LIVELINK")
unreal.log("=" * 60)

results = []

for mesh_path, char_name in character_meshes:
    unreal.log(f"\n--- {char_name} ---")

    skm = unreal.load_asset(mesh_path)

    if skm is None:
        unreal.log(f"  Could not load: {mesh_path}")
        results.append((char_name, "NOT FOUND", 0, []))
        continue

    # Check if it's a SkeletalMesh
    if not isinstance(skm, unreal.SkeletalMesh):
        unreal.log(f"  Asset type: {type(skm).__name__} (not SkeletalMesh)")
        results.append((char_name, type(skm).__name__, 0, []))
        continue

    # Get morph targets
    morph_targets = skm.get_editor_property("morph_targets")

    if morph_targets is None or len(morph_targets) == 0:
        unreal.log(f"  No morph targets found")
        results.append((char_name, "NO MORPHS", 0, []))
        continue

    morph_names = [mt.get_name() for mt in morph_targets]
    unreal.log(f"  Found {len(morph_names)} morph targets")

    # Check for ARKit shapes
    arkit_found = []
    morph_names_lower = [m.lower() for m in morph_names]

    for shape in arkit_shapes:
        if shape.lower() in morph_names_lower:
            arkit_found.append(shape)

    unreal.log(f"  ARKit compatible: {len(arkit_found)}/52")

    if len(morph_names) <= 10:
        unreal.log(f"  Morph targets: {morph_names}")
    else:
        unreal.log(f"  Sample morphs: {morph_names[:5]}...")

    results.append((char_name, "OK", len(morph_names), arkit_found))

# Summary
unreal.log("\n" + "=" * 60)
unreal.log("SUMMARY - LiveLink Facial Capture Readiness")
unreal.log("=" * 60)

ready_count = 0
for char_name, status, morph_count, arkit_shapes_found in results:
    if status == "OK":
        arkit_pct = len(arkit_shapes_found) / 52 * 100
        if arkit_pct >= 50:
            ready_count += 1
            ready = "READY"
        else:
            ready = "PARTIAL"
        unreal.log(f"  {char_name}: {morph_count} morphs, {len(arkit_shapes_found)}/52 ARKit ({arkit_pct:.0f}%) - {ready}")
    elif status == "NO MORPHS":
        unreal.log(f"  {char_name}: NO FACIAL MORPHS - needs blend shapes added")
    else:
        unreal.log(f"  {char_name}: {status}")

unreal.log("")
unreal.log(f"RESULT: {ready_count}/{len(results)} characters ready for LiveLink facial capture")
unreal.log("=" * 60)
