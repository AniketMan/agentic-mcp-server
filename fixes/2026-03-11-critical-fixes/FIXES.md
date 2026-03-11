# AgenticMCP Critical Fixes - 2026-03-11

## Summary

9 commits landed on 3/10 after the last known-good state. Multiple AI coding sessions
stacked duplicate code blocks and missed the `/mcp/*` endpoint layer entirely. Additionally,
the `/mcp/tools` endpoint only returned tool names with no metadata, causing agents to
register every tool with empty parameter schemas -- tools appeared to work but sent no
arguments and executed as no-ops.

## Fix 1: Missing `/mcp/*` Endpoints (CRITICAL)

**Problem:** The JS bridge calls `/mcp/status`, `/mcp/tools`, `/mcp/tool/{name}` but the
C++ plugin only had `/api/*` routes. The bridge could never connect to the plugin.

**Fix:** Added three new endpoint registrations in `Start()`:
- `GET /mcp/status` -- returns server status, project name, engine version
- `GET /mcp/tools` -- returns list of all registered tool names from HandlerMap
- `POST /mcp/tool/*` -- catch-all that extracts tool name from URL path, queues to game thread

## Fix 2: Port Mismatch (CRITICAL)

**Problem:** C++ plugin listens on port `9847`, JS bridge defaulted to `localhost:3000`.

**Fix:** Updated `Tools/index.js` default from `3000` to `9847`. Updated ARCHITECTURE.md.

## Fix 3: Tool Registry -- Empty Parameter Schemas (CRITICAL)

**Problem:** The `/mcp/tools` endpoint returned only `{ "name": "toolName" }` per tool.
The JS bridge accessed `tool.description`, `tool.parameters`, and `tool.annotations` --
all `undefined`. Every tool was registered with the MCP protocol as:
- Description: `"[Unreal Editor] undefined"`
- Input schema: `{ type: "object", properties: {} }` (empty)

Agents saw the tools, called them, but sent **empty request bodies** because the schema
said no parameters were needed. C++ handlers received empty JSON, parsed nothing, and
either returned silent errors or executed no-ops.

**This is why it "went through the process" but nothing actually happened in the editor.**

**Fix:** Created `Tools/tool-registry.json` with full metadata for all 127 tools:
- `description` -- human-readable tool description
- `parameters` -- array of `{ name, type, description, required }` objects
- `annotations` -- `{ readOnlyHint, destructiveHint }` for safety classification

Updated `Tools/index.js` to:
1. Load `tool-registry.json` at startup into a `Map<name, metadata>`
2. When listing tools, cross-reference the live C++ tool list with the registry
3. Enrich each tool with full description, parameter schema, and annotations
4. Log warnings for any tools in C++ but missing from the registry

**Architecture note:** Metadata lives in the JS bridge layer (tool-registry.json), not in
C++. The C++ `/mcp/tools` endpoint serves as a live validation source confirming which
tools are actually registered. This avoids embedding 2000+ lines of JSON string literals
in C++ and makes metadata easy to update without recompiling the plugin.

## Fix 4: 25 Duplicate BindRoute Calls (HIGH)

**Problem:** Same HTTP paths registered twice in UE's HTTP router. Undefined behavior --
second registration may silently overwrite or crash.

**Fix:** Removed all duplicate BindRoute blocks (Level Sequences, Visual Agent, Debug,
Snapshot, Transaction, State categories).

## Fix 5: 154-line Duplicate HandlerMap Block (MEDIUM)

**Problem:** PIE, Console, Audio, Niagara, PixelStreaming handlers registered twice in
HandlerMap. Second registration overwrites first with identical lambda.

**Fix:** Removed the duplicate block (lines 1348-1501).

## Fix 6: Missing Handler Registrations (MEDIUM)

**Problem:** Three handlers had implementations and header declarations but were not
wired up:
- `removeSublevel` -- had handler, no HandlerMap entry, no BindRoute
- `openAsset` -- had handler, no HandlerMap entry, no BindRoute
- `listCVars` -- had handler and header declaration, no HandlerMap entry

**Fix:** Added BindRoute entries for removeSublevel and openAsset. Added HandlerMap entry
for listCVars. All three now in tool-registry.json with full metadata.

## Fix 7: Build Artifacts in Git (LOW)

**Problem:** 27 files in `AgenticMCP/Intermediate/` committed to the repo.

**Fix:** Removed `AgenticMCP/Intermediate/`. Updated `.gitignore` to exclude
`Intermediate/`, `Binaries/`, `DerivedDataCache/`, `Saved/`, `node_modules/`.

## Fix 8: Stale AgenticMCP/Source/ Copy (LOW)

**Problem:** `AgenticMCP/Source/` was an older copy of `Source/`, missing 14 handler files.

**Fix:** Synced all files from `Source/` to `AgenticMCP/Source/`.

## Verification Checklist

- [x] 127 HandlerMap entries (no duplicates)
- [x] 127 tool-registry.json entries (1:1 match with HandlerMap)
- [x] 0 duplicate BindRoute calls
- [x] /mcp/status, /mcp/tools, /mcp/tool/* endpoints present
- [x] JS bridge defaults to port 9847
- [x] Tool registry loaded at startup with full metadata for all 127 tools
- [x] listCVars, removeSublevel, openAsset all wired up
- [x] No Intermediate/ in git
- [x] AgenticMCP/ copy synced with root Source/

## Branch Status

- `master` -- all fixes applied
- `phase1-metaxr` -- stale branch from 3/9, fully superseded by master, safe to delete

## UE 5.6 Oculus Fork Compatibility

All 5 version guards from the previous fix session are confirmed present and correct.
No additional 5.6 compat issues found in the new handler files (Audio, Niagara,
PixelStreaming, MetaXR, Story, DataTable, etc.) as they use stable APIs.

## EXR Screenshot Issue

No EXR references found in the screenshot handler code. The `HandleScreenshot` function
uses `FViewport::Draw()` with standard PNG/JPEG encoding. If the agent was "using EXRs
to take pictures," it was likely a hallucination caused by the empty parameter schema --
the agent could not pass format/width/height parameters, so it may have attempted
workarounds through console commands or Python execution.
