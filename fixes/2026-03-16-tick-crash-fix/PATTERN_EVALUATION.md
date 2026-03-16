# BP_SceneN_Controller Pattern Evaluation

## The Problem

Level blueprints are structurally different from Actor blueprints:

1. **Ownership chain**: Level BP -> ULevel -> UWorld. If the world or level is unloaded, GC'd, or invalidated, the level blueprint pointer becomes dangling.
2. **No standalone existence**: A level blueprint cannot exist without its owning level being loaded. Actor blueprints are standalone assets.
3. **Compilation context**: `MarkBlueprintAsStructurallyModified` on a level BP triggers a recompile that walks back through the ownership chain. If any link is null, crash.
4. **No hot-reload**: Level blueprints don't support the same hot-reload path as Actor BPs. Changes often require a level reload.

## The Actor Blueprint Controller Pattern

Instead of mutating level blueprints directly, create `BP_SceneN_Controller` (an Actor Blueprint) per scene:

- `BP_Scene01_Controller` placed in Scene01 level
- `BP_Scene02_Controller` placed in Scene02 level
- Each controller is a standalone Blueprint asset (not tied to level lifecycle)
- The level blueprint does ONE thing: spawns or references its controller on BeginPlay

## Recommendation: Use Both

The crash fix (GC guard + ownership validation + SEH) makes level blueprint mutations **survivable** — they won't crash the editor anymore. But the Actor Blueprint controller pattern is still the **better architecture** for production:

| Aspect | Level Blueprint | BP_SceneN_Controller |
|--------|----------------|---------------------|
| MCP mutation safety | Fixed (with this patch) | Inherently safe |
| Hot-reload | Limited | Full support |
| Reusability | None (tied to level) | Can be subclassed |
| Version control | Embedded in .umap | Standalone .uasset |
| Merge conflicts | High (binary .umap) | Lower (separate asset) |
| BeginPlay/Tick | Works | Works |
| Level streaming | Breaks if level unloads | Actor persists if configured |

## Practical Approach

1. **Short term**: Use the crash fix. Level blueprint mutations now work safely.
2. **Medium term**: For SOH production scenes, create `BP_SceneN_Controller` actors. Use the MCP `addNode`/`connectPins` tools on these instead.
3. **The level blueprint** becomes a thin bootstrap: one BeginPlay event that spawns or finds the controller.

This is not an either/or. The crash fix ensures the tool never brings down the editor. The controller pattern ensures the architecture scales.
