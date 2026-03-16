/**
 * Local LLM Validator (Layer 2b)
 * 
 * Uses a small local model via llama.cpp HTTP server to validate
 * the semantic correctness of plan steps against UE API docs.
 * 
 * Four inference servers must be running (see models/start-all.bat):
 *   - Validator:         port 8080  (Llama 3.2 3B Instruct Q4_K_M via llama.cpp)
 *   - Worker:            port 8081  (Llama 3.1 8B Instruct Q4_K_M via llama.cpp)
 *   - QA Auditor:        port 8082  (Llama 3.2 3B Instruct Q4_K_M via llama.cpp)
 *   - Spatial Reasoner:  port 8083  (Cosmos Reason2 2B INT8 via vLLM)
 *
 * System prompts are loaded from models/instructions/*.md and injected
 * per-request via the /v1/chat/completions endpoint (system role message).
 * The --system-prompt-file flag was removed from llama-server in PR #9857.
 */

import { readFileSync, existsSync, readdirSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));

// ---------------------------------------------------------------------------
// Server Discovery
// ---------------------------------------------------------------------------
// Default port assignments (can be overridden via env vars).
// The auto-detect logic below probes each port and falls back gracefully:
//   - If all 4 are up: each role gets its dedicated server.
//   - If only 1 is up: all roles share that single server.
//   - If none are up:  isLLMAvailable() returns false, gatekeeper
//     falls back to DIRECT mode (no inference, plan params passed through).

const DEFAULT_PORTS = {
  validator: process.env.LLAMA_VALIDATOR_URL  || 'http://localhost:8080',
  worker:    process.env.LLAMA_WORKER_URL     || 'http://localhost:8081',
  qa:        process.env.LLAMA_QA_AUDITOR_URL || 'http://localhost:8082',
  spatial:   process.env.COSMOS_SPATIAL_URL    || 'http://localhost:8083',
};

// Legacy single-server override
const LLAMA_CPP_URL = process.env.LLAMA_CPP_URL || null;

// Resolved URLs after auto-detect (populated by discoverServers())
const RESOLVED_URLS = { validator: null, worker: null, qa: null, spatial: null };
let _discoveryDone = false;
let _discoveryPromise = null;

/**
 * Probe a URL's /health endpoint. Returns true if server is up.
 */
async function probeHealth(url) {
  try {
    const resp = await fetch(`${url}/health`, { signal: AbortSignal.timeout(2000) });
    return resp.ok;
  } catch {
    return false;
  }
}

/**
 * Auto-detect which inference servers are running.
 * Called once lazily on first use. Results are cached.
 */
async function discoverServers() {
  if (_discoveryDone) return;
  if (_discoveryPromise) return _discoveryPromise;

  _discoveryPromise = (async () => {
    // If explicit single-server override, use it for everything
    if (LLAMA_CPP_URL) {
      const up = await probeHealth(LLAMA_CPP_URL);
      if (up) {
        for (const role of Object.keys(RESOLVED_URLS)) {
          RESOLVED_URLS[role] = LLAMA_CPP_URL;
        }
        console.error(`[LLM-VALIDATOR] Single-server mode: all roles -> ${LLAMA_CPP_URL}`);
      } else {
        console.error(`[LLM-VALIDATOR] LLAMA_CPP_URL=${LLAMA_CPP_URL} is not responding.`);
      }
      _discoveryDone = true;
      return;
    }

    // Probe all default ports in parallel
    const roles = Object.keys(DEFAULT_PORTS);
    const results = await Promise.all(roles.map(r => probeHealth(DEFAULT_PORTS[r])));
    const liveUrls = [];

    for (let i = 0; i < roles.length; i++) {
      if (results[i]) {
        RESOLVED_URLS[roles[i]] = DEFAULT_PORTS[roles[i]];
        liveUrls.push(DEFAULT_PORTS[roles[i]]);
        console.error(`[LLM-VALIDATOR] ${roles[i]} -> ${DEFAULT_PORTS[roles[i]]} (up)`);
      } else {
        console.error(`[LLM-VALIDATOR] ${roles[i]} -> ${DEFAULT_PORTS[roles[i]]} (down)`);
      }
    }

    // Fallback: if some roles have no server, share whatever is alive
    if (liveUrls.length > 0) {
      const fallbackUrl = liveUrls[0];
      for (const role of roles) {
        if (!RESOLVED_URLS[role]) {
          RESOLVED_URLS[role] = fallbackUrl;
          console.error(`[LLM-VALIDATOR] ${role} -> ${fallbackUrl} (fallback)`);
        }
      }
    }

    _discoveryDone = true;
  })();

  return _discoveryPromise;
}

const CONFIDENCE_THRESHOLD = parseFloat(process.env.CONFIDENCE_THRESHOLD || '0.95');
const MAX_RETRIES = parseInt(process.env.MAX_WORKER_RETRIES || '3', 10);

// Load instruction MDs for each role (injected as system prompt per-request)
const INSTRUCTIONS_DIR = join(__dirname, '..', '..', 'models', 'instructions');
const ROLE_INSTRUCTIONS = {};
for (const [role, file] of [['validator', 'validator.md'], ['worker', 'worker.md'], ['qa', 'qa-auditor.md'], ['spatial', 'spatial-reasoner.md']]) {
  const path = join(INSTRUCTIONS_DIR, file);
  if (existsSync(path)) {
    ROLE_INSTRUCTIONS[role] = readFileSync(path, 'utf-8');
  } else {
    ROLE_INSTRUCTIONS[role] = '';
    console.warn(`[LLM-VALIDATOR] Warning: ${path} not found. ${role} will run without system prompt.`);
  }
}

// Map tool names to context file categories (camelCase to match tool-registry.json)
const TOOL_CONTEXT_MAP = {
  blueprint: 'blueprint', graph: 'blueprint', search: 'blueprint', references: 'blueprint',
  addNode: 'blueprint', deleteNode: 'blueprint', connectPins: 'blueprint',
  disconnectPin: 'blueprint', setPinDefault: 'blueprint', moveNode: 'blueprint',
  compileBlueprint: 'blueprint', createBlueprint: 'blueprint', createGraph: 'blueprint',
  addVariable: 'blueprint', removeVariable: 'blueprint', validateBlueprint: 'blueprint',
  snapshotGraph: 'blueprint', restoreGraph: 'blueprint', getPinInfo: 'blueprint',
  listActors: 'actor', getActor: 'actor', spawnActor: 'actor',
  deleteActor: 'actor', setActorProperty: 'actor', setActorTransform: 'actor',
  listLevels: 'actor', loadLevel: 'actor', unloadLevel: 'actor',
  screenshot: 'scene_awareness', moveCamera: 'scene_awareness',
  pcgList: 'blueprint', pcgInspect: 'blueprint', pcgExecute: 'blueprint',
  animInspect: 'animation', animStates: 'animation', animTransitions: 'animation',
  seqCreate: 'level_sequence', seqAddTrack: 'level_sequence', seqGetTracks: 'level_sequence',
  matList: 'material', matInspect: 'material', matCreate: 'material',
  assetImport: 'assets', assetInspect: 'assets', assetDuplicate: 'assets',
  executePythonCapture: 'python_scripting', executeConsole: 'python_scripting',
};

// Normalize snake_case to camelCase for context lookups
function snakeToCamel(s) {
  return s.replace(/_([a-z])/g, (_, c) => c.toUpperCase());
}

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

  // 2. Load relevant context doc (normalize snake_case -> camelCase)
  const normalizedName = snakeToCamel(toolName);
  const category = TOOL_CONTEXT_MAP[normalizedName] || TOOL_CONTEXT_MAP[toolName];
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
 * Get the URL for a specific role.
 * Auto-discovers on first call. Falls back to any live server.
 */
async function getUrlForRole(role = 'worker') {
  await discoverServers();
  return RESOLVED_URLS[role] || RESOLVED_URLS['worker'] || DEFAULT_PORTS[role];
}

/**
 * Check if at least one llama.cpp server is running.
 * Triggers auto-discovery if not done yet.
 */
async function isLLMAvailable() {
  await discoverServers();
  return Object.values(RESOLVED_URLS).some(url => url !== null);
}

/**
 * Run inference on the local LLM to validate a plan step.
 * Returns { confidence, payload, raw_response }
 */
async function runWorkerInference(step, toolDef, context, attempt = 1, role = 'worker') {
  // Build the system prompt: role instructions + context
  const systemPrompt = [
    ROLE_INSTRUCTIONS[role] || '',
    '',
    'CONTEXT (check these before producing output):',
    context,
  ].join('\n');

  const userPrompt = `Execute this plan step by producing the exact JSON payload for the tool call.

Step: ${JSON.stringify(step, null, 2)}

RULES:
- Use ONLY parameter names from the TOOL REGISTRY section above.
- Use ONLY asset paths from SOURCE TRUTH if available.
- If you cannot determine a required value with certainty, respond with {"escalation": true, "reason": "..."}.
- Do NOT invent asset paths, pin names, or node types.

Respond with ONLY valid JSON. No explanation. No markdown.
Format: {"toolName": "${step.tool}", "payload": {...}}`;

  try {
    const url = await getUrlForRole(role);
    // Use /v1/chat/completions endpoint with system role for prompt injection
    const resp = await fetch(`${url}/v1/chat/completions`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        messages: [
          { role: 'system', content: systemPrompt },
          { role: 'user', content: userPrompt },
        ],
        max_tokens: 512,
        temperature: 0.1,  // near-deterministic
        top_p: 0.9,
        stop: ['\n\n'],
        // Request logprobs for confidence scoring
        logprobs: true,
        top_logprobs: 1,
      }),
      signal: AbortSignal.timeout(30000),
    });

    if (!resp.ok) {
      throw new Error(`LLM server returned ${resp.status}`);
    }

    const result = await resp.json();
    const choice = result.choices?.[0];
    const content = choice?.message?.content?.trim();

    // Extract confidence from logprobs if available
    let confidence = 0;
    const logprobsData = choice?.logprobs?.content;
    if (logprobsData && logprobsData.length > 0) {
      // Average token probability across the output (exp of logprob)
      const probs = logprobsData.map(t => Math.exp(t.logprob || -10));
      confidence = probs.reduce((a, b) => a + b, 0) / probs.length;
    } else if (result.usage?.completion_tokens) {
      // Heuristic fallback: if we got tokens but no logprobs, assume moderate confidence
      confidence = 0.80;
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
    context_used: Object.keys(TOOL_CONTEXT_MAP).includes(snakeToCamel(step.tool))
      ? [`tool-registry:${step.tool}`, `contexts/${TOOL_CONTEXT_MAP[snakeToCamel(step.tool)]}.md`]
      : [`tool-registry:${step.tool}`],
  };
}

/**
 * Run spatial analysis on a screenshot via Cosmos Reason2.
 * Accepts a base64-encoded image and a question about the scene.
 * Returns natural language analysis of spatial/physics issues.
 */
async function runSpatialAnalysis(imageBase64, question) {
  const systemPrompt = ROLE_INSTRUCTIONS['spatial'] || 'You are a spatial reasoning agent. Analyze the image for physics, spatial, and lighting issues.';
  const url = await getUrlForRole('spatial');

  try {
    const resp = await fetch(`${url}/v1/chat/completions`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        model: 'nvidia/Cosmos-Reason2-2B',
        messages: [
          { role: 'system', content: [{ type: 'text', text: systemPrompt }] },
          { role: 'user', content: [
            { type: 'image_url', image_url: { url: `data:image/png;base64,${imageBase64}` } },
            { type: 'text', text: question },
          ]},
        ],
        max_tokens: 1024,
        temperature: 0.2,
      }),
      signal: AbortSignal.timeout(60000),
    });

    if (!resp.ok) {
      throw new Error(`Spatial server returned ${resp.status}`);
    }

    const result = await resp.json();
    const content = result.choices?.[0]?.message?.content?.trim();
    return { success: true, analysis: content };
  } catch (error) {
    return { success: false, error: error.message };
  }
}

/**
 * Check if the Spatial Reasoner (Cosmos Reason2) is available.
 */
async function isSpatialAvailable() {
  try {
    const url = await getUrlForRole('spatial');
    const resp = await fetch(`${url}/health`, { signal: AbortSignal.timeout(2000) });
    return resp.ok;
  } catch {
    return false;
  }
}

export {
  isLLMAvailable,
  isSpatialAvailable,
  runWorkerInference,
  runSpatialAnalysis,
  executeWithConfidenceGate,
  loadContextForTool,
  getUrlForRole,
  ROLE_INSTRUCTIONS,
  CONFIDENCE_THRESHOLD,
  MAX_RETRIES,
};
