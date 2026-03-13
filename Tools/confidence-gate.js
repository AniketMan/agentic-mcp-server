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

// Engine docs path mapping -- no scraped indexes, point directly to engine docs
// Claude reads the actual HTML from Engine\Documentation\Builds on the local machine

/**
 * Map a mutation tool + args to the relevant engine doc paths.
 * Points directly to Engine\Documentation\Builds -- no scraped indexes.
 * Claude reads the actual HTML from the engine install.
 *
 * @param {string} toolName - The mutation tool
 * @param {object} args - Tool arguments
 * @returns {string[]} Array of relative engine doc paths
 */
function getRelevantEngineDocs(toolName, args) {
  const docs = [];
  const docsRoot = ENGINE_DOCS_RELATIVE_PATH;

  // Blueprint node operations -> Blueprint API docs
  if (
    toolName.includes("Node") ||
    toolName.includes("Pin") ||
    toolName.includes("Graph") ||
    toolName.includes("Variable") ||
    toolName === "compileBlueprint" ||
    toolName === "createBlueprint"
  ) {
    docs.push(`${docsRoot}/BlueprintAPI/index.html`);
  }

  // Actor operations -> Actor API docs
  if (
    toolName.includes("Actor") ||
    toolName.includes("spawn") ||
    toolName.includes("destroy")
  ) {
    docs.push(`${docsRoot}/BlueprintAPI/Actor/index.html`);
    docs.push(`${docsRoot}/CppAPI/Runtime/Engine/AActor/index.html`);
  }

  // Level sequence operations -> Sequencer docs
  if (toolName.startsWith("ls_")) {
    docs.push(`${docsRoot}/BlueprintAPI/Sequencer/index.html`);
    docs.push(`${docsRoot}/CppAPI/Runtime/MovieScene/index.html`);
  }

  // VR-specific hints from args
  const eventName = (args?.eventName || "").toLowerCase();
  if (eventName.includes("gaze")) {
    docs.push(`${docsRoot}/BlueprintAPI/Input/index.html`);
  }
  if (eventName.includes("grip") || eventName.includes("trigger")) {
    docs.push(`${docsRoot}/BlueprintAPI/Input/MotionController/index.html`);
  }
  if (eventName.includes("haptic")) {
    docs.push(`${docsRoot}/BlueprintAPI/Input/Haptics/index.html`);
  }
  if (eventName.includes("audio") || eventName.includes("sound")) {
    docs.push(`${docsRoot}/BlueprintAPI/Audio/index.html`);
  }
  if (eventName.includes("anim")) {
    docs.push(`${docsRoot}/BlueprintAPI/Animation/index.html`);
  }
  if (eventName.includes("navigation") || eventName.includes("teleport")) {
    docs.push(`${docsRoot}/BlueprintAPI/AI/Navigation/index.html`);
  }

  // Python execution -> Python API docs
  if (toolName === "executePython") {
    docs.push(`${docsRoot}/CppAPI/Developer/PythonScriptPlugin/index.html`);
  }

  return docs;
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

  // --- Engine docs: point Claude to the actual engine documentation ---
  const engineDocs = getRelevantEngineDocs(toolName, args);
  if (engineDocs.length > 0) {
    suggestions.push(
      "Read the following UE5 engine docs before retrying:\n" +
      engineDocs.map((d) => `  - ${d}`).join("\n") +
      "\nThese are at Engine\\Documentation\\Builds relative to the engine root."
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
