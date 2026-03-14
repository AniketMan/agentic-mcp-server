/**
 * Deterministic Rule Engine (Layer 2a)
 * 
 * Zero-latency, model-free validation layer.
 * Checks every plan step against the tool registry and workflow rules.
 * No LLM needed -- pure JSON schema validation and state tracking.
 */

import { readFileSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));

// Load tool registry once at startup
let toolRegistry = null;

function loadRegistry() {
  if (toolRegistry) return toolRegistry;
  const raw = readFileSync(join(__dirname, '..', 'tool-registry.json'), 'utf-8');
  const data = JSON.parse(raw);
  toolRegistry = {};
  for (const tool of data.tools) {
    toolRegistry[tool.name] = tool;
  }
  return toolRegistry;
}

/**
 * Workflow rules that must be enforced across the plan.
 * These are stateful -- they track what has happened so far.
 */
class WorkflowState {
  constructor() {
    this.snapshotTaken = new Set();   // blueprints that have been snapshotted
    this.compilePending = new Set();  // blueprints that have been mutated but not compiled
    this.completedSteps = new Set();  // step IDs that have completed
    this.capturedGuids = {};          // variable name -> GUID from previous steps
  }
}

/**
 * Validate a single plan step against the registry and workflow rules.
 * Returns { valid: true } or { valid: false, errors: [...] }
 */
function validateStep(step, workflowState) {
  const registry = loadRegistry();
  const errors = [];

  // 1. Check tool exists
  const toolDef = registry[step.tool];
  if (!toolDef) {
    errors.push({
      code: 'UNKNOWN_TOOL',
      message: `Tool "${step.tool}" not found in registry. Available tools: ${Object.keys(registry).length}`,
      severity: 'fatal'
    });
    return { valid: false, errors };
  }

  // 2. Check required parameters
  if (toolDef.parameters) {
    for (const param of toolDef.parameters) {
      if (param.required && !(param.name in (step.params || {}))) {
        errors.push({
          code: 'MISSING_REQUIRED_PARAM',
          message: `Tool "${step.tool}" requires parameter "${param.name}" (${param.description})`,
          severity: 'fatal'
        });
      }
    }
  }

  // 3. Check for unknown parameters (not in registry)
  if (step.params && toolDef.parameters) {
    const knownParams = new Set(toolDef.parameters.map(p => p.name));
    for (const key of Object.keys(step.params)) {
      if (!knownParams.has(key)) {
        errors.push({
          code: 'UNKNOWN_PARAM',
          message: `Parameter "${key}" is not recognized by tool "${step.tool}". Known params: ${[...knownParams].join(', ')}`,
          severity: 'warning'
        });
      }
    }
  }

  // 4. Check parameter types
  if (step.params && toolDef.parameters) {
    for (const param of toolDef.parameters) {
      if (param.name in step.params) {
        const value = step.params[param.name];
        if (param.type === 'number' && typeof value !== 'number') {
          errors.push({
            code: 'WRONG_PARAM_TYPE',
            message: `Parameter "${param.name}" expects number, got ${typeof value}`,
            severity: 'fatal'
          });
        }
        if (param.type === 'boolean' && typeof value !== 'boolean') {
          errors.push({
            code: 'WRONG_PARAM_TYPE',
            message: `Parameter "${param.name}" expects boolean, got ${typeof value}`,
            severity: 'fatal'
          });
        }
      }
    }
  }

  // 5. Workflow rule: snapshot before mutation
  if (step.category === 'mutation') {
    const bp = step.params?.blueprint;
    if (bp && !workflowState.snapshotTaken.has(bp)) {
      errors.push({
        code: 'NO_SNAPSHOT',
        message: `Blueprint "${bp}" is being mutated without a prior snapshot_graph call. Add a snapshot step before this mutation.`,
        severity: 'fatal'
      });
    }
    if (bp) {
      workflowState.compilePending.add(bp);
    }
  }

  // 6. Track snapshots
  if (step.tool === 'snapshot_graph') {
    const bp = step.params?.blueprint;
    if (bp) workflowState.snapshotTaken.add(bp);
  }

  // 7. Track compiles
  if (step.tool === 'compile_blueprint') {
    const bp = step.params?.blueprint;
    if (bp) workflowState.compilePending.delete(bp);
  }

  // 8. Check dependency ordering
  if (step.depends_on) {
    for (const depId of step.depends_on) {
      if (!workflowState.completedSteps.has(depId)) {
        errors.push({
          code: 'UNMET_DEPENDENCY',
          message: `Step ${step.step_id} depends on step ${depId}, which has not completed yet.`,
          severity: 'fatal'
        });
      }
    }
  }

  // 9. Check GUID references
  if (step.use_guid) {
    for (const [paramName, guidVar] of Object.entries(step.use_guid)) {
      if (!(guidVar in workflowState.capturedGuids)) {
        errors.push({
          code: 'MISSING_GUID',
          message: `Step ${step.step_id} references GUID variable "${guidVar}" which has not been captured by any prior step.`,
          severity: 'fatal'
        });
      }
    }
  }

  const hasFatal = errors.some(e => e.severity === 'fatal');
  return { valid: !hasFatal, errors, warnings: errors.filter(e => e.severity === 'warning') };
}

/**
 * Validate an entire execution plan.
 * Returns { valid: true, stepResults: [...] } or { valid: false, stepResults: [...], firstError: {...} }
 */
function validatePlan(plan) {
  const state = new WorkflowState();
  const stepResults = [];
  let firstError = null;

  for (const step of plan.steps) {
    const result = validateStep(step, state);
    stepResults.push({ step_id: step.step_id, ...result });

    if (result.valid) {
      state.completedSteps.add(step.step_id);
      if (step.capture_guid) {
        state.capturedGuids[step.capture_guid] = `pending_${step.step_id}`;
      }
    } else if (!firstError) {
      firstError = { step_id: step.step_id, errors: result.errors };
    }
  }

  // Final check: any blueprints mutated but not compiled?
  const uncompiledWarnings = [];
  for (const bp of state.compilePending) {
    uncompiledWarnings.push({
      code: 'NO_COMPILE',
      message: `Blueprint "${bp}" was mutated but no compile_blueprint step follows. Add a compile step at the end.`,
      severity: 'warning'
    });
  }

  const allValid = stepResults.every(r => r.valid);
  return {
    valid: allValid,
    stepResults,
    firstError,
    planWarnings: uncompiledWarnings,
    stats: {
      totalSteps: plan.steps.length,
      validSteps: stepResults.filter(r => r.valid).length,
      failedSteps: stepResults.filter(r => !r.valid).length,
      warnings: stepResults.reduce((sum, r) => sum + (r.warnings?.length || 0), 0) + uncompiledWarnings.length
    }
  };
}

export { validateStep, validatePlan, WorkflowState, loadRegistry };
