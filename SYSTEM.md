# SYSTEM.md: AgenticMCP System Configuration

This document defines the operational rules for the AgenticMCP system. The system uses **direct inference** with native tool calling. There is no external planner. A single local Llama model handles request interpretation, tool selection, and multi-step execution.

## System Flow

```
User Request (natural language)
    -> Request Handler (request-handler.js)
    -> Local Llama (native tool calling via llama.cpp)
    -> Validation Stack (rule engine, confidence gate, project state, idempotency)
    -> C++ Plugin (UE 5.6, port 9847)
    -> Result feeds back to Llama for next step
    -> Loop until request_complete or max iterations
```

## Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `UNREAL_MCP_URL` | `http://localhost:9847` | C++ plugin HTTP endpoint |
| `WORKER_URL` | `http://localhost:8081` | llama.cpp Worker server |
| `VALIDATOR_URL` | `http://localhost:8080` | llama.cpp Validator server (optional) |
| `QA_URL` | `http://localhost:8082` | llama.cpp QA Auditor server (optional) |
| `SPATIAL_URL` | `http://localhost:8083` | Cosmos Reason2 Spatial server (optional) |
| `CONFIDENCE_THRESHOLD` | `0.80` | Minimum confidence to execute a tool call |
| `MAX_REQUEST_ITERATIONS` | `10` | Maximum inference-execution loops per request |
| `MCP_REQUEST_TIMEOUT_MS` | `30000` | Per-request HTTP timeout (ms) |
| `MCP_ASYNC_ENABLED` | `true` | Enable async task queue |
| `AGENTIC_FALLBACK_ENABLED` | `true` | Enable offline binary injector |
| `AGENTIC_PROJECT_ROOT` | (auto-detect) | UE project root for fallback |

## Context Sources

The Worker model receives context from these sources, in priority order:

1. **Tool results** from previous calls in the current session (highest priority)
2. **Source truth** (`reference/source_truth/`) -- project-specific data
3. **Tool definitions** -- 390 function definitions from `tool-registry.json`
4. **User context** (`reference/user_context/`) -- user-provided documentation
5. **Engine documentation** -- UE docs at the engine install path
6. **Tool-specific context** (`Tools/contexts/`) -- domain-specific API docs

## Workflow Rules

These rules are enforced by the deterministic Rule Engine, not by the LLM:

1. **Snapshot before mutation.** Any tool call that modifies a Blueprint must be preceded by a `snapshotGraph` call.
2. **Compile after mutation.** Any Blueprint modification sequence must end with `compileBlueprint`.
3. **Exact tool names.** Tool names must match `tool-registry.json`. The Rule Engine rejects unknown tools.
4. **Exact parameter names.** Parameter names must match the registry schema. Unknown parameters are flagged.
5. **Type checking.** Parameter types (string, number, boolean) are validated against the registry.
6. **Dependency ordering.** Steps that depend on prior results must wait for those results.
7. **GUID resolution.** GUID references from prior steps must be captured before use.

## MCP Tools

The system exposes these MCP tools to external clients (Claude, Cursor, Manus, etc.):

| Tool | Description |
|------|-------------|
| `execute_request` | Send a natural language request. The system handles tool selection, validation, and execution autonomously. |
| All 390 UE tools | Direct tool access for clients that want to bypass inference and call tools directly. |

## File Structure

```
agentic-mcp-server/
  Tools/
    index.js              -- MCP bridge entry point
    request-handler.js    -- Native tool calling inference loop
    tool-registry.json    -- 390 tool definitions
    lib.js                -- Shared utilities
    gatekeeper/
      rule-engine.js      -- Deterministic validation
      llm-validator.js    -- Worker inference + confidence scoring
      dispatcher.js       -- Step execution
      technique-selector.js -- Routing logic
    contexts/             -- Tool-specific API documentation
  models/
    instructions/
      worker.md           -- Worker system prompt
      validator.md        -- Validator system prompt (optional)
      qa-auditor.md       -- QA Auditor system prompt (optional)
      spatial-reasoner.md -- Spatial Reasoner system prompt (optional)
    start-worker.bat      -- Start llama.cpp Worker server
    start-all.bat         -- Start all model servers
  reference/
    source_truth/         -- Project-specific asset data
    user_context/         -- User-provided documentation
  Plugins/
    AgenticMCP/           -- UE 5.6 C++ plugin source
```
