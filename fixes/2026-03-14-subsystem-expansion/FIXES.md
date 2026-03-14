# AgenticMCP Subsystem Expansion - 2026-03-14

## Summary

Added 65 new C++ handlers across 13 new subsystem handler files, bringing the total from 127 to 192 tools. Fixed the Python execution handler to capture stdout/stderr. Updated Build.cs with all required module dependencies. Rebuilt the tool registry with full parameter metadata for all 192 tools.

## New Handler Files (13)

| File | Subsystem | Tools | Key Capabilities |
|------|-----------|-------|------------------|
| Handlers_PCG.cpp | Procedural Content Generation | 6 | List/inspect/execute PCG graphs, get/set node settings, list components |
| Handlers_AnimBlueprint.cpp | Animation Blueprints | 6 | List anim BPs, inspect anim graphs, states, transitions, slot groups, montages |
| Handlers_SceneHierarchy.cpp | World Outliner / Scene | 5 | Get full hierarchy, set folders, attach/detach/rename actors |
| Handlers_SequencerEdit.cpp | Sequencer Editing | 4 | Create sequences, add tracks, get tracks, set play range |
| Handlers_Landscape.cpp | Landscape / Foliage | 5 | List landscapes, get info/layers, list foliage types, get stats |
| Handlers_Physics.cpp | Physics / Constraints | 5 | Get body info, set simulate, apply force, list constraints, get overlaps |
| Handlers_AI.cpp | AI / Behavior Trees | 6 | List/inspect behavior trees, blackboards, AI controllers, EQS queries |
| Handlers_MaterialEdit.cpp | Material Creation/Editing | 4 | List materials, get info, create materials, list instances |
| Handlers_AssetImport.cpp | Asset Import / Management | 6 | Import assets, get info, duplicate, rename, delete, list by type |
| Handlers_EditorSettings.cpp | Editor / Project Settings | 4 | Get project settings, editor prefs, rendering settings, plugin list |
| Handlers_UMG.cpp | UMG Widget Blueprints | 4 | List widgets, get widget tree, get properties, list HUDs |
| Handlers_SkeletalMesh.cpp | Skeletal Mesh Inspection | 5 | List skel meshes, get info, bones, morph targets, sockets |
| Handlers_Packaging.cpp | Build / Source Control | 4 | Build status, build lighting, source control status, checkout |
| Handlers_PythonFix.cpp | Python Execution (Fixed) | 1 | Execute Python with stdout/stderr capture via FOutputDevice hook |

## Python Handler Fix

The original `executePython` handler was fire-and-forget -- it called `GEditor->Exec()` with the `py` command and immediately returned `{"success": true}` with no output. The new `executePythonCapture` hooks a custom `FPythonOutputCapture` device into `GLog` before execution, captures all `LogPython` category output, and returns `stdout`, `stderr`, and `hasErrors` fields.

## Build.cs Updates

Added module dependencies:
- `Foliage` - Foliage editing APIs
- `PhysicsCore` - Physics body instance, constraints
- `AnimGraph` - Animation Blueprint graph nodes
- `AnimGraphRuntime` - Animation state machine runtime
- `UMG` - UMG Widget Blueprint support
- `SourceControl` - Source control integration
- `AIModule` - AI controllers, behavior tree runtime
- `GameplayTasks` - Gameplay task system (used by AI)
- `ToolMenus` - Editor tool menus and settings access
- `DeveloperSettings` - UDeveloperSettings access
- `MaterialEditor` - Material editor APIs
- `PropertyEditor` - Property editing for settings
- `EditorStyle` - Editor style for widget inspection
- `PCG` - Procedural Content Generation (conditional on bBuildEditor)

## Audit Results

```
HandlerMap entries:  192
Registry entries:    192
Cross-reference:     192/192 (1:1 match, 0 gaps)
C++ parsed fields:   319
Registry parameters: 335
Field mismatches:    0
MCP schema audit:    192/192 PASS
Server boot test:    192 tools loaded, clean start
```

## Handler Design Principles

Every new handler follows these rules:
1. Returns actual engine data -- no fire-and-forget, no "check output log"
2. All required fields validated with specific error messages
3. Uses ParseBodyJson for JSON body parsing
4. Returns MakeErrorJson on failure with actionable error text
5. Read-only handlers never modify engine state
6. Mutation handlers use editor transactions for undo support
