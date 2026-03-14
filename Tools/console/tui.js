/**
 * AgenticMCP Terminal Console UI
 * 
 * Lightweight TUI that monitors plan execution and renders
 * inline screenshots via Kitty graphics protocol, sixel, or ASCII fallback.
 * 
 * Zero dependencies. Pure Node.js.
 * 
 * Usage: node tui.js [--plan plan.json]
 */

import { readFileSync, existsSync, createReadStream } from 'fs';
import { basename } from 'path';
import { createInterface } from 'readline';

// ============================================================
// Terminal capability detection
// ============================================================

function detectTerminalCapabilities() {
  const term = process.env.TERM || '';
  const termProgram = process.env.TERM_PROGRAM || '';
  const kittyPid = process.env.KITTY_PID || '';
  const wezterm = process.env.WEZTERM_EXECUTABLE || '';

  if (kittyPid || termProgram === 'kitty') {
    return 'kitty';
  }
  if (termProgram === 'iTerm.app' || termProgram === 'iTerm2') {
    return 'iterm2';
  }
  if (wezterm || termProgram === 'WezTerm') {
    return 'kitty'; // WezTerm supports Kitty graphics protocol
  }
  // Check for sixel support via TERM
  if (term.includes('xterm') || term.includes('mlterm') || term.includes('foot')) {
    return 'sixel';
  }
  // Windows Terminal supports sixel since 2023
  if (process.env.WT_SESSION) {
    return 'sixel';
  }
  return 'ascii';
}

// ============================================================
// Image rendering
// ============================================================

/**
 * Render an image inline in the terminal using the best available protocol.
 */
function renderImage(imagePath, protocol) {
  if (!existsSync(imagePath)) {
    log('WARN', `Screenshot file not found: ${imagePath}`);
    return;
  }

  const imageData = readFileSync(imagePath);
  const b64 = imageData.toString('base64');

  switch (protocol) {
    case 'kitty':
      renderKitty(b64);
      break;
    case 'iterm2':
      renderITerm2(b64, imagePath);
      break;
    case 'sixel':
      renderSixelFallback(imagePath);
      break;
    default:
      renderASCII(imagePath);
      break;
  }
}

function renderKitty(b64) {
  // Kitty graphics protocol: transmit image in chunks of 4096 bytes
  const chunkSize = 4096;
  const chunks = [];
  for (let i = 0; i < b64.length; i += chunkSize) {
    chunks.push(b64.slice(i, i + chunkSize));
  }

  for (let i = 0; i < chunks.length; i++) {
    const isLast = i === chunks.length - 1;
    const m = isLast ? 0 : 1; // m=1 means more chunks follow
    if (i === 0) {
      // First chunk: include format and action
      process.stdout.write(`\x1b_Gf=100,a=T,m=${m};${chunks[i]}\x1b\\`);
    } else {
      process.stdout.write(`\x1b_Gm=${m};${chunks[i]}\x1b\\`);
    }
  }
  process.stdout.write('\n');
}

function renderITerm2(b64, imagePath) {
  const name = Buffer.from(basename(imagePath)).toString('base64');
  process.stdout.write(`\x1b]1337;File=name=${name};inline=1;size=${b64.length}:${b64}\x07\n`);
}

function renderSixelFallback(imagePath) {
  // Sixel requires converting the image to sixel format.
  // Without ImageMagick, we fall back to ASCII.
  // If convert/magick is available, use it.
  const { execSync } = require('child_process');
  try {
    // Try using img2sixel (from libsixel) or convert
    const result = execSync(`img2sixel -w 640 "${imagePath}" 2>/dev/null || convert "${imagePath}" -resize 640x sixel:- 2>/dev/null`, {
      timeout: 5000,
      encoding: 'buffer',
    });
    process.stdout.write(result);
    process.stdout.write('\n');
  } catch {
    // Fall back to ASCII
    renderASCII(imagePath);
  }
}

function renderASCII(imagePath) {
  // Minimal ASCII representation -- just show the file path with a frame
  const name = basename(imagePath);
  const width = Math.min(process.stdout.columns || 60, 60);
  const border = '+' + '-'.repeat(width - 2) + '+';
  const padded = (text) => {
    const pad = width - 4 - text.length;
    return '| ' + text + ' '.repeat(Math.max(0, pad)) + ' |';
  };

  process.stdout.write('\n');
  process.stdout.write(border + '\n');
  process.stdout.write(padded('[VIEWPORT CAPTURE]') + '\n');
  process.stdout.write(padded('') + '\n');
  process.stdout.write(padded(name.slice(0, width - 6)) + '\n');
  process.stdout.write(padded('') + '\n');
  process.stdout.write(padded('Open: ' + imagePath.slice(0, width - 12)) + '\n');
  process.stdout.write(border + '\n');
  process.stdout.write('\n');
}

// ============================================================
// Logging
// ============================================================

function timestamp() {
  const d = new Date();
  return `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}:${d.getSeconds().toString().padStart(2, '0')}`;
}

const COLORS = {
  reset: '\x1b[0m',
  dim: '\x1b[2m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  red: '\x1b[31m',
  cyan: '\x1b[36m',
  white: '\x1b[37m',
  bold: '\x1b[1m',
};

function log(level, message) {
  const ts = `${COLORS.dim}[${timestamp()}]${COLORS.reset}`;
  switch (level) {
    case 'OK':
      process.stdout.write(`${ts} ${COLORS.green}[OK]${COLORS.reset} ${message}\n`);
      break;
    case 'WARN':
      process.stdout.write(`${ts} ${COLORS.yellow}[!!]${COLORS.reset} ${message}\n`);
      break;
    case 'ERR':
      process.stdout.write(`${ts} ${COLORS.red}[ERR]${COLORS.reset} ${message}\n`);
      break;
    case 'INFO':
      process.stdout.write(`${ts} ${COLORS.cyan}[--]${COLORS.reset} ${message}\n`);
      break;
    case 'STEP':
      process.stdout.write(`${ts} ${COLORS.bold}${COLORS.white}${message}${COLORS.reset}\n`);
      break;
    default:
      process.stdout.write(`${ts} ${message}\n`);
      break;
  }
}

function logConfidence(stepId, total, tool, confidence, status) {
  const pct = (confidence * 100).toFixed(1);
  const bar = confidence >= 0.95 ? COLORS.green : confidence >= 0.80 ? COLORS.yellow : COLORS.red;
  const icon = status === 'completed' ? `${COLORS.green}OK${COLORS.reset}` :
               status === 'escalated' ? `${COLORS.yellow}!!${COLORS.reset}` :
               `${COLORS.red}XX${COLORS.reset}`;
  process.stdout.write(
    `${COLORS.dim}[${timestamp()}]${COLORS.reset} Step ${stepId}/${total}: ${tool} ${bar}(${pct}%)${COLORS.reset} ${icon}\n`
  );
}

// ============================================================
// Console input handler
// ============================================================

function startInputLoop(callbacks) {
  const rl = createInterface({
    input: process.stdin,
    output: process.stdout,
    prompt: `${COLORS.dim}>${COLORS.reset} `,
  });

  rl.on('line', (line) => {
    const cmd = line.trim().toLowerCase();

    if (cmd === 'pause') {
      log('INFO', 'Execution paused. Type "resume" to continue.');
      if (callbacks.onPause) callbacks.onPause();
    } else if (cmd === 'resume') {
      log('INFO', 'Execution resumed.');
      if (callbacks.onResume) callbacks.onResume();
    } else if (cmd === 'abort') {
      log('WARN', 'Aborting current plan execution.');
      if (callbacks.onAbort) callbacks.onAbort();
    } else if (cmd === 'status') {
      if (callbacks.onStatus) callbacks.onStatus();
    } else if (cmd === 'screenshot') {
      log('INFO', 'Requesting viewport capture...');
      if (callbacks.onScreenshot) callbacks.onScreenshot();
    } else if (cmd === 'help') {
      log('INFO', 'Commands: pause | resume | abort | status | screenshot | help');
    } else if (line.trim().length > 0) {
      // User message injection -- pass to the worker context
      log('INFO', `User note injected: "${line.trim()}"`);
      if (callbacks.onUserMessage) callbacks.onUserMessage(line.trim());
    }

    rl.prompt();
  });

  rl.prompt();
  return rl;
}

// ============================================================
// Main console runner
// ============================================================

async function runConsole(plan, executePlanFn) {
  const protocol = detectTerminalCapabilities();
  const totalSteps = plan.steps.length;

  // Header
  process.stdout.write('\n');
  process.stdout.write(`${COLORS.bold}  AgenticMCP Console${COLORS.reset}\n`);
  process.stdout.write(`${COLORS.dim}${'='.repeat(50)}${COLORS.reset}\n`);
  log('INFO', `Terminal: ${protocol} graphics`);
  log('INFO', `Plan: ${plan.plan_id} (${totalSteps} steps)`);
  process.stdout.write('\n');

  let paused = false;
  let aborted = false;

  // Execution callbacks
  const execOptions = {
    onStepStart: (step) => {
      log('STEP', `Step ${step.step_id}/${totalSteps}: ${step.tool}`);
    },
    onStepComplete: (step, result) => {
      logConfidence(step.step_id, totalSteps, step.tool, result.confidence || 1.0, 'completed');
    },
    onStepFail: (step, result) => {
      logConfidence(step.step_id, totalSteps, step.tool, result.confidence || 0, 'failed');
      log('ERR', `  Error: ${result.error}`);
    },
    onEscalation: (esc) => {
      log('WARN', `  Escalating to Planner: ${esc.error || esc.reason}`);
      if (esc.max_confidence) {
        log('WARN', `  Best confidence: ${(esc.max_confidence * 100).toFixed(1)}%`);
      }
    },
    onScreenshot: (imagePath) => {
      log('OK', `  Screenshot: ${imagePath}`);
      renderImage(imagePath, protocol);
    },
  };

  // Start input loop
  const inputCallbacks = {
    onPause: () => { paused = true; },
    onResume: () => { paused = false; },
    onAbort: () => { aborted = true; },
    onStatus: () => {
      log('INFO', `Plan: ${plan.plan_id} | Paused: ${paused} | Aborted: ${aborted}`);
    },
    onScreenshot: async () => {
      // Force a screenshot via the plugin
      try {
        const resp = await fetch(`${process.env.UNREAL_MCP_URL || 'http://localhost:9847'}/mcp/tool/screenshot`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: '{}',
        });
        const data = await resp.json();
        if (data.path) renderImage(data.path, protocol);
      } catch (e) {
        log('ERR', `Screenshot failed: ${e.message}`);
      }
    },
    onUserMessage: (msg) => {
      // Store for next worker context injection
      execOptions._userNote = msg;
    },
  };

  const rl = startInputLoop(inputCallbacks);

  // Execute
  const report = await executePlanFn(plan, execOptions);

  // Summary
  process.stdout.write('\n');
  process.stdout.write(`${COLORS.dim}${'='.repeat(50)}${COLORS.reset}\n`);
  log('INFO', `Plan ${report.status}: ${report.completed} completed, ${report.failed} failed, ${report.skipped} skipped`);
  if (report.escalations.length > 0) {
    log('WARN', `${report.escalations.length} escalation(s) sent to Planner`);
  }
  process.stdout.write('\n');

  rl.close();
  return report;
}

export { runConsole, renderImage, detectTerminalCapabilities, log, logConfidence };
