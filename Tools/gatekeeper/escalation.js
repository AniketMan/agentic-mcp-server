/**
 * Escalation Feedback Module
 * 
 * When a worker fails to reach 95% confidence after N retries,
 * this module writes a structured escalation file that Claude
 * can read via the check_job_status MCP tool.
 * 
 * The escalation contains:
 * - The failed step
 * - What the worker tried
 * - Why confidence was low
 * - What source_truth docs were consulted
 * - A request for Claude to provide a revised instruction
 * 
 * Claude reads the escalation, revises the step, and the
 * dispatcher retries with Claude's fix.
 */

import { writeFileSync, readFileSync, existsSync, mkdirSync, readdirSync } from 'fs';
import { join, dirname } from 'path';

const ESCALATION_DIR = join(dirname(new URL(import.meta.url).pathname), '..', '..', 'escalations');

/**
 * Write an escalation for Claude to review.
 */
export function writeEscalation(jobId, stepIndex, step, attempts, reason) {
  // Ensure escalation directory exists
  if (!existsSync(ESCALATION_DIR)) {
    mkdirSync(ESCALATION_DIR, { recursive: true });
  }

  const escalation = {
    job_id: jobId,
    step_index: stepIndex,
    timestamp: new Date().toISOString(),
    status: 'awaiting_planner',

    // What was attempted
    failed_step: {
      tool: step.tool,
      parameters: step.parameters,
      description: step.description || '',
      expected_result: step.expected_result || '',
    },

    // Why it failed
    diagnosis: {
      attempts: attempts.map((a, i) => ({
        attempt: i + 1,
        confidence: a.confidence,
        generated_params: a.generatedParams,
        reason_for_low_confidence: a.reason,
      })),
      highest_confidence: Math.max(...attempts.map(a => a.confidence)),
      threshold: 0.95,
      gap: 0.95 - Math.max(...attempts.map(a => a.confidence)),
    },

    // What docs were consulted
    context_consulted: {
      tool_registry: true,
      source_truth_files: attempts[0]?.sourceTruthFiles || [],
      worker_instructions: true,
      ue_api_docs: attempts[0]?.ueDocsConsulted || [],
    },

    // What Claude should do
    request_to_planner: {
      action: 'revise_step',
      message: `Step ${stepIndex} failed to reach 95% confidence after ${attempts.length} attempts. ` +
               `Highest confidence was ${(Math.max(...attempts.map(a => a.confidence)) * 100).toFixed(1)}%. ` +
               `${reason}`,
      options: [
        'Revise the step parameters to be more specific',
        'Break this step into smaller sub-steps',
        'Add a prerequisite step that the worker needs first',
        'Provide the exact tool call JSON directly (bypass inference)',
      ],
    },

    // Slot for Claude's response
    planner_response: null,
  };

  const filename = `escalation_${jobId}_step${stepIndex}.json`;
  const filepath = join(ESCALATION_DIR, filename);
  writeFileSync(filepath, JSON.stringify(escalation, null, 2));

  return { filepath, escalation };
}

/**
 * Read Claude's response to an escalation.
 * Returns null if Claude hasn't responded yet.
 */
export function readEscalationResponse(jobId, stepIndex) {
  const filename = `escalation_${jobId}_step${stepIndex}.json`;
  const filepath = join(ESCALATION_DIR, filename);

  if (!existsSync(filepath)) return null;

  try {
    const data = JSON.parse(readFileSync(filepath, 'utf-8'));
    if (data.planner_response) {
      return data.planner_response;
    }
    return null;
  } catch {
    return null;
  }
}

/**
 * Check if there are any pending escalations for a job.
 */
export function getPendingEscalations(jobId) {
  if (!existsSync(ESCALATION_DIR)) return [];

  const files = readdirSync(ESCALATION_DIR)
    .filter(f => f.startsWith(`escalation_${jobId}_`) && f.endsWith('.json'));

  const pending = [];
  for (const file of files) {
    try {
      const data = JSON.parse(readFileSync(join(ESCALATION_DIR, file), 'utf-8'));
      if (data.status === 'awaiting_planner' && !data.planner_response) {
        pending.push(data);
      }
    } catch { /* skip corrupt files */ }
  }

  return pending;
}

/**
 * Claude writes its response to an escalation via this function.
 */
export function resolveEscalation(jobId, stepIndex, response) {
  const filename = `escalation_${jobId}_step${stepIndex}.json`;
  const filepath = join(ESCALATION_DIR, filename);

  if (!existsSync(filepath)) {
    return { error: `Escalation not found: ${filename}` };
  }

  const data = JSON.parse(readFileSync(filepath, 'utf-8'));
  data.planner_response = {
    resolved_at: new Date().toISOString(),
    ...response,
  };
  data.status = 'resolved';
  writeFileSync(filepath, JSON.stringify(data, null, 2));

  return { success: true, filepath };
}
