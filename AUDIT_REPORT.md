# AgenticMCP — Full Project Audit Report

**Date:** 2026-03-16
**Project:** `agentic-mcp-server` (Unreal Engine 5.6 MCP Plugin + Node.js Bridge + Python Utilities)
**Scope:** Security, Code Quality, Memory Safety, Thread Safety, Documentation Consistency, Test Coverage
**Files Analyzed:** ~90 files across C++ (27k+ LOC), JavaScript (~15k LOC), Python, Markdown, JSON

---

## Executive Summary

| Severity | C++ Plugin | JS/Node Bridge | Python/Docs | **Total** |
|----------|-----------|----------------|-------------|-----------|
| 🔴 Critical | 2 | 2 | 0 | **4** |
| 🟠 High | 3 | 0 | 0 | **3** |
| 🟡 Medium | 6 | 8 | 6 | **20** |
| 🟢 Low | 3 | 4 | 3 | **10** |
| ✅ Positive | 6 | 7 | 4 | **17** |

**Overall Risk Level: 🔴 HIGH** — 4 critical issues require immediate remediation before any deployment.

---

## 🔴 CRITICAL FINDINGS (4)

### C1. Command Injection via Python Bridge
**File:** `Source/AgenticMCP/Private/Handlers_PythonBridge.cpp`
**Layer:** C++ Plugin

```cpp
FString Command = FString::Printf(TEXT("py \"%s\""), *FilePath);
GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Command);
```

User-controlled `FilePath` is passed directly to `GEditor->Exec()` without sanitization. An attacker can inject arbitrary commands via crafted paths like `"; malicious_command; #` or `../../` traversals.

**Fix:** Validate against a whitelist of allowed directories. Use `FPaths::NormalizeFilename()` and `FPaths::IsRelative()` checks. Reject paths containing semicolons, quotes, or `..` sequences.

---

### C2. No Authentication on HTTP Server
**File:** `Source/AgenticMCP/Private/AgenticMCPServer.cpp`
**Layer:** C++ Plugin

The HTTP server on port 8080/9847 accepts all connections without any auth:
- No API key validation
- No token-based auth
- No IP whitelist
- All 200+ endpoints fully exposed to any network client

**Fix:** At minimum, bind to `127.0.0.1` only (localhost). Add an `X-API-Key` header check. For production, implement JWT or mTLS.

---

### C3. Command Injection via hzdb CLI Execution
**File:** `Tools/request-handler.js`
**Layer:** Node.js Bridge

```javascript
cliArgs.push(`--${key}=${value}`);  // Values passed directly to execFile
```

While `execFile` avoids shell expansion, unsanitized argument values could exploit `hzdb` CLI parser edge cases.

**Fix:** Implement strict allowlist validation per parameter. Add regex validation for expected formats before passing to CLI.

---

### C4. Python Code Execution with Trivially Bypassable Blocklist
**File:** `Tools/project-state-validator.js`
**Layer:** Node.js Bridge

```javascript
const dangerous = ["shutil.rmtree", "os.remove", "eval(", "exec(", ...];
```

This string-matching blocklist is trivially bypassed via:
- `__import__('os').system('...')`
- `getattr(__builtins__, 'exec')(...)`
- Base64/hex encoded payloads
- `importlib.import_module('subprocess')`

**Fix:** Replace string blocklist with AST-based analysis or proper sandboxing (RestrictedPython). Consider executing Python in a containerized environment.

---

## 🟠 HIGH FINDINGS (3)

### H1. Unresolved Git Merge Conflict — Code Won't Compile
**File:** `Source/AgenticMCP/Private/Handlers_BlueprintGraph.cpp`

```cpp
<<<<<<< HEAD
    UClass* Parent = FindObject<UClass>(nullptr, *ParentClass);
=======
    UClass* Parent = FindFirstObject<UClass>(*ParentClass, EFindFirstObjectOptions::NativeFirst);
>>>>>>> dff5884439a2782dee312ccab688904ae4de2c17
```

This file **will not compile**. The `FindFirstObject` variant is the UE 5.6-compatible approach.

---

### H2. Broken Handler — Always Returns Error
**File:** `Source/AgenticMCP/Private/Handlers_Sequences.cpp`

```cpp
FString FAgenticMCPServer::HandleListSequences(const FString& Body)
{
    {
        return MakeErrorJson(TEXT("Editor not available"));
    }
    // Unreachable code below...
```

The `listSequences` tool always returns "Editor not available" regardless of editor state. All subsequent code is dead.

---

### H3. Duplicate Route Registrations
**File:** `Source/AgenticMCP/Private/AgenticMCPServer.cpp`

Multiple routes are registered twice (likely from a bad merge): `/list_actors`, `/get_actor_properties`, and several Blueprint mutation routes. Later registrations silently override earlier ones.

---

## 🟡 MEDIUM FINDINGS (20)

### Security (7)

| ID | Finding | Location |
|----|---------|----------|
| M1 | **Path traversal** — Asset paths from clients used without `/Game/` prefix validation | Multiple C++ handlers |
| M2 | **SSRF** — `UNREAL_BRIDGE_URL` and `INFERENCE_SERVER_URL` env vars accepted without URL validation | `Tools/index.js` |
| M3 | **Global mutable state** — `plannedOperations`, `gateLog`, `sessionConfidence` are global, causing race conditions under concurrency | `Tools/confidence-gate.js` |
| M4 | **Non-atomic history updates** — Concurrent identical requests both pass "not executed" check | `Tools/idempotency-guard.js` |
| M5 | **Fail-open idempotency** — Destructive operations proceed when verification fails | `Tools/idempotency-guard.js` |
| M6 | **No rate limiting** — All mutation endpoints accept unlimited requests | `Tools/index.js`, `request-handler.js` |
| M7 | **`transcribe.py` uses `os.system()`** for pip install + accepts unvalidated `audioPath` from user | `Tools/AudioAnalysis/transcribe.py` |

### Memory Safety (2)

| ID | Finding | Location |
|----|---------|----------|
| M8 | **Inconsistent null checks** — `SpawnActor`, `StaticLoadObject`, `FindObject` returns used without null checks in some handlers | Multiple C++ handlers |
| M9 | **Weak pointer not validated** — `RefRegistry.Find()` checks entry existence but not `IsValid()` before `Get()` | `Handlers_VisualAgent.cpp` |

### Code Quality (5)

| ID | Finding | Location |
|----|---------|----------|
| M10 | **Inconsistent JSON parsing** — Some handlers validate `TSharedPtr<FJsonObject>`, others don't | Multiple C++ handlers |
| M11 | **Empty catch blocks** — Silent error swallowing masks real issues | `Tools/index.js`, `Tools/gatekeeper/llm-validator.js` |
| M12 | **Error context lost** — `catch (error) { return { message: error.message } }` loses stack traces | `Tools/lib.js` |
| M13 | **Large files** — `scene-verifier.js` (1296 lines), `quantized-inference.js` (1273 lines), `project-state-validator.js` (1205 lines) | `Tools/` |
| M14 | **Duplicated retry logic** across `lib.js` and `llm-validator.js` | `Tools/` |

### Documentation Consistency (6)

| ID | Finding | Location |
|----|---------|----------|
| M15 | **Version mismatch** — `CHANGELOG.md` says 3.0.0, `.uplugin` says 1.0.0, `VERSION` says 1.2.0, `package-lock.json` says 2.0.0 | Multiple files |
| M16 | **Tool count conflicts** — Claims range from 225 to 329 to 390 across different docs | `README.md`, `CAPABILITIES.md`, `CHANGELOG.md`, `ROADMAP.md` |
| M17 | **Stale Claude references** — `RESEARCH_ALIGNMENT.md` references removed "Claude planner" architecture (5+ occurrences) | `RESEARCH_ALIGNMENT.md` |
| M18 | **Broken doc reference** — `CAPABILITIES.md` references deleted `CLAUDE.md` file | `CAPABILITIES.md` line 317 |
| M19 | **`FULL_ENGINE_API_GAPS.md` outdated** — Claims 107 endpoints when current count is 329-390 | `planning/FULL_ENGINE_API_GAPS.md` |
| M20 | **Weak confidence threshold** — Default 0.7 means 30% uncertain operations proceed | `Tools/confidence-gate.js` |

---

## 🟢 LOW FINDINGS (10)

| ID | Finding | Location |
|----|---------|----------|
| L1 | Inconsistent error response format (some handlers return raw JSON instead of `MakeErrorJson`) | Various C++ handlers |
| L2 | Static state variables persist across editor sessions (stale references possible) | `Handlers_VisualAgent.cpp` |
| L3 | SEH wrappers are Windows-only, no cross-platform alternative | `AgenticMCPEditorSubsystem.cpp` |
| L4 | Handler method signature inconsistency (most return `FString`, some use callbacks) | `AgenticMCPServer.h` |
| L5 | `blessed` npm dependency last updated 2017, potential vulnerabilities | `Tools/package.json` |
| L6 | Hardcoded user-specific path in `full_animation_catalog.py` | `Tools/full_animation_catalog.py` line 9 |
| L7 | No checksum verification for downloaded model binaries | `models/setup-models.bat` |
| L8 | Connection manager `connect()` has no mutex — duplicate health checks possible | `Tools/lib.js` |
| L9 | Missing security-focused test suite (no path traversal, injection, or race condition tests) | `Tools/tests/` |
| L10 | `SESSION_SUMMARY_MAR15.md` references Godot pivot (confusing tangent) | Root docs |

---

## ✅ POSITIVE PATTERNS (17)

### C++ Plugin
1. **Thread-safe request queue** — HTTP → Game thread queuing via `TQueue<TSharedPtr<FPendingRequest>>` is properly implemented
2. **Crash protection** — SEH wrappers around dangerous compilation operations
3. **GC safety** — `FGCScopeGuard` used during request processing
4. **Conditional compilation** — Clean `#if WITH_NIAGARA` / `#else` pattern for optional modules
5. **Weak pointers** — `TWeakObjectPtr<AActor>` used for actor references
6. **Structured logging** — Consistent `UE_LOG` usage throughout

### Node.js Bridge
7. **Path traversal protection** — `fallback.js` properly validates with `path.resolve()` + prefix check + logging
8. **File extension whitelist** — Only `.umap`, `.uasset`, `.uexp` allowed in fallback
9. **Timeout protection** — `fetchWithTimeout()` with `AbortController` properly implemented
10. **Comprehensive validation rules** — `project-state-validator.js` has per-tool validation with regex
11. **MCP protocol compliant** — Standard SDK usage, proper `ListTools`/`CallTool` handlers
12. **Layered validation pipeline** — Rule Engine → Confidence Gate → Project State → Idempotency
13. **High test quality** — 90% coverage thresholds, good mocking infrastructure

### Python / Documentation
14. **Most Python scripts are secure** — UE Python API only, no shell execution
15. **Well-documented fix history** — `fixes/` directory with thorough root cause analysis
16. **Comprehensive regression test** — `SOH_Regression_Test.ps1` tests 32 interactions across 6 levels
17. **Good model instructions** — Worker/validator/QA prompts enforce zero-hallucination and source-truth adherence

---

## 📋 PRIORITIZED REMEDIATION PLAN

### P0 — Immediate (Before Any Use)

| # | Action | Files |
|---|--------|-------|
| 1 | **Resolve merge conflict** in `Handlers_BlueprintGraph.cpp` (use `FindFirstObject` variant) | `Handlers_BlueprintGraph.cpp` |
| 2 | **Bind HTTP server to localhost** + add API key auth | `AgenticMCPServer.cpp` |
| 3 | **Sanitize Python Bridge paths** — whitelist dirs, reject `..`, `;`, quotes | `Handlers_PythonBridge.cpp` |
| 4 | **Replace Python blocklist** with AST-based validation or RestrictedPython | `project-state-validator.js` |

### P1 — Within 1 Week

| # | Action | Files |
|---|--------|-------|
| 5 | Fix broken `HandleListSequences` — remove erroneous early return | `Handlers_Sequences.cpp` |
| 6 | Remove duplicate route registrations | `AgenticMCPServer.cpp` |
| 7 | Add null checks after all `SpawnActor`, `StaticLoadObject`, `FindObject` calls | All C++ handlers |
| 8 | Add asset path validation (require `/Game/` prefix) | All C++ handlers |
| 9 | Validate hzdb CLI arguments with allowlists | `request-handler.js` |
| 10 | Make idempotency guard fail-closed for destructive operations | `idempotency-guard.js` |
| 11 | Add rate limiting to mutation endpoints | `index.js` |

### P2 — Within 1 Month

| # | Action | Files |
|---|--------|-------|
| 12 | **Sync all versions to 3.0.0** (`.uplugin`, `VERSION`, `package.json`, C++ health endpoint) | Multiple |
| 13 | Update `RESEARCH_ALIGNMENT.md` — remove Claude references | `RESEARCH_ALIGNMENT.md` |
| 14 | Remove `CLAUDE.md` reference from `CAPABILITIES.md` | `CAPABILITIES.md` |
| 15 | Audit `tool-registry.json` and establish single source of truth for tool count | Multiple docs |
| 16 | Standardize error response format across all C++ handlers | All C++ handlers |
| 17 | Add JSON parsing validation helper | C++ utilities |
| 18 | Fix `transcribe.py` — replace `os.system()` with `subprocess.run()`, add path validation | `transcribe.py` |
| 19 | Raise default confidence threshold to 0.85 | `confidence-gate.js` |
| 20 | Use request-scoped state instead of global mutables | `confidence-gate.js`, `idempotency-guard.js` |

### P3 — Technical Debt

| # | Action | Files |
|---|--------|-------|
| 21 | Split large JS files (>1000 lines) | `scene-verifier.js`, `quantized-inference.js`, `project-state-validator.js` |
| 22 | Centralize retry logic | `lib.js`, `llm-validator.js` |
| 23 | Replace `blessed` dependency with maintained alternative | `Tools/package.json` |
| 24 | Add checksum verification for model downloads | `setup-models.bat` |
| 25 | Add security-focused test suite (injection, traversal, race conditions) | `Tools/tests/` |
| 26 | Add cross-platform alternative to SEH wrappers | `AgenticMCPEditorSubsystem.cpp` |
| 27 | Remove hardcoded user path from `full_animation_catalog.py` | `full_animation_catalog.py` |
| 28 | Archive stale docs to `docs/archive/` | `SESSION_SUMMARY_MAR15.md`, `FULL_ENGINE_API_GAPS.md` |

---

## Architecture Diagram

```
User Request (natural language)
    │
    │ MCP stdio
    │
┌───▼──────────────────────────────────────────────────────┐
│                    index.js (MCP Server)                   │
│              ListTools / CallTool handlers                 │
└───┬──────────────────────────────┬───────────────────────┘
    │                              │
┌───▼────────────────────┐  ┌─────▼─────────────────────────┐
│  Validation Pipeline    │  │   request-handler.js           │
│  ┌───────────────────┐ │  │   (Local LLM Tool Calling)     │
│  │ rule-engine.js    │ │  │   ⚠️ hzdb CLI injection risk   │
│  └─────────┬─────────┘ │  └───────────────────────────────┘
│  ┌─────────▼─────────┐ │
│  │ confidence-gate   │ │
│  │ ⚠️ global state   │ │
│  └─────────┬─────────┘ │
│  ┌─────────▼─────────┐ │
│  │ project-state-val │ │
│  │ ⚠️ weak blocklist │ │
│  └─────────┬─────────┘ │
│  ┌─────────▼─────────┐ │
│  │ idempotency-guard │ │
│  │ ⚠️ fail-open      │ │
│  └───────────────────┘ │
└───┬────────────────────┘
    │
┌───▼─────────────────────┐     ┌───────────────────────────┐
│   lib.js (HTTP Client)   │     │  fallback.js (Offline)     │
│   → UE5 Plugin           │     │  ✅ path validation        │
│   ✅ timeout protection   │     │  ✅ extension whitelist    │
└───┬─────────────────────┘     └───────────────────────────┘
    │ HTTP
┌───▼─────────────────────────────────────────────────────────┐
│              C++ Plugin (UE 5.6, port 9847)                  │
│  ⚠️ No auth  ⚠️ Path traversal  ⚠️ Command injection       │
│  ✅ Thread-safe queue  ✅ SEH protection  ✅ GC safety       │
│  44 handler files, 200+ endpoints                            │
└─────────────────────────────────────────────────────────────┘
```

---

## Version Discrepancy Summary

| Source | Version |
|--------|---------|
| `CHANGELOG.md` | **3.0.0** |
| `Tools/index.js` (MCP server declaration) | **3.0.0** |
| `Tools/package-lock.json` | **2.0.0** |
| `AgenticMCP.uplugin` | **1.0.0** |
| `VERSION` file | **1.2.0** |
| `AgenticMCPServer.cpp` (health endpoint) | **1.0.0** |

**All should be unified to 3.0.0.**

---

*Audit generated 2026-03-16. READ-ONLY analysis — no files were modified during this audit.*
