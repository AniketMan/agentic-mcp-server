# Batch Script Fix Log

## Date
2026-03-15

## Issue 1: Instant Crash on Launch

**Symptom:** All `.bat` files closed instantly with no visible output when double-clicked on Windows.

**Root Cause:** The files were written on a Linux sandbox with Unix line endings (LF, `0x0a`). Windows `cmd.exe` requires CRLF line endings (`0x0d 0x0a`) to parse batch scripts. When it encounters LF-only line breaks, the parser fails silently and the window closes.

**Fix:** Converted all `.bat` files to CRLF using `unix2dos`. Added `.gitattributes` with `*.bat binary` to force git to store the files byte-for-byte with CRLF in the blob, preventing any line-ending conversion on checkout regardless of the user's git config.

## Issue 2: `was unexpected at this time` Parse Error

**Symptom:** After fixing line endings, `setup-models.bat` crashed with the error: `was unexpected at this time.`

**Root Cause:** Batch script `if` blocks use parentheses `()` for grouping. When `%VARIABLE%` expansion occurs at parse time and the variable contains parentheses (as URLs do), the parser interprets those parentheses as block delimiters, breaking the syntax.

Example of the failing pattern:
```batch
if %ERRORLEVEL% neq 0 (
    echo URL: %LLAMA_BIN_URL%
)
```
If `%LLAMA_BIN_URL%` contains `(` or `)`, the parser sees mismatched block delimiters.

**Fix:** Replaced `%VARIABLE%` with `!VARIABLE!` (delayed expansion via `setlocal enabledelayedexpansion`). Delayed expansion evaluates variables at runtime, after the `if` block has already been parsed, so parentheses in values do not interfere with block syntax.

## Issue 3: `--system-prompt-file` Invalid Argument

**Symptom:** llama-server launched but immediately exited with: `error: invalid argument: --system-prompt-file`

**Root Cause:** The `--system-prompt-file` flag was removed from `llama-server` in llama.cpp PR #9857 (October 2024). The feature was deprecated because it was incompatible with chat templates used by instruction-tuned models. System prompts must now be sent per-request via the `/v1/chat/completions` endpoint as a message with `"role": "system"`.

**Fix:**
1. Removed `--system-prompt-file` and all related logic from `start-validator.bat`, `start-planner.bat`, and `start-qa.bat`. The servers now launch cleanly with just model, port, GPU, context, and thread arguments.
2. Updated `Tools/gatekeeper/llm-validator.js` to:
   - Load all three instruction MDs (`validator.md`, `worker.md`, `qa-auditor.md`) into memory at startup
   - Switch from the `/completion` endpoint to `/v1/chat/completions`
   - Inject the instruction MD as a `system` role message in the request body
   - Extract confidence scores from the OpenAI-compatible `logprobs` response field instead of the legacy `completion_probabilities` field
3. Updated `models/README.md` to document the per-request injection pattern.

## Key Takeaway

When writing batch scripts from a non-Windows environment:
1. Always ensure CRLF line endings (use `*.bat binary` in `.gitattributes`)
2. Always use delayed expansion (`!var!`) instead of immediate expansion (`%var%`) inside `if/for` blocks when variables may contain special characters
3. Always verify CLI flags against the exact release version being downloaded, as flags get deprecated
