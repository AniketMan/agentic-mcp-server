# Epic UE Assistant: Why New .umap Doesn't Show in Editor

## Question
In UE 5.6, I programmatically created a new .umap file by cloning an existing sublevel and modifying its binary data. I placed it on disk next to the original .umap in the Content directory and submitted it to Perforce. However, the file does not appear in the Content Browser or the Open Level dialog. Why?

## Answer (from Epic Developer Assistant, 2026-03-05)

UE requires more than just a raw `.umap` file on disk to recognize and show a level asset in the Content Browser. Simply cloning and placing the `.umap` file manually will not register it with the Editor's Asset Registry or generate the necessary metadata.

Key points:

1. **Asset Registry Registration:** UE uses the Asset Registry to know which assets exist. This is populated by scanning `.uasset` files, but `.umap` files are still assets that need to be properly imported or registered.

2. **Companion `.uasset` Metadata:** Levels stored as `.umap` actually have companion `.uasset` files that hold metadata and references used by the Editor. The Editor doesn't fully recognize raw `.umap` files without that `.uasset` sidecar.

3. **Proper Way to Create Levels Programmatically:** Use the Unreal Editor's APIs to create and save levels via the `ULevel` or `UWorld` class and save them using the asset tools, so the `.uasset` and `.umap` along with registry entries are properly created.

### How to correctly create a new level programmatically:

1. Use `UEditorLevelLibrary` or `UWorldFactory` to create a new level asset inside the Editor environment rather than copying `.umap` files on disk.
2. Save the newly created world using the Editor asset tools (e.g., `UPackage::SavePackage`) so that the `.uasset` metadata is generated.
3. After saving, the Asset Registry will detect it immediately or on next scan, showing it in Content Browser.

### Example C++ pseudocode:
```cpp
UPackage* Package = CreatePackage(*NewMapPath);
UWorld* NewWorld = UWorld::CreateWorld(EWorldType::Editor, false);
NewWorld->Rename(*FPaths::GetBaseFilename(NewMapPath), Package);
FAssetRegistryModule::AssetCreated(NewWorld);
Package->MarkPackageDirty();
UPackage::SavePackage(Package, NewWorld, EObjectFlags::RF_Public |
    EObjectFlags::RF_Standalone, *FPackageName::LongPackageNameToFilename(NewMapPath));
```

### Summary:
- UE Editor does not just look for `.umap` files but requires the `.uasset` metadata companion.
- Copying or modifying `.umap` binaries directly omits metadata & registry entries.
- Use Editor APIs to create and save new levels to create complete assets.
- Once saved properly, `.umap` assets will show up in the Content Browser and Open Level dialog.

## Implication for Our Pipeline

**Creating new levels from scratch via binary editing alone is NOT sufficient.** The editor needs a companion `.uasset` file and an asset registry entry.

**However, MODIFYING existing levels (that already have registry entries) should work fine.** The editor already knows about the file — we're just changing its contents.

This confirms the correct approach: **edit existing .umap files, don't create new ones.**
