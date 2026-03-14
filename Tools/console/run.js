#!/usr/bin/env node
/**
 * AgenticMCP Console Runner
 * 
 * Entry point for the terminal console UI.
 * Loads a plan JSON file and executes it through the gatekeeper pipeline.
 * 
 * Usage:
 *   node run.js --plan path/to/plan.json
 *   node run.js --plan path/to/plan.json --dry-run
 *   node run.js --validate-only path/to/plan.json
 */

import { readFileSync } from 'fs';
import { resolve } from 'path';
import { runConsole, log } from './tui.js';
import { executePlan, validatePlan } from '../gatekeeper/index.js';

// Parse arguments
const args = process.argv.slice(2);
let planPath = null;
let dryRun = false;
let validateOnly = false;

for (let i = 0; i < args.length; i++) {
  if (args[i] === '--plan' && args[i + 1]) {
    planPath = resolve(args[i + 1]);
    i++;
  } else if (args[i] === '--dry-run') {
    dryRun = true;
  } else if (args[i] === '--validate-only') {
    validateOnly = true;
    if (args[i + 1] && !args[i + 1].startsWith('--')) {
      planPath = resolve(args[i + 1]);
      i++;
    }
  } else if (!planPath && !args[i].startsWith('--')) {
    planPath = resolve(args[i]);
  }
}

if (!planPath) {
  console.error('Usage: node run.js --plan <plan.json> [--dry-run] [--validate-only]');
  console.error('');
  console.error('Options:');
  console.error('  --plan <file>      Path to the JSON execution plan');
  console.error('  --dry-run          Validate and simulate without executing');
  console.error('  --validate-only    Only run the rule engine, no LLM or execution');
  process.exit(1);
}

// Load plan
let plan;
try {
  const raw = readFileSync(planPath, 'utf-8');
  plan = JSON.parse(raw);
} catch (err) {
  console.error(`ERROR: Failed to load plan from ${planPath}`);
  console.error(`  ${err.message}`);
  console.error('');
  console.error('The plan file must be valid JSON matching the format in SYSTEM.md.');
  process.exit(1);
}

// Validate plan structure
if (!plan.steps || !Array.isArray(plan.steps)) {
  console.error('ERROR: Plan must contain a "steps" array.');
  console.error('See SYSTEM.md for the required plan format.');
  process.exit(1);
}

if (!plan.plan_id) {
  console.error('ERROR: Plan must contain a "plan_id" string.');
  process.exit(1);
}

// Validate-only mode
if (validateOnly) {
  const result = validatePlan(plan);
  console.log(JSON.stringify(result, null, 2));
  process.exit(result.valid ? 0 : 1);
}

// Run the console
async function main() {
  try {
    const report = await runConsole(plan, (p, opts) => executePlan(p, { ...opts, dryRun }));
    
    // Write report to file
    const reportPath = planPath.replace('.json', '_report.json');
    const { writeFileSync } = await import('fs');
    writeFileSync(reportPath, JSON.stringify(report, null, 2));
    log('INFO', `Report saved: ${reportPath}`);
    
    process.exit(report.status === 'completed' ? 0 : 1);
  } catch (err) {
    console.error(`FATAL: ${err.message}`);
    console.error(err.stack);
    // Never close the terminal on error -- let the user read it
    process.stdin.resume();
  }
}

main();
