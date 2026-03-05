# Codebase Audit: Mar 04, 2026

## Findings

### uasset_bridge.py — STATUS: PARTIALLY CORRECT
- **Read path**: Fully functional. Loads .uasset/.umap, parses exports, imports, names, actors, properties, Kismet bytecode.
- **Write path**: EXISTS but limited. `set_property_value()` can modify simple property values. `save()` calls `UAsset.Write()` which serializes the full asset back to binary. Backup system works.
- **MISSING**: No functions for adding/removing exports, adding/removing K2 nodes at the binary level, modifying Kismet bytecode, adding/removing actor references from LevelExport.Actors list.
- **VERDICT**: The foundation is here. UAssetAPI's `UAsset.Write()` is the key — it handles full binary serialization. We need to expand the write surface.

### server.py — STATUS: MIXED
- Has a `/api/save` endpoint that calls the bridge's save method. This is correct.
- Has a `/api/property` POST endpoint that calls `set_property_value()`. This is correct.
- Has script generation endpoints (`/api/script/generate`, `/api/script/save`, `/api/script/operations`). These are the WRONG workflow for Manus agents.
- **VERDICT**: Keep the save and property endpoints. Script generation endpoints should be marked as deprecated/local-only.

### script_generator.py — STATUS: WRONG WORKFLOW FOR MANUS
- Generates Python scripts meant to run inside a running Unreal Editor.
- This is Workflow B (local AI). Not Workflow A (Manus headless binary editing).
- **VERDICT**: Do not remove (it's useful for Workflow B), but clearly mark as local-only. Manus agents must not use these endpoints.

### ue_plugin/JarvisEditor/ — STATUS: CORRECT BUT IRRELEVANT TO MANUS
- C++ plugin for Workflow B only.
- **VERDICT**: Keep in repo for future use. Manus agents must never reference it.

### blueprint_editor.py — STATUS: CORRECT (READ-ONLY)
- K2 node resolution, Kismet bytecode parsing, graph visualization.
- All read-only. No write operations.
- **VERDICT**: Good as-is for inspection.

## What Needs to Be Built for Write Path

1. **Add property to export** — append a new PropertyData to a NormalExport's Data list
2. **Remove property from export** — remove a PropertyData by name
3. **Add export to asset** — create a new export (e.g., a new K2Node) and add it to the asset's Exports list
4. **Remove export from asset** — remove an export and fix all FPackageIndex references
5. **Add actor to level** — append a FPackageIndex to LevelExport.Actors
6. **Remove actor from level** — remove a FPackageIndex from LevelExport.Actors
7. **Add import** — add a new import entry (for referencing external classes/packages)
8. **Add name to name map** — ensure FNames exist before referencing them
9. **Modify Kismet bytecode** — this is the hardest part; bytecode Extras contain serialized references
10. **Full validation before save** — run integrity checks, verify all indices are valid
