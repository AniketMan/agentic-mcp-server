"""
Animation Screenshot Tool
Opens each mocap animation asset and takes a screenshot for cataloging.
"""

import unreal
import os
import json

# Output directory for screenshots
OUTPUT_DIR = r"C:\Users\aniketbhatt\Desktop\SOH\Dev\Main\Plugins\AgenticMCP\Tools\AnimationScreenshots"

# Animation paths organized by character
ANIMATIONS = {
    "HeatherAdult": [
        "/Game/Assets/Characters/Heathers/HeatherAdult/Animations/HEATHER__ADULT__sits__excitedly_waiting-__2_ue5",
        "/Game/Assets/Characters/Heathers/HeatherAdult/Animations/Heather_takes_a_sip_of_her_whisky_and_coke-__ue5",
        "/Game/Assets/Characters/Heathers/HeatherAdult/Animations/Livelyhands",
        "/Game/Assets/Characters/Heathers/HeatherAdult/Animations/triggers_Heather_to_place_her_hand_in_Susan_ue5",
        "/Game/Assets/Characters/Heathers/HeatherAdult/Animations/triggers_Heather_to_place_her_hand_in_Susan2_ue5",
    ],
    "HeatherChild": [
        "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_1_02_ue5_Heather_Child_gm01__1_",
        "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_1_02b_ue5_Heather_Child_gm01__1_",
        "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_3_01_ue5_Heather_Child_gm01_Anim",
        "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_3_02_ue5_Heather_Child_gm01",
        "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_3_03_ue5_Heather_Child_gm01",
        "/Game/Assets/Characters/Heathers/HeatherChild/Animations/2_1_04_ue5__1__Anim1",
        "/Game/Assets/Characters/Heathers/HeatherChild/Animations/Full_Scene_1_V2_ue5_Anim",
        "/Game/Assets/Characters/Heathers/HeatherChild/Animations/Hug_Loop_2_Anim",
    ],
    "HeatherPreTeen": [
        "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_1_01_ue5_Heather_Preteen_gm01",
        "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_1_02_ue5_Heather_Preteen_gm01",
        "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_1_03_ue5_Heather_Preteen_gm01",
        "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_1_04_ue5_Heather_Preteen_gm01",
        "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_1_05_ue5_Heather_Preteen_gm01",
        "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_2_05_ue5_Heather_Preteen_gm01",
    ],
}

def get_animation_info(anim_path):
    """Get detailed info about an animation sequence."""
    anim = unreal.load_asset(anim_path)
    if not anim:
        return None

    info = {
        "name": anim.get_name(),
        "path": anim_path,
        "num_frames": anim.get_number_of_frames() if hasattr(anim, 'get_number_of_frames') else 0,
        "length": anim.sequence_length if hasattr(anim, 'sequence_length') else 0,
        "skeleton": str(anim.get_skeleton().get_path_name()) if anim.get_skeleton() else "None",
    }
    return info

def analyze_all_animations():
    """Analyze all animation assets and print info."""
    results = {}

    for character, anim_paths in ANIMATIONS.items():
        print(f"\n=== {character} ===")
        results[character] = []

        for path in anim_paths:
            info = get_animation_info(path)
            if info:
                results[character].append(info)
                print(f"  {info['name']}: {info['length']:.2f}s, {info['num_frames']} frames")

                # Parse scene info from name
                name = info['name']
                if name.startswith(('1_', '2_')):
                    parts = name.split('_')
                    if len(parts) >= 2:
                        scene = parts[0]
                        shot = parts[1]
                        print(f"    -> Scene {scene}, Shot {shot}")
            else:
                print(f"  FAILED TO LOAD: {path}")

    # Save results to JSON
    output_file = os.path.join(OUTPUT_DIR, "animation_catalog.json")
    with open(output_file, 'w') as f:
        json.dump(results, f, indent=2)
    print(f"\nCatalog saved to: {output_file}")

    return results

# Run the analysis
results = analyze_all_animations()
