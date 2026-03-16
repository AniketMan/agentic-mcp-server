# Fix: ANY_PACKAGE Removal + ControlRig Syntax Errors + EditorStyle Cleanup

**Date:** 2026-03-15
**Scope:** 8 handler files + Build.cs

## Problem

1. **`ANY_PACKAGE_COMPAT`** is not a real UE macro. It was used in 10 places across 5 files and would cause immediate compilation failure.
2. **`ANY_PACKAGE`** (the real macro) was deprecated in UE 5.1 and removed in later versions. Used in 3 places across 2 files.
3. **`Handlers_ControlRig.cpp`** had multiple syntax errors:
   - Line 23-27: Orphaned braces with no `if` condition (missing `if (!GEditor)`)
   - Line 77: `ANY_PACKAGE_COMPAT` usage
   - Line 215-234: Extra closing brace causing mismatched blocks
   - Lines 290, 352: `GEditor->Exec()` followed by orphaned `{}` blocks instead of proper control flow
4. **`EditorStyle`** module listed in Build.cs but never used in any source file. This module was deprecated in UE 5.1 (replaced by `FAppStyle`).

## Solution

### ANY_PACKAGE / ANY_PACKAGE_COMPAT Replacement

All 13 instances replaced with `FindFirstObject<T>(Name, EFindFirstObjectOptions::NativeFirst)`.

This is the official UE 5.1+ replacement for `FindObject<T>(ANY_PACKAGE, Name)`. It searches all loaded packages for the first object matching the name, with native classes prioritized.

For Composure and ControlRig handlers, a local `FindClassByName()` helper was added with a `TObjectIterator` fallback for robustness.

| File | Instances | Types |
|------|-----------|-------|
| Handlers_Composure.cpp | 3 | UClass (CompositingElement, etc.) |
| Handlers_ControlRig.cpp | 1 | UClass (ControlRigBlueprintFactory) |
| Handlers_EnhancedInput.cpp | 2 | UClass (InputActionFactory, InputMappingContextFactory) |
| Handlers_VCam.cpp | 2 | UClass (VCamComponent) |
| Handlers_WaterSystem.cpp | 2 | UClass (WaterBody, WaterBodyCustom, etc.) |
| Handlers_PCG.cpp | 1 | UClass (NodeClass) |
| Handlers_BlueprintGraph.cpp | 2 | UClass (ParentClass), UFunction (FunctionName) |

### ControlRig Syntax Fixes

Complete rewrite of `Handlers_ControlRig.cpp` to fix:
- Added proper `if (!GEditor)` guard
- Fixed all brace mismatches in `HandleControlRigAddControl`, `HandleControlRigAddBone`, `HandleControlRigSetupIK`
- Consistent indentation (tabs)
- Added `FindClassByName_ControlRig()` helper

### Build.cs Cleanup

Removed `"EditorStyle"` from `PrivateDependencyModuleNames` — unused in any source file, and the module was deprecated in UE 5.1.

## Verification

- Zero remaining `ANY_PACKAGE` or `ANY_PACKAGE_COMPAT` references in source (only in comments)
- All 61 handler files pass brace balance check
- Zero remaining `SpawnActor` pointer-to-FTransform calls
