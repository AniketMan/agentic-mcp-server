/**
 * Local LLM Validator (Layer 2b)
 * 
 * Uses a small local model via llama.cpp HTTP server to validate
 * the semantic correctness of plan steps against UE API docs.
 * 
 * The LLM server must be running at LLAMA_CPP_URL (default: http://localhost:8080).
 * Recommended model: Qwen2.5-Coder-7B-Q4_K_M.gguf (~4.5GB VRAM)
 */

import { readFileSync, existsSync, readdirSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const LLAMA_CPP_URL = process.env.LLAMA_CPP_URL || 'http://localhost:8080';
const CONFIDENCE_THRESHOLD = parseFloat(process.env.CONFIDENCE_THRESHOLD || '0.95');
const MAX_RETRIES = parseInt(process.env.MAX_WORKER_RETRIES || '3', 10);

// Map tool names to context file categories
const TOOL_CONTEXT_MAP = {
  blueprint: 'blueprint', graph: 'blueprint', search: 'blueprint', references: 'blueprint',
  add_node: 'blueprint', delete_node: 'blueprint', connect_pins: 'blueprint',
  disconnect_pin: 'blueprint', set_pin_default: 'blueprint', move_node: 'blueprint',
  compile_blueprint: 'blueprint', create_blueprint: 'blueprint', create_graph: 'blueprint',
  add_variable: 'blueprint', remove_variable: 'blueprint', validate_blueprint: 'blueprint',
  snapshot_graph: 'blueprint', restore_graph: 'blueprint', get_pin_info: 'blueprint',
  list_actors: 'actor', get_actor: 'actor', spawn_actor: 'actor',
  delete_actor: 'actor', set_actor_property: 'actor', set_actor_transform: 'actor',
  list_levels: 'actor', load_level: 'actor', unload_level: 'actor',
  screenshot: 'scene_awareness', move_camera: 'scene_awareness',
  pcg_list: 'blueprint', pcg_inspect: 'blueprint', pcg_execute: 'blueprint',
  anim_inspect: 'animation', anim_states: 'animation', anim_transitions: 'animation',
  seq_create: 'level_sequence', seq_add_track: 'level_sequence', seq_get_tracks: 'level_sequence',
  mat_list: 'material', mat_inspect: 'material', mat_create: 'material',
  asset_import: 'assets', asset_inspect: 'assets', asset_duplicate: 'assets',
  executePythonCapture: 'python_scripting', executeConsole: 'python_scripting',
};

/**
 * Load the relevant context documents for a given tool.
 * Priority: source_truth > context doc > tool registry
 */
function loadContextForTool(toolName, toolDef) {
  const parts = [];

  // 1. Load source_truth files (highest priority)
  const sourceTruthDir = join(__dirname, '..', '..', 'reference', 'source_truth');
  if (existsSync(sourceTruthDir)) {
    const files = readdirSync(sourceTruthDir).filter(f => f !== 'README.md');
    for (const file of files) {
      try {
        const content = readFileSync(join(sourceTruthDir, file), 'utf-8');
        // Only include if under 4KB to avoid blowing context window
        if (content.length < 4096) {
          parts.push(`=== SOURCE TRUTH: ${file} ===\n${content}`);
        } else {
          parts.push(`=== SOURCE TRUTH: ${file} (truncated to 4KB) ===\n${content.slice(0, 4096)}`);
        }
      } catch (e) { /* skip unreadable files */ }
    }
  }

  // 2. Load relevant context doc
  const category = TOOL_CONTEXT_MAP[toolName];
  if (category) {
    const contextPath = join(__dirname, '..', 'contexts', `${category}.md`);
    if (existsSync(contextPath)) {
      const content = readFileSync(contextPath, 'utf-8');
      parts.push(`=== UE API REFERENCE: ${category} ===\n${content.slice(0, 8192)}`);
    }
  }

  // 3. Tool registry entry
  if (toolDef) {
    parts.push(`=== TOOL REGISTRY: ${toolName} ===\n${JSON.stringify(toolDef, null, 2)}`);
  }

  return parts.join('\n\n');
}

/**
 * Check if the llama.cpp server is running.
 */
async function isLLMAvailable() {
  try {
    const resp = await fetch(`${LLAMA_CPP_URL}/health`, { signal: AbortSignal.timeout(2000) });
    return resp.ok;
  } catch {
    return false;
  }
}

/**
 * Run inference on the local LLM to validate a plan step.
 * Returns { confidence, payload, raw_response }
 */
async function runWorkerInference(step, toolDef, context, attempt = 1) {
  const prompt = `You are a deterministic Unreal Engine tool executor. You produce exact JSON payloads for MCP tool calls.

CONTEXT (check these before producing output):
${context}

TASK:
Execute this plan step by producing the exact JSON payload for the tool call.

Step: ${JSON.stringify(step, null, 2)}

RULES:
- Use ONLY parameter names from the TOOL REGISTRY section above.
- Use ONLY asset paths from SOURCE TRUTH if available.
- If you cannot determine a required value with certainty, respond with {"escalation": true, "reason": "..."}.
- Do NOT invent asset paths, pin names, or node types.

Respond with ONLY valid JSON. No explanation. No markdown.
Format: {"toolName": "${step.tool}", "payload": {...}}`;

  try {
    const resp = await fetch(`${LLAMA_CPP_URL}/completion`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        prompt,
        n_predict: 512,
        temperature: 0.1,  // near-deterministic
        top_p: 0.9,
        stop: ['\n\n'],
        // Request logprobs for confidence scoring
        n_probs: 1,
      }),
      signal: AbortSignal.timeout(30000),
    });

    if (!resp.ok) {
      throw new Error(`LLM server returned ${resp.status}`);
    }

    const result = await resp.json();
    const content = result.content?.trim();

    // Extract confidence from completion_probabilities if available
    // Otherwise estimate from the model's perplexity
    let confidence = 0;
    if (result.completion_probabilities) {
      // Average token probability across the output
      const probs = result.completion_probabilities.map(t => t.probs?.[0]?.prob || 0);
      confidence = probs.length > 0 ? probs.reduce((a, b) => a + b, 0) / probs.length : 0;
    } else if (result.timings?.predicted_per_token_ms) {
      // Heuristic: faster generation = higher confidence (model is more certain)
      // This is a rough proxy when logprobs aren't available
      const msPerToken = result.timings.predicted_per_token_ms;
      confidence = msPerToken < 10 ? 0.95 : msPerToken < 20 ? 0.85 : 0.70;
    }

    // Try to parse the JSON output
    let parsed = null;
    try {
      parsed = JSON.parse(content);
    } catch {
      // Try to extract JSON from the response
      const jsonMatch = content.match(/\{[\s\S]*\}/);
      if (jsonMatch) {
        try { parsed = JSON.parse(jsonMatch[0]); } catch { /* failed */ }
      }
    }

    return {
      confidence,
      payload: parsed,
      raw_response: content,
      attempt,
      escalation: parsed?.escalation || false,
    };
  } catch (error) {
    return {
      confidence: 0,
      payload: null,
      raw_response: error.message,
      attempt,
      escalation: false,
      error: error.message,
    };
  }
}

/**
 * Execute a plan step through the Worker inference loop.
 * Retries up to MAX_RETRIES times. Escalates if confidence stays below threshold.
 */
async function executeWithConfidenceGate(step, toolDef) {
  const context = loadContextForTool(step.tool, toolDef);
  let bestResult = null;

  for (let attempt = 1; attempt <= MAX_RETRIES; attempt++) {
    const result = await runWorkerInference(step, toolDef, context, attempt);

    // Track best attempt
    if (!bestResult || result.confidence > bestResult.confidence) {
      bestResult = result;
    }

    // Check for explicit escalation from the model
    if (result.escalation) {
      return {
        action: 'escalate',
        step_id: step.step_id,
        tool: step.tool,
        reason: result.payload?.reason || 'Model requested escalation',
        attempts: attempt,
        max_confidence: bestResult.confidence,
      };
    }

    // Check confidence gate
    if (result.confidence >= CONFIDENCE_THRESHOLD && result.payload) {
      return {
        action: 'execute',
        step_id: step.step_id,
        tool: step.tool,
        payload: result.payload.payload || result.payload,
        confidence: result.confidence,
        attempts: attempt,
      };
    }

    // Log retry
    console.error(`[WORKER] Step ${step.step_id} attempt ${attempt}/${MAX_RETRIES}: confidence ${(result.confidence * 100).toFixed(1)}% < ${(CONFIDENCE_THRESHOLD * 100).toFixed(0)}%`);
  }

  // All retries exhausted -- escalate
  return {
    action: 'escalate',
    step_id: step.step_id,
    tool: step.tool,
    error: `Could not reach ${(CONFIDENCE_THRESHOLD * 100).toFixed(0)}% confidence after ${MAX_RETRIES} attempts`,
    attempts: MAX_RETRIES,
    max_confidence: bestResult?.confidence || 0,
    best_payload: bestResult?.payload,
    context_used: Object.keys(TOOL_CONTEXT_MAP).includes(step.tool)
      ? [`tool-registry:${step.tool}`, `contexts/${TOOL_CONTEXT_MAP[step.tool]}.md`]
      : [`tool-registry:${step.tool}`],
  };
}

export {
  isLLMAvailable,
  runWorkerInference,
  executeWithConfidenceGate,
  loadContextForTool,
  CONFIDENCE_THRESHOLD,
  MAX_RETRIES,
};
