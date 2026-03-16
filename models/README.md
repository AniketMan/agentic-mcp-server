# Local Inference Models

This folder contains everything needed to run the local LLM inference layer for AgenticMCP v3.0.

## Architecture

No external LLM dependencies. No Claude. No OpenAI. Fully local.

The system uses three specialized inference servers, each running a Meta Llama model via llama.cpp:

| Role | Model | Port | GGUF File | Size | Purpose |
|------|-------|------|-----------|------|---------|
| Validator | Llama 3.2 3B Instruct | 8080 | Llama-3.2-3B-Instruct-Q4_K_M.gguf | ~2.0 GB | Validates plan step parameters against tool registry and source truth |
| Worker | Llama 3.1 8B Instruct | 8081 | Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf | ~4.9 GB | Interprets natural language requests, selects tools, fills parameters, self-corrects |
| QA Auditor | Llama 3.2 3B Instruct | 8082 | Llama-3.2-3B-Instruct-Q4_K_M.gguf | ~2.0 GB | Audits project state for broken references, orphaned assets, placeholders |

The Validator and QA Auditor share the same GGUF file. llama.cpp memory-maps it, so two instances share the weights in RAM. Peak VRAM usage is approximately 5.5 GB (the 8B model is the largest single load).

## Setup

Run the setup script once to download llama.cpp and all three models:

```
setup-models.bat
```

This downloads approximately 7.3 GB total. Files are saved to this folder and never need to be downloaded again.

## Usage

Start all three servers:

```
start-all.bat
```

Or start them individually:

```
start-validator.bat     :: port 8080
start-worker.bat        :: port 8081
start-qa.bat            :: port 8082
```

The request handler (`Tools/request-handler.js`) connects to the Worker server. The gatekeeper modules connect to the Validator and QA Auditor as needed.

## Instruction Injection

Each server loads its system prompt from `instructions/` per-request:

| Server | Instruction File |
|--------|-----------------|
| Validator | `instructions/validator.md` |
| Worker | `instructions/worker.md` |
| QA Auditor | `instructions/qa-auditor.md` |

The system prompt is injected per-request via `/v1/chat/completions` with the instruction MD as the `system` role message and the task context as the `user` role message.

Note: The `--system-prompt-file` flag was removed from llama-server in PR #9857. System prompts must be sent per-request.

## How It Connects

```
User (natural language request)
    |
    v
Request Handler (Tools/request-handler.js)
    |
    | 1. Sends request to Worker for tool call inference
    | 2. Validates via rule engine, confidence gate, project state validator
    | 3. Checks idempotency (skip if already done)
    v
llama.cpp servers (this folder)
    |
    | JSON tool payload (confidence-gated)
    v
C++ Plugin (UE5.6 editor, port 9847)
    |
    | Result feeds back to Worker for next step
    v
Loop until done
```

## Environment Variables

Override default ports if needed:

| Variable | Default | Description |
|----------|---------|-------------|
| `LLAMA_VALIDATOR_URL` | `http://localhost:8080` | Validator server endpoint |
| `LLAMA_WORKER_URL` | `http://localhost:8081` | Worker server endpoint |
| `LLAMA_QA_AUDITOR_URL` | `http://localhost:8082` | QA Auditor server endpoint |
| `CONFIDENCE_THRESHOLD` | `0.80` | Minimum structural confidence to allow execution |
| `MAX_REQUEST_ITERATIONS` | `10` | Max inference loop iterations per request |

## Context Sources

The Worker builds its context from these sources (in priority order):

1. **Execution History** -- results from previous steps in the current session
2. **Source Truth** -- project-specific asset paths, Blueprint names, level names (`reference/source_truth/`)
3. **Tool Registry** -- exact parameter names, types, and constraints (`Tools/tool-registry.json`)
4. **User Context** -- additional documentation provided by the user (`reference/user_context/`)
5. **Engine Documentation** -- UE API reference at `C:\UE56\Engine\Documentation\`

## File Structure

```
models/
  setup-models.bat                          # One-time download script
  start-all.bat                             # Launch all 3 servers
  start-validator.bat                       # Launch Validator only
  start-worker.bat                          # Launch Worker only
  start-qa.bat                              # Launch QA Auditor only
  instructions/
    validator.md                            # Validator system prompt
    worker.md                               # Worker system prompt
    qa-auditor.md                           # QA Auditor system prompt
  bin/                                      # llama.cpp binaries (created by setup)
    llama-server.exe
    *.dll
  Llama-3.2-3B-Instruct-Q4_K_M.gguf       # 3B model (created by setup)
  Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf  # 8B model (created by setup)
```

## Requirements

- Windows 10+ with NVIDIA GPU (CUDA 12 compatible)
- Approximately 8 GB free disk space for models and binaries
- Approximately 5.5 GB free VRAM (only the largest active model is loaded at peak)
