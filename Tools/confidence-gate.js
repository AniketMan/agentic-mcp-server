/**
 * Confidence Gate for AgenticMCP
 * ================================
 *
 * Enforces the quantized inference propagation path at the execution level.
 *
 * Architecture:
 *   - READ freely: No restrictions on any read-only tool
 *   - PLAN freely: Claude can iterate, adapt, try approaches until confidence is high
 *   - EXECUTE gated: Mutations are rejected if inference confidence is below threshold
 *
 * The gate sits between the CallTool handler and the actual UE5 execution.
 * It intercepts mutation calls, evaluates confidence based on:
 *   1. Whether the operation was planned through the quantized inference path
 *   2. Whether referenced assets exist in the cached manifest
 *   3. Whether the script (source truth) supports the operation
 *
 * Claude is free to explore and adapt. The gate only blocks execution of
 * low-confidence mutations -- it never blocks reads or planning.
 *
 * Author: JARVIS
 * Date: March 2026
 */

import { log } from "./lib.js";
import { loadCachedManifest, loadSceneCoordination } from "./quantized-inference.js";
import { readFileSync, existsSync, writeFileSync, mkdirSync, appendFileSync } from "fs";
import { join, dirname } from "path";
import { fileURLToPath } from "url";

const __dirname = dirname(fileURLToPath(import.meta.url));

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

const CONFIDENCE_THRESHOLD = 0.7; // Minimum confidence to allow mutation execution
const GATE_LOG_DIR = join(__dirname, "..", ".quantized_cache", "gate_log");

// Engine documentation path (relative to engine root)
// Claude resolves this against the engine install on the local machine
const ENGINE_DOCS_RELATIVE_PATH = "Engine/Documentation/Builds";

// VR-relevant API index for doc lookup
const VR_API_INDEX_PATH = join(__dirname, "..", "reference", "ue5_api", "vr_relevant_api_index.json");
let _vrApiIndex = null;

/**
 * Load the VR-relevant API index (cached after first load).
 */
function getVrApiIndex() {
  if (_vrApiIndex) return _vrApiIndex;
  try {
    if (existsSync(VR_API_INDEX_PATH)) {
      _vrApiIndex = JSON.parse(readFileSync(VR_API_INDEX_PATH, "utf-8"));
    }
  } catch (error) {
    log.error("Failed to load VR API index", { error: error.message });
  }
  return _vrApiIndex;
}

/**
 * Find relevant API doc categories for a given tool call.
 * Maps the mutation tool + args to Blueprint API categories that Claude
 * should read from Engine/Documentation/Builds before retrying.
 *
 * @param {string} toolName - The mutation tool
 * @param {object} args - Tool arguments
 * @returns {object[]} Array of { category, functions, docHint }
 */
function findRelevantApiDocs(toolName, args) {
  const index = getVrApiIndex();
  if (!index || !index.categories) return [];

  const results = [];

  // Build search terms from tool name and args
  const searchTerms = new Set();

  // From tool name
  if (toolName.includes("Node") || toolName.includes("Pin")) searchTerms.add("blueprint");
  if (toolName.includes("Actor") || toolName.includes("spawn")) searchTerms.add("actor");
  if (toolName.includes("ls_")) searchTerms.add("sequence");
  if (toolName.includes("Level") || toolName.includes("level")) searchTerms.add("level");

  // From args -- extract class names, node types, etc.
  const nodeClass = args?.nodeClass || args?.className || "";
  const nodeType = args?.nodeType || "";
  const actorClass = args?.actorClass || args?.class || "";
  const allArgValues = [nodeClass, nodeType, actorClass].filter(Boolean);

  for (const val of allArgValues) {
    // Extract meaningful keywords from class names like K2Node_CallFunction
    const parts = val.replace(/^K2Node_/, "").replace(/^U/, "").replace(/^A/, "").split(/(?=[A-Z])/);
    for (const part of parts) {
      if (part.length > 2) searchTerms.add(part.toLowerCase());
    }
  }

  // VR-specific terms based on the OC VR project
  if (args?.eventName) {
    const ev = args.eventName.toLowerCase();
    if (ev.includes("gaze")) searchTerms.add("gaze");
    if (ev.includes("grip")) searchTerms.add("grip");
    if (ev.includes("trigger")) searchTerms.add("trigger");
    if (ev.includes("navigation") || ev.includes("teleport")) searchTerms.add("navigation");
    if (ev.includes("haptic")) searchTerms.add("haptic");
    if (ev.includes("animation") || ev.includes("anim")) searchTerms.add("animation");
    if (ev.includes("audio") || ev.includes("sound")) searchTerms.add("audio");
    if (ev.includes("sequence")) searchTerms.add("sequence");
  }

  // Search the index
  for (const cat of index.categories) {
    const catLower = cat.category.toLowerCase();
    for (const term of searchTerms) {
      if (catLower.includes(term)) {
        results.push({
          category: cat.category,
          functions: cat.functions.slice(0, 10), // Top 10 most relevant
          totalFunctions: cat.total,
          docPath: `${ENGINE_DOCS_RELATIVE_PATH}/BlueprintAPI/${cat.category}/index.html`,
        });
        break; // Don't double-add same category
      }
    }
  }

  return results.slice(0, 5); // Cap at 5 most relevant categories
}

// Mutation tools -- these are the tools that modify the UE5 project
// Read-only tools pass through without any gate check
const MUTATION_TOOLS = new Set([
  "addNode",
  "deleteNode",
  "connectPins",
  "disconnectPin",
  "setPinDefault",
  "moveNode",
  "refreshAllNodes",
  "createBlueprint",
  "createGraph",
  "deleteGraph",
  "addVariable",
  "removeVariable",
  "compileBlueprint",
  "setNodeComment",
  "spawnActor",
  "deleteActor",
  "setActorProperty",
  "setActorTransform",
  "loadLevel",
  "executePython",
  // Level Sequence mutations
  "ls_create",
  "ls_bind_actor",
  "ls_add_track",
  "ls_add_section",
  "ls_add_keyframe",
  "ls_add_camera",
  "ls_add_audio",
]);

// Read-only tools -- always allowed, no gate check
const READ_ONLY_TOOLS = new Set([
  "list",
  "blueprint",
  "graph",
  "search",
  "references",
  "listClasses",
  "listFunctions",
  "listProperties",
  "pinInfo",
  "rescanAssets",
  "listActors",
  "getActor",
  "listLevels",
  "getLevelBlueprint",
  "validateBlueprint",
  "snapshotGraph",
  "restoreGraph",
  "getProjectState",
  "updateProjectState",
  "getRecentActions",
  "getOutputLog",
  "getSceneSpatialMap",
  // Level Sequence reads
  "ls_open",
  "ls_list_bindings",
  // Task queue reads
  "task_status",
  "task_list",
]);

// ---------------------------------------------------------------------------
// Gate State
// ---------------------------------------------------------------------------

// Tracks which operations have been planned through the quantized path
// Key: "scene_{id}_step_{step}" or tool-specific identifier
// Value: { confidence, plannedAt, source }
const plannedOperations = new Map();

// ---------------------------------------------------------------------------
// Ensure gate log directory exists
// ---------------------------------------------------------------------------

function ensureGateLogDir() {
  if (!existsSync(GATE_LOG_DIR)) {
    mkdirSync(GATE_LOG_DIR, { recursive: true });
  }
}

// ---------------------------------------------------------------------------
// Core Gate Logic
// ---------------------------------------------------------------------------

/**
 * Evaluate whether a tool call should be allowed to execute.
 *
 * Returns:
 *   { allowed: true } -- proceed with execution
 *   { allowed: false, reason: string, confidence: number, suggestion: string }
 *     -- blocked, with explanation and guidance for Claude to adapt
 *
 * @param {string} toolName - The tool being called (without "unreal_" prefix)
 * @param {object} args - The tool arguments
 * @returns {object} Gate decision
 */
export function evaluateGate(toolName, args) {
  // Read-only tools always pass
  if (READ_ONLY_TOOLS.has(toolName)) {
    return { allowed: true, reason: "read-only", confidence: 1.0 };
  }

  // Non-mutation tools that aren't in either list -- allow with warning
  if (!MUTATION_TOOLS.has(toolName)) {
    log.debug("Gate: unknown tool classification, allowing", { tool: toolName });
    return { allowed: true, reason: "unclassified", confidence: 0.8 };
  }

  // --- This is a mutation tool. Evaluate confidence. ---

  const confidence = calculateMutationConfidence(toolName, args);

  // Log the gate decision
  logGateDecision(toolName, args, confidence);

  if (confidence >= CONFIDENCE_THRESHOLD) {
    return {
      allowed: true,
      reason: "confidence-met",
      confidence,
    };
  }

  // --- Blocked. Build actionable feedback for Claude. ---

  const suggestion = buildAdaptationSuggestion(toolName, args, confidence);

  return {
    allowed: false,
    reason: "low-confidence",
    confidence,
    threshold: CONFIDENCE_THRESHOLD,
    suggestion,
  };
}

/**
 * Calculate confidence for a mutation tool call.
 *
 * Confidence is computed from multiple signals:
 *   1. Was this operation planned through the quantized inference path? (+0.4)
 *   2. Does the referenced Blueprint/actor exist in the cached manifest? (+0.3)
 *   3. Is there a snapshot safety net in place? (+0.2)
 *   4. Does the operation align with script source truth? (+0.1)
 *
 * @param {string} toolName - The mutation tool
 * @param {object} args - Tool arguments
 * @returns {number} Confidence score 0.0 - 1.0
 */
function calculateMutationConfidence(toolName, args) {
  let confidence = 0.0;
  const signals = [];

  // Signal 1: Was this planned through the quantized path?
  const planKey = findPlanKey(toolName, args);
  if (planKey && plannedOperations.has(planKey)) {
    const plan = plannedOperations.get(planKey);
    confidence += 0.4;
    signals.push(`planned (${plan.source})`);
  }

  // Signal 2: Does the referenced asset exist in the manifest?
  const manifest = loadCachedManifest(Infinity); // Don't care about staleness for gate check
  if (manifest) {
    const assetRef = extractAssetReference(toolName, args);
    if (assetRef) {
      const exists = checkAssetExists(assetRef, manifest);
      if (exists) {
        confidence += 0.3;
        signals.push(`asset-verified (${assetRef})`);
      } else {
        signals.push(`asset-missing (${assetRef})`);
      }
    } else {
      // No specific asset reference -- give partial credit
      confidence += 0.15;
      signals.push("no-asset-ref");
    }
  } else {
    signals.push("no-manifest");
  }

  // Signal 3: Is there a snapshot safety net?
  // We check if a snapshot was recently taken (within the last 5 minutes)
  // by looking at the gate log for a recent snapshotGraph call
  if (hasRecentSnapshot()) {
    confidence += 0.2;
    signals.push("snapshot-active");
  } else {
    signals.push("no-snapshot");
  }

  // Signal 4: Script alignment
  // Check if the operation references something from the scene coordination
  const sceneAlignment = checkSceneAlignment(toolName, args);
  if (sceneAlignment) {
    confidence += 0.1;
    signals.push(`script-aligned (scene ${sceneAlignment})`);
  }

  // Cap at 1.0
  confidence = Math.min(confidence, 1.0);

  log.debug("Gate confidence calculated", {
    tool: toolName,
    confidence: confidence.toFixed(2),
    signals,
  });

  return confidence;
}

// ---------------------------------------------------------------------------
// Signal Helpers
// ---------------------------------------------------------------------------

/**
 * Find the plan key for a mutation, if it was pre-planned.
 */
function findPlanKey(toolName, args) {
  // Check if args contain scene/step references
  const blueprintName = args?.blueprintName || args?.name || args?.blueprint || "";
  const eventName = args?.eventName || "";

  // Search planned operations for a match
  for (const [key, plan] of plannedOperations) {
    if (plan.blueprintName && blueprintName.includes(plan.blueprintName)) {
      return key;
    }
    if (plan.eventName && eventName === plan.eventName) {
      return key;
    }
  }

  return null;
}

/**
 * Extract the asset reference from tool arguments.
 */
function extractAssetReference(toolName, args) {
  // Blueprint operations
  if (args?.blueprintName) return args.blueprintName;
  if (args?.name && toolName !== "spawnActor") return args.name;
  if (args?.blueprint) return args.blueprint;

  // Actor operations
  if (args?.actorName) return args.actorName;
  if (args?.actorLabel) return args.actorLabel;

  // Level operations
  if (args?.levelName) return args.levelName;

  return null;
}

/**
 * Check if an asset exists in the manifest.
 */
function checkAssetExists(ref, manifest) {
  if (!ref) return false;
  const search = ref.toLowerCase();

  // Check blueprints
  if (manifest.blueprints?.some((bp) =>
    (bp.name || "").toLowerCase().includes(search) ||
    (bp.path || "").toLowerCase().includes(search)
  )) {
    return true;
  }

  // Check actors
  if (manifest.actors && Object.keys(manifest.actors).some((name) =>
    name.toLowerCase().includes(search)
  )) {
    return true;
  }

  // Check levels
  if (manifest.levels?.some((lvl) =>
    (lvl.name || "").toLowerCase().includes(search) ||
    (lvl.path || "").toLowerCase().includes(search)
  )) {
    return true;
  }

  return false;
}

/**
 * Check if a snapshot was taken recently (within 5 minutes).
 */
function hasRecentSnapshot() {
  try {
    if (!existsSync(GATE_LOG_DIR)) return false;
    const logPath = join(GATE_LOG_DIR, "recent_snapshots.json");
    if (!existsSync(logPath)) return false;

    const data = JSON.parse(readFileSync(logPath, "utf-8"));
    const fiveMinAgo = Date.now() - 300000;
    return data.lastSnapshot && data.lastSnapshot > fiveMinAgo;
  } catch {
    return false;
  }
}

/**
 * Record that a snapshot was taken.
 */
export function recordSnapshot() {
  ensureGateLogDir();
  const logPath = join(GATE_LOG_DIR, "recent_snapshots.json");
  writeFileSync(logPath, JSON.stringify({ lastSnapshot: Date.now() }));
}

/**
 * Check if the operation aligns with a scene from the script.
 */
function checkSceneAlignment(toolName, args) {
  const scenes = loadSceneCoordination();
  const blueprintName = args?.blueprintName || args?.name || args?.blueprint || "";
  const eventName = args?.eventName || "";

  for (const scene of scenes) {
    // Check if the blueprint path matches a scene's level
    if (scene.instructions && blueprintName) {
      const levelMatch = scene.instructions.match(/Level Path:\*\*\s*`([^`]+)`/);
      if (levelMatch && blueprintName.includes(levelMatch[1])) {
        return scene.id;
      }
    }

    // Check if the event name matches a scene interaction
    if (eventName && scene.interactions) {
      const match = scene.interactions.find((i) => i.eventName === eventName);
      if (match) return scene.id;
    }
  }

  return null;
}

// ---------------------------------------------------------------------------
// Plan Registration
// ---------------------------------------------------------------------------

/**
 * Register a planned operation from the quantized inference path.
 * Called by the quantized-inference module when execute_scene_step is invoked.
 *
 * @param {string} key - Unique key (e.g., "scene_1_step_2")
 * @param {object} planData - Plan metadata
 */
export function registerPlannedOperation(key, planData) {
  plannedOperations.set(key, {
    ...planData,
    plannedAt: Date.now(),
    source: "quantized-inference",
  });
  log.debug("Registered planned operation", { key });
}

/**
 * Clear a planned operation after execution.
 */
export function clearPlannedOperation(key) {
  plannedOperations.delete(key);
}

/**
 * Register all operations from a wiring plan.
 */
export function registerWiringPlan(sceneId, operations) {
  for (const op of operations) {
    if (op.status === "pending") {
      const key = `scene_${sceneId}_step_${op.step}`;
      registerPlannedOperation(key, {
        sceneId,
        step: op.step,
        eventName: op.eventName,
        blueprintName: op.calls?.[0]?.params?.blueprintName || null,
      });
    }
  }
}

// ---------------------------------------------------------------------------
// Adaptation Suggestions
// ---------------------------------------------------------------------------

/**
 * Build an actionable suggestion for Claude when a mutation is blocked.
 * This is the key to the adaptive loop -- Claude reads the suggestion,
 * adjusts its approach, and tries again with higher confidence.
 */
function buildAdaptationSuggestion(toolName, args, confidence) {
  const suggestions = [];

  if (confidence < 0.2) {
    suggestions.push(
      "This operation has very low confidence. Start by calling unreal_quantize_project " +
      "to refresh the asset manifest, then use unreal_get_scene_plan to get a " +
      "pre-computed plan for this scene."
    );
  }

  if (!hasRecentSnapshot()) {
    suggestions.push(
      "No recent snapshot detected. Call unreal_snapshotGraph on the target " +
      "blueprint before attempting mutations. This adds +0.2 confidence."
    );
  }

  const manifest = loadCachedManifest(Infinity);
  if (!manifest) {
    suggestions.push(
      "No asset manifest found. Call unreal_quantize_project first to cache " +
      "the project state. This enables asset verification (+0.3 confidence)."
    );
  } else {
    const ref = extractAssetReference(toolName, args);
    if (ref && !checkAssetExists(ref, manifest)) {
      suggestions.push(
        `Asset '${ref}' not found in manifest. Verify it exists by calling ` +
        `unreal_listActors or unreal_list, then refresh the manifest with ` +
        `unreal_quantize_project(forceRefresh: true).`
      );
    }
  }

  if (!findPlanKey(toolName, args)) {
    suggestions.push(
      "This operation was not planned through the quantized inference path. " +
      "Use unreal_get_scene_plan and unreal_execute_scene_step to generate " +
      "a pre-computed plan, which adds +0.4 confidence."
    );
  }

  // --- Engine docs lookup: point Claude to relevant API documentation ---
  const relevantDocs = findRelevantApiDocs(toolName, args);
  if (relevantDocs.length > 0) {
    const docLines = relevantDocs.map(
      (d) => `  - ${d.category} (${d.totalFunctions} functions): ${d.docPath}`
    );
    suggestions.push(
      "Read the following UE5 API docs from the engine before retrying. " +
      "These are the most relevant categories for this operation:\n" +
      docLines.join("\n") +
      "\nKey functions to review: " +
      relevantDocs.flatMap((d) => d.functions.slice(0, 3)).join(", ") + "."
    );
  }

  return suggestions.length > 0
    ? suggestions.join(" ")
    : "Increase confidence by verifying assets exist, taking a snapshot, " +
      "and planning through the quantized inference path.";
}

// ---------------------------------------------------------------------------
// Gate Logging
// ---------------------------------------------------------------------------

/**
 * Log every gate decision for audit trail.
 */
function logGateDecision(toolName, args, confidence) {
  ensureGateLogDir();

  const entry = {
    timestamp: new Date().toISOString(),
    tool: toolName,
    confidence: parseFloat(confidence.toFixed(3)),
    allowed: confidence >= CONFIDENCE_THRESHOLD,
    threshold: CONFIDENCE_THRESHOLD,
    args_summary: summarizeArgs(args),
  };

  const logPath = join(GATE_LOG_DIR, "decisions.jsonl");
  try {
    const line = JSON.stringify(entry) + "\n";
    appendFileSync(logPath, line);
  } catch (error) {
    log.error("Failed to write gate log", { error: error.message });
  }
}

/**
 * Summarize args for logging (avoid dumping huge payloads).
 */
function summarizeArgs(args) {
  if (!args) return {};
  const summary = {};
  for (const [key, value] of Object.entries(args)) {
    if (typeof value === "string" && value.length > 100) {
      summary[key] = value.substring(0, 100) + "...";
    } else {
      summary[key] = value;
    }
  }
  return summary;
}

// ---------------------------------------------------------------------------
// Exports
// ---------------------------------------------------------------------------

export {
  CONFIDENCE_THRESHOLD,
  MUTATION_TOOLS,
  READ_ONLY_TOOLS,
  calculateMutationConfidence,
  checkAssetExists,
  hasRecentSnapshot,
  checkSceneAlignment,
};
