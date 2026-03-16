# Fix: Level Blueprint Mutation Crash + HTTP Timeout

**Date**: 2026-03-16
**Severity**: Critical (editor crash)
**Symptom**: `EXCEPTION_ACCESS_VIOLATION` at `AgenticMCPEditorSubsystem.cpp:82` during level blueprint mutations (addNode, connectPins, setPinDefault on level BPs)

## Root Cause

Three compounding issues:

1. **GC invalidation**: Garbage collection could run mid-request, invalidating the `UWorld` that owns a level blueprint. The handler holds a raw `UBlueprint*` returned by `LoadBlueprintByName`, but by the time `SafeMarkStructurallyModified` calls `MarkBlueprintAsStructurallyModified`, the owning `ULevel->UWorld` chain may have been collected.

2. **No ownership validation**: `SafeMarkStructurallyModified` called `MarkBlueprintAsStructurallyModified` without checking if the level blueprint's ownership chain (Level -> World) was still valid. The engine's recompile path dereferences these without null checks.

3. **C++ try/catch misses SEH**: On Windows, access violations from null pointer dereferences are not caught by C++ `try/catch`. The existing crash protection only used `try/catch`, so the editor would hard-crash instead of gracefully failing.

## Fixes Applied

### 1. `AgenticMCPServer.cpp` — ProcessOneRequest (GC Guard)

Added `FGCScopeGuard GCGuard` at the top of `ProcessOneRequest()`. This prevents garbage collection from running for the entire duration of a handler dispatch, ensuring all `UObject*` pointers remain valid.

```cpp
FGCScopeGuard GCGuard;
```

### 2. `AgenticMCPServer.cpp` — SafeMarkStructurallyModified (Ownership Validation)

Added level blueprint detection via `Cast<ULevelScriptBlueprint>`. If the BP is a level blueprint, validates the full ownership chain before attempting compilation:

- `LevelBP->GetLevel()` must be valid
- `OwningLevel->GetWorld()` must be valid

If either is invalid, marks the package dirty (so the editor knows it needs attention) and returns false instead of crashing.

### 3. `AgenticMCPServer.cpp` — SEH Wrappers for Mark*Modified

Added two new SEH wrappers:

- `TryMarkStructurallyModifiedSEH(BP)` — wraps `MarkBlueprintAsStructurallyModified`
- `TryMarkModifiedSEH(BP)` — wraps `MarkBlueprintAsModified`

Both `SafeMarkStructurallyModified` and `SafeMarkModified` now use these SEH wrappers on Windows, catching access violations that C++ `try/catch` misses.

### 4. `Tools/index.js` — Slow Tool Timeout

Added `SLOW_TOOLS` set and `slowRequestTimeoutMs` config (default 120s, env: `MCP_SLOW_REQUEST_TIMEOUT_MS`). Tools in the set get 120s instead of 30s:

- `levelSave`
- `compileBlueprint`
- `importAsset`
- `exportAsset`
- `levelLoad`
- `levelAddSublevel`
- `packageProject`

Applied to both sync (`executeUnrealTool`) and async (`executeLiveTool`) code paths.

## Files Changed

| File | Change |
|------|--------|
| `Source/AgenticMCP/Private/AgenticMCPServer.cpp` | GC guard, ownership validation, SEH wrappers |
| `Tools/index.js` | SLOW_TOOLS timeout override |
| `fixes/2026-03-16-tick-crash-fix/FIXES.md` | This file |
| `fixes/2026-03-16-tick-crash-fix/PATTERN_EVALUATION.md` | BP_SceneN_Controller architecture evaluation |

## Testing

After applying this fix:
1. Level blueprint mutations should no longer crash the editor
2. If the ownership chain is invalid, the handler returns an error JSON instead of crashing
3. If the compile itself crashes (deep engine bug), SEH catches it and returns a warning
4. `levelSave` should no longer timeout on large levels
