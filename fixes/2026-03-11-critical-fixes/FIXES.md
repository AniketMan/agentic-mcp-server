# Critical Fixes - 2026-03-11

Root cause: 9 commits landed on 3/10 after the last known-good state (morning of 3/10). Multiple AI-assisted coding sessions introduced duplicate code blocks, missing endpoints, and a port mismatch that broke the MCP bridge connection entirely.

## Fix 1: Missing `/mcp/*` Endpoints (CRITICAL - Bridge Connection Failure)

**Problem:** The JS MCP bridge (`Tools/index.js` via `lib.js`) connects to the C++ plugin using `/mcp/status`, `/mcp/tools`, and `/mcp/tool/{name}` endpoints. The C++ plugin had **zero** `/mcp/*` routes registered. The bridge could never establish a connection.

**Root cause:** The original Natfii bridge architecture expects these endpoints. When the C++ plugin was rewritten/expanded on 3/10, only `/api/*` routes were added. The `/mcp/*` layer was never implemented.

**Fix:** Added three new route handlers in `AgenticMCPServer.cpp::Start()`:
- `GET /mcp/status` - Returns connection status, server version, asset counts
- `GET /mcp/tools` - Auto-discovery endpoint that enumerates all registered `HandlerMap` entries
- `POST /mcp/tool/{name}` - Wildcard route that extracts the tool name from the URL path and queues execution to the game thread (same as `/api/*` routes)

## Fix 2: Port Mismatch (CRITICAL - Connection Failure)

**Problem:** C++ plugin listens on port `9847`. JS bridge defaulted to `http://localhost:3000`.

**Root cause:** The JS code was copied from the Natfii bridge which uses port 3000. The C++ plugin was configured for 9847 but the JS default was never updated.

**Fix:** Changed default in `Tools/index.js` from `localhost:3000` to `localhost:9847`. Updated `ARCHITECTURE.md` to match.

## Fix 3: 25 Duplicate `BindRoute` Calls (Build/Runtime Failure)

**Problem:** The `Start()` function had entire blocks of `Router->BindRoute()` calls duplicated verbatim:
- Level Sequences block (3 routes) duplicated
- Visual Agent block (11 routes) duplicated
- Debug Drawing block (2 routes) duplicated
- Blueprint Snapshot (1 route) duplicated
- Transaction/Undo block (4 routes) duplicated
- State Management block (4 routes) duplicated

UE's HTTP router may crash, silently fail, or produce undefined behavior when the same path is bound twice.

**Root cause:** Multiple commit sessions on 3/10 copy-pasted route blocks without checking for existing registrations.

**Fix:** Removed all duplicate `BindRoute` blocks (lines 1560-1567 and 1598-1651 in the original file).

## Fix 4: 154-Line Duplicate `HandlerMap.Add` Block (Silent Override)

**Problem:** The `RegisterHandlers()` function had a full duplicate block of `HandlerMap.Add` calls for PIE Control, Console Commands, Audio, Niagara, and PixelStreaming handlers (lines 1348-1501). `TMap::Add` silently overwrites existing keys, so this wasn't a compile error but was unnecessary bloat and a maintenance hazard.

**Fix:** Removed the duplicate block (154 lines).

## Fix 5: Missing Route Bindings for Existing Handlers

**Problem:** Two handlers had `HandlerMap.Add` entries but no corresponding `Router->BindRoute`:
- `removeSublevel` - Handler at line 940, no `/api/remove-sublevel` route
- `openAsset` - Handler at line 1086, no `/api/open-asset` route

These tools were registered in the dispatch map but unreachable via HTTP.

**Fix:** Added `BindRoute` calls for both in the Level Management section.

## Fix 6: Build Artifacts in Git

**Problem:** `AgenticMCP/Intermediate/` contained 27 UE build artifact files committed to the repo.

**Fix:** Removed the directory and updated `.gitignore` to exclude `Intermediate/`, `Binaries/`, `DerivedDataCache/`, and `Saved/`.

## Fix 7: Stale Duplicate File Tree

**Problem:** The repo has two copies of the plugin:
- `Source/` (root) - current, 25 handler files
- `AgenticMCP/Source/` - stale copy, only 11 handler files

The root `Source/` was being actively edited but `AgenticMCP/Source/` was out of sync (missing 14 handler files added on 3/10).

**Fix:** Synced all files from `Source/` to `AgenticMCP/Source/`. Both copies now match.

## Branch Audit

| Branch | Status | Notes |
|--------|--------|-------|
| `master` | Active | All fixes applied here. 13 commits total. |
| `phase1-metaxr` | Stale | Diverged at commit `c01a8c4` (3/9). Contains 2 commits not on master (`fb603e0`, `aa14796`) but master has 9 newer commits that supersede this branch. Safe to delete. |

## UE 5.6 Oculus Fork Compatibility

The existing version guards from the previous fix session (`fixes/ue56-api-compat/`) are confirmed present and correct:
- SpawnActor uses `FTransform` overload (universal)
- `AddLevelToWorld` guarded with `ENGINE_MINOR_VERSION >= 6`
- `AddFunctionGraph` guarded with 4th parameter for 5.6+
- `BlueprintFactory.h` include added
- `ErrorsFromLastCompile` replaced with `BP->Status` for 5.6+

No additional 5.6 compat issues found in the new handler files (Audio, Niagara, PixelStreaming, MetaXR, etc.) as they use stable APIs.
