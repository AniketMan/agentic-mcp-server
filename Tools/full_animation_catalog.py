"""
Animation Asset Analyzer and Thumbnail Extractor
Opens each mocap animation and extracts metadata + thumbnail for cataloging.
"""
import unreal
import os
import json

OUTPUT_DIR = r"C:\Users\aniketbhatt\Desktop\SOH\Dev\Main\Plugins\AgenticMCP\Tools\AnimationScreenshots"

# All mocap animation paths
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

def parse_scene_shot(name):
    """Parse scene and shot number from animation name."""
    if name.startswith(('1_', '2_')):
        parts = name.split('_')
        if len(parts) >= 2:
            return parts[0], parts[1]
    elif "Full_Scene" in name:
        return "1", "Full"
    elif "Hug" in name:
        return "Special", "Hug"
    return "Adult", "-"

def get_animation_details(anim_path):
    """Get comprehensive details about an animation."""
    anim = unreal.load_asset(anim_path)
    if not anim:
        return None

    name = anim.get_name()
    scene, shot = parse_scene_shot(name)

    details = {
        "name": name,
        "path": anim_path,
        "scene": scene,
        "shot": shot,
        "length_seconds": round(anim.sequence_length, 2) if hasattr(anim, 'sequence_length') else 0,
        "rate_scale": anim.rate_scale if hasattr(anim, 'rate_scale') else 1.0,
    }

    # Get skeleton info
    skeleton = anim.get_skeleton()
    if skeleton:
        details["skeleton_name"] = skeleton.get_name()
        details["skeleton_path"] = skeleton.get_path_name()
        details["num_bones"] = len(skeleton.get_bone_tree())

    # Get number of animation frames/keys
    if hasattr(anim, 'get_number_of_sampled_keys'):
        details["num_keys"] = anim.get_number_of_sampled_keys()

    # Check for curves (blend shapes, facial data)
    if hasattr(anim, 'get_raw_curve_data'):
        curve_data = anim.get_raw_curve_data()
        if curve_data:
            details["has_curves"] = True

    return details

def analyze_all():
    """Analyze all animations and save catalog."""
    catalog = {
        "summary": {
            "total_animations": 0,
            "by_character": {},
            "by_scene": {}
        },
        "animations": {}
    }

    print("=" * 80)
    print("MOCAP ANIMATION CATALOG")
    print("=" * 80)

    for character, paths in ANIMATIONS.items():
        catalog["animations"][character] = []
        catalog["summary"]["by_character"][character] = len(paths)

        print(f"\n--- {character} ({len(paths)} animations) ---")

        for path in paths:
            details = get_animation_details(path)
            if details:
                catalog["animations"][character].append(details)
                catalog["summary"]["total_animations"] += 1

                # Track by scene
                scene_key = f"Scene_{details['scene']}"
                if scene_key not in catalog["summary"]["by_scene"]:
                    catalog["summary"]["by_scene"][scene_key] = []
                catalog["summary"]["by_scene"][scene_key].append(details["name"])

                print(f"  [{details['scene']}.{details['shot']}] {details['name']}: {details['length_seconds']}s | Skeleton: {details.get('skeleton_name', 'Unknown')}")
            else:
                print(f"  [ERROR] Failed to load: {path}")

    # Save catalog to JSON
    catalog_file = os.path.join(OUTPUT_DIR, "animation_catalog.json")
    with open(catalog_file, 'w') as f:
        json.dump(catalog, f, indent=2)
    print(f"\n\nCatalog saved to: {catalog_file}")

    # Print summary
    print("\n" + "=" * 80)
    print("SUMMARY")
    print("=" * 80)
    print(f"Total Animations: {catalog['summary']['total_animations']}")
    print("\nBy Character:")
    for char, count in catalog["summary"]["by_character"].items():
        print(f"  {char}: {count}")
    print("\nBy Scene:")
    for scene, anims in catalog["summary"]["by_scene"].items():
        print(f"  {scene}: {len(anims)} animations")

analyze_all()
