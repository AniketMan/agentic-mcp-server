"""
Direct Animation Rename - Using Unreal's asset rename functionality
"""
import unreal

def rename_animation(old_path, new_name):
    """Rename a single animation asset."""
    editor_lib = unreal.EditorAssetLibrary()

    if not editor_lib.does_asset_exist(old_path):
        print(f"NOT FOUND: {old_path}")
        return False

    directory = old_path.rsplit('/', 1)[0]
    new_path = f"{directory}/{new_name}"

    if editor_lib.does_asset_exist(new_path):
        print(f"EXISTS: {new_name}")
        return False

    result = editor_lib.rename_asset(old_path, new_path)
    if result:
        print(f"OK: {old_path.split('/')[-1]} -> {new_name}")
        return True
    else:
        print(f"FAILED: {old_path.split('/')[-1]}")
        return False

# Rename one animation as a test
print("Testing rename...")
rename_animation(
    "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_1_02_ue5_Heather_Child_gm01__1_",
    "LS_1_1_HeatherChild"
)
