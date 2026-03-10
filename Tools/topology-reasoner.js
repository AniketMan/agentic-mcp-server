/**
 * TopologyReasoner MCP Integration
 *
 * Provides a tool for AI-driven topology reasoning decisions.
 * Called by TopologyReasoner's Python server instead of Ollama.
 *
 * The Python server sends mesh metadata, and this tool returns
 * a topology strategy JSON that guides retopology decisions.
 */

import { log } from "./lib.js";

// Topology ontology system prompt - same as TopologyReasoner's original
const TOPOLOGY_ONTOLOGY_PROMPT = `You are a topology reasoning engine for 3D mesh retopology.
You receive mesh metadata and a target platform, and you output a JSON topology strategy.

HARD RULES (must never be violated):
- All faces must be quads where possible
- No vertex may have valence greater than 5
- No overlapping or non-manifold geometry

SOFT RULES (weighted by context):
- Edge flow follows form
- Uniform density on flat surfaces
- Higher density at curvature transitions
- Support loops near hard/sharp edges

You MUST respond with ONLY valid JSON:
{
    "object_class": "hard_surface_prop | character | vehicle | architecture | organic | unknown",
    "object_subclass": "string",
    "target_polys": integer,
    "preserve_sharp_edges": true/false,
    "sharp_angle_threshold": float,
    "uv_strategy": "smart_project | cube_project | cylinder_project",
    "uv_island_margin": float,
    "density_uniform": true/false,
    "notes": "brief reasoning"
}`;

/**
 * Tool definition for MCP registration
 */
export const TOPOLOGY_REASONER_TOOL = {
  name: "topology_reason",
  description: `Analyze 3D mesh metadata and provide an AI-driven topology strategy for retopology.
Input: mesh metadata (dimensions, face counts, materials, platform preset)
Output: JSON topology strategy with target poly count, UV strategy, and optimization hints.
Used by TopologyReasoner plugin for intelligent mesh optimization decisions.`,
  inputSchema: {
    type: "object",
    properties: {
      mesh_metadata: {
        type: "object",
        description: "Mesh analysis data from Blender/bpy",
        properties: {
          object_name: { type: "string", description: "Name of the mesh object" },
          dimensions: {
            type: "object",
            properties: {
              x: { type: "number" },
              y: { type: "number" },
              z: { type: "number" },
            },
          },
          face_count: { type: "number", description: "Total number of faces" },
          vertex_count: { type: "number", description: "Total number of vertices" },
          edge_count: { type: "number", description: "Total number of edges" },
          face_types: {
            type: "object",
            properties: {
              tris: { type: "number" },
              quads: { type: "number" },
              ngons: { type: "number" },
            },
          },
          material_count: { type: "number" },
          material_names: { type: "array", items: { type: "string" } },
          sharp_edge_ratio: { type: "number" },
          surface_area: { type: "number" },
          aspect_ratios: {
            type: "object",
            properties: {
              xy: { type: "number" },
              xz: { type: "number" },
              yz: { type: "number" },
            },
          },
          platform: {
            type: "object",
            properties: {
              preset: { type: "string" },
              label: { type: "string" },
              min_polys: { type: "number" },
              max_polys: { type: "number" },
              texture_res: { type: "number" },
            },
          },
        },
        required: ["object_name", "face_count", "platform"],
      },
    },
    required: ["mesh_metadata"],
  },
  annotations: {
    readOnlyHint: true,
    destructiveHint: false,
    idempotentHint: true,
    openWorldHint: false,
  },
};

/**
 * Execute topology reasoning based on mesh metadata.
 * Returns a topology strategy JSON.
 *
 * @param {object} args - Tool arguments containing mesh_metadata
 * @returns {object} MCP tool response with topology strategy
 */
export function executeTopologyReason(args) {
  const metadata = args?.mesh_metadata;

  if (!metadata) {
    return {
      success: false,
      message: "Missing mesh_metadata parameter",
    };
  }

  log.info("TopologyReasoner: Analyzing mesh", {
    name: metadata.object_name,
    faces: metadata.face_count,
    platform: metadata.platform?.preset,
  });

  // Generate intelligent topology strategy based on mesh analysis
  const strategy = analyzeAndGenerateStrategy(metadata);

  return {
    success: true,
    message: "Topology strategy generated",
    data: {
      strategy,
      system_prompt: TOPOLOGY_ONTOLOGY_PROMPT,
    },
  };
}

/**
 * Analyze mesh metadata and generate a topology strategy.
 * This provides heuristic-based reasoning that can be enhanced by the AI.
 *
 * @param {object} metadata - Mesh metadata from bpy
 * @returns {object} Topology strategy JSON
 */
function analyzeAndGenerateStrategy(metadata) {
  const platform = metadata.platform || {};
  const minPolys = platform.min_polys || 5000;
  const maxPolys = platform.max_polys || 30000;

  // Classify object based on name and geometry
  const objectClass = classifyObject(metadata);

  // Calculate target poly count based on current count and platform
  const currentFaces = metadata.face_count || 10000;
  let targetPolys;

  if (currentFaces <= minPolys) {
    // Already optimized, keep it
    targetPolys = currentFaces;
  } else if (currentFaces <= maxPolys) {
    // Within budget, slight reduction
    targetPolys = Math.round(currentFaces * 0.8);
  } else {
    // Over budget, reduce to max with buffer
    targetPolys = Math.round(maxPolys * 0.9);
  }

  // Clamp to platform limits
  targetPolys = Math.max(minPolys, Math.min(maxPolys, targetPolys));

  // Determine UV strategy based on object shape
  const uvStrategy = determineUVStrategy(metadata, objectClass);

  // Determine if sharp edges should be preserved
  const sharpEdgeRatio = metadata.sharp_edge_ratio || 0;
  const preserveSharp = sharpEdgeRatio > 0.05 || objectClass.class === "hard_surface_prop";

  // Generate notes explaining the reasoning
  const notes = generateNotes(metadata, objectClass, targetPolys, platform);

  return {
    object_class: objectClass.class,
    object_subclass: objectClass.subclass,
    target_polys: targetPolys,
    preserve_sharp_edges: preserveSharp,
    sharp_angle_threshold: preserveSharp ? 30.0 : 45.0,
    uv_strategy: uvStrategy,
    uv_island_margin: platform.texture_res >= 2048 ? 0.002 : 0.004,
    density_uniform: objectClass.class !== "organic" && objectClass.class !== "character",
    notes,
  };
}

/**
 * Classify object based on name patterns and geometry
 */
function classifyObject(metadata) {
  const name = (metadata.object_name || "").toLowerCase();
  const dims = metadata.dimensions || { x: 1, y: 1, z: 1 };
  const aspectRatios = metadata.aspect_ratios || { xy: 1, xz: 1, yz: 1 };

  // Name-based classification
  if (/character|body|human|person|npc/i.test(name)) {
    return { class: "character", subclass: "humanoid" };
  }
  if (/vehicle|car|truck|bike|plane|ship/i.test(name)) {
    return { class: "vehicle", subclass: detectVehicleType(name) };
  }
  if (/tree|plant|bush|grass|foliage|leaf/i.test(name)) {
    return { class: "organic", subclass: "vegetation" };
  }
  if (/rock|stone|cliff|terrain/i.test(name)) {
    return { class: "organic", subclass: "terrain" };
  }
  if (/wall|floor|ceiling|door|window|building|house|room/i.test(name)) {
    return { class: "architecture", subclass: "structural" };
  }
  if (/chair|table|desk|lamp|furniture|cabinet|shelf/i.test(name)) {
    return { class: "hard_surface_prop", subclass: "furniture" };
  }
  if (/weapon|gun|sword|tool|device|machine/i.test(name)) {
    return { class: "hard_surface_prop", subclass: "mechanical" };
  }

  // Geometry-based classification fallback
  const maxAspect = Math.max(aspectRatios.xy, aspectRatios.xz, aspectRatios.yz);

  if (maxAspect > 10) {
    return { class: "architecture", subclass: "elongated" };
  }
  if (maxAspect < 1.5 && dims.x > 0 && dims.y > 0 && dims.z > 0) {
    return { class: "hard_surface_prop", subclass: "compact" };
  }

  return { class: "unknown", subclass: "generic" };
}

function detectVehicleType(name) {
  if (/car|sedan|suv|coupe/i.test(name)) return "ground_vehicle";
  if (/plane|aircraft|jet|helicopter/i.test(name)) return "aircraft";
  if (/ship|boat|vessel/i.test(name)) return "watercraft";
  if (/bike|motorcycle|cycle/i.test(name)) return "two_wheeler";
  return "vehicle";
}

/**
 * Determine UV unwrapping strategy based on object geometry
 */
function determineUVStrategy(metadata, objectClass) {
  const aspectRatios = metadata.aspect_ratios || { xy: 1, xz: 1, yz: 1 };

  // Cylindrical objects
  if (objectClass.subclass === "two_wheeler" ||
      /pipe|cylinder|pole|column/i.test(metadata.object_name || "")) {
    return "cylinder_project";
  }

  // Box-like objects
  const maxAspect = Math.max(aspectRatios.xy, aspectRatios.xz, aspectRatios.yz);
  if (maxAspect < 3 && objectClass.class === "hard_surface_prop") {
    return "cube_project";
  }

  // Default to smart project for complex shapes
  return "smart_project";
}

/**
 * Generate human-readable notes explaining the topology strategy
 */
function generateNotes(metadata, objectClass, targetPolys, platform) {
  const parts = [];

  parts.push(`Classified as ${objectClass.class}/${objectClass.subclass}.`);

  const currentFaces = metadata.face_count || 0;
  const reduction = currentFaces > 0 ? Math.round((1 - targetPolys / currentFaces) * 100) : 0;

  if (reduction > 0) {
    parts.push(`Reducing from ${currentFaces} to ${targetPolys} faces (${reduction}% reduction).`);
  } else {
    parts.push(`Maintaining ${targetPolys} faces within budget.`);
  }

  parts.push(`Targeting ${platform.label || platform.preset || "standard"} platform.`);

  return parts.join(" ");
}
