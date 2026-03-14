/**
 * Plan Dispatcher
 * 
 * Orchestrates the full execution pipeline:
 * 1. Receives a JSON plan from the Planner (Claude)
 * 2. Validates every step through the Rule Engine
 * 3. Dispatches approved steps to Workers (local LLM inference)
 * 4. Sends approved payloads to the C++ plugin via HTTP
 * 5. Collects results and escalates failures back to the Planner
 */

import { validatePlan, loadRegistry } from './rule-engine.js';
import { isLLMAvailable, executeWithConfidenceGate } from './llm-validator.js';

const UNREAL_MCP_URL = process.env.UNREAL_MCP_URL || 'http://localhost:9847';

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

/**
 * Execute a full plan end-to-end.
 * 
 * @param {object} plan - The JSON execution plan from the Planner
 * @param {object} options - Execution options
 * @param {function} options.onStepStart - Callback(step) when a step begins
 * @param {function} options.onStepComplete - Callback(step, result) when a step finishes
 * @param {function} options.onStepFail - Callback(step, error) when a step fails
 * @param {function} options.onEscalation - Callback(escalation) when a step escalates to Planner
 * @param {function} options.onScreenshot - Callback(imagePath) when a screenshot is captured
 * @param {boolean} options.dryRun - If true, validate only, do not execute
 * 
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

  // Phase 3: Execute steps sequentially
  const completedSteps = new Set();
  const capturedGuids = {};

  for (const step of plan.steps) {
    // Check dependencies
    if (step.depends_on) {
      const unmet = step.depends_on.filter(id => !completedSteps.has(id));
      if (unmet.length > 0) {
        const skipResult = {
          step_id: step.step_id,
          status: 'skipped',
          reason: `Unmet dependencies: steps ${unmet.join(', ')}`,
        };
        report.steps.push(skipResult);
        report.skipped++;
        continue;
      }
    }

    onStepStart(step);

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

    if (llmAvailable) {
      // Use Worker inference with confidence gating
      const toolDef = registry[step.tool];
      const workerResult = await executeWithConfidenceGate(resolvedStep, toolDef);

      if (workerResult.action === 'escalate') {
        report.escalations.push(workerResult);
        report.steps.push({ step_id: step.step_id, status: 'escalated', ...workerResult });
        report.failed++;
        onEscalation(workerResult);
        // Stop execution at first escalation -- Planner needs to revise
        break;
      }

      payload = workerResult.payload;
      confidence = workerResult.confidence;
    } else {
      // Direct execution -- use plan params directly (no confidence gating)
      payload = resolvedParams;
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

      report.steps.push(stepResult);
      report.completed++;
      completedSteps.add(step.step_id);
      onStepComplete(step, stepResult);
    } else {
      const failResult = {
        step_id: step.step_id,
        status: 'failed',
        confidence,
        error: pluginResult.data?.error || `HTTP ${pluginResult.status}`,
        data: pluginResult.data,
      };
      report.steps.push(failResult);
      report.failed++;
      onStepFail(step, failResult);

      // Escalate plugin failures to the Planner
      const escalation = {
        step_id: step.step_id,
        tool: step.tool,
        error: failResult.error,
        plugin_response: pluginResult.data,
        suggestion: 'Plugin returned an error. Check tool parameters and asset paths.',
      };
      report.escalations.push(escalation);
      onEscalation(escalation);
      break;
    }
  }

  report.finished_at = new Date().toISOString();
  report.status = report.failed === 0 && report.skipped === 0 ? 'completed' : 'partial';
  return report;
}

export { executePlan, sendToPlugin };
