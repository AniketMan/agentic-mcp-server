# Agentic MCP Server - Airtight Audit Report

**Date:** 2026-03-14
**Scope:** Full end-to-end trace from VS Code MCP client through JS bridge to C++ plugin and back

---

## Executive Summary

The MCP bridge was connecting successfully and discovering 127 tools, but **every tool call was sent with wrong or missing parameters**. The agent appeared to be working -- it listed tools, called them, got responses -- but nothing actually happened in the editor because:

1. The tool registry had **guessed parameter names** that didn't match the C++ handler field names
2. Five handlers only read from query params (GET-style), but the MCP bridge sends POST with JSON body
3. No crash protection existed -- if a handler threw, the HTTP response never fired and the bridge hung

---

## Audit Methodology

### Layer 1: MCP Protocol (VS Code -> JS Bridge)
- **Transport:** stdio (correct for VS Code MCP)
- **Entry point:** `node Tools/index.js` via `package.json` main field
- **SDK:** `@modelcontextprotocol/sdk` 1.7.0
- **Server init:** `StdioServerTransport` -> `McpServer` with `ListToolsRequestSchema` and `CallToolRequestSchema` handlers
- **Result:** PASS. Server boots clean, 127 tools loaded.

### Layer 2: Tool Discovery (JS Bridge -> Registry -> MCP ListTools)
- **Flow:** `ListToolsRequestSchema` handler -> `fetchUnrealTools()` -> load `tool-registry.json` -> `convertToMCPSchema()` -> MCP response
- **Previous state:** Registry had guessed param names. `convertToMCPSchema()` produced valid MCP schemas but with wrong field names.
- **Result after fix:** All 127 tools produce valid MCP `inputSchema` with correct field names matching C++ handlers. 263 total parameter definitions across 93 parameterized tools.

### Layer 3: Tool Execution (MCP CallTool -> JS -> HTTP POST -> C++ -> Game Thread -> Handler)
- **Flow:** `CallToolRequestSchema` handler -> `executeLiveTool(name, args)` -> HTTP POST `http://localhost:9847/mcp/tool/{name}` with JSON body -> C++ `/mcp/tool` catch-all -> extract tool name from `RelativePath` -> queue `FPendingRequest` -> `ProcessOneRequest()` on game thread tick -> `HandlerMap[endpoint](QueryParams, Body)` -> JSON response -> HTTP response -> MCP result
- **Critical fix applied:** `/mcp/tool` POST handler now parses JSON body and merges string/number/bool fields into `QueryParams` so Params-only handlers receive data correctly.
- **Result:** PASS. All 127 tools can receive parameters via POST JSON body regardless of handler signature.

### Layer 4: Handler Parameter Cross-Reference
- **Method:** Automated script (`audit-handlers.mjs`) that:
  - Parses `HandlerMap.Add` entries to determine each handler's dispatch signature (Params-only vs Body vs both)
  - Parses each handler's C++ implementation to extract actual field names from `GetStringField`, `GetNumberField`, `Params.Contains`, etc.
  - Cross-references against `tool-registry.json` parameter definitions
- **Previous state:** 166 mismatches (C++ field names not in registry), 56 warnings (registry params not found in C++ parsing), 5 critical errors (Params-only handlers)
- **Result after fix:** 0 errors, 0 warnings, 5 informational notes (Params-only handlers covered by JSON-to-QueryParams merge)

### Layer 5: Error Handling
- **Handler crash:** `ProcessOneRequest` now has SEH crash protection on Windows. If a handler throws, an error JSON response is sent instead of hanging.
- **Unknown endpoint:** Returns `{"error": "Unknown endpoint: {name}"}` with HTTP 200 (JSON error, not HTTP error -- correct for MCP)
- **Invalid JSON body:** `ParseBodyJson` returns null, handlers return `{"error": "Invalid JSON body"}`
- **Connection drop:** JS bridge has 30s timeout per request, returns MCP error on timeout
- **Result:** PASS. No silent failure paths remain.

---

## Fixes Applied (This Session)

| # | Fix | Severity | Files Changed |
|---|-----|----------|---------------|
| 1 | **Rebuilt tool-registry.json from C++ source** | CRITICAL | `Tools/tool-registry.json` |
| 2 | **JSON-to-QueryParams merge in /mcp/tool handler** | CRITICAL | `Source/.../AgenticMCPServer.cpp` |
| 3 | **SEH crash protection in ProcessOneRequest** | HIGH | `Source/.../AgenticMCPServer.cpp` |
| 4 | **Added missing addNode params** (delegateName, ownerClass, duration) | MEDIUM | `Tools/tool-registry.json` |
| 5 | **Fixed executePython required flags** (script/file are mutually exclusive, neither individually required) | LOW | `Tools/tool-registry.json` |

---

## Audit Tools Added

Three audit scripts are now in `Tools/` for ongoing validation:

- **`rebuild-registry.mjs`** - Regenerates `tool-registry.json` from C++ source. Run after adding new handlers.
- **`audit-handlers.mjs`** - Cross-references every handler's C++ field parsing against registry params. Run before any release.
- **`audit-test.mjs`** - Validates every registry entry produces a valid MCP `inputSchema`. Run after registry changes.

---

## Final Audit State

```
Handlers checked: 127
Registry tools:   127
Errors:           0
Warnings:         0
MCP schema valid: 127/127
```

---

## Maintenance Protocol

When adding a new C++ handler:

1. Add `HandlerMap.Add(TEXT("toolName"), ...)` in `AgenticMCPServer.cpp`
2. Run `node Tools/rebuild-registry.mjs` to regenerate the registry
3. Manually review the new entry's description in `tool-registry.json`
4. Run `node Tools/audit-handlers.mjs` to confirm zero errors
5. Run `node Tools/audit-test.mjs` to confirm valid MCP schema
