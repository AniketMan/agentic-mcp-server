#!/usr/bin/env node
/**
 * AgenticMCP Stack Launcher
 * 
 * Automated launcher that Claude calls to start the full execution stack:
 * 1. Checks if llama.cpp server is running, starts it if not
 * 2. Checks if Unreal Engine is reachable on the MCP port
 * 3. Launches the console TUI with the plan
 * 4. Writes the execution report when done
 * 
 * Usage:
 *   node launch.js --plan <plan.json>
 *   node launch.js --plan <plan.json> --dry-run
 *   node launch.js --plan <plan.json> --model <path_to_gguf>
 * 
 * Environment Variables:
 *   LLAMA_CPP_PATH       - Path to llama.cpp server binary (default: llama-server)
 *   LLAMA_MODEL_PATH     - Path to the GGUF model file
 *   LLAMA_CPP_URL        - URL of running llama.cpp server (default: http://localhost:8080)
 *   UNREAL_MCP_URL       - URL of the UE C++ plugin (default: http://localhost:9847)
 *   CONFIDENCE_THRESHOLD - Minimum confidence for execution (default: 0.95)
 */

import { existsSync, readFileSync, writeFileSync } from 'fs';
import { resolve, dirname, join } from 'path';
import { fileURLToPath } from 'url';
import { spawn, execSync } from 'child_process';

const __dirname = dirname(fileURLToPath(import.meta.url));

// ============================================================
// Configuration
// ============================================================

const LLAMA_CPP_PATH = process.env.LLAMA_CPP_PATH || 'llama-server';
const LLAMA_MODEL_PATH = process.env.LLAMA_MODEL_PATH || '';
const LLAMA_CPP_URL = process.env.LLAMA_CPP_URL || 'http://localhost:8080';
const UNREAL_MCP_URL = process.env.UNREAL_MCP_URL || 'http://localhost:9847';

// ============================================================
// Argument parsing
// ============================================================

const args = process.argv.slice(2);
let planPath = null;
let dryRun = false;
let modelPath = LLAMA_MODEL_PATH;

for (let i = 0; i < args.length; i++) {
  if (args[i] === '--plan' && args[i + 1]) {
    planPath = resolve(args[i + 1]);
    i++;
  } else if (args[i] === '--dry-run') {
    dryRun = true;
  } else if (args[i] === '--model' && args[i + 1]) {
    modelPath = resolve(args[i + 1]);
    i++;
  } else if (!planPath && !args[i].startsWith('--')) {
    planPath = resolve(args[i]);
  }
}

if (!planPath) {
  console.error('AgenticMCP Stack Launcher');
  console.error('');
  console.error('Usage: node launch.js --plan <plan.json> [--dry-run] [--model <path_to_gguf>]');
  console.error('');
  console.error('This script is called by Claude (the Planner) to start the full execution stack.');
  console.error('It checks dependencies, starts the local LLM if needed, and runs the plan.');
  process.exit(1);
}

// ============================================================
// Logging
// ============================================================

function log(level, msg) {
  const ts = new Date().toISOString().slice(11, 19);
  const prefix = level === 'OK' ? '\x1b[32m[OK]\x1b[0m' :
                 level === 'WARN' ? '\x1b[33m[!!]\x1b[0m' :
                 level === 'ERR' ? '\x1b[31m[ERR]\x1b[0m' :
                 '\x1b[36m[--]\x1b[0m';
  console.error(`[${ts}] ${prefix} ${msg}`);
}

// ============================================================
// Health checks
// ============================================================

async function checkService(url, name, timeoutMs = 3000) {
  try {
    const resp = await fetch(url, { signal: AbortSignal.timeout(timeoutMs) });
    if (resp.ok) {
      log('OK', `${name} is running at ${url}`);
      return true;
    }
    log('WARN', `${name} returned ${resp.status} at ${url}`);
    return false;
  } catch {
    log('WARN', `${name} not reachable at ${url}`);
    return false;
  }
}

async function waitForService(url, name, maxWaitMs = 30000) {
  const start = Date.now();
  while (Date.now() - start < maxWaitMs) {
    const ok = await checkService(url, name, 2000);
    if (ok) return true;
    log('INFO', `Waiting for ${name}...`);
    await new Promise(r => setTimeout(r, 2000));
  }
  return false;
}

// ============================================================
// LLM Server Management
// ============================================================

let llamaProcess = null;

async function ensureLlamaServer() {
  // Check if already running
  const running = await checkService(`${LLAMA_CPP_URL}/health`, 'llama.cpp');
  if (running) return true;

  // Need to start it
  if (!modelPath) {
    log('WARN', 'No GGUF model path provided. Set LLAMA_MODEL_PATH or use --model flag.');
    log('WARN', 'Continuing without local LLM -- direct execution mode (no confidence gating).');
    return false;
  }

  if (!existsSync(modelPath)) {
    log('ERR', `Model file not found: ${modelPath}`);
    log('WARN', 'Continuing without local LLM -- direct execution mode (no confidence gating).');
    return false;
  }

  log('INFO', `Starting llama.cpp server with model: ${modelPath}`);

  try {
    llamaProcess = spawn(LLAMA_CPP_PATH, [
      '-m', modelPath,
      '-c', '8192',
      '--port', '8080',
      '--host', '0.0.0.0',
      '-ngl', '99',  // Offload all layers to GPU
    ], {
      stdio: ['ignore', 'pipe', 'pipe'],
      detached: false,
    });

    llamaProcess.stderr.on('data', (data) => {
      const line = data.toString().trim();
      if (line.includes('listening') || line.includes('ready')) {
        log('OK', `llama.cpp: ${line}`);
      }
    });

    llamaProcess.on('error', (err) => {
      log('ERR', `Failed to start llama.cpp: ${err.message}`);
      log('WARN', 'Is llama-server in your PATH? Install from: https://github.com/ggerganov/llama.cpp');
    });

    llamaProcess.on('exit', (code) => {
      if (code !== null && code !== 0) {
        log('ERR', `llama.cpp exited with code ${code}`);
      }
    });

    // Wait for it to be ready
    const ready = await waitForService(`${LLAMA_CPP_URL}/health`, 'llama.cpp', 60000);
    if (!ready) {
      log('ERR', 'llama.cpp failed to start within 60 seconds.');
      log('WARN', 'Continuing without local LLM -- direct execution mode.');
      return false;
    }

    return true;
  } catch (err) {
    log('ERR', `Failed to spawn llama.cpp: ${err.message}`);
    return false;
  }
}

// ============================================================
// Main
// ============================================================

async function main() {
  console.error('');
  console.error('\x1b[1m  AgenticMCP Stack Launcher\x1b[0m');
  console.error('\x1b[2m' + '='.repeat(50) + '\x1b[0m');
  console.error('');

  // 1. Validate plan file
  log('INFO', `Plan file: ${planPath}`);
  if (!existsSync(planPath)) {
    log('ERR', `Plan file not found: ${planPath}`);
    process.exit(1);
  }

  let plan;
  try {
    plan = JSON.parse(readFileSync(planPath, 'utf-8'));
  } catch (err) {
    log('ERR', `Invalid JSON in plan file: ${err.message}`);
    process.exit(1);
  }

  if (!plan.steps || !Array.isArray(plan.steps)) {
    log('ERR', 'Plan must contain a "steps" array. See SYSTEM.md for format.');
    process.exit(1);
  }

  log('OK', `Plan loaded: ${plan.plan_id || 'unnamed'} (${plan.steps.length} steps)`);

  // 2. Check Unreal Engine connection
  const ueReachable = await checkService(`${UNREAL_MCP_URL}/mcp/status`, 'Unreal Engine');
  if (!ueReachable) {
    log('ERR', `Unreal Engine not reachable at ${UNREAL_MCP_URL}`);
    log('ERR', 'Start UE with the AgenticMCP plugin enabled. For headless: add -RenderOffScreen to launch args.');
    
    // Write failure report
    const report = {
      plan_id: plan.plan_id,
      status: 'failed',
      error: 'Unreal Engine not reachable',
      started_at: new Date().toISOString(),
      finished_at: new Date().toISOString(),
      steps: [],
      escalations: [{
        error: `Unreal Engine not reachable at ${UNREAL_MCP_URL}. Start UE with AgenticMCP plugin.`,
        suggestion: 'Ensure Unreal Editor is running with the AgenticMCP plugin enabled on port 9847.'
      }]
    };
    const reportPath = planPath.replace('.json', '_report.json');
    writeFileSync(reportPath, JSON.stringify(report, null, 2));
    log('INFO', `Report saved: ${reportPath}`);
    process.exit(1);
  }

  // 3. Start local LLM if needed
  const llmReady = await ensureLlamaServer();
  if (llmReady) {
    log('OK', 'Local LLM ready -- confidence gating enabled');
  } else {
    log('WARN', 'No local LLM -- running in direct execution mode (plan params used as-is)');
  }

  // 4. Launch the console TUI
  log('INFO', 'Launching execution console...');
  console.error('');

  // Import and run the console
  const { runConsole } = await import('./tui.js');
  const { executePlan } = await import('../gatekeeper/dispatcher.js');

  const report = await runConsole(plan, (p, opts) => executePlan(p, { ...opts, dryRun }));

  // 5. Write the report
  const reportPath = planPath.replace('.json', '_report.json');
  writeFileSync(reportPath, JSON.stringify(report, null, 2));
  log('INFO', `Report saved: ${reportPath}`);

  // 6. Cleanup
  if (llamaProcess) {
    log('INFO', 'Shutting down llama.cpp server...');
    llamaProcess.kill('SIGTERM');
  }

  // Exit with appropriate code
  process.exit(report.status === 'completed' ? 0 : 1);
}

// Handle cleanup on interrupt
process.on('SIGINT', () => {
  log('WARN', 'Interrupted. Cleaning up...');
  if (llamaProcess) llamaProcess.kill('SIGTERM');
  process.exit(130);
});

process.on('SIGTERM', () => {
  if (llamaProcess) llamaProcess.kill('SIGTERM');
  process.exit(143);
});

main().catch((err) => {
  log('ERR', `Fatal: ${err.message}`);
  console.error(err.stack);
  if (llamaProcess) llamaProcess.kill('SIGTERM');
  // Never close the terminal -- let the user read the error
  process.stdin.resume();
});
