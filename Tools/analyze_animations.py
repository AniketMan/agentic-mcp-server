"""
Animation Screenshot Capture Script
Opens each animation asset in the editor to enable viewport screenshot capture.
"""
import unreal

# Animation assets to open
ANIMATIONS = [
    # HeatherAdult
    "/Game/Assets/Characters/Heathers/HeatherAdult/Animations/HEATHER__ADULT__sits__excitedly_waiting-__2_ue5",
    "/Game/Assets/Characters/Heathers/HeatherAdult/Animations/Heather_takes_a_sip_of_her_whisky_and_coke-__ue5",
    "/Game/Assets/Characters/Heathers/HeatherAdult/Animations/Livelyhands",
    "/Game/Assets/Characters/Heathers/HeatherAdult/Animations/triggers_Heather_to_place_her_hand_in_Susan_ue5",
    "/Game/Assets/Characters/Heathers/HeatherAdult/Animations/triggers_Heather_to_place_her_hand_in_Susan2_ue5",
    # HeatherChild
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_1_02_ue5_Heather_Child_gm01__1_",
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_1_02b_ue5_Heather_Child_gm01__1_",
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_3_01_ue5_Heather_Child_gm01_Anim",
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_3_02_ue5_Heather_Child_gm01",
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_3_03_ue5_Heather_Child_gm01",
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/2_1_04_ue5__1__Anim1",
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/Full_Scene_1_V2_ue5_Anim",
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/Hug_Loop_2_Anim",
    # HeatherPreTeen
    "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_1_01_ue5_Heather_Preteen_gm01",
    "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_1_02_ue5_Heather_Preteen_gm01",
    "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_1_03_ue5_Heather_Preteen_gm01",
    "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_1_04_ue5_Heather_Preteen_gm01",
    "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_1_05_ue5_Heather_Preteen_gm01",
    "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_2_05_ue5_Heather_Preteen_gm01",
]

def analyze_animations():
    """Analyze all mocap animations and print detailed info."""
    editor_subsystem = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)

    results = []
    for anim_path in ANIMATIONS:
        anim = unreal.load_asset(anim_path)
        if anim:
            name = anim.get_name()
            length = anim.sequence_length if hasattr(anim, 'sequence_length') else 0
            num_frames = anim.get_number_of_sampled_keys() if hasattr(anim, 'get_number_of_sampled_keys') else 0
            skeleton_name = anim.get_skeleton().get_name() if anim.get_skeleton() else "None"

            # Parse scene/shot from name
            scene = "?"
            shot = "?"
            if name.startswith(('1_', '2_')):
                parts = name.split('_')
                if len(parts) >= 2:
                    scene = parts[0]
                    shot = parts[1]
            elif "Full_Scene" in name:
                scene = "1"
                shot = "Full"
            elif "Hug" in name:
                scene = "Emotional"
                shot = "Hug"
            elif "ADULT" in name:
                scene = "Adult"
                shot = "-"
            elif "takes_a_sip" in name or "Livelyhands" in name or "triggers_" in name:
                scene = "Adult"
                shot = "-"

            info = {
                "name": name,
                "path": anim_path,
                "length": length,
                "frames": num_frames,
                "skeleton": skeleton_name,
                "scene": scene,
                "shot": shot
            }
            results.append(info)
            print(f"{scene}.{shot} | {name} | {length:.2f}s | {skeleton_name}")
        else:
            print(f"FAILED: {anim_path}")

    return results

# Run analysis
print("=" * 80)
print("MOCAP ANIMATION CATALOG")
print("=" * 80)
analyze_animations()
print("=" * 80)
