/**
 * Request Handler (Native Tool Calling)
 * ======================================
 *
 * Direct inference loop using Llama's native tool calling format.
 * No free-form JSON parsing. No regex extraction. No planner.
 *
 * Flow:
 *   1. User sends a natural language request
 *   2. Worker (Llama) receives the request + all available tools as function defs
 *   3. Model outputs structured tool_calls (native format)
 *   4. Each tool call goes through the validation stack:
 *      - Rule engine validates params
 *      - Confidence gate checks structural integrity
 *      - Project state validator cross-checks live editor
 *      - Idempotency guard skips if already done
 *   5. C++ plugin executes
 *   6. Result feeds back as tool_result message for next iteration
 *   7. Loop until model responds with text (done) or max iterations reached
 *
 * Request in -> native tool_calls out -> results back -> done.
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

// Meta hzdb MCP server (device debugging, docs, Perfetto, 3D assets)
const HZDB_ENABLED = process.env.HZDB_ENABLED !== 'false';
const HZDB_COMMAND = process.env.HZDB_COMMAND || 'npx';
const HZDB_ARGS = (process.env.HZDB_ARGS || '-y @meta-quest/hzdb').split(' ');

// ---------------------------------------------------------------------------
// Tool Definition Builder
// ---------------------------------------------------------------------------

/**
 * Convert the tool-registry.json format into OpenAI-compatible function defs.
 * llama.cpp /v1/chat/completions accepts this format when started with --jinja.
 */
function buildToolDefinitions() {
  const registry = getRegistry();
  const tools = [];

  for (const [name, def] of Object.entries(registry)) {
    const properties = {};
    const required = [];

    if (def.parameters && def.parameters.length > 0) {
      for (const param of def.parameters) {
        properties[param.name] = {
          type: mapParamType(param.type),
          description: param.description || param.name,
        };
        if (param.required) {
          required.push(param.name);
        }
      }
    }

    tools.push({
      type: 'function',
      function: {
        name,
        description: def.description || name,
        parameters: {
          type: 'object',
          properties,
          required,
          additionalProperties: false,
        },
      },
    });
  }

  return tools;
}

/**
 * Map registry param types to JSON Schema types.
 */
function mapParamType(registryType) {
  const map = {
    string: 'string',
    number: 'number',
    integer: 'integer',
    boolean: 'boolean',
    object: 'object',
    array: 'array',
  };
  return map[registryType] || 'string';
}

// ---------------------------------------------------------------------------
// Meta hzdb Tool Definitions
// ---------------------------------------------------------------------------

/**
 * Meta's Horizon Debug Bridge (hzdb) MCP server tools.
 * These run via `npx @meta-quest/hzdb` CLI or the MQDH bundled server.
 * The Worker can call these alongside UE editor tools.
 */
function buildHzdbToolDefinitions() {
  if (!HZDB_ENABLED) return [];

  return [
    {
      type: 'function',
      function: {
        name: 'hzdb_search_doc',
        description: 'Search Meta Horizon OS documentation for Unreal Engine, Quest, XR SDK, Interaction SDK, hand tracking, passthrough, and all Meta platform features. Always specify: Unreal Engine, Meta XR SDK, Quest 3.',
        parameters: {
          type: 'object',
          properties: {
            query: { type: 'string', description: 'Search query. Use direct imperative language. Include platform (Unreal), SDK (Meta XR), and device (Quest 3).' },
          },
          required: ['query'],
          additionalProperties: false,
        },
      },
    },
    {
      type: 'function',
      function: {
        name: 'hzdb_fetch_doc',
        description: 'Fetch the full content of a specific Meta documentation page by URL. URLs must start with https://developers.meta.com/horizon/llmstxt/documentation/',
        parameters: {
          type: 'object',
          properties: {
            url: { type: 'string', description: 'Full URL to the documentation page' },
          },
          required: ['url'],
          additionalProperties: false,
        },
      },
    },
    {
      type: 'function',
      function: {
        name: 'hzdb_device_logcat',
        description: 'Retrieve Android logcat logs from a connected Meta Quest device via ADB. Useful for debugging crashes, performance, and runtime errors.',
        parameters: {
          type: 'object',
          properties: {
            lines: { type: 'integer', description: 'Number of log lines to retrieve (default 100)' },
            tag: { type: 'string', description: 'Filter by log tag' },
            level: { type: 'string', description: 'Minimum log level: verbose, debug, info, warning, error' },
            package: { type: 'string', description: 'Filter by package name' },
          },
          required: [],
          additionalProperties: false,
        },
      },
    },
    {
      type: 'function',
      function: {
        name: 'hzdb_screenshot',
        description: 'Capture a screenshot from a connected Meta Quest device via ADB.',
        parameters: {
          type: 'object',
          properties: {
            width: { type: 'integer', description: 'Screenshot width' },
            height: { type: 'integer', description: 'Screenshot height' },
          },
          required: [],
          additionalProperties: false,
        },
      },
    },
    {
      type: 'function',
      function: {
        name: 'hzdb_perfetto_context',
        description: 'Initialize performance analysis context. Must be called before other Perfetto trace tools.',
        parameters: {
          type: 'object',
          properties: {},
          required: [],
          additionalProperties: false,
        },
      },
    },
    {
      type: 'function',
      function: {
        name: 'hzdb_load_trace',
        description: 'Load a Perfetto trace file for analysis. Call hzdb_perfetto_context first.',
        parameters: {
          type: 'object',
          properties: {
            session_id: { type: 'string', description: 'The trace file name / session ID' },
          },
          required: ['session_id'],
          additionalProperties: false,
        },
      },
    },
    {
      type: 'function',
      function: {
        name: 'hzdb_trace_sql',
        description: 'Run a SQL query on a loaded Perfetto trace to retrieve performance data.',
        parameters: {
          type: 'object',
          properties: {
            session_id: { type: 'string', description: 'The trace session ID' },
            query: { type: 'string', description: 'SQL query to run against the trace' },
          },
          required: ['session_id', 'query'],
          additionalProperties: false,
        },
      },
    },
    {
      type: 'function',
      function: {
        name: 'hzdb_gpu_counters',
        description: 'Get GPU metric counters for frame ranges from a loaded trace.',
        parameters: {
          type: 'object',
          properties: {
            session_id: { type: 'string', description: 'The trace session ID' },
            start_ts: { type: 'array', description: 'Array of GPU frame start timestamps' },
            end_ts: { type: 'array', description: 'Array of GPU frame end timestamps' },
          },
          required: ['session_id', 'start_ts', 'end_ts'],
          additionalProperties: false,
        },
      },
    },
    {
      type: 'function',
      function: {
        name: 'hzdb_asset_search',
        description: 'Search Meta\'s 3D asset library for models. Returns download URLs and previews.',
        parameters: {
          type: 'object',
          properties: {
            prompt: { type: 'string', description: 'Text description of the 3D models to search for' },
            number_of_models: { type: 'integer', description: 'Number of results to return' },
          },
          required: ['prompt'],
          additionalProperties: false,
        },
      },
    },
  ];
}

/**
 * Map hzdb_ prefixed tool names to actual hzdb CLI commands.
 */
const HZDB_TOOL_MAP = {
  hzdb_search_doc: 'search-doc',
  hzdb_fetch_doc: 'fetch-meta-quest-doc',
  hzdb_device_logcat: 'logcat',
  hzdb_screenshot: 'screenshot',
  hzdb_perfetto_context: 'get-perfetto-context',
  hzdb_load_trace: 'load-trace-for-analysis',
  hzdb_trace_sql: 'run-sql-query',
  hzdb_gpu_counters: 'get-counter-for-gpu-frames',
  hzdb_asset_search: 'asset-search',
};

/**
 * Execute an hzdb tool via CLI subprocess.
 * Returns { success, data/message }.
 */
import { execFile } from 'child_process';
import { promisify } from 'util';
const execFileAsync = promisify(execFile);

async function executeHzdbTool(toolName, args) {
  const cmd = HZDB_TOOL_MAP[toolName];
  if (!cmd) return { success: false, message: `Unknown hzdb tool: ${toolName}` };

  const cliArgs = [...HZDB_ARGS, cmd];

  // Convert args object to CLI flags
  for (const [key, value] of Object.entries(args || {})) {
    if (value === undefined || value === null) continue;
    const flag = `--${key.replace(/_/g, '-')}`;
    if (typeof value === 'boolean') {
      if (value) cliArgs.push(flag);
    } else if (Array.isArray(value)) {
      cliArgs.push(flag, JSON.stringify(value));
    } else {
      cliArgs.push(flag, String(value));
    }
  }

  try {
    const { stdout, stderr } = await execFileAsync(HZDB_COMMAND, cliArgs, {
      timeout: 30000,
      maxBuffer: 1024 * 1024,
    });
    return { success: true, data: stdout || stderr };
  } catch (error) {
    return { success: false, message: error.message || 'hzdb command failed' };
  }
}

// Also export a "done" pseudo-tool so the model can signal completion
function buildDoneTool() {
  return {
    type: 'function',
    function: {
      name: 'request_complete',
      description: 'Call this when the user request has been fully completed. Provide a summary of what was accomplished.',
      parameters: {
        type: 'object',
        properties: {
          summary: {
            type: 'string',
            description: 'Brief summary of what was accomplished',
          },
        },
        required: ['summary'],
        additionalProperties: false,
      },
    },
  };
}

// ---------------------------------------------------------------------------
// Context Loading
// ---------------------------------------------------------------------------

let _registry = null;
function getRegistry() {
  if (!_registry) _registry = loadRegistry();
  return _registry;
}

function loadSourceTruth() {
  const dir = join(__dirname, '..', 'reference', 'source_truth');
  if (!existsSync(dir)) return '';

  const parts = [];
  const files = readdirSync(dir).filter(f => f !== 'README.md');
  for (const file of files) {
    try {
      const content = readFileSync(join(dir, file), 'utf-8');
      parts.push(`=== SOURCE TRUTH: ${file} ===\n${content.slice(0, 4096)}`);
    } catch { /* skip */ }
  }
  return parts.join('\n\n');
}

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

function loadEngineDocsHint() {
  return `=== ENGINE DOCUMENTATION ===
UE Engine docs are available at: C:\\UE56\\Engine\\Documentation\\
If you need to verify a class, function, or property name, reference these docs.`;
}

// Load tool-specific context docs
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

  const normalized = toolName.replace(/_([a-z])/g, (_, c) => c.toUpperCase());
  const category = TOOL_CONTEXT_MAP[normalized] || TOOL_CONTEXT_MAP[toolName];
  if (!category) return '';

  const contextPath = join(__dirname, 'contexts', `${category}.md`);
  if (!existsSync(contextPath)) return '';
  return readFileSync(contextPath, 'utf-8').slice(0, 8192);
}

// ---------------------------------------------------------------------------
// Worker System Prompt
// ---------------------------------------------------------------------------

const WORKER_INSTRUCTION_PATH = join(__dirname, '..', 'models', 'instructions', 'worker.md');
let workerInstruction = '';
if (existsSync(WORKER_INSTRUCTION_PATH)) {
  workerInstruction = readFileSync(WORKER_INSTRUCTION_PATH, 'utf-8');
}

// ---------------------------------------------------------------------------
// Inference (Native Tool Calling)
// ---------------------------------------------------------------------------

/**
 * Send a chat completion request with native tool definitions.
 * The model responds with either:
 *   - tool_calls: structured function calls to execute
 *   - content: text response (signals done or asks for clarification)
 *
 * @param {string} workerUrl - llama.cpp server URL
 * @param {Array} messages - Full conversation history
 * @param {Array} tools - OpenAI-format tool definitions
 * @returns {object} The raw chat completion response
 */
async function inferWithTools(workerUrl, messages, tools) {
  const resp = await fetch(`${workerUrl}/v1/chat/completions`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      messages,
      tools,
      tool_choice: 'auto',
      max_tokens: 1024,
      temperature: 0.1,
      top_p: 0.9,
    }),
    signal: AbortSignal.timeout(60000),
  });

  if (!resp.ok) {
    const body = await resp.text().catch(() => '');
    throw new Error(`LLM server returned ${resp.status}: ${body.slice(0, 200)}`);
  }

  return resp.json();
}

/**
 * Score confidence of a tool call based on structural checks.
 * Since native tool calling guarantees valid JSON structure,
 * confidence is inherently higher than free-form parsing.
 */
function scoreToolCall(toolCall) {
  const registry = getRegistry();
  let score = 0;
  let checks = 0;

  // Check 1: Has function name
  checks++;
  if (toolCall.function?.name) score++;

  // Check 2: Tool exists in registry
  checks++;
  const name = toolCall.function?.name;
  if (registry[name]) score++;

  // Check 3: Arguments parse as valid JSON
  checks++;
  let args = null;
  try {
    args = typeof toolCall.function?.arguments === 'string'
      ? JSON.parse(toolCall.function.arguments)
      : toolCall.function?.arguments;
    if (args && typeof args === 'object') score++;
  } catch { /* invalid JSON */ }

  // Check 4: No placeholder values
  checks++;
  const argsStr = JSON.stringify(args || {});
  if (!argsStr.match(/UNKNOWN|TODO|PLACEHOLDER|undefined/i)) score++;

  // Check 5: Has at least one argument (most tools need params)
  checks++;
  if (args && Object.keys(args).length > 0) score++;

  return checks > 0 ? score / checks : 0;
}

// ---------------------------------------------------------------------------
// Main Request Handler
// ---------------------------------------------------------------------------

/**
 * Handle a natural language request end-to-end using native tool calling.
 *
 * @param {string} request - The user's natural language request
 * @param {object} options
 * @param {string} options.workerUrl - llama.cpp server URL
 * @param {function} options.executeToolFn - async (toolName, payload) => result
 * @param {function} options.validateMutationFn - async (toolName, args, readFn) => result
 * @param {function} options.checkIdempotencyFn - async (toolName, args, readFn) => result
 * @param {function} options.evaluateGateFn - (toolName, args) => result
 * @param {function} options.readToolFn - async (toolName, args) => result
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

  // Build system prompt with all context
  const sourceTruth = loadSourceTruth();
  const userContext = loadUserContext();
  const engineDocs = loadEngineDocsHint();

  const systemPrompt = [
    workerInstruction,
    '',
    sourceTruth,
    '',
    userContext,
    '',
    engineDocs,
  ].filter(Boolean).join('\n');

  // Build native tool definitions from registry + hzdb tools
  const tools = [...buildToolDefinitions(), ...buildHzdbToolDefinitions(), buildDoneTool()];

  // Conversation history for multi-turn tool calling
  const messages = [
    { role: 'system', content: systemPrompt },
    { role: 'user', content: request },
  ];

  const steps = [];
  let iteration = 0;

  while (iteration < MAX_ITERATIONS) {
    iteration++;

    // --- Inference ---
    let result;
    try {
      result = await inferWithTools(workerUrl, messages, tools);
    } catch (error) {
      log.error('Inference failed', { iteration, error: error.message });
      // Add error as assistant message so model can self-correct
      messages.push({ role: 'assistant', content: `Error: ${error.message}` });
      continue;
    }

    const choice = result.choices?.[0];
    if (!choice) {
      log.error('No choice in response', { iteration });
      break;
    }

    const message = choice.message;

    // --- Case 1: Model responded with text (done or clarification) ---
    if (choice.finish_reason === 'stop' || (!message.tool_calls && message.content)) {
      return {
        success: steps.some(s => s.status === 'completed'),
        steps,
        summary: message.content || 'Request completed.',
        iterations: iteration,
      };
    }

    // --- Case 2: Model wants to call tools ---
    if (message.tool_calls && message.tool_calls.length > 0) {
      // Add the assistant message with tool_calls to history
      messages.push(message);

      for (const toolCall of message.tool_calls) {
        const toolName = toolCall.function?.name;
        let args;
        try {
          args = typeof toolCall.function?.arguments === 'string'
            ? JSON.parse(toolCall.function.arguments)
            : toolCall.function?.arguments || {};
        } catch {
          args = {};
          log.warn('Failed to parse tool arguments', { toolName, raw: toolCall.function?.arguments });
        }

        // --- Handle "done" signal ---
        if (toolName === 'request_complete') {
          // Push tool result to close the conversation properly
          messages.push({
            role: 'tool',
            tool_call_id: toolCall.id,
            content: JSON.stringify({ acknowledged: true }),
          });
          return {
            success: steps.some(s => s.status === 'completed'),
            steps,
            summary: args.summary || 'Request completed.',
            iterations: iteration,
          };
        }

        // --- Confidence Check ---
        const confidence = scoreToolCall(toolCall);
        if (confidence < CONFIDENCE_THRESHOLD) {
          log.warn('Low confidence tool call', { iteration, toolName, confidence });
          messages.push({
            role: 'tool',
            tool_call_id: toolCall.id,
            content: JSON.stringify({
              error: `Low confidence (${(confidence * 100).toFixed(0)}%). Check tool name and parameters against the registry.`,
            }),
          });
          steps.push({ iteration, toolName, payload: args, status: 'rejected', reason: `confidence ${(confidence * 100).toFixed(0)}%` });
          continue;
        }

        // --- Confidence Gate ---
        if (evaluateGateFn) {
          const gateResult = evaluateGateFn(toolName, args);
          if (!gateResult.allowed) {
            messages.push({
              role: 'tool',
              tool_call_id: toolCall.id,
              content: JSON.stringify({ error: `Blocked by confidence gate: ${gateResult.reason}` }),
            });
            steps.push({ iteration, toolName, payload: args, status: 'blocked', reason: gateResult.reason });
            continue;
          }
        }

        // --- Project State Validation ---
        if (validateMutationFn && readToolFn) {
          const validationResult = await validateMutationFn(toolName, args, readToolFn);
          if (!validationResult.valid) {
            const failures = validationResult.failures?.map(f => f.message).join('; ') || 'Validation failed';
            messages.push({
              role: 'tool',
              tool_call_id: toolCall.id,
              content: JSON.stringify({ error: `Validation failed: ${failures}` }),
            });
            steps.push({ iteration, toolName, payload: args, status: 'blocked', reason: failures });
            continue;
          }
        }

        // --- Idempotency Check ---
        if (checkIdempotencyFn && readToolFn) {
          const idempotencyResult = await checkIdempotencyFn(toolName, args, readToolFn);
          if (idempotencyResult.alreadyDone) {
            messages.push({
              role: 'tool',
              tool_call_id: toolCall.id,
              content: JSON.stringify({ skipped: true, reason: idempotencyResult.reason }),
            });
            steps.push({ iteration, toolName, payload: args, status: 'skipped', reason: idempotencyResult.reason });
            continue;
          }
        }

        // --- Route: hzdb tools vs UE tools ---
        let execResult;
        if (toolName.startsWith('hzdb_')) {
          // Meta hzdb tool - execute via CLI
          execResult = await executeHzdbTool(toolName, args);
        } else {
          // UE editor tool - execute via C++ plugin
          execResult = await executeToolFn(toolName, args);
        }

        const stepResult = {
          iteration,
          toolName,
          payload: args,
          status: execResult.success ? 'completed' : 'failed',
          data: execResult.data || execResult.message,
        };
        steps.push(stepResult);

        // Feed result back as tool message for next iteration
        const resultContent = execResult.success
          ? JSON.stringify(execResult.data || { success: true, message: execResult.message }).slice(0, 2000)
          : JSON.stringify({ error: execResult.message || 'Execution failed' }).slice(0, 2000);

        messages.push({
          role: 'tool',
          tool_call_id: toolCall.id,
          content: resultContent,
        });
      }
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

// ---------------------------------------------------------------------------
// Exports
// ---------------------------------------------------------------------------

export {
  buildToolDefinitions,
  buildHzdbToolDefinitions,
  buildDoneTool,
  loadSourceTruth,
  loadUserContext,
  loadToolContext,
  loadEngineDocsHint,
  scoreToolCall,
  executeHzdbTool,
  HZDB_TOOL_MAP,
};
