/**
 * Request Handler
 * ===============
 * 
 * Replaces the Claude Planner with a direct inference loop.
 * 
 * Flow:
 *   1. User sends a natural language request
 *   2. Worker (Llama) infers the tool call(s) needed
 *   3. Each tool call goes through the full validation stack:
 *      - Rule engine validates params
 *      - Confidence gate checks score
 *      - Project state validator cross-checks live editor
 *      - Idempotency guard skips if already done
 *   4. C++ plugin executes
 *   5. Result feeds back to Worker for next step (if multi-step)
 *   6. Loop until Worker signals "done" or max iterations reached
 * 
 * No plans. No plan files. No escalation files. No polling.
 * Request in -> tool calls out -> results back -> done.
 */

import { readFileSync, existsSync, readdirSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import { loadRegistry } from './gatekeeper/rule-engine.js';
import { log } from './lib.js';

const __dirname = dirname(fileURLToPath(import.meta.url));

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

const MAX_ITERATIONS = parseInt(process.env.MAX_REQUEST_ITERATIONS || '10', 10);
const CONFIDENCE_THRESHOLD = parseFloat(process.env.CONFIDENCE_THRESHOLD || '0.80');

// ---------------------------------------------------------------------------
// Context Loading
// ---------------------------------------------------------------------------

// Load tool registry for the Worker's context
let _registry = null;
function getRegistry() {
  if (!_registry) _registry = loadRegistry();
  return _registry;
}

// Build a compact tool summary for the Worker's system prompt
function buildToolSummary() {
  const registry = getRegistry();
  const lines = [];
  for (const [name, def] of Object.entries(registry)) {
    const params = (def.parameters || [])
      .map(p => `${p.name}${p.required ? '*' : ''}:${p.type}`)
      .join(', ');
    lines.push(`- ${name}(${params}) -- ${def.description || ''}`);
  }
  return lines.join('\n');
}

// Load source truth files
function loadSourceTruth() {
  const dir = join(__dirname, '..', 'reference', 'source_truth');
  if (!existsSync(dir)) return '';
  
  const parts = [];
  const files = readdirSync(dir).filter(f => f !== 'README.md');
  for (const file of files) {
    try {
      const content = readFileSync(join(dir, file), 'utf-8');
      // Cap at 4KB per file to stay within context window
      parts.push(`=== ${file} ===\n${content.slice(0, 4096)}`);
    } catch { /* skip */ }
  }
  return parts.join('\n\n');
}

// Load relevant context doc for a specific tool
function loadToolContext(toolName) {
  const TOOL_CONTEXT_MAP = {
    addNode: 'blueprint', deleteNode: 'blueprint', connectPins: 'blueprint',
    disconnectPin: 'blueprint', setPinDefault: 'blueprint', moveNode: 'blueprint',
    compileBlueprint: 'blueprint', createBlueprint: 'blueprint', createGraph: 'blueprint',
    addVariable: 'blueprint', removeVariable: 'blueprint', validateBlueprint: 'blueprint',
    snapshotGraph: 'blueprint', restoreGraph: 'blueprint', getPinInfo: 'blueprint',
    listActors: 'actor', getActor: 'actor', spawnActor: 'actor',
    deleteActor: 'actor', setActorProperty: 'actor', setActorTransform: 'actor',
    listLevels: 'actor', loadLevel: 'actor', unloadLevel: 'actor',
    screenshot: 'scene_awareness', moveCamera: 'scene_awareness',
    animInspect: 'animation', animStates: 'animation', animTransitions: 'animation',
    seqCreate: 'level_sequence', seqAddTrack: 'level_sequence', seqGetTracks: 'level_sequence',
    matList: 'material', matInspect: 'material', matCreate: 'material',
    assetImport: 'assets', assetInspect: 'assets', assetDuplicate: 'assets',
    executePythonCapture: 'python_scripting', executeConsole: 'python_scripting',
  };

  // Normalize snake_case to camelCase
  const normalized = toolName.replace(/_([a-z])/g, (_, c) => c.toUpperCase());
  const category = TOOL_CONTEXT_MAP[normalized] || TOOL_CONTEXT_MAP[toolName];
  if (!category) return '';

  const contextPath = join(__dirname, 'contexts', `${category}.md`);
  if (!existsSync(contextPath)) return '';
  return readFileSync(contextPath, 'utf-8').slice(0, 8192);
}

// Load user-provided context files (Meta docs, custom notes, etc.)
function loadUserContext() {
  const dir = join(__dirname, '..', 'reference', 'user_context');
  if (!existsSync(dir)) return '';

  const parts = [];
  const files = readdirSync(dir).filter(f => !f.startsWith('.'));
  for (const file of files) {
    try {
      const content = readFileSync(join(dir, file), 'utf-8');
      parts.push(`=== USER CONTEXT: ${file} ===\n${content.slice(0, 8192)}`);
    } catch { /* skip */ }
  }
  return parts.join('\n\n');
}

// Load engine documentation path hints
function loadEngineDocsHint() {
  // Point the Worker at the local engine docs
  return `=== ENGINE DOCUMENTATION ===
UE Engine docs are available at: C:\\UE56\\Engine\\Documentation\\
- Blueprint API: C:\\UE56\\Engine\\Documentation\\Builds\\BlueprintAPI-HTML.tgz
- C++ API: C:\\UE56\\Engine\\Documentation\\Builds\\CppAPI-HTML.tgz
- Type docs: C:\\UE56\\Engine\\Documentation\\Source\\Shared\\Types\\
If you need to verify a class, function, or property name, reference these docs.`;
}

// ---------------------------------------------------------------------------
// Worker System Prompt
// ---------------------------------------------------------------------------

// Load the worker instruction file
const WORKER_INSTRUCTION_PATH = join(__dirname, '..', 'models', 'instructions', 'worker.md');
let workerInstruction = '';
if (existsSync(WORKER_INSTRUCTION_PATH)) {
  workerInstruction = readFileSync(WORKER_INSTRUCTION_PATH, 'utf-8');
}

// ---------------------------------------------------------------------------
// Inference
// ---------------------------------------------------------------------------

/**
 * Send a request to the local Llama server and get a tool call back.
 * 
 * @param {string} workerUrl - The llama.cpp server URL
 * @param {string} systemPrompt - Full system prompt with context
 * @param {string} userPrompt - The user's request + execution history
 * @returns {object} { toolName, payload, done, confidence, raw }
 */
async function inferToolCall(workerUrl, systemPrompt, userPrompt) {
  try {
    const resp = await fetch(`${workerUrl}/v1/chat/completions`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        messages: [
          { role: 'system', content: systemPrompt },
          { role: 'user', content: userPrompt },
        ],
        max_tokens: 512,
        temperature: 0.1,
        top_p: 0.9,
      }),
      signal: AbortSignal.timeout(30000),
    });

    if (!resp.ok) {
      throw new Error(`LLM server returned ${resp.status}`);
    }

    const result = await resp.json();
    const content = result.choices?.[0]?.message?.content?.trim();

    // Parse the JSON response
    let parsed = null;
    try {
      parsed = JSON.parse(content);
    } catch {
      // Try to extract JSON from the response
      const jsonMatch = content?.match(/\{[\s\S]*\}/);
      if (jsonMatch) {
        try { parsed = JSON.parse(jsonMatch[0]); } catch { /* failed */ }
      }
    }

    if (!parsed) {
      return { toolName: null, payload: null, done: false, confidence: 0, raw: content, error: 'Failed to parse JSON' };
    }

    // Check if Worker signals "done"
    if (parsed.done === true) {
      return { toolName: null, payload: null, done: true, confidence: 1.0, raw: content, summary: parsed.summary || 'Request completed.' };
    }

    // Score confidence structurally
    let score = 0;
    let checks = 0;

    // Check 1: Valid JSON with toolName
    checks++;
    if (parsed.toolName) score++;

    // Check 2: Has payload object
    checks++;
    if (parsed.payload && typeof parsed.payload === 'object') score++;

    // Check 3: Payload has at least one key
    checks++;
    if (parsed.payload && Object.keys(parsed.payload).length > 0) score++;

    // Check 4: No placeholder values
    checks++;
    const payloadStr = JSON.stringify(parsed.payload || {});
    if (!payloadStr.match(/UNKNOWN|TODO|PLACEHOLDER|undefined|null/i)) score++;

    // Check 5: Tool exists in registry
    checks++;
    const registry = getRegistry();
    const normalizedTool = (parsed.toolName || '').replace(/_([a-z])/g, (_, c) => c.toUpperCase());
    if (registry[normalizedTool] || registry[parsed.toolName]) score++;

    const confidence = checks > 0 ? score / checks : 0;

    return {
      toolName: parsed.toolName,
      payload: parsed.payload,
      done: false,
      confidence,
      raw: content,
    };
  } catch (error) {
    return { toolName: null, payload: null, done: false, confidence: 0, raw: error.message, error: error.message };
  }
}

// ---------------------------------------------------------------------------
// Main Request Handler
// ---------------------------------------------------------------------------

/**
 * Handle a natural language request end-to-end.
 * 
 * @param {string} request - The user's natural language request
 * @param {object} options
 * @param {string} options.workerUrl - llama.cpp server URL
 * @param {function} options.executeToolFn - async (toolName, payload) => result
 * @param {function} options.validateMutationFn - async (toolName, args, readFn) => result
 * @param {function} options.checkIdempotencyFn - async (toolName, args, readFn) => result
 * @param {function} options.evaluateGateFn - (toolName, args) => result
 * @returns {object} { success, steps, summary }
 */
export async function handleRequest(request, options) {
  const {
    workerUrl,
    executeToolFn,
    validateMutationFn,
    checkIdempotencyFn,
    evaluateGateFn,
    readToolFn,
  } = options;

  // Build the system prompt once
  const toolSummary = buildToolSummary();
  const sourceTruth = loadSourceTruth();
  const userContext = loadUserContext();
  const engineDocs = loadEngineDocsHint();

  const systemPrompt = [
    workerInstruction,
    '',
    '=== AVAILABLE TOOLS ===',
    toolSummary,
    '',
    sourceTruth,
    '',
    userContext,
    '',
    engineDocs,
  ].join('\n');

  // Execution loop
  const steps = [];
  let iteration = 0;

  // Build the initial user prompt
  let history = '';

  while (iteration < MAX_ITERATIONS) {
    iteration++;

    const userPrompt = `REQUEST: ${request}

${history ? `EXECUTION HISTORY (previous steps this session):\n${history}\n` : ''}
Produce the next tool call to fulfill this request.
If the request is fully complete, respond with: {"done": true, "summary": "What was accomplished"}
If you need to read something first (list actors, get blueprint, etc.), do that.
Respond with ONLY valid JSON. No explanation. No markdown.
Format: {"toolName": "name", "payload": {...}}`;

    // Infer the next tool call
    const inference = await inferToolCall(workerUrl, systemPrompt, userPrompt);

    // Check if done
    if (inference.done) {
      return {
        success: true,
        steps,
        summary: inference.summary,
        iterations: iteration,
      };
    }

    // Check for inference failure
    if (!inference.toolName || inference.confidence < CONFIDENCE_THRESHOLD) {
      log.warn('Low confidence inference', {
        iteration,
        confidence: inference.confidence,
        raw: inference.raw?.slice(0, 200),
      });

      // Add to history so Worker can self-correct
      history += `Step ${iteration}: FAILED - Could not determine tool call (confidence: ${(inference.confidence * 100).toFixed(0)}%). Raw: ${inference.raw?.slice(0, 100)}\n`;
      continue;
    }

    const { toolName, payload } = inference;

    // --- Validation Stack ---

    // 1. Confidence gate
    if (evaluateGateFn) {
      const gateResult = evaluateGateFn(toolName, payload);
      if (!gateResult.allowed) {
        history += `Step ${iteration}: BLOCKED by confidence gate - ${gateResult.reason} (confidence: ${gateResult.confidence?.toFixed(2)})\n`;
        continue;
      }
    }

    // 2. Project state validation
    if (validateMutationFn && readToolFn) {
      const validationResult = await validateMutationFn(toolName, payload, readToolFn);
      if (!validationResult.valid) {
        const failures = validationResult.failures?.map(f => f.message).join('; ') || 'Unknown validation failure';
        history += `Step ${iteration}: BLOCKED by project state validator - ${failures}\n`;
        continue;
      }
    }

    // 3. Idempotency check
    if (checkIdempotencyFn && readToolFn) {
      const idempotencyResult = await checkIdempotencyFn(toolName, payload, readToolFn);
      if (idempotencyResult.alreadyDone) {
        history += `Step ${iteration}: SKIPPED (already done) - ${idempotencyResult.reason}\n`;
        steps.push({ iteration, toolName, payload, status: 'skipped', reason: idempotencyResult.reason });
        continue;
      }
    }

    // --- Execute ---
    const result = await executeToolFn(toolName, payload);

    const stepResult = {
      iteration,
      toolName,
      payload,
      status: result.success ? 'completed' : 'failed',
      data: result.data || result.message,
    };
    steps.push(stepResult);

    // Add to history for next iteration
    if (result.success) {
      const dataStr = typeof result.data === 'object' ? JSON.stringify(result.data).slice(0, 500) : String(result.data || result.message).slice(0, 500);
      history += `Step ${iteration}: ${toolName} -> SUCCESS. Result: ${dataStr}\n`;
    } else {
      history += `Step ${iteration}: ${toolName} -> FAILED. Error: ${result.message || JSON.stringify(result.data).slice(0, 300)}\n`;
    }
  }

  // Max iterations reached
  return {
    success: steps.some(s => s.status === 'completed'),
    steps,
    summary: `Reached max iterations (${MAX_ITERATIONS}). ${steps.filter(s => s.status === 'completed').length} steps completed.`,
    iterations: iteration,
  };
}

export { buildToolSummary, loadSourceTruth, loadUserContext, loadToolContext, loadEngineDocsHint };
