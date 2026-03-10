/**
 * UE5 MCP Server - Extracted Library Functions
 *
 * Pure/testable functions extracted from index.js.
 * Functions that previously read from the module-scoped CONFIG closure
 * now accept those values as explicit parameters.
 */

/**
 * Structured logging helper - writes to stderr to not interfere with MCP protocol
 */
export const log = {
  info: (msg, data) => console.error(`[INFO] ${msg}`, data ? JSON.stringify(data) : ""),
  error: (msg, data) => console.error(`[ERROR] ${msg}`, data ? JSON.stringify(data) : ""),
  debug: (msg, data) => process.env.DEBUG && console.error(`[DEBUG] ${msg}`, data ? JSON.stringify(data) : ""),
  warn: (msg, data) => console.error(`[WARN] ${msg}`, data ? JSON.stringify(data) : ""),
};

// ---------------------------------------------------------------------------
// Retry and Reconnection Configuration
// ---------------------------------------------------------------------------
const RETRY_CONFIG = {
  maxRetries: 3,
  initialDelayMs: 1000,
  maxDelayMs: 10000,
  backoffMultiplier: 2,
  jitterFactor: 0.1,
};

/**
 * Calculate delay with exponential backoff and jitter
 * @param {number} attempt - Current attempt number (0-indexed)
 * @param {object} config - Retry configuration
 */
function calculateBackoff(attempt, config = RETRY_CONFIG) {
  const baseDelay = Math.min(
    config.initialDelayMs * Math.pow(config.backoffMultiplier, attempt),
    config.maxDelayMs
  );
  const jitter = baseDelay * config.jitterFactor * (Math.random() * 2 - 1);
  return Math.floor(baseDelay + jitter);
}

/**
 * Retry a function with exponential backoff
 * @param {function} fn - Async function to retry
 * @param {object} options - Retry options
 * @param {number} options.maxRetries - Maximum retry attempts
 * @param {function} options.shouldRetry - Function to determine if error is retryable
 * @param {function} options.onRetry - Callback on each retry
 */
export async function withRetry(fn, options = {}) {
  const {
    maxRetries = RETRY_CONFIG.maxRetries,
    shouldRetry = (error) => isRetryableError(error),
    onRetry = null,
  } = options;

  let lastError;
  for (let attempt = 0; attempt <= maxRetries; attempt++) {
    try {
      return await fn();
    } catch (error) {
      lastError = error;

      if (attempt >= maxRetries || !shouldRetry(error)) {
        throw error;
      }

      const delayMs = calculateBackoff(attempt);
      log.debug("Retrying after error", {
        attempt: attempt + 1,
        maxRetries,
        delayMs,
        error: error.message,
      });

      if (onRetry) {
        onRetry({ attempt: attempt + 1, error, delayMs });
      }

      await sleep(delayMs);
    }
  }
  throw lastError;
}

/**
 * Check if an error is retryable (transient network issues)
 * @param {Error} error - The error to check
 */
export function isRetryableError(error) {
  if (error.name === "AbortError") return true;
  if (error.code === "ECONNREFUSED") return true;
  if (error.code === "ECONNRESET") return true;
  if (error.code === "ETIMEDOUT") return true;
  if (error.code === "ENOTFOUND") return false;
  if (error.message?.includes("fetch failed")) return true;
  if (error.message?.includes("network")) return true;
  return false;
}

// ---------------------------------------------------------------------------
// Connection Health Manager
// ---------------------------------------------------------------------------
class ConnectionManager {
  constructor() {
    this.connected = false;
    this.lastCheck = 0;
    this.consecutiveFailures = 0;
    this.healthCheckIntervalMs = 30000;
    this.healthCheckTimer = null;
    this.listeners = new Set();
  }

  onConnectionChange(callback) {
    this.listeners.add(callback);
    return () => this.listeners.delete(callback);
  }

  notifyListeners(connected, reason) {
    for (const listener of this.listeners) {
      try {
        listener({ connected, reason });
      } catch (e) {
        log.error("Connection listener error", { error: e.message });
      }
    }
  }

  updateStatus(connected, reason = null) {
    const wasConnected = this.connected;
    this.connected = connected;
    this.lastCheck = Date.now();

    if (connected) {
      this.consecutiveFailures = 0;
    } else {
      this.consecutiveFailures++;
    }

    if (wasConnected !== connected) {
      log.info("Connection status changed", {
        connected,
        reason,
        consecutiveFailures: this.consecutiveFailures
      });
      this.notifyListeners(connected, reason);
    }
  }

  shouldAttemptReconnect() {
    if (this.consecutiveFailures >= 10) {
      return false;
    }
    return true;
  }

  startHealthCheck(checkFn, intervalMs = 30000) {
    this.healthCheckIntervalMs = intervalMs;
    this.stopHealthCheck();

    this.healthCheckTimer = setInterval(async () => {
      try {
        const result = await checkFn();
        this.updateStatus(result.connected, result.reason);
      } catch (error) {
        this.updateStatus(false, error.message);
      }
    }, intervalMs);

    log.debug("Health check started", { intervalMs });
  }

  stopHealthCheck() {
    if (this.healthCheckTimer) {
      clearInterval(this.healthCheckTimer);
      this.healthCheckTimer = null;
    }
  }

  getStatus() {
    return {
      connected: this.connected,
      lastCheck: this.lastCheck,
      consecutiveFailures: this.consecutiveFailures,
      shouldReconnect: this.shouldAttemptReconnect(),
    };
  }
}

export const connectionManager = new ConnectionManager();

/**
 * Fetch with timeout using AbortController
 * @param {string} url - URL to fetch
 * @param {object} options - fetch options
 * @param {number} timeoutMs - timeout in milliseconds
 */
export async function fetchWithTimeout(url, options = {}, timeoutMs = 30000) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), timeoutMs);

  try {
    const response = await fetch(url, {
      ...options,
      signal: controller.signal,
    });
    return response;
  } finally {
    clearTimeout(timeout);
  }
}

/**
 * Fetch tools from the Unreal HTTP server with retry support
 * @param {string} baseUrl - Unreal MCP server base URL
 * @param {number} timeoutMs - request timeout in milliseconds
 */
export async function fetchUnrealTools(baseUrl, timeoutMs) {
  const execute = async () => {
    const response = await fetchWithTimeout(`${baseUrl}/mcp/tools`, {}, timeoutMs);
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}: ${response.statusText}`);
    }
    const data = await response.json();
    connectionManager.updateStatus(true);
    return data.tools || [];
  };

  try {
    return await withRetry(execute, {
      maxRetries: 2,
      onRetry: ({ attempt, delayMs }) => {
        log.warn("Retrying tools fetch", { attempt, delayMs });
      },
    });
  } catch (error) {
    if (error.name === "AbortError") {
      log.error("Request timeout fetching tools", { url: `${baseUrl}/mcp/tools` });
    } else {
      log.error("Failed to fetch tools from Unreal", { error: error.message });
    }
    connectionManager.updateStatus(false, error.message);
    return [];
  }
}

/**
 * Execute a tool via the Unreal HTTP server with retry support
 * @param {string} baseUrl - Unreal MCP server base URL
 * @param {number} timeoutMs - request timeout in milliseconds
 * @param {string} toolName - name of the tool to execute
 * @param {object} args - tool arguments
 * @param {object} options - additional options
 * @param {boolean} options.retry - enable retry with backoff (default: true)
 * @param {number} options.maxRetries - max retry attempts (default: 3)
 */
export async function executeUnrealTool(baseUrl, timeoutMs, toolName, args, options = {}) {
  const { retry = true, maxRetries = 3 } = options;
  const url = `${baseUrl}/mcp/tool/${toolName}`;

  const execute = async () => {
    const response = await fetchWithTimeout(url, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify(args || {}),
    }, timeoutMs);

    const data = await response.json();
    log.debug("Tool executed", { tool: toolName, success: data.success });

    connectionManager.updateStatus(true);
    return data;
  };

  try {
    if (retry) {
      return await withRetry(execute, {
        maxRetries,
        onRetry: ({ attempt, delayMs }) => {
          log.warn("Retrying tool execution", { tool: toolName, attempt, delayMs });
        },
      });
    }
    return await execute();
  } catch (error) {
    const errorMessage = error.name === "AbortError"
      ? `Request timeout after ${timeoutMs}ms`
      : error.message;

    connectionManager.updateStatus(false, errorMessage);
    log.error("Tool execution failed", { tool: toolName, error: errorMessage });

    return {
      success: false,
      message: `Failed to execute tool: ${errorMessage}`,
    };
  }
}

/**
 * Check if Unreal Editor is running with the plugin (with retry for transient failures)
 * @param {string} baseUrl - Unreal MCP server base URL
 * @param {number} timeoutMs - request timeout in milliseconds
 */
export async function checkUnrealConnection(baseUrl, timeoutMs) {
  const execute = async () => {
    const response = await fetchWithTimeout(`${baseUrl}/mcp/status`, {}, timeoutMs);
    if (response.ok) {
      const data = await response.json();
      connectionManager.updateStatus(true);
      return { connected: true, ...data };
    }
    throw new Error(`HTTP ${response.status}`);
  };

  try {
    return await withRetry(execute, {
      maxRetries: 2,
      shouldRetry: (error) => {
        if (error.code === "ECONNREFUSED") return false;
        return isRetryableError(error);
      },
    });
  } catch (error) {
    const reason = error.name === "AbortError" ? "timeout" : error.message;
    connectionManager.updateStatus(false, reason);
    return { connected: false, reason };
  }
}

/**
 * Convert Unreal tool parameter schema to MCP tool input schema
 * @param {Array} unrealParams - array of parameter descriptors from Unreal
 */
export function convertToMCPSchema(unrealParams) {
  const properties = {};
  const required = [];

  for (const param of unrealParams || []) {
    const prop = {
      type: param.type === "number" ? "number" :
            param.type === "boolean" ? "boolean" :
            param.type === "array" ? "array" :
            param.type === "object" ? "object" : "string",
      description: param.description,
    };

    if (param.default !== undefined) {
      prop.default = param.default;
    }

    properties[param.name] = prop;

    if (param.required) {
      required.push(param.name);
    }
  }

  return {
    type: "object",
    properties,
    required: required.length > 0 ? required : undefined,
  };
}

/**
 * Sleep for a given number of milliseconds.
 * @param {number} ms - milliseconds to sleep
 */
export function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

/**
 * Execute a tool via Unreal's async task queue (task_submit → poll task_status → task_result).
 * Falls back to synchronous executeUnrealTool() if task_submit fails.
 *
 * @param {string} baseUrl - Unreal MCP server base URL
 * @param {number} timeoutMs - per-request HTTP timeout in milliseconds
 * @param {string} toolName - name of the tool to execute
 * @param {object} args - tool arguments
 * @param {object} [options]
 * @param {function} [options.onProgress] - callback({progress, total, message})
 * @param {number}   [options.pollIntervalMs=2000] - poll interval
 * @param {number}   [options.asyncTimeoutMs=300000] - overall async timeout (5 min)
 */
export async function executeUnrealToolAsync(baseUrl, timeoutMs, toolName, args, options = {}) {
  const {
    onProgress,
    pollIntervalMs = 2000,
    asyncTimeoutMs = 300000,
  } = options;

  // Step 1: Submit task
  let taskId;
  try {
    const submitResponse = await fetchWithTimeout(`${baseUrl}/mcp/tool/task_submit`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        tool_name: toolName,
        params: args || {},
        timeout_ms: asyncTimeoutMs,
      }),
    }, timeoutMs);

    const submitData = await submitResponse.json();
    if (!submitData.success || !submitData.data?.task_id) {
      log.debug("task_submit failed or no task_id, falling back to sync", { tool: toolName });
      return executeUnrealTool(baseUrl, timeoutMs, toolName, args);
    }
    taskId = submitData.data.task_id;
    log.debug("Task submitted", { tool: toolName, taskId });
  } catch (error) {
    log.debug("task_submit error, falling back to sync", { tool: toolName, error: error.message });
    return executeUnrealTool(baseUrl, timeoutMs, toolName, args);
  }

  // Step 2: Poll for completion
  const deadline = Date.now() + asyncTimeoutMs;
  let pollCount = 0;

  while (Date.now() < deadline) {
    await sleep(pollIntervalMs);
    pollCount++;

    let statusData;
    try {
      const statusResponse = await fetchWithTimeout(`${baseUrl}/mcp/tool/task_status`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ task_id: taskId }),
      }, timeoutMs);
      statusData = await statusResponse.json();
    } catch (error) {
      log.error("task_status poll failed", { taskId, error: error.message });
      continue;
    }

    const taskStatus = statusData.data?.status || statusData.status;
    const progress = statusData.data?.progress ?? pollCount;
    const total = statusData.data?.total ?? 0;
    const progressMessage = statusData.data?.progress_message || `Polling... (${pollCount})`;

    // Send progress notification
    if (onProgress) {
      onProgress({ progress, total, message: progressMessage });
    }

    // Check for terminal states
    if (taskStatus === "completed" || taskStatus === "failed" || taskStatus === "cancelled") {
      // Step 3: Retrieve result
      try {
        const resultResponse = await fetchWithTimeout(`${baseUrl}/mcp/tool/task_result`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ task_id: taskId }),
        }, timeoutMs);
        const resultData = await resultResponse.json();
        log.debug("Task completed", { tool: toolName, taskId, status: taskStatus });
        return resultData;
      } catch (error) {
        log.error("task_result fetch failed", { taskId, error: error.message });
        return {
          success: false,
          message: `Task ${taskStatus} but failed to retrieve result: ${error.message}`,
        };
      }
    }
  }

  // Async timeout exceeded
  return {
    success: false,
    message: `Task timed out after ${asyncTimeoutMs}ms (task_id: ${taskId})`,
  };
}

/**
 * Convert Unreal tool annotations to MCP annotations format
 * @param {object} unrealAnnotations - annotation object from Unreal
 */
export function convertAnnotations(unrealAnnotations) {
  if (!unrealAnnotations) {
    return {
      readOnlyHint: false,
      destructiveHint: true,
      idempotentHint: false,
      openWorldHint: false,
    };
  }
  return {
    readOnlyHint: unrealAnnotations.readOnlyHint ?? false,
    destructiveHint: unrealAnnotations.destructiveHint ?? true,
    idempotentHint: unrealAnnotations.idempotentHint ?? false,
    openWorldHint: unrealAnnotations.openWorldHint ?? false,
  };
}
