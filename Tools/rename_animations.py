"""
Rename Animation Assets to Match Sequence Names
Maps mocap animations to their corresponding Level Sequence names.
"""
import unreal

# Mapping: current animation path -> new name
# Format: LS_{Scene}_{Shot}_{Character}
RENAME_MAP = {
    # Scene 1 - HeatherChild (LS_1_1, LS_1_3)
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_1_02_ue5_Heather_Child_gm01__1_": "LS_1_1_HeatherChild",
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_1_02b_ue5_Heather_Child_gm01__1_": "LS_1_1_HeatherChild_v2",
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_3_01_ue5_Heather_Child_gm01_Anim": "LS_1_3_HeatherChild_v1",
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_3_02_ue5_Heather_Child_gm01": "LS_1_3_HeatherChild_v2",
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_3_03_ue5_Heather_Child_gm01": "LS_1_3_HeatherChild_v3",
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/Full_Scene_1_V2_ue5_Anim": "LS_1_Full_HeatherChild",
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/Hug_Loop_2_Anim": "LS_HugLoop_HeatherChild",
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/2_1_04_ue5__1__Anim1": "LS_2_1_HeatherChild",

    # Scene 2 - HeatherPreTeen (LS_2_1, LS_2_2R)
    "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_1_01_ue5_Heather_Preteen_gm01": "LS_2_1_HeatherPreTeen_v1",
    "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_1_02_ue5_Heather_Preteen_gm01": "LS_2_1_HeatherPreTeen_v2",
    "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_1_03_ue5_Heather_Preteen_gm01": "LS_2_1_HeatherPreTeen_v3",
    "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_1_04_ue5_Heather_Preteen_gm01": "LS_2_1_HeatherPreTeen_v4",
    "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_1_05_ue5_Heather_Preteen_gm01": "LS_2_1_HeatherPreTeen_v5",
    "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations/2_2_05_ue5_Heather_Preteen_gm01": "LS_2_2R_HeatherPreTeen",

    # Adult scenes - need to know which sequences these belong to
    # Skipping adult animations until we know the mapping
}

def rename_animations():
    """Rename animation assets to match sequence naming convention."""
    editor_lib = unreal.EditorAssetLibrary()

    success_count = 0
    skip_count = 0
    fail_count = 0

    print("=" * 80)
    print("RENAMING ANIMATIONS TO MATCH SEQUENCES")
    print("=" * 80)

    for old_path, new_name in RENAME_MAP.items():
        # Check if source asset exists
        if not editor_lib.does_asset_exist(old_path):
            print(f"[SKIP] Source not found: {old_path.split('/')[-1]}")
            skip_count += 1
            continue

        # Get directory and build new path
        directory = old_path.rsplit('/', 1)[0]
        new_path = f"{directory}/{new_name}"

        # Check if target already exists
        if editor_lib.does_asset_exist(new_path):
            print(f"[SKIP] Already exists: {new_name}")
            skip_count += 1
            continue

        # Perform the rename
        try:
            success = editor_lib.rename_asset(old_path, new_path)
            if success:
                print(f"[OK] {old_path.split('/')[-1]} -> {new_name}")
                success_count += 1
            else:
                print(f"[FAIL] {old_path.split('/')[-1]}")
                fail_count += 1
        except Exception as e:
            print(f"[ERROR] {old_path.split('/')[-1]}: {e}")
            fail_count += 1

    print("=" * 80)
    print(f"DONE: {success_count} renamed, {skip_count} skipped, {fail_count} failed")
    print("=" * 80)
    print("\nNOTE: Adult animations not renamed - need sequence mapping info")

rename_animations()
