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
 *   UNREAL_MCP_URL          - Base URL for Unreal MCP server (default: http://localhost:3000)
 *   MCP_REQUEST_TIMEOUT_MS  - HTTP request timeout in ms (default: 30000)
 *   MCP_ASYNC_ENABLED       - Enable async task queue (default: true)
 *   MCP_ASYNC_TIMEOUT_MS    - Async operation timeout (default: 300000)
 *   MCP_POLL_INTERVAL_MS    - Async poll interval (default: 2000)
 *   INJECT_CONTEXT          - Auto-inject UE API docs on tool calls (default: false)
 *   AGENTIC_FALLBACK_ENABLED - Enable offline binary injector fallback (default: true)
 *   AGENTIC_PROJECT_ROOT    - UE project root for fallback (auto-detect if not set)
 */

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
} from "./lib.js";

// Offline fallback via Python binary injector
import {
  checkFallbackAvailable,
  executeFallbackTool,
  FALLBACK_TOOLS,
} from "./fallback.js";

// ---------------------------------------------------------------------------
// Configuration with defaults
// ---------------------------------------------------------------------------
const CONFIG = {
  unrealMcpUrl: process.env.UNREAL_MCP_URL || "http://localhost:3000",
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
  if (editorConnected) {
    const unrealTools = await fetchUnrealTools();
    cachedTools = unrealTools;

    for (const tool of unrealTools) {
      mcpTools.push({
        name: `unreal_${tool.name}`,
        description: `[Unreal Editor] ${tool.description}`,
        inputSchema: convertToMCPSchema(tool.parameters),
        annotations: convertAnnotations(tool.annotations),
      });
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

  // ---- Route to appropriate path ----
  let result;

  if (editorConnected) {
    // Primary path: live editor
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
async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);

  const categories = listCategories();
  const testContext = loadContextForCategory("animation");
  const contextStatus = testContext
    ? `OK (${categories.length} categories loaded)`
    : "FAILED";

  log.info("AgenticMCP Server started", {
    version: "2.0.0",
    unrealUrl: CONFIG.unrealMcpUrl,
    timeoutMs: CONFIG.requestTimeoutMs,
    asyncEnabled: CONFIG.asyncEnabled,
    fallbackEnabled: CONFIG.fallbackEnabled,
    projectRoot: CONFIG.projectRoot || "(auto-detect)",
    contextSystem: contextStatus,
    contextCategories: categories,
  });
}

main().catch((error) => {
  log.error("Fatal error", { error: error.message, stack: error.stack });
  process.exit(1);
});
