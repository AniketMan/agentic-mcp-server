"""
Open Animation Asset in Editor
Opens the first animation asset in the editor for visual inspection.
"""
import unreal

# Open first HeatherChild animation
anim_path = "/Game/Assets/Characters/Heathers/HeatherChild/Animations/1_1_02_ue5_Heather_Child_gm01__1_"

# Load the asset
anim = unreal.load_asset(anim_path)
if anim:
    # Open in asset editor
    editor_subsystem = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
    editor_subsystem.open_editor_for_assets([anim])
    print(f"Opened: {anim.get_name()}")
    print(f"Length: {anim.sequence_length:.2f}s")
    print(f"Skeleton: {anim.get_skeleton().get_name() if anim.get_skeleton() else 'None'}")
else:
    print(f"Failed to load: {anim_path}")
