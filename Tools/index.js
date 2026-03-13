#!/usr/bin/env node

/**
 * AgenticMCP Server
 *
 * Dual-path MCP server for Unreal Engine 5 editor manipulation.
 *
 * Primary path: HTTP connection to C++ plugin running inside UE5 editor.
 * Fallback path: Python binary injector for offline .umap manipulation.
 *
 * Based on Natfii/ue5-mcp-bridge (MIT). Extended with:
 * - Offline fallback via binary injector
 * - Snapshot/rollback system
 * - Blueprint validation endpoint
 * - Auto-path selection with status reporting
 *
 * Environment Variables:
 *   UNREAL_MCP_URL          - Base URL for Unreal MCP server (default: http://localhost:9847)
 *   MCP_REQUEST_TIMEOUT_MS  - HTTP request timeout in ms (default: 30000)
 *   MCP_ASYNC_ENABLED       - Enable async task queue (default: true)
 *   MCP_ASYNC_TIMEOUT_MS    - Async operation timeout (default: 300000)
 *   MCP_POLL_INTERVAL_MS    - Async poll interval (default: 2000)
 *   INJECT_CONTEXT          - Auto-inject UE API docs on tool calls (default: false)
 *   AGENTIC_FALLBACK_ENABLED - Enable offline binary injector fallback (default: true)
 *   AGENTIC_PROJECT_ROOT    - UE project root for fallback (auto-detect if not set)
 */

import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
} from "@modelcontextprotocol/sdk/types.js";

// Dynamic context loader for UE API documentation
import {
  getContextForTool,
  getContextForQuery,
  listCategories,
  getCategoryInfo,
  loadContextForCategory,
} from "./context-loader.js";

// HTTP client and schema helpers
import {
  log,
  fetchUnrealTools as _fetchUnrealTools,
  executeUnrealTool as _executeUnrealTool,
  executeUnrealToolAsync as _executeUnrealToolAsync,
  checkUnrealConnection as _checkUnrealConnection,
  convertToMCPSchema,
  convertAnnotations,
  connectionManager,
} from "./lib.js";

// Offline fallback via Python binary injector
import {
  checkFallbackAvailable,
  executeFallbackTool,
  FALLBACK_TOOLS,
} from "./fallback.js";

// VisualAgent unified CLI tool
import {
  VISUALAGENT_TOOL_DEFINITION,
  executeVisualAgentCommand,
} from "./visual-agent.js";

// Quantized Inference Layer -- deterministic scene wiring via cached manifests
import {
  QUANTIZED_TOOLS,
  handleQuantizedTool,
  getManifest,
} from "./quantized-inference.js";

// Scene Verifier -- exhaustive post-wiring validation per scene
import {
  VERIFIER_TOOLS,
  handleVerifierTool,
} from "./scene-verifier.js";

// Confidence Gate -- enforces quantized inference propagation path
// Reads and plans pass freely. Mutations are gated by confidence threshold.
import {
  evaluateGate,
  recordSnapshot,
  registerWiringPlan,
  CONFIDENCE_THRESHOLD,
} from "./confidence-gate.js";

// Project State Validator -- cross-validates mutations against live project data
// Every mutation is verified against the actual UE5 project state before execution.
// Accuracy at all costs.
import {
  validateMutation,
  formatValidationFailure,
} from "./project-state-validator.js";

// Idempotency Guard -- checks if the intended result already exists.
// Makes the entire pipeline resumable: start, stop, resume without duplicates.
import {
  checkIdempotency,
  formatSkippedResponse,
} from "./idempotency-guard.js";

// ---------------------------------------------------------------------------
// Configuration with defaults
// ---------------------------------------------------------------------------
const CONFIG = {
  unrealMcpUrl: process.env.UNREAL_MCP_URL || "http://localhost:9847",
  requestTimeoutMs: parseInt(process.env.MCP_REQUEST_TIMEOUT_MS, 10) || 30000,
  injectContext: process.env.INJECT_CONTEXT === "true",
  asyncEnabled: process.env.MCP_ASYNC_ENABLED !== "false",
  asyncTimeoutMs: parseInt(process.env.MCP_ASYNC_TIMEOUT_MS, 10) || 300000,
  pollIntervalMs: parseInt(process.env.MCP_POLL_INTERVAL_MS, 10) || 2000,
  fallbackEnabled: process.env.AGENTIC_FALLBACK_ENABLED !== "false",
  projectRoot: process.env.AGENTIC_PROJECT_ROOT || "",
};

// Bind CONFIG values to library functions
const fetchUnrealTools = () =>
  _fetchUnrealTools(CONFIG.unrealMcpUrl, CONFIG.requestTimeoutMs);
const executeUnrealTool = (toolName, args) =>
  _executeUnrealTool(CONFIG.unrealMcpUrl, CONFIG.requestTimeoutMs, toolName, args);
const checkUnrealConnection = () =>
  _checkUnrealConnection(CONFIG.unrealMcpUrl, CONFIG.requestTimeoutMs);

// ---------------------------------------------------------------------------
// Connection state tracking
// ---------------------------------------------------------------------------
let editorConnected = false;
let fallbackAvailable = false;

// Create the MCP server
const server = new Server(
  { name: "agentic-mcp-server", version: "2.0.0" },
  { capabilities: { tools: {} } }
);

// Cache for tools (refreshed on each list request)
let cachedTools = [];

// ---------------------------------------------------------------------------
// Tool Registry — loads description, parameters, and annotations from JSON
// The C++ /mcp/tools endpoint only returns tool names. This registry provides
// the full metadata (description, parameter schema, annotations) that the MCP
// protocol requires so that agents know HOW to call each tool.
// ---------------------------------------------------------------------------
const __dirname = dirname(fileURLToPath(import.meta.url));
let toolRegistry = new Map();
try {
  const registryPath = join(__dirname, "tool-registry.json");
  const registryData = JSON.parse(readFileSync(registryPath, "utf-8"));
  for (const tool of registryData.tools || []) {
    toolRegistry.set(tool.name, tool);
  }
  log.info("Tool registry loaded", { count: toolRegistry.size });
} catch (error) {
  log.error("Failed to load tool registry", { error: error.message });
  log.warn("Tools will be registered with minimal metadata -- agents may not send correct parameters");
}

// ---------------------------------------------------------------------------
// Handle list_tools request
// ---------------------------------------------------------------------------
server.setRequestHandler(ListToolsRequestSchema, async () => {
  // Check both paths
  const status = await checkUnrealConnection();
  editorConnected = status.connected;

  if (CONFIG.fallbackEnabled) {
    fallbackAvailable = await checkFallbackAvailable(CONFIG.projectRoot);
  }

  const mcpTools = [];

  // Status tool is always available
  mcpTools.push({
    name: "unreal_status",
    description: editorConnected
      ? `Check Unreal Editor connection. Currently: CONNECTED to ${status.projectName || "Unknown"} (${status.engineVersion || "Unknown"}). Fallback: ${fallbackAvailable ? "AVAILABLE" : "NOT AVAILABLE"}`
      : `Check Unreal Editor connection. Currently: NOT CONNECTED. Fallback: ${fallbackAvailable ? "AVAILABLE (offline mode)" : "NOT AVAILABLE"}. Start Unreal Editor with the AgenticMCP plugin for full functionality.`,
    inputSchema: { type: "object", properties: {} },
    annotations: {
      readOnlyHint: true,
      destructiveHint: false,
      idempotentHint: true,
      openWorldHint: false,
    },
  });

  // If editor is connected, register all live tools via auto-discovery
  // The C++ plugin returns tool names; we enrich with metadata from tool-registry.json
  if (editorConnected) {
    const unrealTools = await fetchUnrealTools();
    cachedTools = unrealTools;

    for (const tool of unrealTools) {
      const registry = toolRegistry.get(tool.name);
      if (registry) {
        // Full metadata from registry
        mcpTools.push({
          name: `unreal_${tool.name}`,
          description: `[Unreal Editor] ${registry.description}`,
          inputSchema: convertToMCPSchema(registry.parameters),
          annotations: convertAnnotations(registry.annotations),
        });
      } else {
        // Tool exists in C++ but not in registry -- register with minimal info
        // so it is still callable, but log a warning
        log.warn("Tool missing from registry", { tool: tool.name });
        mcpTools.push({
          name: `unreal_${tool.name}`,
          description: `[Unreal Editor] ${tool.description || tool.name} (no registry metadata)`,
          inputSchema: convertToMCPSchema(tool.parameters),
          annotations: convertAnnotations(tool.annotations),
        });
      }
    }
  }
  // If editor is NOT connected but fallback is available, register fallback tools
  else if (fallbackAvailable) {
    for (const tool of FALLBACK_TOOLS) {
      mcpTools.push({
        name: `unreal_${tool.name}`,
        description: `[Offline Fallback] ${tool.description}`,
        inputSchema: tool.inputSchema,
        annotations: {
          readOnlyHint: tool.readOnly || false,
          destructiveHint: !tool.readOnly,
          idempotentHint: tool.readOnly || false,
          openWorldHint: false,
        },
      });
    }
  }

  // UE context documentation tool is always available
  mcpTools.push({
    name: "unreal_get_ue_context",
    description: `Get Unreal Engine API context/documentation. Categories: ${listCategories().join(", ")}. Can also search by query keywords.`,
    inputSchema: {
      type: "object",
      properties: {
        category: {
          type: "string",
          description: `Specific category to load: ${listCategories().join(", ")}`,
        },
        query: {
          type: "string",
          description:
            "Search query to find relevant context (e.g., 'state machine transitions', 'async loading')",
        },
      },
    },
    annotations: {
      readOnlyHint: true,
      destructiveHint: false,
      idempotentHint: true,
      openWorldHint: false,
    },
  });

  // VisualAgent unified CLI tool (only when editor is connected)
  if (editorConnected) {
    mcpTools.push({
      name: `unreal_${VISUALAGENT_TOOL_DEFINITION.name}`,
      description: VISUALAGENT_TOOL_DEFINITION.description,
      inputSchema: VISUALAGENT_TOOL_DEFINITION.inputSchema,
      annotations: VISUALAGENT_TOOL_DEFINITION.annotations,
    });
  }

  // Quantized Inference tools -- always available (work with cached data + live editor)
  for (const qTool of QUANTIZED_TOOLS) {
    mcpTools.push({
      name: `unreal_${qTool.name}`,
      description: `[Quantized Inference] ${qTool.description}`,
      inputSchema: qTool.inputSchema,
      annotations: {
        readOnlyHint: qTool.annotations?.readOnlyHint || false,
        destructiveHint: qTool.annotations?.destructiveHint || false,
        idempotentHint: qTool.annotations?.readOnlyHint || false,
        openWorldHint: false,
      },
    });
  }

  // Scene Verifier tools -- post-wiring validation (always available)
  for (const vTool of VERIFIER_TOOLS) {
    mcpTools.push({
      name: `unreal_${vTool.name}`,
      description: `[Scene Verifier] ${vTool.description}`,
      inputSchema: vTool.inputSchema,
      annotations: {
        readOnlyHint: true,
        destructiveHint: false,
        idempotentHint: true,
        openWorldHint: false,
      },
    });
  }

  log.info("Tools listed", {
    count: mcpTools.length,
    editorConnected,
    fallbackAvailable,
  });

  return { tools: mcpTools };
});

// ---------------------------------------------------------------------------
// Handle call_tool request
// ---------------------------------------------------------------------------
server.setRequestHandler(CallToolRequestSchema, async (request) => {
  const { name, arguments: args } = request.params;

  // ---- UE Context documentation request ----
  if (name === "unreal_get_ue_context") {
    return handleContextRequest(args);
  }

  // ---- Status check ----
  if (name === "unreal_status") {
    return handleStatusRequest();
  }

  // ---- Validate tool name prefix ----
  if (!name.startsWith("unreal_")) {
    return {
      content: [{ type: "text", text: `Unknown tool: ${name}` }],
      isError: true,
    };
  }

  const toolName = name.substring(7); // strip "unreal_" prefix

  // ---- Scene Verifier tools ----
  const verifierToolNames = VERIFIER_TOOLS.map((t) => t.name);
  if (verifierToolNames.includes(toolName)) {
    const callUnreal = async (endpoint, method = "POST", body = {}) => {
      if (!editorConnected) return null;
      try {
        const url = `${CONFIG.unrealMcpUrl}${endpoint}`;
        const options = {
          method,
          headers: { "Content-Type": "application/json" },
          signal: AbortSignal.timeout(CONFIG.requestTimeoutMs),
        };
        if (method !== "GET") {
          options.body = JSON.stringify(body);
        }
        const response = await fetch(url, options);
        return await response.json();
      } catch (error) {
        log.error("callUnreal failed", { endpoint, error: error.message });
        return null;
      }
    };
    return handleVerifierTool(toolName, args, callUnreal);
  }

  // ---- Quantized Inference tools ----
  const quantizedToolNames = QUANTIZED_TOOLS.map((t) => t.name);
  if (quantizedToolNames.includes(toolName)) {
    // Create a callUnreal wrapper for the quantized layer
    const callUnreal = async (endpoint, method = "POST", body = {}) => {
      if (!editorConnected) return null;
      try {
        const url = `${CONFIG.unrealMcpUrl}${endpoint}`;
        const options = {
          method,
          headers: { "Content-Type": "application/json" },
          signal: AbortSignal.timeout(CONFIG.requestTimeoutMs),
        };
        if (method !== "GET") {
          options.body = JSON.stringify(body);
        }
        const response = await fetch(url, options);
        return await response.json();
      } catch (error) {
        log.error("callUnreal failed", { endpoint, error: error.message });
        return null;
      }
    };
    return handleQuantizedTool(toolName, args, callUnreal, CONFIG);
  }

  // ---- VisualAgent unified CLI ----
  if (toolName === "cli") {
    if (!editorConnected) {
      return {
        content: [{ type: "text", text: "unreal_cli requires Unreal Editor to be connected." }],
        isError: true,
      };
    }

    const commandStr = args?.command || "";

    // Create HTTP client wrapper for VisualAgent commands
    const httpClient = async (endpoint, params) => {
      return await _executeUnrealTool(CONFIG.unrealMcpUrl, CONFIG.requestTimeoutMs, endpoint, params);
    };

    const result = await executeVisualAgentCommand(commandStr, httpClient, {
      includeSnapshot: true,
    });

    // Build response content
    const content = [];

    // Main result text
    let resultText = result.message || JSON.stringify(result, null, 2);
    content.push({ type: "text", text: resultText });

    // If screenshot was taken, include as image
    if (result.data && result.mimeType) {
      content.push({
        type: "image",
        data: result.data,
        mimeType: result.mimeType,
      });
    }

    // If scene snapshot was auto-appended, include it
    if (result._sceneSnapshot) {
      content.push({
        type: "text",
        text: `\n## Scene Snapshot (${result._actorCount} actors)\n\`\`\`yaml\n${result._sceneSnapshot}\`\`\``,
      });
    }

    return {
      content,
      isError: result.success === false,
    };
  }

  // ---- Confidence Gate: evaluate before mutation ----
  const gateResult = evaluateGate(toolName, args);
  if (!gateResult.allowed) {
    log.info("Confidence gate BLOCKED mutation", {
      tool: toolName,
      confidence: gateResult.confidence,
      threshold: gateResult.threshold,
    });
    return {
      content: [
        {
          type: "text",
          text: JSON.stringify({
            blocked: true,
            tool: toolName,
            confidence: gateResult.confidence,
            threshold: gateResult.threshold,
            reason: gateResult.reason,
            suggestion: gateResult.suggestion,
            message: `Mutation blocked: confidence ${gateResult.confidence.toFixed(2)} is below threshold ${gateResult.threshold}. ` +
              `Claude can freely read and plan -- adapt your approach to increase confidence, then retry.`,
          }, null, 2),
        },
      ],
      isError: true,
    };
  }

  // Record snapshot calls for confidence tracking
  if (toolName === "snapshotGraph" || toolName === "snapshot_graph") {
    recordSnapshot();
  }

  // ---- Project State Validator: cross-validate against live project ----
  // Only runs when the editor is connected (needs live read access).
  // Every mutation is verified against actual project state before execution.
  if (editorConnected) {
    const validationResult = await validateMutation(
      toolName,
      args,
      // Pass the read-only tool executor so the validator can query the editor
      async (readToolName, readArgs) => {
        return await executeUnrealTool(readToolName, readArgs);
      }
    );

    if (!validationResult.valid) {
      log.info("Project state validator BLOCKED mutation", {
        tool: toolName,
        checksRun: validationResult.checksRun,
        failures: validationResult.failures.length,
        failedChecks: validationResult.failures.map((f) => f.check),
      });

      const failureResponse = formatValidationFailure(toolName, args, validationResult);

      return {
        content: [
          {
            type: "text",
            text: JSON.stringify(failureResponse, null, 2),
          },
        ],
        isError: true,
      };
    }

    log.debug("Project state validation PASSED", {
      tool: toolName,
      checksRun: validationResult.checksRun,
    });
  }

  // ---- Idempotency Guard: skip if the intended result already exists ----
  // Makes the pipeline fully resumable. Start, stop, come back, re-run --
  // completed work is never duplicated.
  if (editorConnected) {
    const idempotencyResult = await checkIdempotency(
      toolName,
      args,
      async (readToolName, readArgs) => {
        return await executeUnrealTool(readToolName, readArgs);
      }
    );

    if (idempotencyResult.alreadyDone) {
      log.info("Idempotency guard: SKIPPED (already done)", {
        tool: toolName,
        reason: idempotencyResult.reason,
      });

      const skippedResponse = formatSkippedResponse(toolName, args, idempotencyResult);

      return {
        content: [
          {
            type: "text",
            text: JSON.stringify(skippedResponse, null, 2),
          },
        ],
        isError: false, // Not an error -- just already done
      };
    }
  }

  // ---- Route to appropriate path ----
  let result;

  if (editorConnected) {
    // Primary path: live editor (already validated against project state)
    result = await executeLiveTool(request, toolName, args);
  } else if (fallbackAvailable) {
    // Fallback path: binary injector
    result = await executeFallbackTool(toolName, args, CONFIG.projectRoot);
  } else {
    return {
      content: [
        {
          type: "text",
          text: `Cannot execute tool "${toolName}": Unreal Editor is not connected and offline fallback is not available. Start the editor with AgenticMCP plugin enabled, or configure AGENTIC_PROJECT_ROOT for offline mode.`,
        },
      ],
      isError: true,
    };
  }

  // ---- Optionally inject UE API context ----
  let responseText = result.success
    ? result.message + (result.data ? "\n\n" + JSON.stringify(result.data) : "")
    : `Error: ${result.message}`;

  if (CONFIG.injectContext && result.success) {
    const context = getContextForTool(toolName);
    if (context) {
      responseText += `\n\n---\n\n## Relevant UE API Context\n\n${context}`;
      log.debug("Injected context for tool", { tool: toolName });
    }
  }

  return {
    content: [{ type: "text", text: responseText }],
    isError: !result.success,
  };
});

// ---------------------------------------------------------------------------
// Execute tool via live editor (with async support)
// ---------------------------------------------------------------------------
async function executeLiveTool(request, toolName, args) {
  // Task queue tools are the async infrastructure itself — don't wrap them
  const isTaskTool = toolName.startsWith("task_");

  if (CONFIG.asyncEnabled && !isTaskTool) {
    const progressToken = request.params._meta?.progressToken;
    const onProgress = progressToken
      ? ({ progress, total, message }) => {
          server.notification({
            method: "notifications/progress",
            params: { progressToken, progress, total: total || 0, message },
          });
        }
      : undefined;

    return _executeUnrealToolAsync(
      CONFIG.unrealMcpUrl,
      CONFIG.requestTimeoutMs,
      toolName,
      args,
      {
        onProgress,
        pollIntervalMs: CONFIG.pollIntervalMs,
        asyncTimeoutMs: CONFIG.asyncTimeoutMs,
      }
    );
  }

  return executeUnrealTool(toolName, args);
}

// ---------------------------------------------------------------------------
// Handle UE context documentation request
// ---------------------------------------------------------------------------
function handleContextRequest(args) {
  const { category, query } = args || {};

  if (category) {
    const content = loadContextForCategory(category);
    if (content) {
      return {
        content: [
          { type: "text", text: `# UE API Context: ${category}\n\n${content}` },
        ],
      };
    }
    return {
      content: [
        {
          type: "text",
          text: `Unknown category: ${category}. Available: ${listCategories().join(", ")}`,
        },
      ],
      isError: true,
    };
  }

  if (query) {
    const queryResult = getContextForQuery(query);
    if (queryResult) {
      return {
        content: [
          {
            type: "text",
            text: `# UE API Context: ${queryResult.categories.join(", ")}\n\n${queryResult.content}`,
          },
        ],
      };
    }
    return {
      content: [
        {
          type: "text",
          text: `No context found for query: "${query}". Try categories: ${listCategories().join(", ")}`,
        },
      ],
    };
  }

  // No category or query — list all available categories
  const categoryList = listCategories().map((cat) => {
    const info = getCategoryInfo(cat);
    return `- **${cat}**: Keywords: ${info.keywords.slice(0, 5).join(", ")}...`;
  });

  return {
    content: [
      {
        type: "text",
        text: `# Available UE API Context Categories\n\n${categoryList.join("\n")}\n\nUse \`category\` param for specific context or \`query\` to search by keywords.`,
      },
    ],
  };
}

// ---------------------------------------------------------------------------
// Handle status request
// ---------------------------------------------------------------------------
async function handleStatusRequest() {
  const status = await checkUnrealConnection();
  editorConnected = status.connected;

  if (CONFIG.fallbackEnabled) {
    fallbackAvailable = await checkFallbackAvailable(CONFIG.projectRoot);
  }

  const response = {
    editor_connected: editorConnected,
    fallback_available: fallbackAvailable,
    active_path: editorConnected ? "live_editor" : fallbackAvailable ? "offline_fallback" : "none",
    context_categories: listCategories(),
  };

  if (editorConnected) {
    response.project = status.projectName;
    response.engine = status.engineVersion;

    const unrealTools = await fetchUnrealTools();
    const categories = {};
    for (const tool of unrealTools) {
      let category = "utility";
      if (tool.name.startsWith("blueprint_")) category = "blueprint";
      else if (tool.name.startsWith("anim_blueprint")) category = "animation";
      else if (tool.name.startsWith("asset_")) category = "asset";
      else if (tool.name.startsWith("task_")) category = "task_queue";
      else if (
        tool.name.includes("actor") ||
        tool.name.includes("spawn") ||
        tool.name.includes("move") ||
        tool.name.includes("level")
      )
        category = "actor";
      categories[category] = (categories[category] || 0) + 1;
    }
    response.tool_summary = categories;
    response.total_tools = unrealTools.length;
    response.message = "Unreal Editor connected. All tools operational.";
  } else if (fallbackAvailable) {
    response.total_tools = FALLBACK_TOOLS.length;
    response.message =
      "Unreal Editor not connected. Offline fallback active — subset of tools available. Start editor for full functionality.";
    response.degraded_capabilities = [
      "No compilation or validation",
      "No real-time actor spawning",
      "No viewport control",
      "Read/write .umap files only",
    ];
  } else {
    response.message =
      "No connection available. Start Unreal Editor with AgenticMCP plugin, or set AGENTIC_PROJECT_ROOT for offline mode.";
  }

  return {
    content: [{ type: "text", text: JSON.stringify(response, null, 2) }],
    isError: !editorConnected && !fallbackAvailable,
  };
}

// ---------------------------------------------------------------------------
// Start the server
// ---------------------------------------------------------------------------

// Global error handlers for unhandled rejections and exceptions
process.on('unhandledRejection', (reason, promise) => {
  log.error('Unhandled Promise Rejection', {
    reason: reason?.message || String(reason),
    stack: reason?.stack
  });
});

process.on('uncaughtException', (error) => {
  log.error('Uncaught Exception', {
    error: error.message,
    stack: error.stack
  });
  // Give time to flush logs before exiting
  setTimeout(() => process.exit(1), 100);
});

async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);

  const categories = listCategories();
  const testContext = loadContextForCategory("animation");
  const contextStatus = testContext
    ? `OK (${categories.length} categories loaded)`
    : "FAILED";

  // Start health check monitoring
  connectionManager.onConnectionChange(({ connected, reason }) => {
    if (connected) {
      log.info("Editor reconnected");
      editorConnected = true;
    } else {
      log.warn("Editor disconnected", { reason });
      editorConnected = false;
    }
  });

  // Initial connection check
  const initialStatus = await checkUnrealConnection();
  editorConnected = initialStatus.connected;

  // Start periodic health checks (every 30 seconds)
  if (editorConnected) {
    connectionManager.startHealthCheck(
      () => _checkUnrealConnection(CONFIG.unrealMcpUrl, CONFIG.requestTimeoutMs),
      30000
    );
  }

  log.info("AgenticMCP Server started", {
    version: "2.0.0",
    unrealUrl: CONFIG.unrealMcpUrl,
    timeoutMs: CONFIG.requestTimeoutMs,
    asyncEnabled: CONFIG.asyncEnabled,
    fallbackEnabled: CONFIG.fallbackEnabled,
    projectRoot: CONFIG.projectRoot || "(auto-detect)",
    contextSystem: contextStatus,
    contextCategories: categories,
    editorConnected,
    reconnectionEnabled: true,
  });
}

main().catch((error) => {
  log.error("Fatal error", { error: error.message, stack: error.stack });
  process.exit(1);
});
