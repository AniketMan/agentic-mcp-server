"""
Fix Redirectors - Clean up asset redirectors after rename operation.
"""
import unreal

def fix_redirectors():
    """Fix all redirectors in the Animations folders."""
    editor_lib = unreal.EditorAssetLibrary()
    asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()

    # Paths to check for redirectors
    paths = [
        "/Game/Assets/Characters/Heathers/HeatherChild/Animations",
        "/Game/Assets/Characters/Heathers/HeatherPreTeen/Animations",
        "/Game/Assets/Characters/Heathers/HeatherAdult/Animations",
    ]

    print("=" * 80)
    print("FIXING REDIRECTORS")
    print("=" * 80)

    total_fixed = 0

    for path in paths:
        print(f"\nChecking: {path}")

        # Get all assets in the path
        assets = editor_lib.list_assets(path, recursive=True, include_folder=False)

        redirectors = []
        for asset_path in assets:
            # Check if it's a redirector
            asset_data = asset_registry.get_asset_by_object_path(asset_path)
            if asset_data.is_valid():
                asset_class = str(asset_data.asset_class_path.asset_name)
                if "Redirector" in asset_class:
                    redirectors.append(asset_path)

        if redirectors:
            print(f"  Found {len(redirectors)} redirectors")
            for redir in redirectors:
                print(f"    - {redir.split('/')[-1]}")

            # Fix redirectors using the asset tools
            asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

            # Load the redirectors
            redirector_objects = []
            for redir_path in redirectors:
                obj = unreal.load_asset(redir_path)
                if obj:
                    redirector_objects.append(obj)

            if redirector_objects:
                # Fix up the redirectors
                unreal.EditorAssetLibrary.consolidate_assets(redirector_objects[0], redirector_objects[1:] if len(redirector_objects) > 1 else [])
                total_fixed += len(redirector_objects)
        else:
            print("  No redirectors found")

    # Alternative: Use FixupReferencers command
    print("\n" + "=" * 80)
    print("Running Fixup Redirectors command...")
    print("=" * 80)

    # This consolidates and deletes redirectors
    unreal.EditorLoadingAndSavingUtils.fixup_redirectors_on_loaded_assets()

    print(f"\nDone! Attempted to fix redirectors.")

fix_redirectors()
