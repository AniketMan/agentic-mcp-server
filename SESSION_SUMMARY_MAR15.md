# Session Summary: Local Inference & Godot Agentic Pipeline
**Date:** March 15, 2026
**Status:** Completed

## 1. Local Inference Layer (UE Agentic MCP Server)

We built the local inference architecture that allows Claude to dispatch validation and execution tasks to local LLMs running on your machine.

### Components Built
- **Model Stack:** Downloaded and configured `llama.cpp` to run three specialized Llama models:
  - **Validator (Llama 3.2 3B):** Port 8080. Validates tool parameters against the registry.
  - **Planner/Worker (Llama 3.1 8B):** Port 8081. Runs the execute-verify-fix loop.
  - **QA Auditor (Llama 3.2 3B):** Port 8082. Scans project state for broken references and placeholders.
- **Instruction MDs:** Created system prompts for each role in `models/instructions/`.
- **Gatekeeper Update:** Modified `Tools/gatekeeper/llm-validator.js` to route requests to the correct local port and inject the instruction MDs dynamically per-request (bypassing the deprecated `--system-prompt-file` flag).
- **Batch Scripts:** Wrote `setup-models.bat` (with delayed expansion to fix parsing errors) and `start-all.bat` to handle environment setup and execution.

### Architecture Flow
1. Claude writes a JSON plan and saves it to the `plan/` folder.
2. Gatekeeper reads the plan and sends steps to the local Llama models.
3. Local Llamas infer the exact JSON payloads with a 95% confidence gate.
4. Gatekeeper dispatches confident payloads to the UE C++ plugin.

*Note: All legacy Qwen docs were updated and moved to the `docs/archive` branch to keep `master` clean.*

---

## 2. The Godot Pivot

We evaluated Godot as an alternative to Unreal Engine for Quest 3 VR development, specifically regarding its suitability for AI-driven pipelines.

### Findings
- **Agentic Suitability:** Godot is vastly superior for AI generation. Because scenes (`.tscn`) and scripts (`.gd`) are plaintext, agents can build entire projects by writing text files. No C++ bridge, no reflection system, no HTTP server needed.
- **Quest 3 Viability:** Since UE's high-end features (Lumen, Nanite) are disabled on Quest 3 anyway, Godot's Vulkan mobile renderer provides equivalent visual quality with significantly less engine overhead.
- **GPU Lightbaking:** We discovered the [NVIDIA RTX Godot fork](https://github.com/NVIDIA-RTX/godot.git), which adds full GPU path tracing and lightbaking, solving Godot's biggest limitation (CPU-only lightmapping).

---

## 3. UE to Godot Importer Plugin

To facilitate the transition, we created a new repository (`AniketMan/godotFork`) and built a Godot EditorPlugin to automatically import assets from an Unreal Engine project.

### The Pipeline
Instead of relying on lossy intermediate formats like USDZ or flat level exports, the plugin extracts the UE Content folder into a reusable Godot asset library.

### Components Built (in `addons/ue_importer/`)
- **Direct `.uasset` Parsing:** Integrated `CUE4Parse` to read UE binary files directly from disk without needing UE open.
- **Asset Extraction:**
  - **Meshes & Animations:** Extracted to `glTF 2.0` (`.glb`), preserving geometry, rigs, and animation clips.
  - **Textures:** Decoded and saved as `.png`.
  - **Materials:** Extracted PBR parameters and generated native Godot `StandardMaterial3D` (`.tres`) resources.
  - **Audio:** Decoded `USoundWave` to native formats (`.ogg`/`.wav`).
- **Manifest Generation:** Outputs `import_manifest.json`, mapping original UE paths to new Godot `res://` paths.
- **Agent Handoff:** The Godot agents read this manifest to understand the asset library and reconstruct the scenes via plaintext `.tscn` generation.

### Next Steps for Godot Pipeline
1. Add the `CUE4Parse` NuGet package to the Godot project.
2. Run the importer on the SoH Content folder.
3. Build the Godot-side agent that reads the manifest and writes `.tscn` files.
