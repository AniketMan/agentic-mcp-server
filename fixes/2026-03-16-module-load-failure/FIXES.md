# Fix: Plugin Module Load Failure

**Date:** 2026-03-16
**Scope:** Build.cs architecture rewrite + preprocessor guards on 3 handler files

## Problem

The plugin compiled successfully but failed to load at runtime with:

> Plugin 'AgenticMCP' failed to load because module 'AgenticMCP' could not be loaded.

This is a **DLL import failure**. The compiled plugin DLL had hard linker dependencies on ~20 optional plugin modules (OculusXR, Niagara, Composure, Water, ControlRig, etc.). If any of those plugins were not enabled in the project, the OS could not resolve the DLL imports and the entire plugin refused to load.

## Root Cause

All optional modules were listed as `PrivateDependencyModuleNames` in Build.cs. This creates a compile-time and link-time dependency, meaning the resulting DLL contains import entries for those modules' DLLs. At runtime, the OS loader fails if any referenced DLL is missing.

## Solution

### Build.cs Rewrite

The Build.cs was completely rewritten with a three-tier dependency architecture:

**Tier 1 -- Core Dependencies (always available)**

Engine modules that ship with every UE editor build: Core, CoreUObject, Engine, UnrealEd, AssetRegistry, LevelSequence, MovieScene, etc. These remain as hard `PrivateDependencyModuleNames`.

**Tier 2 -- Guarded Dependencies (direct type usage)**

Niagara and OculusXR handlers use direct header includes and concrete types (`UNiagaraComponent`, `UOculusXRFunctionLibrary`). These are conditionally added using `GetModuleDirectory()` to check existence at build time, with preprocessor defines (`WITH_NIAGARA`, `WITH_OCULUSXR`) controlling compilation.

**Tier 3 -- Runtime-Only Dependencies (no type usage)**

All other optional modules (ControlRig, Composure, Water, LiveLink, GAS, MassEntity, etc.) use only `FModuleManager::IsModuleLoaded()` and `FindFirstObject<UClass>()` at runtime. They are conditionally added as dependencies only if `GetModuleDirectory()` finds them. If absent, the handler code still compiles and returns graceful error messages.

### Handler File Changes

| File | Change |
|------|--------|
| Handlers_Niagara.cpp | Wrapped entire file in `#if WITH_NIAGARA` / `#else` with 16 stub functions |
| Handlers_MetaXR.cpp | Wrapped in `#if WITH_OCULUSXR` / `#else` with 10 stub functions |
| Handlers_MetaXRAudioHaptics.cpp | Wrapped in `#if WITH_OCULUSXR` / `#else` with 6 stub functions |

When the corresponding plugin is not available, stub implementations return a JSON error message telling the user to enable the plugin. Routes remain registered so MCP clients get a clear error instead of a 404.

### Key API Used

`GetModuleDirectory(string ModuleName)` is a method on the `ModuleRules` base class that returns the module's directory path if it exists, or an empty string if not. This is the official UE mechanism for conditional module detection in Build.cs files.

## Verification

After this fix, the plugin DLL will only import modules that actually exist in the engine build. Missing optional plugins result in graceful runtime error messages instead of DLL load failures.
