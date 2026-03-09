/**
 * AgenticMCP VisualAgent - Unified CLI Tool
 *
 * Provides a single `unreal_cli` tool with rich command vocabulary
 * for visual automation of Unreal Engine 5.
 *
 * Screenshot Behavior:
 *   - Off by default (GPU-friendly)
 *   - Explicit: "screenshot" command captures viewport
 *   - Per-command: Any command with --screenshot flag
 *   - Auto mode: "auto-screenshot on" enables for visual actions
 *   - Auto mode visual actions: spawn, move, rotate, delete, camera, focus
 *
 * Commands:
 *   screenshot [--format=png|jpeg] [--width=N] [--height=N] [--quality=N]
 *   snapshot [--classFilter=X] [--noComponents]
 *   auto-screenshot <on|off>     Toggle auto-screenshot for visual actions
 *   navigate <levelName>
 *   focus <actorNameOrRef>
 *   select <actorNameOrRef> [--add]
 *   set <actorNameOrRef> <property> <value>
 *   spawn <className> <x> <y> <z> [--label=X]
 *   move <actorNameOrRef> <x> <y> <z>
 *   rotate <actorNameOrRef> <pitch> <yaw> <roll>
 *   delete <actorNameOrRef>
 *   query [classFilter] [--nameFilter=X]
 *   camera <x> <y> <z> <pitch> <yaw> <roll>
 *   wait <assets|compile|render>
 *   ref <ref>
 *   record start|stop|play|save|load|list [name]
 */

import { log } from "./lib.js";

// ============================================================
// Screenshot Mode State
// ============================================================

let autoScreenshotEnabled = false;
const VISUAL_ACTIONS = new Set(["spawn", "move", "rotate", "delete", "camera", "focus", "navigate"]);

// ============================================================
// Recording System
// ============================================================

const recording = {
  active: false,
  actions: [],
  startTime: null,
  savedSessions: new Map(),
};

function recordAction(command, result) {
  if (recording.active) {
    recording.actions.push({
      command,
      timestamp: Date.now() - recording.startTime,
      success: result.success !== false,
    });
  }
}

// ============================================================
// Command Parser
// ============================================================

function parseCommand(commandStr) {
  const tokens = [];
  let current = "";
  let inQuotes = false;
  let quoteChar = "";

  for (const char of commandStr) {
    if ((char === '"' || char === "'") && !inQuotes) {
      inQuotes = true;
      quoteChar = char;
    } else if (char === quoteChar && inQuotes) {
      inQuotes = false;
      quoteChar = "";
    } else if (char === " " && !inQuotes) {
      if (current) {
        tokens.push(current);
        current = "";
      }
    } else {
      current += char;
    }
  }
  if (current) tokens.push(current);

  if (tokens.length === 0) {
    return { action: null, args: {}, flags: {} };
  }

  const action = tokens[0].toLowerCase();
  const args = [];
  const flags = {};

  for (let i = 1; i < tokens.length; i++) {
    const token = tokens[i];
    if (token.startsWith("--")) {
      const eqIdx = token.indexOf("=");
      if (eqIdx > 0) {
        const key = token.substring(2, eqIdx);
        const value = token.substring(eqIdx + 1);
        flags[key] = value;
      } else {
        flags[token.substring(2)] = true;
      }
    } else {
      args.push(token);
    }
  }

  return { action, args, flags };
}

// ============================================================
// Command Executors
// ============================================================

async function executeScreenshot(httpClient, flags) {
  const params = {
    width: parseInt(flags.width) || 1280,
    height: parseInt(flags.height) || 720,
    format: flags.format || "jpeg",
    quality: parseInt(flags.quality) || 75,
  };
  return await httpClient("screenshot", params);
}

async function executeSnapshot(httpClient, flags) {
  const params = {
    classFilter: flags.classFilter || "",
    includeComponents: !flags.noComponents,
  };
  return await httpClient("sceneSnapshot", params);
}

async function executeFocus(httpClient, args) {
  if (args.length < 1) {
    return { success: false, message: "Usage: focus <actorNameOrRef>" };
  }
  return await httpClient("focusActor", { name: args[0] });
}

async function executeSelect(httpClient, args, flags) {
  if (args.length < 1) {
    return { success: false, message: "Usage: select <actorNameOrRef> [--add]" };
  }
  return await httpClient("selectActor", {
    name: args[0],
    addToSelection: !!flags.add,
  });
}

async function executeSet(httpClient, args) {
  if (args.length < 3) {
    return { success: false, message: "Usage: set <actorNameOrRef> <property> <value>" };
  }
  return await httpClient("setActorProperty", {
    name: args[0],
    property: args[1],
    value: args.slice(2).join(" "),
  });
}

async function executeSpawn(httpClient, args, flags) {
  if (args.length < 4) {
    return { success: false, message: "Usage: spawn <className> <x> <y> <z> [--label=X]" };
  }
  const params = {
    className: args[0],
    locationX: parseFloat(args[1]),
    locationY: parseFloat(args[2]),
    locationZ: parseFloat(args[3]),
  };
  if (flags.label) params.label = flags.label;
  return await httpClient("spawnActor", params);
}

async function executeMove(httpClient, args) {
  if (args.length < 4) {
    return { success: false, message: "Usage: move <actorNameOrRef> <x> <y> <z>" };
  }
  return await httpClient("setActorTransform", {
    name: args[0],
    locationX: parseFloat(args[1]),
    locationY: parseFloat(args[2]),
    locationZ: parseFloat(args[3]),
  });
}

async function executeRotate(httpClient, args) {
  if (args.length < 4) {
    return { success: false, message: "Usage: rotate <actorNameOrRef> <pitch> <yaw> <roll>" };
  }
  return await httpClient("setActorTransform", {
    name: args[0],
    rotationPitch: parseFloat(args[1]),
    rotationYaw: parseFloat(args[2]),
    rotationRoll: parseFloat(args[3]),
  });
}

async function executeDelete(httpClient, args) {
  if (args.length < 1) {
    return { success: false, message: "Usage: delete <actorNameOrRef>" };
  }
  return await httpClient("deleteActor", { name: args[0] });
}

async function executeQuery(httpClient, args, flags) {
  const params = {
    classFilter: args[0] || "",
    nameFilter: flags.nameFilter || "",
  };
  return await httpClient("listActors", params);
}

async function executeCamera(httpClient, args) {
  if (args.length < 6) {
    return { success: false, message: "Usage: camera <x> <y> <z> <pitch> <yaw> <roll>" };
  }
  return await httpClient("setViewport", {
    locationX: parseFloat(args[0]),
    locationY: parseFloat(args[1]),
    locationZ: parseFloat(args[2]),
    pitch: parseFloat(args[3]),
    yaw: parseFloat(args[4]),
    roll: parseFloat(args[5]),
  });
}

async function executeWait(httpClient, args) {
  if (args.length < 1) {
    return { success: false, message: "Usage: wait <assets|compile|render>" };
  }
  return await httpClient("waitReady", { condition: args[0] });
}

async function executeRef(httpClient, args) {
  if (args.length < 1) {
    return { success: false, message: "Usage: ref <refId>" };
  }
  return await httpClient("resolveRef", { ref: args[0] });
}

async function executeNavigate(httpClient, args) {
  if (args.length < 1) {
    return { success: false, message: "Usage: navigate <levelName>" };
  }
  return await httpClient("loadLevel", { name: args[0] });
}

// ============================================================
// Auto-Screenshot Toggle
// ============================================================

function executeAutoScreenshot(args) {
  const mode = args[0]?.toLowerCase();
  if (mode === "on" || mode === "true" || mode === "enable") {
    autoScreenshotEnabled = true;
    return {
      success: true,
      message: "Auto-screenshot ENABLED. Visual actions (spawn, move, rotate, delete, camera, focus) will include screenshots.",
    };
  } else if (mode === "off" || mode === "false" || mode === "disable") {
    autoScreenshotEnabled = false;
    return {
      success: true,
      message: "Auto-screenshot DISABLED. Use 'screenshot' command or --screenshot flag for explicit captures.",
    };
  } else {
    return {
      success: true,
      message: `Auto-screenshot is currently ${autoScreenshotEnabled ? "ON" : "OFF"}. Use 'auto-screenshot on' or 'auto-screenshot off' to toggle.`,
    };
  }
}

// ============================================================
// Recording Commands (local, no HTTP)
// ============================================================

function executeRecord(subcommand, args) {
  switch (subcommand) {
    case "start":
      recording.active = true;
      recording.actions = [];
      recording.startTime = Date.now();
      return {
        success: true,
        message: "Recording started. Execute commands, then use 'record stop' to finish.",
      };

    case "stop":
      recording.active = false;
      const session = {
        actions: [...recording.actions],
        totalDuration: Date.now() - recording.startTime,
        recordedAt: new Date().toISOString(),
      };
      return {
        success: true,
        message: `Recording stopped. Captured ${session.actions.length} action(s).`,
        session,
      };

    case "save":
      if (args.length < 1) {
        return { success: false, message: "Usage: record save <name>" };
      }
      recording.savedSessions.set(args[0], {
        actions: [...recording.actions],
        totalDuration: Date.now() - (recording.startTime || Date.now()),
        savedAt: new Date().toISOString(),
      });
      return {
        success: true,
        message: `Session saved as '${args[0]}' with ${recording.actions.length} action(s).`,
      };

    case "load":
      if (args.length < 1) {
        return { success: false, message: "Usage: record load <name>" };
      }
      const loaded = recording.savedSessions.get(args[0]);
      if (!loaded) {
        return { success: false, message: `Session '${args[0]}' not found.` };
      }
      recording.actions = [...loaded.actions];
      return {
        success: true,
        message: `Loaded session '${args[0]}' with ${loaded.actions.length} action(s). Use 'record play' to replay.`,
      };

    case "list":
      const names = [...recording.savedSessions.keys()];
      return {
        success: true,
        message: names.length > 0 ? `Saved sessions: ${names.join(", ")}` : "No saved sessions.",
        sessions: names,
      };

    case "play":
      return {
        success: true,
        message: "Replay queued.",
        replay: recording.actions.map((a) => a.command),
        _isReplayRequest: true,
      };

    default:
      return {
        success: false,
        message: "Usage: record <start|stop|save|load|list|play> [name]",
      };
  }
}

// ============================================================
// Main Command Router
// ============================================================

async function executeVisualAgentCommand(commandStr, httpClient, options = {}) {
  const { action, args, flags } = parseCommand(commandStr);

  if (!action) {
    return {
      success: false,
      message: "Empty command. Try: screenshot, snapshot, focus, select, spawn, move, query, etc.",
    };
  }

  let result;
  const includeSnapshot = options.includeSnapshot !== false && action !== "snapshot" && action !== "record" && action !== "auto-screenshot";

  // Determine if screenshot should be taken
  const explicitScreenshot = flags.screenshot === true;
  const isVisualAction = VISUAL_ACTIONS.has(action);
  const shouldAutoScreenshot = autoScreenshotEnabled && isVisualAction;
  const captureScreenshot = explicitScreenshot || shouldAutoScreenshot;

  try {
    switch (action) {
      case "screenshot":
        result = await executeScreenshot(httpClient, flags);
        break;
      case "snapshot":
        result = await executeSnapshot(httpClient, flags);
        break;
      case "auto-screenshot":
        result = executeAutoScreenshot(args);
        break;
      case "focus":
        result = await executeFocus(httpClient, args);
        break;
      case "select":
        result = await executeSelect(httpClient, args, flags);
        break;
      case "set":
        result = await executeSet(httpClient, args);
        break;
      case "spawn":
        result = await executeSpawn(httpClient, args, flags);
        break;
      case "move":
        result = await executeMove(httpClient, args);
        break;
      case "rotate":
        result = await executeRotate(httpClient, args);
        break;
      case "delete":
        result = await executeDelete(httpClient, args);
        break;
      case "query":
        result = await executeQuery(httpClient, args, flags);
        break;
      case "camera":
        result = await executeCamera(httpClient, args);
        break;
      case "wait":
        result = await executeWait(httpClient, args);
        break;
      case "ref":
        result = await executeRef(httpClient, args);
        break;
      case "navigate":
        result = await executeNavigate(httpClient, args);
        break;
      case "record":
        const subCmd = args[0] || "";
        result = executeRecord(subCmd, args.slice(1));
        break;
      case "help":
        result = {
          success: true,
          message: getHelpText(),
        };
        break;
      default:
        result = {
          success: false,
          message: `Unknown command: ${action}. Use 'help' to see available commands.`,
        };
    }

    // Record the action if recording is active
    recordAction(commandStr, result);

    // Capture screenshot if requested (explicit flag or auto-screenshot mode)
    if (captureScreenshot && result.success !== false && action !== "screenshot") {
      try {
        const screenshot = await executeScreenshot(httpClient, { format: "jpeg", quality: "70", width: "1280", height: "720" });
        if (screenshot.data) {
          result._screenshot = screenshot.data;
          result._screenshotMimeType = screenshot.mimeType || "image/jpeg";
          result._screenshotWidth = screenshot.width;
          result._screenshotHeight = screenshot.height;
        }
      } catch (e) {
        log.debug("Failed to capture auto-screenshot", { error: e.message });
      }
    }

    // Optionally append scene snapshot for context (except for snapshot/screenshot/record commands)
    if (includeSnapshot && result.success !== false && !["screenshot", "record", "help", "ref"].includes(action)) {
      try {
        const snapshot = await executeSnapshot(httpClient, {});
        if (snapshot.yamlSnapshot) {
          result._sceneSnapshot = snapshot.yamlSnapshot;
          result._actorCount = snapshot.actorCount;
        }
      } catch (e) {
        log.debug("Failed to append scene snapshot", { error: e.message });
      }
    }
  } catch (error) {
    result = {
      success: false,
      message: `Command failed: ${error.message}`,
    };
  }

  return result;
}

// ============================================================
// Help Text
// ============================================================

function getHelpText() {
  return `
# unreal_cli - VisualAgent for Unreal Engine Automation

## Visual Commands
  screenshot [--format=png|jpeg] [--width=N] [--height=N] [--quality=N]
    Capture viewport using UE's built-in screenshot system

  snapshot [--classFilter=X] [--noComponents]
    Get scene hierarchy with short refs (a0, a1.c0)

  auto-screenshot <on|off>
    Toggle auto-screenshot for visual actions (spawn, move, delete, camera, etc.)

## Actor Commands
  focus <ref|name>           Move camera to focus on actor
  select <ref|name> [--add]  Select actor in editor
  spawn <class> <x> <y> <z> [--label=X]  Spawn new actor
  move <ref|name> <x> <y> <z>    Set actor location
  rotate <ref|name> <pitch> <yaw> <roll>  Set actor rotation
  delete <ref|name>          Delete actor
  set <ref|name> <prop> <value>  Set actor property
  query [classFilter] [--nameFilter=X]   Query actors

## Viewport Commands
  camera <x> <y> <z> <pitch> <yaw> <roll>  Set viewport camera
  navigate <levelName>       Load level

## Utility Commands
  wait <assets|compile|render>  Wait for condition
  ref <refId>                Resolve ref to actor name

## Recording Commands
  record start    Begin recording actions
  record stop     Stop recording, return session
  record save <name>  Save current recording
  record load <name>  Load saved recording
  record list     List saved recordings
  record play     Replay loaded recording

## Examples
  snapshot
  focus a0
  spawn StaticMeshActor 100 200 0 --label="MyActor"
  move a3 500 0 100
  screenshot --format=png --width=1920
  auto-screenshot on
  record start
  record stop
`.trim();
}

// ============================================================
// Tool Definition for MCP Registration
// ============================================================

export const VISUALAGENT_TOOL_DEFINITION = {
  name: "cli",
  description: `VisualAgent - Unified CLI for Unreal Engine visual automation. Single tool with rich command vocabulary.

COMMANDS:
• screenshot - Capture viewport using UE's built-in screenshot system
• snapshot - Get scene hierarchy with refs (a0, a1.c0) like accessibility tree
• auto-screenshot <on|off> - Toggle auto-capture for visual actions
• focus <ref> - Move camera to actor
• select <ref> - Select actor
• spawn <class> <x> <y> <z> - Spawn actor
• move <ref> <x> <y> <z> - Move actor
• rotate <ref> <pitch> <yaw> <roll> - Rotate actor
• delete <ref> - Delete actor
• set <ref> <property> <value> - Set property
• query [class] - List actors
• camera <x> <y> <z> <pitch> <yaw> <roll> - Set viewport
• navigate <level> - Load level
• wait <assets|compile|render> - Wait for condition
• record start|stop|play|save|load|list - Recording

Use 'help' for full documentation. Refs (a0, a1.c2) from snapshot can be used instead of actor names.`,
  inputSchema: {
    type: "object",
    properties: {
      command: {
        type: "string",
        description:
          "Command to execute. Examples: 'snapshot', 'focus a0', 'spawn PointLight 100 200 300', 'screenshot --format=png'",
      },
    },
    required: ["command"],
  },
  annotations: {
    readOnlyHint: false,
    destructiveHint: true,
    idempotentHint: false,
    openWorldHint: false,
  },
};

export { executeVisualAgentCommand, parseCommand, getHelpText };
