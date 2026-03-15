# Local Inference Models

This folder contains everything needed to run the local LLM inference layer for AgenticMCP.

## Architecture

The system uses three specialized inference servers, each running a Meta Llama model via llama.cpp:

| Role | Model | Port | GGUF File | Size | Purpose |
|------|-------|------|-----------|------|---------|
| Validator | Llama 3.2 3B Instruct | 8080 | Llama-3.2-3B-Instruct-Q4_K_M.gguf | ~2.0 GB | Validates plan step parameters against tool registry and source truth |
| Worker | Llama 3.1 8B Instruct | 8081 | Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf | ~4.9 GB | Produces exact JSON payloads for MCP tool calls, self-corrects on failure |
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
start-planner.bat       :: port 8081
start-qa.bat            :: port 8082
```

The gatekeeper (`Tools/gatekeeper/`) connects to these servers automatically during plan execution.

## Instruction Injection

Each server loads its system prompt from `instructions/` at startup:

| Server | Instruction File |
|--------|-----------------|
| Validator | `instructions/validator.md` |
| Worker | `instructions/worker.md` |
| QA Auditor | `instructions/qa-auditor.md` |

The system prompt is injected via llama.cpp's `--system-prompt-file` flag. When Claude dispatches a plan step through the gatekeeper, the Worker server already has its role instructions loaded. The gatekeeper sends only the step context and task as the user prompt. The model runs inference against both the system prompt (its role) and the user prompt (the task) to produce the exact JSON payload.

## How It Connects

```
Claude (Planner)
    |
    | JSON execution plan
    v
Gatekeeper (Tools/gatekeeper/)
    |
    | 1. Rule engine validates structure
    | 2. Sends step to local LLM for inference
    v
llama.cpp servers (this folder)
    |
    | JSON tool payload (confidence-gated)
    v
C++ Plugin (UE5.6 editor, port 9847)
```

## Environment Variables

Override default ports if needed:

| Variable | Default | Description |
|----------|---------|-------------|
| `LLAMA_VALIDATOR_URL` | `http://localhost:8080` | Validator server endpoint |
| `LLAMA_WORKER_URL` | `http://localhost:8081` | Worker server endpoint |
| `LLAMA_QA_AUDITOR_URL` | `http://localhost:8082` | QA Auditor server endpoint |
| `LLAMA_CPP_URL` | (unset) | Legacy single-server mode. If set, overrides all three role URLs. |
| `CONFIDENCE_THRESHOLD` | `0.95` | Minimum token probability to allow execution |
| `MAX_WORKER_RETRIES` | `3` | Max inference attempts before escalation |

## File Structure

```
models/
  setup-models.bat                          # One-time download script
  start-all.bat                             # Launch all 3 servers
  start-validator.bat                       # Launch Validator only
  start-planner.bat                         # Launch Worker only
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
