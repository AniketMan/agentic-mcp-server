# Quantized Inference Architecture

## Overview

The Quantized Inference Layer is the bridge between the theoretical **Quantized Spatial Data Paradigm** (from the `quantized-architecture` repo) and the operational reality of the **AgenticMCP** server. 

It solves the fundamental problem of AI-driven game development: *How does an LLM interact with a 2TB Unreal Engine project without running out of context or hallucinating references?*

The answer: **It doesn't.** Instead, the project is quantized into a lightweight, tiered manifest, and the LLM operates on that manifest to generate deterministic wiring plans.

## Core Components

### 1. The Adaptive Asset Manifest (`quantize_project`)
Instead of scanning the project live on every request, the MCP server now caches a structured representation of the project state. Assets are tiered by semantic importance:
- **Hero Tier (float16 equivalent)**: Characters, key props, level blueprints. Full structural breakdown (transforms, components, parent classes).
- **Standard Tier (int16 equivalent)**: Triggers, markers, standard props. Basic metadata (name, class, position).
- **Background Tier (int8 equivalent)**: Static meshes, lights. Minimal metadata (name, class only).

This reduces a massive project down to a few kilobytes of JSON, which fits comfortably in Claude's context window.

### 2. Deterministic Scene Inference (`get_scene_plan`)
When Claude needs to wire a scene (e.g., Scene 7: The Hospital), it does not guess what nodes to add. The inference layer:
1. Reads the `INSTRUCTIONS.md` for the scene.
2. Cross-references the required actors against the cached manifest.
3. Generates an exact, deterministic array of MCP tool calls (`add_node`, `connect_pins`, etc.).

### 3. Confidence Scoring
Because the LLM is not generating the plan (the inference layer is), the system can calculate mathematical confidence:
- **1.0**: All referenced actors and sequences exist in the manifest. Safe to auto-execute.
- **0.5 - 0.9**: Some references are missing (e.g., a trigger volume hasn't been spawned yet). Proceed with caution.
- **< 0.5**: Critical missing data. Do not execute.

### 4. Progress Persistence (`mark_step_complete`)
Every executed step is immediately written to the scene's `STATUS.json`. If the connection drops or Claude loses context, the state is preserved on disk.

## How to Use It (Claude Code / VS Code MCP)

1. Open Claude Code in the project directory.
2. Run `/wireSceneQuantized 1` (or use the `.llms/commands/wireSceneQuantized.md` command).
3. Claude will invoke the `Quantized Coordinator` agent.
4. The agent will:
   - Call `unreal_quantize_project` to get the manifest.
   - Call `unreal_get_scene_plan` to get the deterministic call sequence.
   - Execute the calls step-by-step, verifying confidence.
   - Mark the steps complete.

## Why This Matters

This architecture proves that you do not need massive Generative AI models to build complex spatial logic. By using deterministic inference on quantized data, a lightweight LLM can act as a flawless Technical Director, wiring together human-made assets with zero hallucinations.
