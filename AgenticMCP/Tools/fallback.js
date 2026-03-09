/**
 * AgenticMCP Fallback Bridge
 *
 * Routes tool calls to the Python binary injector when the UE5 editor is not running.
 * The injector uses UAssetAPI to parse and modify .umap/.uasset files directly.
 *
 * This module:
 * 1. Checks if the fallback path is available (Python + project root exist)
 * 2. Defines the subset of tools supported in offline mode
 * 3. Executes tools by spawning Python subprocesses
 *
 * The fallback supports read operations and limited write operations.
 * Compilation, validation, and real-time actor manipulation require the live editor.
 */

import { execFile } from "child_process";
import { existsSync } from "fs";
import { join, dirname, resolve, normalize, isAbsolute } from "path";
import { fileURLToPath } from "url";
import { log } from "./lib.js";

const __dirname = dirname(fileURLToPath(import.meta.url));

// Path to the Python core module (relative to AgenticMCP/Tools/)
const CORE_DIR = join(__dirname, "..", "..", "core");
const CLI_PATH = join(CORE_DIR, "cli.py");

// ---------------------------------------------------------------------------
// Security: Path validation
// ---------------------------------------------------------------------------

/**
 * Validate and sanitize a user-provided path.
 * Prevents path traversal attacks and ensures paths are within allowed boundaries.
 *
 * @param {string} userPath - User-provided path
 * @param {string} allowedRoot - Root directory that paths must be under
 * @returns {string} - Validated absolute path
 * @throws {Error} - If path is invalid or outside allowed directory
 */
function validatePath(userPath, allowedRoot) {
  if (!userPath || typeof userPath !== "string") {
    throw new Error("Path is required and must be a string");
  }

  // Normalize to prevent ../ traversal
  const normalized = normalize(userPath);

  // Check for explicit path traversal attempts
  if (normalized.includes("..")) {
    log.error("Path traversal attempt blocked", { path: userPath });
    throw new Error("Path traversal not allowed");
  }

  // Resolve to absolute path
  let resolved;
  if (isAbsolute(normalized)) {
    resolved = normalize(normalized);
  } else if (allowedRoot) {
    resolved = resolve(allowedRoot, normalized);
  } else {
    throw new Error("Relative paths require an allowed root directory");
  }

  // Ensure path is under allowed root
  if (allowedRoot) {
    const resolvedRoot = resolve(allowedRoot);
    if (!resolved.startsWith(resolvedRoot)) {
      log.error("Path outside allowed directory", {
        path: resolved,
        allowed: resolvedRoot
      });
      throw new Error(`Path must be under ${resolvedRoot}`);
    }
  }

  return resolved;
}

/**
 * Validate that a path has an allowed extension.
 *
 * @param {string} filePath - Path to validate
 * @param {string[]} allowedExtensions - List of allowed extensions (e.g., [".umap", ".uasset"])
 * @throws {Error} - If extension is not allowed
 */
function validateExtension(filePath, allowedExtensions) {
  const ext = filePath.toLowerCase().split(".").pop();
  const normalizedExt = "." + ext;

  if (!allowedExtensions.includes(normalizedExt)) {
    throw new Error(`File extension '${normalizedExt}' not allowed. Allowed: ${allowedExtensions.join(", ")}`);
  }
}

const ALLOWED_ASSET_EXTENSIONS = [".umap", ".uasset", ".uexp"];

// ---------------------------------------------------------------------------
// Fallback tool definitions
// These are the tools available when the editor is not running.
// ---------------------------------------------------------------------------
export const FALLBACK_TOOLS = [
  {
    name: "offline_list_levels",
    description:
      "List all .umap level files in the project Content directory. Returns file paths and sizes.",
    readOnly: true,
    inputSchema: {
      type: "object",
      properties: {
        contentPath: {
          type: "string",
          description: "Path to the project Content directory",
        },
      },
      required: ["contentPath"],
    },
  },
  {
    name: "offline_list_actors",
    description:
      "List all actors in a .umap level file. Returns actor names, classes, and indices.",
    readOnly: true,
    inputSchema: {
      type: "object",
      properties: {
        umapPath: {
          type: "string",
          description: "Absolute path to the .umap file",
        },
        classFilter: {
          type: "string",
          description: "Optional class name filter (e.g., 'BP_TeleportPoint')",
        },
      },
      required: ["umapPath"],
    },
  },
  {
    name: "offline_get_actor",
    description:
      "Get detailed properties of a specific actor in a .umap file.",
    readOnly: true,
    inputSchema: {
      type: "object",
      properties: {
        umapPath: {
          type: "string",
          description: "Absolute path to the .umap file",
        },
        actorName: {
          type: "string",
          description: "Name of the actor to inspect",
        },
      },
      required: ["umapPath", "actorName"],
    },
  },
  {
    name: "offline_get_graph",
    description:
      "Get the Blueprint graph (Ubergraph) from a level's script actor. Returns node names, types, and bytecode size.",
    readOnly: true,
    inputSchema: {
      type: "object",
      properties: {
        umapPath: {
          type: "string",
          description: "Absolute path to the .umap file",
        },
        graphName: {
          type: "string",
          description: "Graph name (default: 'EventGraph')",
        },
      },
      required: ["umapPath"],
    },
  },
  {
    name: "offline_level_info",
    description:
      "Get summary information about a .umap file — actor count, export count, import count, engine version.",
    readOnly: true,
    inputSchema: {
      type: "object",
      properties: {
        umapPath: {
          type: "string",
          description: "Absolute path to the .umap file",
        },
      },
      required: ["umapPath"],
    },
  },
  {
    name: "offline_generate_paste_text",
    description:
      "Generate UE5 Blueprint paste text for a given interaction pattern. The paste text can be manually pasted into the Blueprint editor.",
    readOnly: true,
    inputSchema: {
      type: "object",
      properties: {
        pattern: {
          type: "string",
          description:
            "Pattern name: 'teleport_listener', 'story_step_broadcast', 'begin_play_chain', 'multigate'",
        },
        params: {
          type: "object",
          description:
            "Pattern-specific parameters (e.g., { health: 100, speed: 600 })",
        },
      },
      required: ["pattern"],
    },
  },
];

// ---------------------------------------------------------------------------
// Check if fallback is available
// ---------------------------------------------------------------------------

/**
 * Verify that the Python binary injector is available and the project root exists.
 * @param {string} projectRoot - UE project root directory
 * @returns {Promise<boolean>}
 */
export async function checkFallbackAvailable(projectRoot) {
  // Check if CLI script exists
  if (!existsSync(CLI_PATH)) {
    log.debug("Fallback not available: CLI script not found", {
      path: CLI_PATH,
    });
    return false;
  }

  // Check if Python is available
  try {
    await execPromise("python3", ["--version"]);
  } catch {
    log.debug("Fallback not available: Python not found");
    return false;
  }

  // If project root is specified, check it exists
  if (projectRoot && !existsSync(projectRoot)) {
    log.debug("Fallback not available: project root not found", {
      path: projectRoot,
    });
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// Execute a fallback tool
// ---------------------------------------------------------------------------

/**
 * Execute a tool via the Python binary injector.
 * @param {string} toolName - Tool name (without "unreal_" prefix)
 * @param {object} args - Tool arguments
 * @param {string} projectRoot - UE project root directory
 * @returns {Promise<{success: boolean, message: string, data?: any}>}
 */
export async function executeFallbackTool(toolName, args, projectRoot) {
  // Check if this tool is supported in fallback mode
  const toolDef = FALLBACK_TOOLS.find((t) => t.name === toolName);
  if (!toolDef) {
    return {
      success: false,
      message: `Tool "${toolName}" is not available in offline mode. Start Unreal Editor with the AgenticMCP plugin for full functionality. Available offline tools: ${FALLBACK_TOOLS.map((t) => t.name).join(", ")}`,
    };
  }

  // Map tool names to CLI commands with path validation
  let cliArgs;
  try {
    cliArgs = mapToolToCLIArgs(toolName, args, projectRoot);
  } catch (error) {
    log.error("Path validation failed", { tool: toolName, error: error.message });
    return {
      success: false,
      message: `Security validation failed: ${error.message}`,
    };
  }

  if (!cliArgs) {
    return {
      success: false,
      message: `Failed to map tool "${toolName}" to CLI command.`,
    };
  }

  try {
    const result = await execPromise("python3", [
      "-m",
      "core.cli",
      ...cliArgs,
    ], {
      cwd: join(__dirname, "..", ".."), // ue56-level-editor root
      timeout: 30000,
    });

    return {
      success: true,
      message: `[Offline] ${toolName} completed successfully.`,
      data: result.stdout,
    };
  } catch (error) {
    const errorMsg = error.stderr || error.message || "Unknown error";
    log.error("Fallback tool execution failed", {
      tool: toolName,
      error: errorMsg,
    });
    return {
      success: false,
      message: `[Offline] ${toolName} failed: ${errorMsg}`,
    };
  }
}

// ---------------------------------------------------------------------------
// Map MCP tool calls to CLI arguments
// ---------------------------------------------------------------------------

/**
 * Convert tool name and arguments to CLI command arguments.
 * @param {string} toolName - Tool name
 * @param {object} args - Tool arguments
 * @param {string} projectRoot - Project root for path validation
 * @returns {string[]|null} CLI arguments array, or null if mapping fails
 */
function mapToolToCLIArgs(toolName, args, projectRoot) {
  // Validate paths for all tools that accept file paths
  const validateAndResolvePath = (pathArg, requireExtension = true) => {
    const validated = validatePath(pathArg, projectRoot || "/");
    if (requireExtension) {
      validateExtension(validated, ALLOWED_ASSET_EXTENSIONS);
    }
    return validated;
  };

  switch (toolName) {
    case "offline_list_levels": {
      const contentPath = args.contentPath
        ? validatePath(args.contentPath, projectRoot || "/")
        : ".";
      return ["scan", contentPath];
    }

    case "offline_list_actors": {
      const umapPath = validateAndResolvePath(args.umapPath);
      return [
        "actors",
        umapPath,
        ...(args.classFilter ? ["--filter", args.classFilter] : []),
      ];
    }

    case "offline_get_actor": {
      const umapPath = validateAndResolvePath(args.umapPath);
      // Sanitize actor name - only allow alphanumeric, underscore, hyphen
      const actorName = args.actorName?.replace(/[^a-zA-Z0-9_\-]/g, "");
      if (!actorName) {
        throw new Error("Invalid actor name");
      }
      return ["props", umapPath, actorName];
    }

    case "offline_get_graph": {
      const umapPath = validateAndResolvePath(args.umapPath);
      return [
        "functions",
        umapPath,
        ...(args.graphName ? ["--graph", args.graphName] : []),
      ];
    }

    case "offline_level_info": {
      const umapPath = validateAndResolvePath(args.umapPath);
      return ["info", umapPath];
    }

    case "offline_generate_paste_text": {
      // Validate pattern is a known value
      const allowedPatterns = ["teleport_listener", "story_step_broadcast", "begin_play_chain", "multigate"];
      if (!allowedPatterns.includes(args.pattern)) {
        throw new Error(`Unknown pattern: ${args.pattern}. Allowed: ${allowedPatterns.join(", ")}`);
      }

      // Sanitize params - limit depth and size to prevent DoS
      let paramsStr = null;
      if (args.params) {
        const paramsJson = JSON.stringify(args.params);
        if (paramsJson.length > 10000) {
          throw new Error("Params too large (max 10KB)");
        }
        paramsStr = paramsJson;
      }

      return [
        "paste-text",
        args.pattern,
        ...(paramsStr ? ["--params", paramsStr] : []),
      ];
    }

    default:
      return null;
  }
}

// ---------------------------------------------------------------------------
// Promise wrapper for child_process.execFile
// ---------------------------------------------------------------------------

/**
 * Execute a command and return stdout/stderr as a promise.
 * @param {string} command - Command to execute
 * @param {string[]} args - Command arguments
 * @param {object} options - execFile options
 * @returns {Promise<{stdout: string, stderr: string}>}
 */
function execPromise(command, args = [], options = {}) {
  return new Promise((resolve, reject) => {
    execFile(
      command,
      args,
      { maxBuffer: 10 * 1024 * 1024, ...options },
      (error, stdout, stderr) => {
        if (error) {
          error.stdout = stdout;
          error.stderr = stderr;
          reject(error);
        } else {
          resolve({ stdout, stderr });
        }
      }
    );
  });
}
