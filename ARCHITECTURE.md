# AgenticMCP Architecture: Direct Inference with Native Tool Calling

## Overview

AgenticMCP uses **Direct Inference** to route natural language requests to Unreal Engine tool calls. A single local Llama model receives the request, selects the correct tool from 390 available functions using native tool calling, and executes it through a deterministic validation stack. No external LLM. No planner. No intermediary. The model runs locally on your GPU via llama.cpp.

## System Architecture

```text
+-------------------------------------------------------------------+
|                   User Request (Natural Language)                  |
|                                                                   |
| "Add a grab component to the phone actor"                         |
+-------------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------------+
|                   Layer 1: Request Handler                        |
|                   (request-handler.js)                            |
|                                                                   |
| Builds conversation with system prompt + context + tool defs.     |
| Sends to local Llama via /v1/chat/completions with native tools.  |
| Receives structured tool_calls. Loops until request_complete.     |
+-------------------------------------------------------------------+
                              |
                     Native tool_calls
                              |
+-------------------------------------------------------------------+
|                   Layer 2: Validation Stack                       |
|                   (Deterministic, No LLM)                         |
|                                                                   |
| 1. Confidence Scorer: Structural integrity of the tool call.      |
| 2. Rule Engine: Params match registry, workflow rules enforced.   |
| 3. Project State Validator: Cross-checks against live editor.     |
| 4. Idempotency Guard: Skips if action already done.               |
+-------------------------------------------------------------------+
                              |
                     Validated tool call
                              |
+-------------------------------------------------------------------+
|                   Layer 3: C++ Plugin                             |
|                   (Unreal Engine 5.6, port 9847)                  |
|                                                                   |
| Executes the tool call on the game thread.                        |
| Returns deterministic success/error with real engine data.        |
+-------------------------------------------------------------------+
                              |
                     Execution result
                              |
+-------------------------------------------------------------------+
|                   Feedback Loop                                   |
|                                                                   |
| Result is sent back to the Llama model as a tool_result message.  |
| Model decides: call another tool, or call request_complete.       |
| Loop continues until done or max iterations (10) reached.         |
+-------------------------------------------------------------------+
```

## Layer 1: Request Handler

The Request Handler is the entry point. It receives a natural language request from the user and manages the full inference-execution loop.

**How it works:**

1. Loads system prompt from `models/instructions/worker.md`
2. Loads context: source truth, user context, engine docs hints
3. Converts all 390 tools from `tool-registry.json` into OpenAI-compatible function definitions
4. Sends the conversation to the local Llama server at `/v1/chat/completions` with `tools` parameter
5. The model responds with `tool_calls` (native structured output, not free-form JSON)
6. Each tool call is validated and executed
7. Results feed back as `tool` role messages for the next iteration
8. When the model calls `request_complete`, the loop ends

**Native Tool Calling** means the model's output is structurally guaranteed to be valid JSON with the correct function name and argument format. No regex extraction. No JSON parsing heuristics. The model's tokenizer enforces the schema.

## Layer 2: Validation Stack

Every tool call passes through four deterministic checks before execution. No LLM is involved in validation.

### Confidence Scorer

Scores the structural integrity of each tool call on a 0-1 scale:
- Does the function name exist in the registry?
- Do the arguments parse as valid JSON?
- Are there placeholder values (UNKNOWN, TODO)?
- Does the call have at least one argument?

Calls below the confidence threshold (default 80%) are rejected and the error is fed back to the model for self-correction.

### Rule Engine (`rule-engine.js`)

Zero-latency, model-free validation:
- Tool exists in registry
- Required parameters are present
- Parameter types match (string, number, boolean)
- No unknown parameters
- Workflow rules enforced (snapshot before mutation, compile after mutation)
- Dependency ordering respected
- GUID references resolved from prior steps

### Project State Validator

Cross-checks the tool call against the live editor state:
- Does the target Blueprint/Actor/Asset actually exist?
- Are the pin names valid?
- Is the level loaded?

Uses read-only tools to query the engine before allowing mutations.

### Idempotency Guard

Prevents duplicate operations:
- If the exact same tool call with the same parameters was already executed successfully, skip it
- Tracks execution history per session

## Layer 3: C++ Plugin

The plugin runs inside UE 5.6 (Oculus fork, `oculus-5.6.1-release`). It exposes 390 HTTP endpoints at `http://localhost:9847/api/<endpoint>`. Each endpoint maps to a specific editor operation.

The plugin processes all requests on the game thread to avoid concurrency issues with the UEdGraph API. Responses include real engine data (node GUIDs, pin names, asset paths, compilation errors) that feed back into the inference loop.

## Context System

The Worker model's context window is populated with project-specific information at inference time.

### Context Priority (highest to lowest)

1. **Tool results** from previous calls in the current session
2. **Source truth** (`reference/source_truth/`) -- project-specific asset paths, Blueprint names, level names, scene mappings
3. **Tool definitions** -- the 390 function definitions with exact parameter names, types, and descriptions
4. **User context** (`reference/user_context/`) -- additional documentation provided by the user (Meta dev docs, scripts, roadmaps)
5. **Engine documentation** -- UE docs available at `C:\UE56\Engine\Documentation\`
6. **Tool-specific context** (`Tools/contexts/`) -- detailed API docs loaded on-demand based on which tool is being called

### Source Truth

The `reference/source_truth/` folder contains machine-readable project state:
- Asset paths and Blueprint names
- Level names and scene order
- Data table schemas
- Filesystem structure

The more complete this folder is, the fewer read-only queries the model needs to make before executing mutations.

### User Context

The `reference/user_context/` folder contains user-provided documentation:
- Meta developer docs
- Project roadmaps and scripts
- Custom rules or constraints

These files are **read-only** and are never modified by the system. User context takes priority over model inference when there is a conflict.

## Self-Correction

When a tool call fails, the error message is sent back to the model as a `tool` role message. The model reads the error, adjusts its approach, and tries again. This loop continues for up to 10 iterations (configurable via `MAX_REQUEST_ITERATIONS`).

Common self-correction patterns:
- Wrong asset path -> model calls `search` or `listActors` to find the correct path -> retries
- Wrong pin name -> model calls `getPinInfo` to get the real pin names -> retries
- Missing snapshot -> model calls `snapshotGraph` first -> retries the mutation

## Local Inference

All inference runs locally via llama.cpp. No external API calls. No cloud dependencies.

### Model Configuration

| Role | Model | Port | VRAM |
|------|-------|------|------|
| Worker | Llama 3.1 8B (Q4_K_M) | 8081 | ~5 GB |
| Validator | Llama 3.2 3B (Q4_K_M) | 8080 | ~2 GB |
| QA Auditor | Llama 3.2 3B (Q4_K_M) | 8082 | ~2 GB |
| Spatial | Cosmos Reason2 2B | 8083 | ~2 GB |

The Worker is the primary model. Validator, QA Auditor, and Spatial Reasoner are optional auxiliary models for additional verification.

### llama.cpp Requirements

The Worker server must be started with `--jinja` flag to enable native tool calling:

```bash
llama-server -m models/llama-3.1-8b-q4_k_m.gguf --port 8081 -ngl 99 --jinja
```

The `--jinja` flag enables the chat template to process the `tools` parameter in chat completion requests and produce structured `tool_calls` in responses.

## Offline Fallback

When the UE editor is not running, the system falls back to a Python binary injector that reads and modifies `.umap` files directly. This supports a subset of read operations (level data, actor inspection) and Blueprint paste text generation. The fallback is enabled via `AGENTIC_FALLBACK_ENABLED=true`.
