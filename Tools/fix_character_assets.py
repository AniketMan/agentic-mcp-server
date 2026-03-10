"""
Fix asset warnings in HeatherChild and HeatherPreTeen characters.
"""
import unreal

unreal.log("=== Starting asset fixes ===")

# Fix 1: Resave HeatherPreTeen2 assets to fix empty engine version
assets_to_resave = [
    "/Game/Assets/Characters/Heathers/HeatherPreTeen/HeatherPreTeen2",
    "/Game/Assets/Characters/Heathers/HeatherPreTeen/HeatherPreTeen2_PhysicsAsset"
]

for asset_path in assets_to_resave:
    try:
        asset = unreal.load_asset(asset_path)
        if asset:
            unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
            unreal.log(f"Resaved: {asset_path}")
        else:
            unreal.log(f"Could not load: {asset_path}")
    except Exception as e:
        unreal.log(f"Failed to resave {asset_path}: {e}")

# Fix 2: Check and fix SKM_HeatherChild physics asset reference
unreal.log("Checking HeatherChild skeletal mesh...")
skm_path = "/Game/Assets/Characters/Heathers/HeatherChild/SKM_HeatherChild"
skm = unreal.load_asset(skm_path)
if skm:
    unreal.log(f"Loaded SKM_HeatherChild: {skm.get_name()}")
    physics = skm.get_editor_property("physics_asset")
    if physics:
        unreal.log(f"Current physics asset: {physics.get_path_name()}")
    else:
        unreal.log("No physics asset assigned, attempting to fix...")
        # Try to assign the correct physics asset
        correct_physics = unreal.load_asset("/Game/Assets/Characters/Heathers/HeatherChild/SKM_HeatherChild_PhysicsAsset")
        if correct_physics:
            skm.set_editor_property("physics_asset", correct_physics)
            unreal.EditorAssetLibrary.save_asset(skm_path, only_if_is_dirty=False)
            unreal.log("Fixed HeatherChild physics asset reference!")
        else:
            unreal.log("Could not load SKM_HeatherChild_PhysicsAsset")
else:
    unreal.log("Could not load SKM_HeatherChild")

unreal.log("=== Asset fixes complete ===")
