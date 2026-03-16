# TryGetArrayField ANSI String Deprecation

From Meta's Oculus UE 5.5 fork:
> Warning C4996: 'FJsonObject::TryGetArrayField': Passing an ANSI string to TryGetArrayField has been deprecated outside of UTF-8 mode.

This is a WARNING, not an error. The calls using TEXT() macro should be fine since TEXT() produces wide strings.
All 135 instances in the codebase use TEXT() macro, so this should not cause compilation failures.

Status: NOT a blocker. These are warnings only.
