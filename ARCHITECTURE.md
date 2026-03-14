# AgenticMCP Architecture: Inference-Gated Determinism

## Overview

The AgenticMCP system operates on a philosophy of **Inference-Gated Determinism**. Generative AI models (like Claude) are never allowed to execute tools directly against the Unreal Engine. Instead, a strict three-layer hierarchy separates planning, validation, and execution. This ensures that massive UE5 projects are modified safely, predictably, and without hallucination.

## The Three-Layer System

```text
+-------------------------------------------------------------------+
|                   Layer 1: The Planner (Claude)                   |
|                                                                   |
| Reads project roadmap, scripts, and high-level requirements.      |
| Generates a structured JSON execution plan.                       |
| ZERO direct access to the engine or plugin tools.                 |
+-------------------------------------------------------------------+
                              |
                     JSON Execution Plan
                              |
+-------------------------------------------------------------------+
|                   Layer 2: The Gatekeeper                         |
|                   (Node.js Bridge + Local LLM)                    |
|                                                                   |
| 1. Deterministic Rule Engine: Validates params, workflow order.   |
| 2. Local LLM Validator: Checks semantic validity against docs.    |
| Rejects bad steps back to Planner. Dispatches good steps.         |
+-------------------------------------------------------------------+
                              |
                     Approved Tool Calls
                              |
+-------------------------------------------------------------------+
|                   Layer 3: The Workers                            |
|                   (Local Inference Executors)                     |
|                                                                   |
| Small local models (e.g., Qwen2.5 7B) running via llama.cpp.      |
| Context: API Docs + source_truth + tool registry.                 |
| Action: Run inference to determine exact JSON payload.            |
| Gate: If confidence >= 95%, execute. Else, escalate.              |
+-------------------------------------------------------------------+
                              |
                     HTTP (port 9847)
                              |
+-------------------------------------------------------------------+
|                   Layer 4: C++ Plugin                             |
|                   (Unreal Engine 5.6)                             |
|                                                                   |
| Runs headless via -RenderOffScreen.                               |
| Processes requests on the game thread.                            |
| Returns deterministic success/error with real engine data.        |
+-------------------------------------------------------------------+
```

### Layer 1: The Planner (Frontier Model)
The Planner is the strategic brain. It reads the project roadmap, script, and high-level requirements to understand the goal. It has **zero direct access to the engine or the plugin**. 

The Planner's only output is a structured JSON plan—a sequence of required tool calls, their intended parameters, and the expected outcome of each step. If a plan fails during execution, the Planner receives a structured error report and must generate a revised plan. It cannot "improvise" or "try things out" live in the engine.

### Layer 2: The Gatekeeper (Validation Layer)
The Gatekeeper sits in the JS bridge layer and intercepts the Planner's JSON plan. It consists of two sub-layers:

1. **Deterministic Rule Engine:** A zero-latency, model-free validation layer that checks the plan against the tool registry. It ensures parameter names match exactly, required fields are present, and workflow rules (e.g., snapshot before mutation) are followed.
2. **Local LLM Validator:** A small, fast local model running via `llama.cpp`. It checks the semantic validity of the plan against the Unreal Engine API documentation and the project's `source_truth` files. 

If the Gatekeeper rejects a step, the plan is bounced back to the Planner with a specific error. If approved, the Gatekeeper dispatches the steps to the Workers.

### Layer 3: The Workers (Local Executors)
The Workers are the only entities that actually execute tool calls against the Unreal Engine C++ plugin. They operate purely on inference, not generation.

When a Worker receives an approved step, it loads a specific context window containing:
- The exact tool registry schema for the required tool
- The relevant Unreal Engine API documentation (from the `contexts/` folder)
- The user's `source_truth/` override files
- The step instructions from the Gatekeeper

The Worker runs inference to determine the exact JSON payload to send to the C++ plugin. It then checks the confidence score (logits) of its output. 

**The 95% Confidence Gate:**
- If confidence is **>= 95%**, the Worker executes the call.
- If confidence is **< 95%**, the Worker retries with a slightly adjusted context window (up to 3 attempts).
- If confidence remains **< 95%**, the Worker halts execution, marks the step as blocked, and escalates back to the Gatekeeper (and ultimately the Planner) with a detailed error report.

Workers have no internet access, no creativity, and no ability to hallucinate. They either know exactly what to do based on the provided documentation, or they do nothing.

## Documentation Hierarchy

The documentation that drives this system is structured to prioritize user control and deterministic execution.

### 1. `source_truth/` (User Overrides)
This folder contains project-specific files provided by the user, such as the screenplay, asset naming conventions, or specific architectural rules. **This is the highest authority.** If a Worker detects a conflict between the Planner's instructions and a file in `source_truth/`, the `source_truth/` file always wins. 

### 2. `contexts/` (Domain Knowledge)
This folder contains detailed technical documentation for specific Unreal Engine subsystems (e.g., Blueprints, PCG, Animation, Sequencer). Workers load these files on-demand based on the tool they are about to execute. This ensures the Worker's context window is filled only with highly relevant, accurate API information.

### 3. `SYSTEM.md` & `WORKER_INSTRUCTIONS.md`
These files define the operational rules for the system. `SYSTEM.md` defines the overall architecture and plan formatting for the Planner. `WORKER_INSTRUCTIONS.md` defines the strict execution loop, confidence gating, and escalation protocols for the local models.

## Engine Integration: Headless Agent Mode

When the AgenticMCP plugin is initialized for automated workflows, it launches Unreal Engine using the `-RenderOffScreen` argument. 

This mode runs the full engine, including the complete GPU rendering pipeline, but without creating an OS window. This provides several critical advantages:
- The agent can execute PIE sessions, render Level Sequences, and capture high-resolution viewport screenshots.
- The user's workflow is not interrupted by a visible editor window stealing focus.
- VRAM usage is optimized by rendering to offscreen buffers instead of a display swapchain.

### The Terminal Console UI

While the engine runs headless, the user monitors the system via a lightweight Terminal UI (TUI). This console tails the execution log, showing every plan step, the Gatekeeper's validation status, and the Worker's confidence scores.

Crucially, the console supports inline image rendering via the Kitty graphics protocol (with sixel fallback). When a Worker executes a `screenshot` tool, the resulting image is rendered directly in the terminal log. This allows the user to visually verify the agent's progress without needing a separate image viewer or editor window. The user can also interrupt execution or inject new instructions via the console prompt.
