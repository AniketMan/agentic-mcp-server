/**
 * Quantized Inference Layer for AgenticMCP
 * ==========================================
 *
 * Implements the Quantized Spatial Data Architecture principles as an MCP
 * tool layer. Instead of re-scanning the UE5 project on every Claude request,
 * this module:
 *
 * 1. Generates an Adaptive Asset Manifest (quantized project representation)
 * 2. Caches it to disk as a sidecar JSON file
 * 3. Provides deterministic scene-wiring plans by cross-referencing the
 *    manifest against the OC VR script's interaction definitions
 * 4. Reports confidence per operation so Claude knows what needs human review
 *
 * No generative AI is used. The data IS the model.
 * The manifest IS the weights. Claude is the logic router.
 *
 * Author: JARVIS
 * Date: March 2026
 */

import { log } from "./lib.js";
import { readFileSync, writeFileSync, existsSync, mkdirSync } from "fs";
import { join, dirname } from "path";
import { fileURLToPath } from "url";

const __dirname = dirname(fileURLToPath(import.meta.url));

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

const CACHE_DIR = join(__dirname, "..", ".quantized_cache");
const MANIFEST_PATH = join(CACHE_DIR, "asset_manifest.json");
const WIRING_PLAN_PATH = join(CACHE_DIR, "wiring_plans.json");
const MANIFEST_VERSION = "1.0.0";

// Precision tiers for manifest detail level
// Hero assets get full structural breakdown, background assets get one line
const TIER_HERO = "hero";
const TIER_STANDARD = "standard";
const TIER_BACKGROUND = "background";

// ---------------------------------------------------------------------------
// Ensure cache directory exists
// ---------------------------------------------------------------------------

function ensureCacheDir() {
  if (!existsSync(CACHE_DIR)) {
    mkdirSync(CACHE_DIR, { recursive: true });
    log.info("Created quantized cache directory", { path: CACHE_DIR });
  }
}

// ---------------------------------------------------------------------------
// Asset Manifest Generation
// ---------------------------------------------------------------------------

/**
 * Generate the Adaptive Asset Manifest by querying the live UE5 editor.
 * This is the "quantization" step -- we compress the entire project into
 * a lightweight structured representation that fits in Claude's context.
 *
 * @param {function} callUnreal - Function to call the UE5 HTTP API
 * @param {object} config - Server configuration
 * @returns {object} The generated manifest
 */
export async function generateAssetManifest(callUnreal, config) {
  ensureCacheDir();
  log.info("Generating Adaptive Asset Manifest (quantizing project)...");

  const manifest = {
    version: MANIFEST_VERSION,
    generated_at: new Date().toISOString(),
    project: null,
    engine: null,
    levels: [],
    blueprints: [],
    actors: {},
    sequences: [],
    audio: [],
    scene_coordination: loadSceneCoordination(),
    confidence: 1.0,
  };

  try {
    // Step 1: Get project info
    const health = await callUnreal("/api/health", "GET");
    if (health && health.projectName) {
      manifest.project = health.projectName;
      manifest.engine = health.engineVersion || "5.6+";
    }

    // Step 2: List all blueprints (quantized -- names and paths only)
    const bpList = await callUnreal("/api/list", "GET");
    if (bpList && bpList.blueprints) {
      manifest.blueprints = bpList.blueprints.map((bp) => ({
        name: bp.name || bp,
        path: bp.path || bp,
        tier: assignBlueprintTier(bp.name || bp),
      }));
    }

    // Step 3: List levels
    const levels = await callUnreal("/api/list-levels", "POST", {});
    if (levels && levels.levels) {
      manifest.levels = levels.levels.map((lvl) => ({
        name: lvl.name || lvl,
        path: lvl.path || lvl,
        loaded: lvl.loaded || false,
      }));
    }

    // Step 4: List actors in loaded levels (quantized -- name, class, transform)
    const actors = await callUnreal("/api/list-actors", "POST", {});
    if (actors && actors.actors) {
      manifest.actors = quantizeActorList(actors.actors);
    }

    // Step 5: List level sequences
    const sequences = await callUnreal("/api/list-sequences", "POST", {});
    if (sequences && sequences.sequences) {
      manifest.sequences = sequences.sequences.map((seq) => ({
        name: seq.actorLabel || seq.actorName,
        path: seq.sequencePath,
        duration: seq.duration,
        frameRate: seq.frameRate,
        trackCount: seq.trackCount,
      }));
    }

    manifest.confidence = 1.0;
    log.info("Asset manifest generated", {
      blueprints: manifest.blueprints.length,
      levels: manifest.levels.length,
      actorCount: Object.keys(manifest.actors).length,
      sequences: manifest.sequences.length,
    });
  } catch (error) {
    log.error("Manifest generation failed (editor may be offline)", {
      error: error.message,
    });
    manifest.confidence = 0.0;
    manifest.error = error.message;
  }

  // Write to cache
  writeFileSync(MANIFEST_PATH, JSON.stringify(manifest, null, 2));
  log.info("Manifest cached", { path: MANIFEST_PATH });

  return manifest;
}

/**
 * Load cached manifest from disk. Returns null if no cache exists
 * or if the cache is stale (older than the configured TTL).
 *
 * @param {number} maxAgeMs - Maximum cache age in milliseconds (default: 1 hour)
 * @returns {object|null} Cached manifest or null
 */
export function loadCachedManifest(maxAgeMs = 3600000) {
  if (!existsSync(MANIFEST_PATH)) {
    log.debug("No cached manifest found");
    return null;
  }

  try {
    const raw = readFileSync(MANIFEST_PATH, "utf-8");
    const manifest = JSON.parse(raw);

    // Check staleness
    const generatedAt = new Date(manifest.generated_at).getTime();
    const age = Date.now() - generatedAt;

    if (age > maxAgeMs) {
      log.info("Cached manifest is stale", {
        ageMinutes: Math.round(age / 60000),
        maxMinutes: Math.round(maxAgeMs / 60000),
      });
      return null;
    }

    log.info("Loaded cached manifest", {
      ageMinutes: Math.round(age / 60000),
      blueprints: manifest.blueprints?.length || 0,
      sequences: manifest.sequences?.length || 0,
    });
    return manifest;
  } catch (error) {
    log.error("Failed to load cached manifest", { error: error.message });
    return null;
  }
}

/**
 * Get manifest -- returns cached version if fresh, otherwise regenerates.
 *
 * @param {function} callUnreal - Function to call the UE5 HTTP API
 * @param {object} config - Server configuration
 * @param {boolean} forceRefresh - Force regeneration even if cache is fresh
 * @returns {object} The asset manifest
 */
export async function getManifest(callUnreal, config, forceRefresh = false) {
  if (!forceRefresh) {
    const cached = loadCachedManifest();
    if (cached) return cached;
  }
  return await generateAssetManifest(callUnreal, config);
}

// ---------------------------------------------------------------------------
// Scene Coordination Loader
// ---------------------------------------------------------------------------

/**
 * Load all scene coordination data from the scene_coordination directory.
 * This reads STATUS.json and INSTRUCTIONS.md for each scene.
 *
 * @returns {Array} Array of scene coordination objects
 */
export function loadSceneCoordination() {
  const coordDir = join(__dirname, "..", "scene_coordination");
  const scenes = [];

  for (let i = 1; i <= 8; i++) {
    const sceneDir = join(coordDir, `scene_${i}`);
    const statusPath = join(sceneDir, "STATUS.json");
    const instructionsPath = join(sceneDir, "INSTRUCTIONS.md");

    const scene = {
      id: i,
      status: null,
      instructions: null,
      interactions: [],
    };

    // Load status
    if (existsSync(statusPath)) {
      try {
        scene.status = JSON.parse(readFileSync(statusPath, "utf-8"));
      } catch (error) {
        log.error(`Failed to parse STATUS.json for scene ${i}`, {
          error: error.message,
        });
      }
    }

    // Load instructions and parse interaction table
    if (existsSync(instructionsPath)) {
      try {
        const md = readFileSync(instructionsPath, "utf-8");
        scene.instructions = md;
        scene.interactions = parseInteractionTable(md);
      } catch (error) {
        log.error(`Failed to parse INSTRUCTIONS.md for scene ${i}`, {
          error: error.message,
        });
      }
    }

    scenes.push(scene);
  }

  return scenes;
}

/**
 * Parse the interaction definition table from a scene's INSTRUCTIONS.md.
 * Extracts Step, Event Name, Trigger type, Actor, and Description.
 *
 * @param {string} markdown - The raw markdown content
 * @returns {Array} Parsed interaction objects
 */
function parseInteractionTable(markdown) {
  const interactions = [];
  const lines = markdown.split("\n");
  let inTable = false;
  let headerParsed = false;

  for (const line of lines) {
    const trimmed = line.trim();

    // Detect table start (header row with "Step" and "Event Name")
    if (
      trimmed.startsWith("|") &&
      trimmed.includes("Step") &&
      trimmed.includes("Event Name")
    ) {
      inTable = true;
      headerParsed = false;
      continue;
    }

    // Skip separator row
    if (inTable && !headerParsed && trimmed.match(/^\|[\s-|]+\|$/)) {
      headerParsed = true;
      continue;
    }

    // Parse data rows
    if (inTable && headerParsed && trimmed.startsWith("|")) {
      const cells = trimmed
        .split("|")
        .map((c) => c.trim())
        .filter((c) => c.length > 0);

      if (cells.length >= 5) {
        const step = parseInt(cells[0], 10);
        if (!isNaN(step)) {
          interactions.push({
            step,
            eventName: cells[1].replace(/`/g, ""),
            trigger: cells[2],
            actor: cells[3],
            description: cells[4],
          });
        }
      }
    }

    // End of table
    if (inTable && headerParsed && !trimmed.startsWith("|") && trimmed.length > 0) {
      inTable = false;
    }
  }

  return interactions;
}

// ---------------------------------------------------------------------------
// Wiring Plan Generator
// ---------------------------------------------------------------------------

/**
 * Generate a deterministic wiring plan for a specific scene.
 * Cross-references the scene's interaction definitions against the
 * asset manifest to produce exact MCP call sequences.
 *
 * This is the core of the quantized inference path:
 * - The script defines WHAT should happen (the intent)
 * - The manifest defines WHAT EXISTS (the data)
 * - This function computes the EXACT OPERATIONS needed (the inference)
 *
 * No generative model is involved. The plan is deterministic.
 *
 * @param {number} sceneId - Scene number (1-8)
 * @param {object} manifest - The asset manifest
 * @returns {object} The wiring plan with confidence scores
 */
export function generateWiringPlan(sceneId, manifest) {
  const scenes = manifest.scene_coordination || loadSceneCoordination();
  const scene = scenes.find((s) => s.id === sceneId);

  if (!scene) {
    return {
      error: `Scene ${sceneId} not found in coordination data`,
      confidence: 0.0,
    };
  }

  if (!scene.interactions || scene.interactions.length === 0) {
    return {
      error: `Scene ${sceneId} has no parsed interactions`,
      confidence: 0.0,
    };
  }

  // Extract level path from instructions
  const levelPath = extractLevelPath(scene.instructions || "");

  const plan = {
    scene: sceneId,
    name: scene.status?.name || `Scene ${sceneId}`,
    levelPath,
    status: scene.status?.status || "unknown",
    completedSteps: scene.status?.completed_steps || [],
    totalInteractions: scene.interactions.length,
    operations: [],
    confidence: 1.0,
    lowConfidenceItems: [],
  };

  // For each interaction, generate the MCP call sequence
  for (const interaction of scene.interactions) {
    // Skip already completed steps
    if (plan.completedSteps.includes(interaction.step)) {
      plan.operations.push({
        step: interaction.step,
        eventName: interaction.eventName,
        status: "already_complete",
        calls: [],
        confidence: 1.0,
      });
      continue;
    }

    const operation = generateOperationCalls(interaction, manifest, levelPath);
    plan.operations.push(operation);

    // Track low confidence items
    if (operation.confidence < 0.8) {
      plan.lowConfidenceItems.push({
        step: interaction.step,
        eventName: interaction.eventName,
        reason: operation.confidenceReason,
        confidence: operation.confidence,
      });
    }

    // Overall plan confidence is the minimum of all operations
    plan.confidence = Math.min(plan.confidence, operation.confidence);
  }

  return plan;
}

/**
 * Generate the exact MCP API calls for a single interaction.
 * This is deterministic -- same input always produces same output.
 *
 * @param {object} interaction - Parsed interaction from INSTRUCTIONS.md
 * @param {object} manifest - The asset manifest
 * @param {string} levelPath - The level path for this scene
 * @returns {object} Operation with MCP calls and confidence
 */
function generateOperationCalls(interaction, manifest, levelPath) {
  const operation = {
    step: interaction.step,
    eventName: interaction.eventName,
    trigger: interaction.trigger,
    actor: interaction.actor,
    description: interaction.description,
    status: "pending",
    confidence: 1.0,
    confidenceReason: null,
    calls: [],
  };

  // Check if the actor exists in the manifest
  const actorExists = checkActorInManifest(interaction.actor, manifest);
  if (!actorExists) {
    operation.confidence = 0.6;
    operation.confidenceReason = `Actor '${interaction.actor}' not found in manifest. It may need to be spawned or the level may not be loaded.`;
  }

  // Check if the referenced level sequence exists
  const lsMatch = interaction.description.match(/\(LS_\d+_\d+\)/);
  const lsName = lsMatch ? lsMatch[0].replace(/[()]/g, "") : null;
  if (lsName) {
    const seqExists = manifest.sequences?.some(
      (s) => s.name === lsName || s.path?.includes(lsName)
    );
    if (!seqExists) {
      operation.confidence = Math.min(operation.confidence, 0.5);
      operation.confidenceReason =
        (operation.confidenceReason || "") +
        ` Level Sequence '${lsName}' not found in manifest.`;
    }
  }

  // Generate the standard wiring call sequence
  // Phase 1: Snapshot for safety
  operation.calls.push({
    order: 1,
    tool: "snapshot_graph",
    params: { blueprintName: levelPath },
    purpose: "Safety snapshot before mutation",
  });

  // Phase 2: Create CustomEvent node
  operation.calls.push({
    order: 2,
    tool: "add_node",
    params: {
      blueprintName: levelPath,
      nodeType: "CustomEvent",
      eventName: interaction.eventName,
      graphName: "EventGraph",
    },
    purpose: `Create custom event: ${interaction.eventName}`,
    captureOutput: "customEventGuid",
  });

  // Phase 3: Create GetSubsystem call
  operation.calls.push({
    order: 3,
    tool: "add_node",
    params: {
      blueprintName: levelPath,
      nodeType: "CallFunction",
      functionName: "GetSubsystem",
      className: "UGameplayMessageSubsystem",
      graphName: "EventGraph",
    },
    purpose: "Get GameplayMessageSubsystem reference",
    captureOutput: "getSubsystemGuid",
  });

  // Phase 4: Create BroadcastMessage call
  operation.calls.push({
    order: 4,
    tool: "add_node",
    params: {
      blueprintName: levelPath,
      nodeType: "CallFunction",
      functionName: "BroadcastMessage",
      graphName: "EventGraph",
    },
    purpose: "Create BroadcastMessage node",
    captureOutput: "broadcastGuid",
  });

  // Phase 5: Create MakeStruct for the message
  operation.calls.push({
    order: 5,
    tool: "add_node",
    params: {
      blueprintName: levelPath,
      nodeType: "MakeStruct",
      structType: "Msg_StoryStep",
      graphName: "EventGraph",
    },
    purpose: "Create Msg_StoryStep struct",
    captureOutput: "makeStructGuid",
  });

  // Phase 6: Set the step number on the struct
  operation.calls.push({
    order: 6,
    tool: "set_pin_default",
    params: {
      blueprintName: levelPath,
      nodeGuid: "${makeStructGuid}",
      pinName: "Step",
      value: String(interaction.step),
    },
    purpose: `Set step number to ${interaction.step}`,
  });

  // Phase 7: Wire the execution chain
  operation.calls.push(
    {
      order: 7,
      tool: "connect_pins",
      params: {
        blueprintName: levelPath,
        sourceNodeGuid: "${customEventGuid}",
        sourcePinName: "then",
        targetNodeGuid: "${getSubsystemGuid}",
        targetPinName: "execute",
      },
      purpose: "Wire: CustomEvent -> GetSubsystem",
    },
    {
      order: 8,
      tool: "connect_pins",
      params: {
        blueprintName: levelPath,
        sourceNodeGuid: "${getSubsystemGuid}",
        sourcePinName: "ReturnValue",
        targetNodeGuid: "${broadcastGuid}",
        targetPinName: "Target",
      },
      purpose: "Wire: GetSubsystem.ReturnValue -> BroadcastMessage.Target",
    },
    {
      order: 9,
      tool: "connect_pins",
      params: {
        blueprintName: levelPath,
        sourceNodeGuid: "${getSubsystemGuid}",
        sourcePinName: "then",
        targetNodeGuid: "${broadcastGuid}",
        targetPinName: "execute",
      },
      purpose: "Wire: GetSubsystem.then -> BroadcastMessage.execute",
    },
    {
      order: 10,
      tool: "connect_pins",
      params: {
        blueprintName: levelPath,
        sourceNodeGuid: "${makeStructGuid}",
        sourcePinName: "Msg_StoryStep",
        targetNodeGuid: "${broadcastGuid}",
        targetPinName: "Message",
      },
      purpose: "Wire: MakeStruct.output -> BroadcastMessage.Message",
    }
  );

  return operation;
}

// ---------------------------------------------------------------------------
// Generate All Wiring Plans
// ---------------------------------------------------------------------------

/**
 * Generate wiring plans for all 8 scenes and cache them.
 *
 * @param {object} manifest - The asset manifest
 * @returns {object} All wiring plans with aggregate stats
 */
export function generateAllWiringPlans(manifest) {
  ensureCacheDir();

  const allPlans = {
    generated_at: new Date().toISOString(),
    manifest_version: manifest.version,
    scenes: [],
    summary: {
      totalScenes: 8,
      totalInteractions: 0,
      completedInteractions: 0,
      pendingInteractions: 0,
      lowConfidenceCount: 0,
      overallConfidence: 1.0,
    },
  };

  for (let i = 1; i <= 8; i++) {
    const plan = generateWiringPlan(i, manifest);
    allPlans.scenes.push(plan);

    if (plan.operations) {
      allPlans.summary.totalInteractions += plan.operations.length;
      allPlans.summary.completedInteractions += plan.operations.filter(
        (op) => op.status === "already_complete"
      ).length;
      allPlans.summary.pendingInteractions += plan.operations.filter(
        (op) => op.status === "pending"
      ).length;
      allPlans.summary.lowConfidenceCount += (plan.lowConfidenceItems || []).length;
      allPlans.summary.overallConfidence = Math.min(
        allPlans.summary.overallConfidence,
        plan.confidence
      );
    }
  }

  // Cache the plans
  writeFileSync(WIRING_PLAN_PATH, JSON.stringify(allPlans, null, 2));
  log.info("All wiring plans generated and cached", {
    path: WIRING_PLAN_PATH,
    totalInteractions: allPlans.summary.totalInteractions,
    pending: allPlans.summary.pendingInteractions,
    confidence: allPlans.summary.overallConfidence,
  });

  return allPlans;
}

/**
 * Load cached wiring plans from disk.
 *
 * @returns {object|null} Cached plans or null
 */
export function loadCachedWiringPlans() {
  if (!existsSync(WIRING_PLAN_PATH)) return null;
  try {
    return JSON.parse(readFileSync(WIRING_PLAN_PATH, "utf-8"));
  } catch (error) {
    log.error("Failed to load cached wiring plans", { error: error.message });
    return null;
  }
}

// ---------------------------------------------------------------------------
// Status Update
// ---------------------------------------------------------------------------

/**
 * Update a scene's STATUS.json after completing a wiring step.
 * This is the progress-saving mechanism -- every completed step is
 * persisted immediately so work is never lost.
 *
 * @param {number} sceneId - Scene number (1-8)
 * @param {number} step - The completed step number
 * @param {string} status - New status: "in_progress", "complete", "blocked"
 * @param {string|null} blockedReason - Reason if blocked
 * @returns {object} Updated status
 */
export function updateSceneStatus(sceneId, step, status, blockedReason = null) {
  const coordDir = join(__dirname, "..", "scene_coordination");
  const statusPath = join(coordDir, `scene_${sceneId}`, "STATUS.json");

  if (!existsSync(statusPath)) {
    return { error: `STATUS.json not found for scene ${sceneId}` };
  }

  try {
    const current = JSON.parse(readFileSync(statusPath, "utf-8"));

    // Add completed step if not already there
    if (step && !current.completed_steps.includes(step)) {
      current.completed_steps.push(step);
      current.completed_steps.sort((a, b) => a - b);
    }

    // Update status
    if (status) {
      current.status = status;
    }

    // Auto-detect completion
    if (current.completed_steps.length >= current.total_steps) {
      current.status = "complete";
    } else if (current.completed_steps.length > 0 && current.status === "not_started") {
      current.status = "in_progress";
    }

    current.blocked_reason = blockedReason;
    current.last_updated = new Date().toISOString();

    writeFileSync(statusPath, JSON.stringify(current, null, 2));
    log.info(`Scene ${sceneId} status updated`, {
      status: current.status,
      completed: current.completed_steps.length,
      total: current.total_steps,
    });

    return current;
  } catch (error) {
    log.error(`Failed to update scene ${sceneId} status`, {
      error: error.message,
    });
    return { error: error.message };
  }
}

// ---------------------------------------------------------------------------
// Helper Functions
// ---------------------------------------------------------------------------

/**
 * Assign a precision tier to a Blueprint based on naming conventions.
 * Hero assets (characters, key props) get full detail.
 * Background assets get minimal representation.
 */
function assignBlueprintTier(name) {
  const n = name.toLowerCase();
  if (n.includes("heather") || n.includes("susan") || n.includes("player") || n.includes("pawn")) {
    return TIER_HERO;
  }
  if (n.includes("bp_") || n.includes("interaction") || n.includes("trigger") || n.includes("grab")) {
    return TIER_STANDARD;
  }
  return TIER_BACKGROUND;
}

/**
 * Quantize the actor list -- group by class, keep transforms compact.
 * Hero actors get full detail. Background actors get name + class only.
 */
function quantizeActorList(actors) {
  const quantized = {};

  for (const actor of actors) {
    const name = actor.label || actor.name;
    const cls = actor.class || "Unknown";
    const tier = assignActorTier(name, cls);

    if (tier === TIER_HERO) {
      quantized[name] = {
        class: cls,
        tier,
        x: actor.x,
        y: actor.y,
        z: actor.z,
        pitch: actor.pitch,
        yaw: actor.yaw,
        roll: actor.roll,
        components: actor.components || [],
      };
    } else if (tier === TIER_STANDARD) {
      quantized[name] = {
        class: cls,
        tier,
        x: actor.x,
        y: actor.y,
        z: actor.z,
      };
    } else {
      quantized[name] = { class: cls, tier };
    }
  }

  return quantized;
}

/**
 * Assign a precision tier to an actor.
 */
function assignActorTier(name, className) {
  const n = (name || "").toLowerCase();
  const c = (className || "").toLowerCase();

  // Hero: characters, key interactables
  if (
    n.includes("heather") ||
    n.includes("susan") ||
    n.includes("detective") ||
    n.includes("officer") ||
    n.includes("friend") ||
    c.includes("character") ||
    c.includes("pawn")
  ) {
    return TIER_HERO;
  }

  // Standard: interactable props, triggers, markers
  if (
    n.includes("bp_") ||
    n.includes("trigger") ||
    n.includes("marker") ||
    n.includes("grab") ||
    n.includes("phone") ||
    n.includes("teapot") ||
    n.includes("pitcher") ||
    n.includes("door") ||
    n.includes("fridge") ||
    n.includes("illustration") ||
    c.includes("triggervolume") ||
    c.includes("levelsequence")
  ) {
    return TIER_STANDARD;
  }

  return TIER_BACKGROUND;
}

/**
 * Check if an actor name exists in the manifest.
 */
function checkActorInManifest(actorName, manifest) {
  if (!manifest.actors) return false;
  const search = actorName.toLowerCase();
  return Object.keys(manifest.actors).some(
    (name) => name.toLowerCase().includes(search) || search.includes(name.toLowerCase())
  );
}

/**
 * Extract the level path from INSTRUCTIONS.md content.
 */
function extractLevelPath(markdown) {
  const match = markdown.match(/Level Path:\*\*\s*`([^`]+)`/);
  return match ? match[1] : null;
}

// ---------------------------------------------------------------------------
// MCP Tool Definitions
// ---------------------------------------------------------------------------

/**
 * Tool definitions for registration in the MCP tool registry.
 * These are the tools Claude Code calls to use the quantized inference path.
 */
export const QUANTIZED_TOOLS = [
  {
    name: "quantize_project",
    description:
      "Generate or refresh the Adaptive Asset Manifest -- a quantized representation of the entire UE5 project. " +
      "This scans all blueprints, actors, levels, and sequences and caches the result. " +
      "Subsequent calls return the cached version unless forceRefresh is true. " +
      "This is the first tool to call before any scene wiring operation.",
    inputSchema: {
      type: "object",
      properties: {
        forceRefresh: {
          type: "boolean",
          description: "Force regeneration even if cache is fresh (default: false)",
        },
      },
    },
    annotations: { readOnlyHint: true },
  },
  {
    name: "get_scene_plan",
    description:
      "Get the deterministic wiring plan for a specific scene (1-8). " +
      "Returns the exact sequence of MCP calls needed to wire each interaction, " +
      "with confidence scores per operation. Low confidence items need human review. " +
      "Requires a manifest (call quantize_project first if none exists).",
    inputSchema: {
      type: "object",
      properties: {
        sceneId: {
          type: "number",
          description: "Scene number (1-8)",
        },
      },
      required: ["sceneId"],
    },
    annotations: { readOnlyHint: true },
  },
  {
    name: "get_all_scene_plans",
    description:
      "Get wiring plans for all 8 scenes at once. Returns aggregate stats " +
      "including total/completed/pending interactions and overall confidence. " +
      "Use this for a full project status overview.",
    inputSchema: {
      type: "object",
      properties: {},
    },
    annotations: { readOnlyHint: true },
  },
  {
    name: "get_wiring_status",
    description:
      "Get the current wiring status across all 8 scenes. " +
      "Returns completion percentages, blocked scenes, and next actions. " +
      "This is the coordinator view -- use it to decide which scene to work on next.",
    inputSchema: {
      type: "object",
      properties: {},
    },
    annotations: { readOnlyHint: true },
  },
  {
    name: "execute_scene_step",
    description:
      "Execute a single wiring step from a scene plan. " +
      "Takes the scene ID and step number, executes the pre-computed MCP calls " +
      "in order, and updates STATUS.json on completion. " +
      "Always snapshots before mutation. Reports confidence on result.",
    inputSchema: {
      type: "object",
      properties: {
        sceneId: {
          type: "number",
          description: "Scene number (1-8)",
        },
        step: {
          type: "number",
          description: "Step number from the wiring plan",
        },
      },
      required: ["sceneId", "step"],
    },
    annotations: { destructiveHint: true },
  },
  {
    name: "mark_step_complete",
    description:
      "Manually mark a wiring step as complete in STATUS.json. " +
      "Use this when a step was completed outside of the automated pipeline " +
      "(e.g., manually wired in the editor). Persists immediately.",
    inputSchema: {
      type: "object",
      properties: {
        sceneId: {
          type: "number",
          description: "Scene number (1-8)",
        },
        step: {
          type: "number",
          description: "Step number to mark complete",
        },
      },
      required: ["sceneId", "step"],
    },
    annotations: { destructiveHint: true },
  },
  {
    name: "mark_scene_blocked",
    description:
      "Mark a scene as blocked with a reason. " +
      "Use when a scene cannot proceed due to missing assets, " +
      "broken references, or dependencies on other scenes.",
    inputSchema: {
      type: "object",
      properties: {
        sceneId: {
          type: "number",
          description: "Scene number (1-8)",
        },
        reason: {
          type: "string",
          description: "Why the scene is blocked",
        },
      },
      required: ["sceneId", "reason"],
    },
    annotations: { destructiveHint: true },
  },
];

// ---------------------------------------------------------------------------
// MCP Tool Handlers
// ---------------------------------------------------------------------------

/**
 * Handle a quantized inference tool call.
 *
 * @param {string} toolName - The tool name
 * @param {object} args - Tool arguments
 * @param {function} callUnreal - Function to call the UE5 HTTP API
 * @param {object} config - Server configuration
 * @returns {object} MCP tool response
 */
export async function handleQuantizedTool(toolName, args, callUnreal, config) {
  try {
    switch (toolName) {
      case "quantize_project": {
        const manifest = await getManifest(
          callUnreal,
          config,
          args.forceRefresh || false
        );
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(
                {
                  success: true,
                  cached: !args.forceRefresh,
                  project: manifest.project,
                  engine: manifest.engine,
                  blueprints: manifest.blueprints?.length || 0,
                  levels: manifest.levels?.length || 0,
                  actors: Object.keys(manifest.actors || {}).length,
                  sequences: manifest.sequences?.length || 0,
                  scenes: (manifest.scene_coordination || []).map((s) => ({
                    id: s.id,
                    status: s.status?.status || "unknown",
                    interactions: s.interactions?.length || 0,
                  })),
                  confidence: manifest.confidence,
                  generated_at: manifest.generated_at,
                },
                null,
                2
              ),
            },
          ],
        };
      }

      case "get_scene_plan": {
        const manifest = await getManifest(callUnreal, config);
        const plan = generateWiringPlan(args.sceneId, manifest);
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(plan, null, 2),
            },
          ],
        };
      }

      case "get_all_scene_plans": {
        const manifest = await getManifest(callUnreal, config);
        const allPlans = generateAllWiringPlans(manifest);
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(allPlans, null, 2),
            },
          ],
        };
      }

      case "get_wiring_status": {
        const scenes = loadSceneCoordination();
        const status = {
          scenes: scenes.map((s) => ({
            id: s.id,
            name: s.status?.name || `Scene ${s.id}`,
            status: s.status?.status || "unknown",
            completed: s.status?.completed_steps?.length || 0,
            total: s.status?.total_steps || 0,
            percent:
              s.status?.total_steps > 0
                ? Math.round(
                    ((s.status?.completed_steps?.length || 0) /
                      s.status.total_steps) *
                      100
                  )
                : 0,
            blocked: s.status?.blocked_reason || null,
            lastUpdated: s.status?.last_updated || null,
          })),
          summary: {},
        };

        const total = status.scenes.reduce((sum, s) => sum + s.total, 0);
        const completed = status.scenes.reduce((sum, s) => sum + s.completed, 0);
        const blocked = status.scenes.filter((s) => s.status === "blocked").length;

        status.summary = {
          totalInteractions: total,
          completedInteractions: completed,
          pendingInteractions: total - completed,
          percentComplete: total > 0 ? Math.round((completed / total) * 100) : 0,
          blockedScenes: blocked,
          nextScene: status.scenes.find(
            (s) => s.status !== "complete" && s.status !== "blocked"
          )?.id || null,
        };

        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(status, null, 2),
            },
          ],
        };
      }

      case "execute_scene_step": {
        // This generates the plan but does NOT auto-execute the UE5 calls.
        // It returns the exact call sequence for Claude to execute step by step.
        // This keeps the human in the loop -- Claude sees the plan, executes
        // each call individually, and can abort if confidence drops.
        const manifest = await getManifest(callUnreal, config);
        const plan = generateWiringPlan(args.sceneId, manifest);
        const operation = plan.operations?.find((op) => op.step === args.step);

        if (!operation) {
          return {
            content: [
              {
                type: "text",
                text: JSON.stringify({
                  success: false,
                  error: `Step ${args.step} not found in scene ${args.sceneId} plan`,
                }),
              },
            ],
            isError: true,
          };
        }

        if (operation.status === "already_complete") {
          return {
            content: [
              {
                type: "text",
                text: JSON.stringify({
                  success: true,
                  status: "already_complete",
                  message: `Step ${args.step} (${operation.eventName}) is already wired.`,
                }),
              },
            ],
          };
        }

        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(
                {
                  success: true,
                  status: "plan_ready",
                  step: operation.step,
                  eventName: operation.eventName,
                  trigger: operation.trigger,
                  confidence: operation.confidence,
                  confidenceWarning: operation.confidenceReason || null,
                  levelPath: plan.levelPath,
                  callCount: operation.calls.length,
                  calls: operation.calls,
                  instructions:
                    "Execute each call in order. Capture GUIDs from add_node responses " +
                    "and substitute them into subsequent calls where ${variableName} appears. " +
                    "After all calls succeed, call mark_step_complete to persist progress.",
                },
                null,
                2
              ),
            },
          ],
        };
      }

      case "mark_step_complete": {
        const result = updateSceneStatus(
          args.sceneId,
          args.step,
          "in_progress"
        );
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify({
                success: !result.error,
                ...result,
              }),
            },
          ],
          isError: !!result.error,
        };
      }

      case "mark_scene_blocked": {
        const result = updateSceneStatus(
          args.sceneId,
          null,
          "blocked",
          args.reason
        );
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify({
                success: !result.error,
                ...result,
              }),
            },
          ],
          isError: !!result.error,
        };
      }

      default:
        return {
          content: [
            {
              type: "text",
              text: `Unknown quantized tool: ${toolName}`,
            },
          ],
          isError: true,
        };
    }
  } catch (error) {
    log.error(`Quantized tool error: ${toolName}`, {
      error: error.message,
      stack: error.stack,
    });
    return {
      content: [
        {
          type: "text",
          text: JSON.stringify({
            success: false,
            error: error.message,
            tool: toolName,
          }),
        },
      ],
      isError: true,
    };
  }
}

// ---------------------------------------------------------------------------
// Exports for testing
// ---------------------------------------------------------------------------
export {
  parseInteractionTable,
  assignBlueprintTier,
  quantizeActorList,
  assignActorTier,
  checkActorInManifest,
  extractLevelPath,
  generateOperationCalls,
  ensureCacheDir,
  CACHE_DIR,
  MANIFEST_PATH,
  WIRING_PLAN_PATH,
};
