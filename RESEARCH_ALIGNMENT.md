# Research Alignment Audit

Pipeline architecture validated against four Stanford / Together AI papers. Audit date: March 15, 2026.

---

## Paper 1: Minions -- Cost-efficient Collaboration Between On-device and Cloud LMs

> arxiv: 2502.15964

Cloud model decomposes tasks into subtasks; local model executes them on local data.

| Paper Recommends | Our Pipeline | Status | Implementation |
|-----------------|-------------|--------|----------------|
| Cloud model decomposes into subtasks | Claude writes JSON plan with ordered steps | **Aligned** | `SYSTEM.md`, `CLAUDE.md` |
| Local model executes subtasks on local data | Llama Workers infer exact payloads from tool registry + source truth | **Aligned** | `Tools/gatekeeper/llm-validator.js` |
| Parallel execution of independent subtasks | Dependency-graph wave builder, `Promise.all` per wave, max 4 concurrent | **Aligned** | `Tools/gatekeeper/dispatcher.js` (buildWaves) |
| Aggregation of local results back to cloud | Escalation protocol sends failures back to Claude | **Aligned** | `Tools/gatekeeper/escalation.js` |

**Prior art in this repo:** Parallel execution was designed from the start in `Tools/contexts/parallel_workflows.md`, which defines tool parallelization classes (parallel-safe, per-object safe, sequential-only), wave-based batching, subagent constraints (max 3, max 8 sequential calls), and 6 complete workflow patterns. The dispatcher now implements this specification.

---

## Paper 2: Intelligence per Watt -- Measuring Intelligence Efficiency of Local AI

> arxiv: 2511.07885

Proposes IPW as a unified metric for local AI efficiency. Validates local inference viability.

| Paper Recommends | Our Pipeline | Status | Implementation |
|-----------------|-------------|--------|----------------|
| Local models for single-turn structured tasks | Workers produce single JSON payloads | **Aligned** | `models/instructions/worker.md` |
| 95% threshold for escalation to cloud | `CONFIDENCE_THRESHOLD = 0.95` | **Exact match** | `Tools/gatekeeper/llm-validator.js` line 31 |
| Fallback to cloud when local fails | Escalation to Claude on confidence failure | **Aligned** | `Tools/gatekeeper/escalation.js` |
| Local accelerators more power-efficient | RTX 4080/5080 running Llama 3.2 3B + 3.1 8B locally | **Aligned** | `models/` |

---

## Paper 3: WEAVER -- Shrinking the Generation-Verification Gap with Weak Verifiers

> arxiv: 2506.18203

Combines multiple weak verifiers into a strong verification ensemble.

| Paper Recommends | Our Pipeline | Status | Implementation |
|-----------------|-------------|--------|----------------|
| Combine multiple weak verifiers | Rule Engine (deterministic) + LLM Validator (inference) + Confidence Gate (token probs) | **Aligned** -- 3 layers | `Tools/gatekeeper/rule-engine.js`, `llm-validator.js`, `confidence-gate.js` |
| Weak supervision to estimate verifier accuracy | Confidence scoring from logprobs + 4-signal weighted scoring | **Aligned** | `Tools/confidence-gate.js` (buildAdaptationSuggestion) |
| Ensemble verification for critical operations | ENSEMBLE technique: Worker + Validator both infer, require consensus | **Aligned** | `Tools/gatekeeper/technique-selector.js` |

**Prior art in this repo:** The confidence gate (`Tools/confidence-gate.js`) was designed with adaptive suggestions from the start, including `buildAdaptationSuggestion()` which provides Claude with specific guidance on how to revise failed steps. The multi-layer verification (rule engine -> LLM validator -> confidence gate) predates the WEAVER paper.

---

## Paper 4: ARCHON -- Architecture Search for Inference-Time Techniques

> arxiv: 2409.15254

Automatically discovers optimal combinations of inference-time techniques per task type.

| Paper Recommends | Our Pipeline | Status | Implementation |
|-----------------|-------------|--------|----------------|
| Adaptive technique selection per task | 5-technique selector: DIRECT, SINGLE, MULTI_PASS, ENSEMBLE, SPATIAL | **Aligned** | `Tools/gatekeeper/technique-selector.js` |
| Performance history drives adaptation | In-memory success rate + avg confidence tracking, auto-upgrade on failure | **Aligned** | `technique-selector.js` (recordOutcome, getSuccessRate) |
| Multi-pass sampling for complex tasks | MULTI_PASS: up to 3 retries with escalating context | **Aligned** | `technique-selector.js` + `llm-validator.js` |
| Ensembling multiple model outputs | ENSEMBLE: Worker + Validator parallel inference with consensus check | **Aligned** | `dispatcher.js` (ENSEMBLE branch) |

**Prior art in this repo:** Adaptive behavior was designed into the confidence gate from the start. `Tools/confidence-gate.js` line 513: *"This is the key to the adaptive loop -- Claude reads the suggestion, adapts its approach."* The technique selector formalizes this into a classification system with 5 distinct strategies and performance-driven upgrades.

---

## Technique Classification Summary

| Technique | When Used | Retries | Confidence Threshold | Models |
|-----------|-----------|---------|---------------------|--------|
| DIRECT | Read-only tools (get_level_actors, asset_search, etc.) | 0 | N/A | None |
| SINGLE | Simple modifying tools (spawn_actor, move_actor, etc.) | 1 | 90% | Worker |
| MULTI_PASS | Complex tools (add_node, connect_pins, create_blueprint, etc.) | 3 | 95% | Worker |
| ENSEMBLE | Critical tools (delete_actors, execute_script, compile_blueprint) | 2 | 95% | Worker + Validator |
| SPATIAL | Visual/spatial analysis (screenshot, capture_viewport) | 1 | 80% | Cosmos Reason2 |

---

## Original Design References

The following files in this repository document the parallel execution and adaptive technique concepts that were designed before these papers were reviewed:

1. **`Tools/contexts/parallel_workflows.md`** -- Complete parallel execution specification with wave-based batching, tool conflict classes, 6 workflow patterns, anti-patterns, and subagent instruction templates. Defines max 3 subagents, max 4 simultaneous MCP tool calls, and wave-based execution.

2. **`Tools/confidence-gate.js`** (lines 508-550) -- Adaptive suggestion system: `buildAdaptationSuggestion()` dynamically generates revision guidance based on tool type, confidence level, and failure mode. Comment on line 513: *"This is the key to the adaptive loop."*

3. **`WORKER_INSTRUCTIONS.md`** -- Workers as autonomous agents that correct the Planner, trust engine state over plan assumptions, and escalate only when outside their scope.

4. **`ARCHITECTURE.md`** -- Three-layer inference-gated architecture: Planner (cloud) -> Gatekeeper (validation) -> Workers (local inference) -> Plugin (execution).

---

## Agent VRAM Budget

| Agent | Model | Port | VRAM |
|-------|-------|------|------|
| Validator | Llama 3.2 3B Instruct Q4_K_M | 8080 | ~2.5 GB |
| Worker | Llama 3.1 8B Instruct Q4_K_M | 8081 | ~5.5 GB |
| QA Auditor | Llama 3.2 3B Instruct Q4_K_M | 8082 | ~2.5 GB |
| Spatial Reasoner | Cosmos Reason2 2B INT8 | 8083 | ~4 GB |
| **Peak (all loaded)** | | | **~14.5 GB** |
| **Actual (hot-swapped)** | | | **~9.5 GB max** |

---

## Conclusion

All gaps identified in the initial audit are closed:

- **Parallel execution**: Implemented via dependency-graph wave builder in `dispatcher.js` (Minions-aligned)
- **Adaptive technique selection**: Implemented via `technique-selector.js` with 5 techniques and performance-driven upgrades (ARCHON-aligned)
- **Ensemble verification**: Implemented as the ENSEMBLE technique for critical tools (WEAVER-aligned)
- **Performance tracking**: Implemented via `recordOutcome()` with automatic technique upgrades when tools underperform (IPW-aligned)

The pipeline is fully aligned with all four papers.
