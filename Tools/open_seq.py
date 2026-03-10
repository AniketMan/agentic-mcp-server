"""
Simple open LS_1_1 in editor
"""
import unreal

# Open the sequence asset in editor
path = "/Game/Sequences/Scene1/LS_1_1"
asset = unreal.load_asset(path)

if asset:
    unreal.AssetEditorSubsystem().open_editor_for_assets([asset])
    print(f"Opening: {asset.get_name()}")
else:
    print(f"Could not find: {path}")
