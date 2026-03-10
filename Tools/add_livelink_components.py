"""
Add LiveLinkComponent to character blueprints using EditorUtilityWidget or direct Blueprint manipulation.
This script opens each blueprint in the editor and adds the component using the editor's own APIs.
"""
import unreal

character_blueprints = [
    "/Game/Assets/Characters/Heathers/HeatherAdult/BP_Heather_Adult",
    "/Game/Assets/Characters/Heathers/HeatherChild/BP_HeatherChild",
    "/Game/Assets/Characters/Heathers/HeatherPreTeen/BP_HeatherPreTeen2",
    "/Game/Assets/Characters/Heathers/HeatherTeen/BP_Heather_Teen",
    "/Game/Assets/Characters/Detective/BP_Detective",
    "/Game/Assets/Characters/Friend1/BP_FriendMale",
    "/Game/Assets/Characters/Friend2/BP_FriendFemale",
    "/Game/Assets/Characters/Officer2/BP_Officer2",
    "/Game/Assets/Characters/Receptionist/BP_Recepcionist"
]

added_count = 0
failed = []

# Get subsystems
subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
asset_editor = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)

for bp_path in character_blueprints:
    try:
        # Load the blueprint
        bp = unreal.load_asset(bp_path)
        if bp is None:
            failed.append(f"{bp_path}: Asset not found")
            continue

        # Get root handles
        root_handles = subsystem.k2_gather_subobject_data_for_blueprint(bp)

        if len(root_handles) == 0:
            failed.append(f"{bp_path}: No subobject data")
            continue

        # Find the default scene root or first valid parent
        parent_handle = root_handles[0]

        # Try add_new_subobject with AddNewSubobjectParams
        params = unreal.AddNewSubobjectParams()
        params.parent_handle = parent_handle
        params.new_class = unreal.LiveLinkComponent
        params.blueprint_context = bp

        # Call add_new_subobject
        new_handle, reason = subsystem.add_new_subobject(params)

        # Check result - the handle might be valid even if it looks invalid
        # Try to get data from it
        data = subsystem.k2_find_subobject_data_from_handle(new_handle)

        if data is not None:
            # Success! Compile and save
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
            unreal.EditorAssetLibrary.save_asset(bp_path, only_if_is_dirty=True)
            added_count += 1
            unreal.log(f"SUCCESS: Added LiveLinkComponent to {bp_path}")
        else:
            failed.append(f"{bp_path}: {reason if reason else 'Unknown error'}")

    except Exception as e:
        failed.append(f"{bp_path}: {str(e)}")

unreal.log(f"")
unreal.log(f"=== SUMMARY ===")
unreal.log(f"Successfully processed {added_count}/9 blueprints")
if failed:
    unreal.log(f"")
    unreal.log(f"Failed ({len(failed)}):")
    for f in failed:
        unreal.log(f"  - {f}")

# If nothing worked, print instructions for manual addition
if added_count == 0:
    unreal.log(f"")
    unreal.log(f"=== MANUAL INSTRUCTIONS ===")
    unreal.log(f"The Python API is not working for component addition.")
    unreal.log(f"Please add LiveLinkComponent manually:")
    unreal.log(f"1. Open each Blueprint listed above")
    unreal.log(f"2. In the Components panel, click 'Add Component'")
    unreal.log(f"3. Search for 'LiveLinkComponent' and add it")
    unreal.log(f"4. Rename to 'LiveLinkFace'")
    unreal.log(f"5. Compile and Save")
