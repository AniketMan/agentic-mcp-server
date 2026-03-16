/**
 * Plan Dispatcher (Parallel Execution)
 * 
 * Orchestrates the full execution pipeline with parallel step execution:
 * 1. Receives a JSON plan from the Planner (Claude)
 * 2. Validates every step through the Rule Engine
 * 3. Groups steps into parallel waves based on dependency graph
 * 4. Dispatches each wave concurrently (max 4 simultaneous, per MCPTaskQueue limit)
 * 5. Sends approved payloads to the C++ plugin via HTTP
 * 6. Collects results and escalates failures back to the Planner
 * 
 * Implements Minions-style parallel subtask execution (arxiv: 2502.15964).
 * Respects tool parallelization classes from parallel_workflows.md:
 *   - Parallel-safe (read-only): freely concurrent
 *   - Per-object safe (modifying): concurrent on DIFFERENT objects only
 *   - Sequential-only: must run alone (open_level, delete_actors, execute_script)
 */

import { validatePlan, loadRegistry } from './rule-engine.js';
import { isLLMAvailable, executeWithConfidenceGate, runWorkerInference, loadContextForTool } from './llm-validator.js';
import { selectTechnique, recordOutcome, TECHNIQUES, DIRECT_TOOLS } from './technique-selector.js';

const UNREAL_MCP_URL = process.env.UNREAL_MCP_URL || 'http://localhost:9847';

// Normalize snake_case to camelCase so both conventions match
function snakeToCamel(s) {
  return s.replace(/_([a-z])/g, (_, c) => c.toUpperCase());
}

// Max concurrent tool calls per MCPTaskQueue constraint
const MAX_CONCURRENT = parseInt(process.env.MCP_MAX_CONCURRENT || '4', 10);

// Tools that must run alone -- never in parallel with anything
const SEQUENTIAL_ONLY_TOOLS = new Set([
  'openLevel', 'deleteActors', 'executeScript',
  'cleanupScripts', 'runConsoleCommand',
]);

// Read-only tools that are always safe to parallelize
const READ_ONLY_TOOLS = new Set([
  'assetSearch', 'getLevelActors', 'blueprintQuery',
  'assetDependencies', 'assetReferencers', 'captureViewport',
  'getOutputLog', 'screenshot', 'list', 'listActors',
  'getActorProperties', 'snapshotGraph', 'getScenePlan',
  'getAllScenePlans', 'getWiringStatus', 'quantizeProject',
]);

/**
 * Send a tool call to the C++ plugin.
 */
async function sendToPlugin(toolName, payload) {
  try {
    const resp = await fetch(`${UNREAL_MCP_URL}/mcp/tool/${toolName}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
      signal: AbortSignal.timeout(30000),
    });

    const text = await resp.text();
    let data;
    try { data = JSON.parse(text); } catch { data = { raw: text }; }

    return {
      success: resp.ok,
      status: resp.status,
      data,
    };
  } catch (error) {
    return {
      success: false,
      status: 0,
      data: { error: error.message },
    };
  }
}

// ---------------------------------------------------------------------------
// Dependency Graph & Wave Builder
// ---------------------------------------------------------------------------

/**
 * Extract the target object from a step's params for conflict detection.
 * Two modifying steps conflict if they target the same object.
 */
function getTargetObject(step) {
  const p = step.params || {};
  return p.blueprintName || p.blueprint || p.actorName || p.actorLabel
    || p.name || p.levelName || p.materialName || null;
}

/**
 * Check if two modifying steps conflict (same target object).
 */
function stepsConflict(a, b) {
  // Sequential-only tools conflict with everything
  if (SEQUENTIAL_ONLY_TOOLS.has(snakeToCamel(a.tool)) || SEQUENTIAL_ONLY_TOOLS.has(snakeToCamel(b.tool))) {
    return true;
  }

  // Read-only tools never conflict
  if (READ_ONLY_TOOLS.has(snakeToCamel(a.tool)) && READ_ONLY_TOOLS.has(snakeToCamel(b.tool))) {
    return false;
  }

  // If either is modifying, check if they target the same object
  const targetA = getTargetObject(a);
  const targetB = getTargetObject(b);

  if (targetA && targetB && targetA === targetB) {
    return true; // Same object -- not safe to parallelize
  }

  return false;
}

/**
 * Build execution waves from the plan's dependency graph.
 * 
 * A wave is a group of steps that can execute concurrently because:
 * 1. All their depends_on steps have already completed in prior waves
 * 2. No two steps in the same wave conflict (same target object or sequential-only)
 * 3. Wave size <= MAX_CONCURRENT
 * 
 * This is a topological sort with conflict-aware grouping.
 */
function buildWaves(steps) {
  const waves = [];
  const completed = new Set();
  const remaining = [...steps];

  while (remaining.length > 0) {
    // Find all steps whose dependencies are met
    const ready = [];
    const notReady = [];

    for (const step of remaining) {
      const deps = step.depends_on || [];
      const allMet = deps.every(id => completed.has(id));
      if (allMet) {
        ready.push(step);
      } else {
        notReady.push(step);
      }
    }

    if (ready.length === 0) {
      // Deadlock -- remaining steps have circular or unresolvable dependencies
      // Push them all as skipped
      for (const step of notReady) {
        waves.push([{ ...step, _forced_skip: true }]);
      }
      break;
    }

    // Group ready steps into conflict-free waves of max MAX_CONCURRENT
    const grouped = [];
    const ungrouped = [...ready];

    while (ungrouped.length > 0) {
      const wave = [];
      const deferred = [];

      for (const step of ungrouped) {
        // Sequential-only tools always get their own wave
        if (SEQUENTIAL_ONLY_TOOLS.has(snakeToCamel(step.tool))) {
          if (wave.length === 0) {
            wave.push(step);
            // Sequential-only means nothing else in this wave
            deferred.push(...ungrouped.filter(s => s !== step));
            break;
          } else {
            deferred.push(step);
            continue;
          }
        }

        // Check for conflicts with steps already in this wave
        const conflicts = wave.some(existing => stepsConflict(existing, step));
        if (!conflicts && wave.length < MAX_CONCURRENT) {
          wave.push(step);
        } else {
          deferred.push(step);
        }
      }

      if (wave.length > 0) {
        grouped.push(wave);
        // Mark wave steps as completed for next iteration
        for (const s of wave) {
          completed.add(s.step_id);
        }
      }
      ungrouped.length = 0;
      ungrouped.push(...deferred);
    }

    waves.push(...grouped);

    // Update remaining
    remaining.length = 0;
    remaining.push(...notReady);
  }

  return waves;
}

// ---------------------------------------------------------------------------
// Step Execution
// ---------------------------------------------------------------------------

/**
 * Execute a single step through the full pipeline:
 * GUID resolution -> Worker inference -> Plugin dispatch
 */
async function executeStep(step, { registry, llmAvailable, capturedGuids, onStepStart, onStepComplete, onStepFail, onEscalation, onScreenshot }) {
  onStepStart(step);

  // Handle forced skips (deadlocked dependencies)
  if (step._forced_skip) {
    return {
      step_id: step.step_id,
      status: 'skipped',
      reason: 'Unresolvable dependency cycle',
    };
  }

  // Resolve GUID references
  const resolvedParams = { ...step.params };
  if (step.use_guid) {
    for (const [paramName, guidVar] of Object.entries(step.use_guid)) {
      if (capturedGuids[guidVar]) {
        resolvedParams[paramName] = capturedGuids[guidVar];
      }
    }
  }
  const resolvedStep = { ...step, params: resolvedParams };

  let payload;
  let confidence = 1.0;
  let technique;

  // Adaptive technique selection (ARCHON-inspired)
  // Normalize tool name for registry lookup
  const normalizedTool = snakeToCamel(step.tool);
  technique = selectTechnique(resolvedStep);
  console.error(`[GATEKEEPER] Step ${step.step_id} (${step.tool}): technique=${technique.name} reason=${technique.reason}`);

  if (technique.name === 'DIRECT' || !llmAvailable) {
    // DIRECT: read-only tools or LLM unavailable -- pass params through
    payload = resolvedParams;
    confidence = 1.0;
  } else if (technique.name === 'ENSEMBLE') {
    // ENSEMBLE: Worker + Validator both infer, require consensus
    const toolDef = registry[normalizedTool] || registry[step.tool];
    const context = loadContextForTool(normalizedTool, toolDef);

    const [workerResult, validatorResult] = await Promise.all([
      runWorkerInference(resolvedStep, toolDef, context, 1, 'worker'),
      runWorkerInference(resolvedStep, toolDef, context, 1, 'validator'),
    ]);

    // Check consensus: both must produce valid JSON and agree on key params
    const workerPayload = workerResult.payload?.payload || workerResult.payload;
    const validatorPayload = validatorResult.payload?.payload || validatorResult.payload;

    if (workerResult.escalation || validatorResult.escalation) {
      const escalation = {
        action: 'escalate',
        step_id: step.step_id,
        tool: step.tool,
        reason: 'Ensemble: one or both models requested escalation',
        worker_confidence: workerResult.confidence,
        validator_confidence: validatorResult.confidence,
      };
      onEscalation(escalation);
      recordOutcome(step.tool, false, 0);
      return { step_id: step.step_id, status: 'escalated', ...escalation };
    }

    // Consensus check: JSON stringify comparison of sorted keys
    const workerStr = JSON.stringify(workerPayload, Object.keys(workerPayload || {}).sort());
    const validatorStr = JSON.stringify(validatorPayload, Object.keys(validatorPayload || {}).sort());
    const consensus = workerStr === validatorStr;
    const avgConfidence = (workerResult.confidence + validatorResult.confidence) / 2;

    if (consensus && avgConfidence >= technique.confidence_threshold) {
      payload = workerPayload;
      confidence = avgConfidence;
    } else if (!consensus) {
      // No consensus -- fall back to MULTI_PASS with Worker only
      console.error(`[GATEKEEPER] Ensemble disagreement on step ${step.step_id}. Falling back to MULTI_PASS.`);
      const fallbackResult = await executeWithConfidenceGate(resolvedStep, toolDef);
      if (fallbackResult.action === 'escalate') {
        onEscalation(fallbackResult);
        recordOutcome(step.tool, false, 0);
        return { step_id: step.step_id, status: 'escalated', ...fallbackResult };
      }
      payload = fallbackResult.payload;
      confidence = fallbackResult.confidence;
    } else {
      // Consensus but low confidence -- escalate
      const escalation = {
        action: 'escalate',
        step_id: step.step_id,
        tool: step.tool,
        reason: `Ensemble consensus but confidence ${(avgConfidence * 100).toFixed(1)}% below ${(technique.confidence_threshold * 100).toFixed(0)}%`,
      };
      onEscalation(escalation);
      recordOutcome(step.tool, false, avgConfidence);
      return { step_id: step.step_id, status: 'escalated', ...escalation };
    }
  } else {
    // SINGLE or MULTI_PASS: standard Worker inference with confidence gating
    const toolDef = registry[normalizedTool] || registry[step.tool];
    const workerResult = await executeWithConfidenceGate(resolvedStep, toolDef);

    if (workerResult.action === 'escalate') {
      onEscalation(workerResult);
      recordOutcome(step.tool, false, workerResult.max_confidence || 0);
      return {
        step_id: step.step_id,
        status: 'escalated',
        ...workerResult,
      };
    }

    payload = workerResult.payload;
    confidence = workerResult.confidence;
  }

  // Send to C++ plugin
  const pluginResult = await sendToPlugin(step.tool, payload);

  if (pluginResult.success) {
    const stepResult = {
      step_id: step.step_id,
      status: 'completed',
      confidence,
      data: pluginResult.data,
    };

    // Capture GUIDs if requested
    if (step.capture_guid && pluginResult.data) {
      const guid = pluginResult.data.nodeGuid || pluginResult.data.guid || pluginResult.data.id;
      if (guid) {
        capturedGuids[step.capture_guid] = guid;
        stepResult.captured_guid = { [step.capture_guid]: guid };
      }
    }

    // Check for screenshots
    if (step.tool === 'screenshot' && pluginResult.data?.path) {
      onScreenshot(pluginResult.data.path);
    }

    recordOutcome(step.tool, true, confidence);
    stepResult.technique = technique.name;
    onStepComplete(step, stepResult);
    return stepResult;
  } else {
    const failResult = {
      step_id: step.step_id,
      status: 'failed',
      confidence,
      error: pluginResult.data?.error || `HTTP ${pluginResult.status}`,
      data: pluginResult.data,
    };
    recordOutcome(step.tool, false, confidence);
    failResult.technique = technique.name;
    onStepFail(step, failResult);
    return failResult;
  }
}

// ---------------------------------------------------------------------------
// Main Executor
// ---------------------------------------------------------------------------

/**
 * Execute a full plan end-to-end with parallel wave execution.
 * 
 * Steps are grouped into waves based on their dependency graph and
 * tool conflict rules. Each wave executes concurrently up to
 * MAX_CONCURRENT (default 4). Waves execute sequentially -- wave N+1
 * starts only after wave N completes.
 * 
 * If any step in a wave fails or escalates, the entire plan halts
 * and the failure is reported back to the Planner for revision.
 * 
 * @param {object} plan - The JSON execution plan from the Planner
 * @param {object} options - Execution options
 * @returns {object} Execution report
 */
async function executePlan(plan, options = {}) {
  const {
    onStepStart = () => {},
    onStepComplete = () => {},
    onStepFail = () => {},
    onEscalation = () => {},
    onScreenshot = () => {},
    dryRun = false,
  } = options;

  const registry = loadRegistry();
  const report = {
    plan_id: plan.plan_id,
    started_at: new Date().toISOString(),
    steps: [],
    escalations: [],
    waves: [],
    completed: 0,
    failed: 0,
    skipped: 0,
  };

  // Phase 1: Validate the entire plan with the rule engine
  const validation = validatePlan(plan);
  if (!validation.valid) {
    return {
      ...report,
      status: 'rejected',
      validation,
      message: `Plan rejected by rule engine: ${validation.firstError?.errors?.[0]?.message}`,
    };
  }

  if (validation.planWarnings.length > 0) {
    console.error(`[GATEKEEPER] Plan warnings: ${validation.planWarnings.map(w => w.message).join('; ')}`);
  }

  if (dryRun) {
    return { ...report, status: 'dry_run_passed', validation };
  }

  // Phase 2: Check if local LLM is available
  const llmAvailable = await isLLMAvailable();
  if (!llmAvailable) {
    console.error('[GATEKEEPER] Local LLM not available. Falling back to direct execution (no confidence gating).');
  }

  // Phase 3: Build execution waves from dependency graph
  const waves = buildWaves(plan.steps);
  report.wave_count = waves.length;
  report.parallelized_steps = waves.filter(w => w.length > 1).reduce((sum, w) => sum + w.length, 0);

  console.error(`[GATEKEEPER] Plan decomposed into ${waves.length} waves (${plan.steps.length} steps, ${report.parallelized_steps} parallelized)`);

  // Phase 4: Execute waves
  const capturedGuids = {};
  let halted = false;

  for (let waveIdx = 0; waveIdx < waves.length; waveIdx++) {
    if (halted) break;

    const wave = waves[waveIdx];
    const waveReport = {
      wave_id: waveIdx + 1,
      step_count: wave.length,
      parallel: wave.length > 1,
      step_ids: wave.map(s => s.step_id),
    };

    console.error(`[GATEKEEPER] Wave ${waveIdx + 1}/${waves.length}: ${wave.length} step(s) [${wave.map(s => s.tool).join(', ')}]`);

    // Execute all steps in this wave concurrently
    const results = await Promise.all(
      wave.map(step => executeStep(step, {
        registry,
        llmAvailable,
        capturedGuids,
        onStepStart,
        onStepComplete,
        onStepFail,
        onEscalation,
        onScreenshot,
      }))
    );

    // Process results
    for (const result of results) {
      report.steps.push(result);

      if (result.status === 'completed') {
        report.completed++;
      } else if (result.status === 'skipped') {
        report.skipped++;
      } else if (result.status === 'escalated') {
        report.escalations.push(result);
        report.failed++;
        halted = true;
      } else if (result.status === 'failed') {
        report.failed++;
        // Escalate plugin failures to the Planner
        const escalation = {
          step_id: result.step_id,
          tool: wave.find(s => s.step_id === result.step_id)?.tool,
          error: result.error,
          plugin_response: result.data,
          suggestion: 'Plugin returned an error. Check tool parameters and asset paths.',
        };
        report.escalations.push(escalation);
        onEscalation(escalation);
        halted = true;
      }
    }

    waveReport.results = results.map(r => ({ step_id: r.step_id, status: r.status }));
    report.waves.push(waveReport);
  }

  report.finished_at = new Date().toISOString();
  report.status = report.failed === 0 && report.skipped === 0 ? 'completed' : 'partial';
  return report;
}

export { executePlan, sendToPlugin, buildWaves, MAX_CONCURRENT };
