#!/usr/bin/env node
/**
 * AgenticMCP TUI Dashboard
 * 
 * Unified terminal interface for the MCP server.
 * Three panels: Status (top-left), Execution Log (right), Chat (bottom-left).
 * Persistent chat history saved to user_context/chat_logs/.
 * 
 * Usage: node Tools/console/tui.js
 * 
 * Commands:
 *   /status  - Refresh connection status
 *   /clear   - Clear execution log
 *   /help    - Show help
 *   /quit    - Exit
 *   (anything else) - Send as natural language request to Worker
 */

const blessed = require('blessed');
const path = require('path');
const fs = require('fs');
const http = require('http');

// ─── Paths ───────────────────────────────────────────────────────────────────
const ROOT = path.resolve(__dirname, '..', '..');
const CHAT_LOG_DIR = path.join(ROOT, 'user_context', 'chat_logs');
const WORKER_URL = process.env.WORKER_URL || 'http://localhost:8081';
const UE_URL = process.env.UNREAL_MCP_URL || 'http://localhost:9847';

// Ensure chat log directory exists
if (!fs.existsSync(CHAT_LOG_DIR)) {
  fs.mkdirSync(CHAT_LOG_DIR, { recursive: true });
}

// ─── Session State ───────────────────────────────────────────────────────────
const sessionId = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
const chatLogPath = path.join(CHAT_LOG_DIR, `session_${sessionId}.md`);
const state = {
  ueConnected: false,
  workerConnected: false,
  lastToolCall: 'None',
  lastConfidence: '-',
  totalCalls: 0,
  totalErrors: 0,
  requestActive: false,
  currentIteration: 0,
};

// ─── Blessed Screen ──────────────────────────────────────────────────────────
const screen = blessed.screen({
  smartCSR: true,
  title: 'AgenticMCP Dashboard',
  fullUnicode: true,
});

// ─── Status Panel (top-left, compact) ────────────────────────────────────────
const statusBox = blessed.box({
  parent: screen,
  top: 0,
  left: 0,
  width: '40%',
  height: '35%',
  label: ' Status ',
  border: { type: 'line' },
  style: {
    border: { fg: 'cyan' },
    label: { fg: 'cyan', bold: true },
  },
  tags: true,
  padding: { left: 1, right: 1 },
});

// ─── Chat Panel (bottom-left) ────────────────────────────────────────────────
const chatBox = blessed.log({
  parent: screen,
  top: '35%',
  left: 0,
  width: '40%',
  height: '65%-3',
  label: ' Chat ',
  border: { type: 'line' },
  style: {
    border: { fg: 'green' },
    label: { fg: 'green', bold: true },
  },
  tags: true,
  scrollable: true,
  alwaysScroll: true,
  scrollbar: { style: { bg: 'green' } },
  mouse: true,
  padding: { left: 1, right: 1 },
});

// ─── Execution Log (right side, full height minus input) ─────────────────────
const logBox = blessed.log({
  parent: screen,
  top: 0,
  left: '40%',
  width: '60%',
  height: '100%-3',
  label: ' Execution Log ',
  border: { type: 'line' },
  style: {
    border: { fg: 'yellow' },
    label: { fg: 'yellow', bold: true },
  },
  tags: true,
  scrollable: true,
  alwaysScroll: true,
  scrollbar: { style: { bg: 'yellow' } },
  mouse: true,
  padding: { left: 1, right: 1 },
});

// ─── Input Bar (bottom, full width) ──────────────────────────────────────────
const inputBar = blessed.textbox({
  parent: screen,
  bottom: 0,
  left: 0,
  width: '100%',
  height: 3,
  label: ' > Type a request (or /help) ',
  border: { type: 'line' },
  style: {
    border: { fg: 'white' },
    label: { fg: 'white', bold: true },
    focus: { border: { fg: 'green' } },
  },
  inputOnFocus: true,
  mouse: true,
  keys: true,
});

// ─── Status Rendering ────────────────────────────────────────────────────────
function renderStatus() {
  const ue = state.ueConnected
    ? '{green-fg}CONNECTED{/green-fg}'
    : '{red-fg}DISCONNECTED{/red-fg}';
  const worker = state.workerConnected
    ? '{green-fg}CONNECTED{/green-fg}'
    : '{red-fg}DISCONNECTED{/red-fg}';
  const req = state.requestActive
    ? `{yellow-fg}RUNNING{/yellow-fg} (step ${state.currentIteration})`
    : '{white-fg}IDLE{/white-fg}';

  statusBox.setContent(
    `{bold}UE 5.6:{/bold}      ${ue}\n` +
    `{bold}Worker:{/bold}      ${worker}\n` +
    `{bold}Request:{/bold}     ${req}\n` +
    `\n` +
    `{bold}Last Tool:{/bold}   ${state.lastToolCall}\n` +
    `{bold}Confidence:{/bold}  ${state.lastConfidence}\n` +
    `{bold}Calls:{/bold}       ${state.totalCalls}  {bold}Errors:{/bold} ${state.totalErrors}\n` +
    `\n` +
    `{bold}Session:{/bold}     ${sessionId}`
  );
  screen.render();
}

// ─── Logging Helpers ─────────────────────────────────────────────────────────
function logExec(msg) {
  const ts = new Date().toLocaleTimeString('en-US', { hour12: false });
  logBox.log(`{gray-fg}[${ts}]{/gray-fg} ${msg}`);
}

function logChat(role, msg) {
  const color = role === 'You' ? 'cyan' : 'green';
  chatBox.log(`{${color}-fg}{bold}${role}:{/bold}{/${color}-fg} ${msg}`);
}

// ─── Persistent Chat Log ─────────────────────────────────────────────────────
function appendChatLog(role, content) {
  const ts = new Date().toISOString();
  const entry = `### [${ts}] ${role}\n${content}\n\n`;
  fs.appendFileSync(chatLogPath, entry);
}

function initChatLog() {
  const header =
    `# AgenticMCP Chat Session\n\n` +
    `**Session ID:** ${sessionId}\n` +
    `**Started:** ${new Date().toISOString()}\n` +
    `**UE URL:** ${UE_URL}\n` +
    `**Worker URL:** ${WORKER_URL}\n\n---\n\n`;
  fs.writeFileSync(chatLogPath, header);
}

// ─── Health Checks ───────────────────────────────────────────────────────────
function checkHealth(url, callback) {
  const req = http.get(url, { timeout: 3000 }, (res) => {
    callback(res.statusCode >= 200 && res.statusCode < 500);
  });
  req.on('error', () => callback(false));
  req.on('timeout', () => { req.destroy(); callback(false); });
}

function pollHealth() {
  checkHealth(`${UE_URL}/api/status`, (ok) => {
    state.ueConnected = ok;
    renderStatus();
  });
  checkHealth(`${WORKER_URL}/health`, (ok) => {
    state.workerConnected = ok;
    renderStatus();
  });
}

// ─── Request Execution ───────────────────────────────────────────────────────
async function executeRequest(userMessage) {
  if (state.requestActive) {
    logExec('{red-fg}Request already in progress. Wait for it to finish.{/red-fg}');
    return;
  }

  state.requestActive = true;
  state.currentIteration = 0;
  renderStatus();

  logChat('You', userMessage);
  appendChatLog('User', userMessage);
  logExec('{cyan-fg}--- New Request ---{/cyan-fg}');

  try {
    // Dynamic import of request-handler (it may use ES modules or CommonJS)
    let handleRequest;
    try {
      const handler = require('../request-handler');
      handleRequest = handler.handleRequest || handler.default;
    } catch (requireErr) {
      logExec(`{red-fg}Cannot load request-handler: ${requireErr.message}{/red-fg}`);
      logExec('{yellow-fg}Falling back to direct UE proxy mode.{/yellow-fg}');
      state.requestActive = false;
      renderStatus();
      return;
    }

    const result = await handleRequest(userMessage, {
      // Callback: before each tool call
      onToolCall: (toolName, params) => {
        state.lastToolCall = toolName;
        state.totalCalls++;
        state.currentIteration++;
        const paramStr = JSON.stringify(params || {});
        const preview = paramStr.length > 80 ? paramStr.slice(0, 77) + '...' : paramStr;
        logExec(`{yellow-fg}CALL{/yellow-fg}  ${toolName}(${preview})`);
        renderStatus();
      },
      // Callback: after each successful tool result
      onToolResult: (toolName, result, confidence) => {
        state.lastConfidence = confidence ? `${(confidence * 100).toFixed(0)}%` : '-';
        const preview = typeof result === 'string'
          ? result.slice(0, 100)
          : JSON.stringify(result || '').slice(0, 100);
        logExec(`{green-fg}  OK{/green-fg}  ${toolName} -> ${preview}`);
        renderStatus();
      },
      // Callback: on tool error
      onToolError: (toolName, error) => {
        state.totalErrors++;
        logExec(`{red-fg}  ERR{/red-fg} ${toolName} -> ${error}`);
        renderStatus();
      },
      // Callback: on retry
      onRetry: (toolName, attempt, reason) => {
        logExec(`{yellow-fg}  RETRY{/yellow-fg} ${toolName} #${attempt}: ${reason}`);
      },
      // Callback: on completion
      onComplete: (summary) => {
        logExec(`{green-fg}--- Complete ---{/green-fg}`);
      },
    });

    const summary = (result && result.summary) || 'Done.';
    logChat('Worker', summary);
    appendChatLog('Worker', summary);

  } catch (err) {
    const errMsg = err.message || String(err);
    logExec(`{red-fg}FATAL{/red-fg} ${errMsg}`);
    logChat('Worker', `Error: ${errMsg}`);
    appendChatLog('Worker', `Error: ${errMsg}`);
    state.totalErrors++;
  }

  state.requestActive = false;
  state.currentIteration = 0;
  renderStatus();
}

// ─── Input Handling ──────────────────────────────────────────────────────────
inputBar.on('submit', (value) => {
  const text = (value || '').trim();
  inputBar.clearValue();
  inputBar.focus();
  screen.render();

  if (!text) return;

  // Commands
  if (text === '/quit' || text === '/exit') {
    appendChatLog('System', 'Session ended by user.');
    process.exit(0);
  }
  if (text === '/status') {
    pollHealth();
    logExec('{cyan-fg}Refreshing status...{/cyan-fg}');
    return;
  }
  if (text === '/clear') {
    logBox.setContent('');
    screen.render();
    return;
  }
  if (text === '/help') {
    chatBox.log('{white-fg}{bold}Commands:{/bold}{/white-fg}');
    chatBox.log('  /status  - Refresh connection status');
    chatBox.log('  /clear   - Clear execution log');
    chatBox.log('  /help    - Show this help');
    chatBox.log('  /quit    - Exit');
    chatBox.log('');
    chatBox.log('Anything else is sent as a request to the Worker.');
    screen.render();
    return;
  }

  // Everything else is a request
  executeRequest(text);
});

// ─── Key Bindings ────────────────────────────────────────────────────────────
screen.key(['escape', 'C-c'], () => {
  appendChatLog('System', 'Session ended.');
  process.exit(0);
});

screen.key(['tab'], () => {
  inputBar.focus();
  screen.render();
});

// ─── Startup ─────────────────────────────────────────────────────────────────
initChatLog();
renderStatus();
pollHealth();
setInterval(pollHealth, 10000);

inputBar.focus();
screen.render();

logExec('{cyan-fg}AgenticMCP Dashboard v3.0{/cyan-fg}');
logExec('Direct inference. No planner. Native tool calling.');
logExec(`Chat log: user_context/chat_logs/session_${sessionId}.md`);
logExec('');

chatBox.log('{green-fg}Ready. Type a request below.{/green-fg}');
chatBox.log('{gray-fg}Chat history saves automatically.{/gray-fg}');

screen.render();
