# Code Review — Security Critical Fixes
**Date:** 2026-03-17
**Reviewer:** Devmate (parallel general + expert code reviewers)
**Scope:** Uncommitted changes — 4 critical audit fixes across C++, JavaScript

---

## Changes Reviewed

| # | Fix | Files |
|---|-----|-------|
| 1 | Merge conflict resolution (15 conflicts, 10 files) | `Handlers_BlueprintGraph.cpp`, `Handlers_VCam.cpp`, `Handlers_WaterSystem.cpp`, `Handlers_Composure.cpp`, `Handlers_ControlRig.cpp`, `Handlers_EnhancedInput.cpp`, `Handlers_Level.cpp`, `Handlers_MetaHuman.cpp`, `Handlers_PCG.cpp`, `Handlers_SceneHierarchy.cpp`, `AgenticMCPServer.cpp` |
| 2 | HTTP server authentication (localhost-only + API key) | `AgenticMCPServer.cpp` (lines 2267–2424) |
| 3 | Path sanitization for Python bridge | `Handlers_PythonBridge.cpp` (full rewrite) |
| 4 | AST-tokenization Python sandbox replacing string blocklist | `Tools/project-state-validator.js` (lines 1025–1168) |

---

## Findings Summary

| Severity | Count | Status |
|----------|-------|--------|
| 🔴 Error | **1** | Open — needs fix |
| 🟡 Warning | **5** | Open — should fix |
| ℹ️ Info | **1** | Pre-existing, not caused by changes |

---

## 🔴 ERROR — Auth Bypass on Direct Routes

**File:** `Source/AgenticMCP/Private/AgenticMCPServer.cpp`, lines 2427–2604
**Found by:** Both reviewers independently

Six routes are bound with direct lambdas that **never call `AuthenticateRequest`**:

| Route | Line | Risk |
|-------|------|------|
| `/mcp/tool/*` | 2528 | **CRITICAL** — executes ANY tool without auth, including `pythonExecString`, `deleteActor`, `executeConsole` |
| `/api/shutdown` | 2468 | **HIGH** — shuts down the engine without auth |
| `/api/capabilities` | 2445 | MEDIUM — enumerates all endpoints |
| `/mcp/tools` | 2508 | MEDIUM — enumerates all tools |
| `/mcp/status` | 2490 | LOW — leaks internal state |
| `/api/health` | 2427 | LOW — leaks blueprint/map counts |

**Root cause:** The `QueuedHandler` lambda correctly captures `ApiKey` and calls `AuthenticateRequest`, but the direct-response route lambdas (especially `/mcp/tool/*`) only capture `[this]` and skip auth entirely. The `/mcp/tool/*` route is the primary MCP tool execution path — **all authentication is effectively bypassable**.

**Recommended fix:** Add `AuthenticateRequest` call to every direct-response route lambda. Capture `ApiKey` in each lambda closure. At minimum, protect `/mcp/tool/*` and `/api/shutdown`.

```cpp
// Example fix for /mcp/tool/* route:
Router->BindRoute(FHttpPath(TEXT("/mcp/tool")), EHttpServerRequestVerbs::VERB_POST,
    FHttpRequestHandler::CreateLambda(
        [this, ApiKey](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
        {
            if (!AuthenticateRequest(Request, OnComplete, ApiKey))
                return true;
            // ... existing tool dispatch logic ...
        }));
```

---

## 🟡 WARNING #1 — `open('file', 'r+')` Bypasses Write-Detection Regex

**File:** `Tools/project-state-validator.js`, line 1093

The regex `/\bopen\s*\([^)]*['"]\s*[wax+]\s*['"]/i` only checks the **first character** of the mode string against `[wax+]`. The mode `r+` starts with `r` (not in the class), so `open('file', 'r+')` passes — yet `r+` allows both reading **and writing**.

**Recommended fix:** Match `wax+` **anywhere** in the mode string:
```javascript
/\bopen\s*\([^)]*['"][^'"]*[wax+][^'"]*['"]/i
```

---

## 🟡 WARNING #2 — `globals()` / `vars()` Not Blocked in Python Sandbox

**File:** `Tools/project-state-validator.js`, lines 1064–1096

The sandbox blocks `__globals__` (attribute access) but not the `globals()` built-in function. Bypass:
```python
globals()['__builtins__']['__import__']('os').system('cmd')
```
Similarly, `vars(__builtins__)['__import__']` bypasses the `__builtins__.__dict__` pattern.

**Recommended fix:** Add patterns:
```javascript
/\bglobals\s*\(\s*\)/i,
/\bvars\s*\(\s*(?:__builtins__|builtins)/i,
```

---

## 🟡 WARNING #3 — Fixed Temp File Path Race Condition + Symlink Risk

**File:** `Source/AgenticMCP/Private/Handlers_PythonBridge.cpp`, lines 187–192

`HandlePythonExecString` always writes to `Saved/MCP_TempScript.py`. While requests are game-thread serialized (mitigating the race), a local attacker could pre-create a symlink at that path pointing to a sensitive file, causing the server to overwrite it with attacker-controlled content. The file also persists on disk containing the last executed script.

**Recommended fix:**
```cpp
FString TempPath = FPaths::ConvertRelativePathToFull(
    FPaths::ProjectSavedDir() / FString::Printf(TEXT("MCP_%s.py"), *FGuid::NewGuid().ToString()));
// ... execute ...
IFileManager::Get().Delete(*TempPath); // Clean up after execution
```

---

## 🟡 WARNING #4 — Path Sanitization Misses Windows-Specific Vectors

**File:** `Source/AgenticMCP/Private/Handlers_PythonBridge.cpp`, lines 28–94

Not handled:
- **Windows Alternate Data Streams:** `script.py::$DATA` resolves to `script.py` but `.py` extension check may not see past `::$DATA`
- **8.3 short filenames:** `C:\PROGRA~1\...` may resolve outside allowed directories
- **Case sensitivity:** `FString::StartsWith` is case-sensitive by default, but Windows paths are case-insensitive
- **UNC paths:** `\\server\share\script.py` — no explicit block

**Recommended fix:**
```cpp
// Block ADS
if (NormalizedPath.Contains(TEXT("::")))
{
    OutError = TEXT("Alternate data stream syntax is not allowed");
    return false;
}

// Block UNC paths
if (NormalizedPath.StartsWith(TEXT("\\\\")))
{
    OutError = TEXT("UNC paths are not allowed");
    return false;
}

// Use case-insensitive prefix comparison on Windows
bool bAllowed = NormalizedPath.StartsWith(ProjectDir, ESearchCase::IgnoreCase)
    || NormalizedPath.StartsWith(EngineDir, ESearchCase::IgnoreCase)
    || NormalizedPath.StartsWith(PluginDir, ESearchCase::IgnoreCase)
    || NormalizedPath.StartsWith(SavedDir, ESearchCase::IgnoreCase);
```

---

## 🟡 WARNING #5 — API Key Comparison Not Timing-Safe

**File:** `Source/AgenticMCP/Private/AgenticMCPServer.cpp`, line 2343

```cpp
if (ProvidedKey != ConfiguredApiKey)
```

Standard string inequality short-circuits on first mismatch, enabling timing side-channel attacks. Low practical risk on localhost, but a defense-in-depth gap.

**Recommended fix:** Implement constant-time comparison:
```cpp
static bool TimingSafeCompare(const FString& A, const FString& B)
{
    if (A.Len() != B.Len()) return false;
    uint8 Result = 0;
    for (int32 i = 0; i < A.Len(); ++i)
    {
        Result |= (uint8)(A[i] ^ B[i]);
    }
    return Result == 0;
}
```

---

## ℹ️ INFO — Pre-existing Issues (Not Caused by Changes)

The expert reviewer identified command injection in `Handlers_VCam.cpp` (line 165) and `Handlers_WaterSystem.cpp` (line 166) where user-supplied strings are interpolated into Python code without sanitization. These are **pre-existing** issues not introduced by the current changes.

Additionally, several Python sandbox bypass vectors are inherent limitations of regex-based sandboxing:
- String concatenation: `getattr(__builtins__, 'ex'+'ec')`
- `bytes()` constructor: `bytes([111,115]).decode()` → `"os"`
- `mro()` method call vs `__mro__` attribute

The validator is a defense-in-depth layer. The real security boundary should be server-side auth (which is why Finding #1 is the highest priority).

---

## Positive Observations

- ✅ Merge conflicts correctly resolved — all 15 conflicts use UE 5.6 `FindFirstObject` API
- ✅ Path sanitization is thorough for common attack vectors (traversal, injection chars, extension enforcement, directory whitelisting)
- ✅ Python sandbox is a massive improvement over the original 10-entry string blocklist (4-phase scanner with normalization, regex, obfuscation detection, and size limits)
- ✅ Auth architecture is clean — env-var-based configuration, clear logging, proper JSON error responses
- ✅ `SanitizePythonArg` correctly strips double-quote to prevent breaking out of the command string context
- ✅ Localhost-only binding is a sensible default for a development tool

---

## Priority Order for Remaining Work

1. **P0:** Add `AuthenticateRequest` to `/mcp/tool/*` and `/api/shutdown` routes
2. **P1:** Fix `open()` regex and add `globals()`/`vars()` to sandbox patterns
3. **P1:** Add Windows-specific path checks (ADS, UNC, case-insensitive prefix)
4. **P2:** Use unique temp file path with cleanup in `HandlePythonExecString`
5. **P3:** Implement timing-safe API key comparison

---

*Review completed 2026-03-17 01:35 UTC. Two parallel reviewers (general + expert). No files modified.*
