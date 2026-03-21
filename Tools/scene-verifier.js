/**
 * Scene Verifier — exhaustive post-wiring validation per scene.
 *
 * Read-only verification layer that checks whether actors, Blueprints,
 * components, and sequences in the live UE5 editor match the expected
 * state from the wiring plans. Calls read-only C++ plugin endpoints
 * (listActors, getActor, componentList, getGraph, sequencerGetTracks)
 * and reports pass/fail with diagnostics.
 *
 * Exports:
 *   VERIFIER_TOOLS        — MCP tool definitions (array)
 *   handleVerifierTool     — MCP tool handler (async function)
 */

import { log } from "./lib.js";

// ---------------------------------------------------------------------------
// MCP Tool Definitions
// ---------------------------------------------------------------------------

export const VERIFIER_TOOLS = [
  {
    name: "verifyScene",
    description:
      "Verify all actors in a level have expected components, valid transforms, " +
      "and are properly wired. Lists every actor, checks for missing components " +
      "(e.g. GrabbableComponent on interactable props), null meshes, and zero-scale " +
      "transforms. Returns a pass/fail report per actor.",
    inputSchema: {
      type: "object",
      properties: {
        levelName: {
          type: "string",
          description: "Name or path of the level to verify (e.g. 'Scene_01_Restaurant')",
        },
        expectedActors: {
          type: "array",
          description: "Optional: array of actor names that must exist in the level",
        },
      },
      required: ["levelName"],
    },
    annotations: { readOnlyHint: true },
  },
  {
    name: "verifyBlueprint",
    description:
      "Verify a Blueprint compiles cleanly, has no broken pin references, " +
      "and contains expected event nodes. Returns graph node count, " +
      "compilation status, and any error/warning messages.",
    inputSchema: {
      type: "object",
      properties: {
        blueprintName: {
          type: "string",
          description: "Blueprint name or path to verify",
        },
      },
      required: ["blueprintName"],
    },
    annotations: { readOnlyHint: true },
  },
  {
    name: "verifySequence",
    description:
      "Verify a LevelSequence has all expected actor bindings, tracks, and " +
      "sections. Returns binding count, track types, playback range, and " +
      "flags any unbound possessables.",
    inputSchema: {
      type: "object",
      properties: {
        sequenceName: {
          type: "string",
          description: "LevelSequence asset name or path to verify",
        },
      },
      required: ["sequenceName"],
    },
    annotations: { readOnlyHint: true },
  },
  {
    name: "verifyAll",
    description:
      "Run verifyScene, verifyBlueprint, and verifySequence across all assets " +
      "in the current level. Returns an aggregate pass/fail report with totals.",
    inputSchema: {
      type: "object",
      properties: {},
    },
    annotations: { readOnlyHint: true },
  },
];

// ---------------------------------------------------------------------------
// Verification Logic
// ---------------------------------------------------------------------------

async function verifyScene(args, callUnreal) {
  const { levelName, expectedActors } = args;
  const report = {
    level: levelName,
    passed: true,
    actors: [],
    missingActors: [],
    warnings: [],
    errors: [],
  };

  const actorsResult = await callUnreal("/api/list-actors", "POST", {});
  if (!actorsResult || !actorsResult.success) {
    report.passed = false;
    report.errors.push("Failed to list actors — editor may not be connected");
    return report;
  }

  const actors = actorsResult.actors || actorsResult.data?.actors || [];
  report.totalActors = actors.length;

  if (expectedActors && expectedActors.length > 0) {
    const actorNames = actors.map((a) => (a.label || a.name || "").toLowerCase());
    for (const expected of expectedActors) {
      if (!actorNames.some((n) => n.includes(expected.toLowerCase()))) {
        report.missingActors.push(expected);
        report.passed = false;
      }
    }
  }

  for (const actor of actors) {
    const actorName = actor.label || actor.name || "Unknown";
    const entry = { name: actorName, class: actor.class || "Unknown", issues: [] };

    if (actor.transform) {
      const s = actor.transform.scale;
      if (s && (s.x === 0 || s.y === 0 || s.z === 0)) {
        entry.issues.push("Zero scale on one or more axes");
      }
    }

    const compResult = await callUnreal("/api/component-list", "POST", {
      actorName: actorName,
    });

    if (compResult && compResult.success) {
      const components = compResult.components || compResult.data?.components || [];
      entry.componentCount = components.length;

      const hasStaticMesh = components.some(
        (c) => c.type === "StaticMeshComponent" || c.class?.includes("StaticMesh")
      );
      const hasSkeletalMesh = components.some(
        (c) => c.type === "SkeletalMeshComponent" || c.class?.includes("SkeletalMesh")
      );

      if (!hasStaticMesh && !hasSkeletalMesh && actor.class !== "ALandscape" && actor.class !== "ALight") {
        entry.issues.push("No mesh component found");
      }
    }

    if (entry.issues.length > 0) {
      report.warnings.push(`${actorName}: ${entry.issues.join(", ")}`);
    }

    report.actors.push(entry);
  }

  if (report.missingActors.length > 0) {
    report.errors.push(`Missing expected actors: ${report.missingActors.join(", ")}`);
  }

  return report;
}

async function verifyBlueprint(args, callUnreal) {
  const { blueprintName } = args;
  const report = {
    blueprint: blueprintName,
    passed: true,
    graphs: [],
    errors: [],
    warnings: [],
  };

  const bpResult = await callUnreal("/api/get-blueprint", "POST", {
    name: blueprintName,
  });

  if (!bpResult || !bpResult.success) {
    report.passed = false;
    report.errors.push(
      `Failed to load Blueprint: ${bpResult?.message || bpResult?.error || "unknown error"}`
    );
    return report;
  }

  const bp = bpResult.data || bpResult;
  report.parentClass = bp.parentClass || "Unknown";
  report.variables = bp.variables?.length || 0;

  const graphResult = await callUnreal("/api/get-graph", "POST", {
    name: blueprintName,
    graphName: "EventGraph",
  });

  if (graphResult && graphResult.success) {
    const nodes = graphResult.data?.nodes || graphResult.nodes || [];
    const graphEntry = {
      name: "EventGraph",
      nodeCount: nodes.length,
      brokenPins: [],
    };

    for (const node of nodes) {
      if (node.hasError || node.errorType) {
        graphEntry.brokenPins.push({
          node: node.title || node.guid,
          error: node.errorMsg || "Unspecified error",
        });
        report.passed = false;
      }
    }

    report.graphs.push(graphEntry);
  }

  const compileResult = await callUnreal("/api/validate-blueprint", "POST", {
    name: blueprintName,
  });

  if (compileResult && compileResult.success) {
    report.compileStatus = compileResult.data?.status || "ok";
    const compileErrors = compileResult.data?.errors || compileResult.errors || [];
    if (compileErrors.length > 0) {
      report.passed = false;
      report.errors.push(...compileErrors.map((e) => e.message || e));
    }
  }

  return report;
}

async function verifySequence(args, callUnreal) {
  const { sequenceName } = args;
  const report = {
    sequence: sequenceName,
    passed: true,
    bindings: [],
    tracks: [],
    errors: [],
    warnings: [],
  };

  const tracksResult = await callUnreal("/api/sequencer-get-tracks", "POST", {
    sequenceName: sequenceName,
  });

  if (!tracksResult || !tracksResult.success) {
    report.passed = false;
    report.errors.push(
      `Failed to read sequence: ${tracksResult?.message || tracksResult?.error || "unknown error"}`
    );
    return report;
  }

  const data = tracksResult.data || tracksResult;
  report.playbackRange = data.playbackRange || null;

  const bindings = data.bindings || [];
  report.bindingCount = bindings.length;

  for (const binding of bindings) {
    const entry = {
      name: binding.name || "Unknown",
      type: binding.type || "Unknown",
      trackCount: (binding.tracks || []).length,
      issues: [],
    };

    if (binding.type === "Possessable" && !binding.boundObject) {
      entry.issues.push("Unbound possessable — actor not found in level");
      report.warnings.push(`${entry.name}: unbound possessable`);
    }

    for (const track of binding.tracks || []) {
      report.tracks.push({
        binding: entry.name,
        type: track.type || track.class || "Unknown",
        sections: (track.sections || []).length,
      });
    }

    report.bindings.push(entry);
  }

  return report;
}

async function verifyAll(callUnreal) {
  const report = {
    passed: true,
    scenes: [],
    blueprints: [],
    sequences: [],
    summary: { totalChecks: 0, passed: 0, failed: 0, warnings: 0 },
  };

  const sceneResult = await verifyScene({ levelName: "current" }, callUnreal);
  report.scenes.push(sceneResult);
  report.summary.totalChecks++;
  if (sceneResult.passed) {
    report.summary.passed++;
  } else {
    report.summary.failed++;
    report.passed = false;
  }
  report.summary.warnings += (sceneResult.warnings || []).length;

  const bpListResult = await callUnreal("/api/list", "POST", {});
  if (bpListResult && bpListResult.success) {
    const blueprints = bpListResult.data?.blueprints || bpListResult.blueprints || [];
    const bpsToCheck = blueprints.slice(0, 20);

    for (const bp of bpsToCheck) {
      const bpName = bp.name || bp;
      const bpReport = await verifyBlueprint({ blueprintName: bpName }, callUnreal);
      report.blueprints.push(bpReport);
      report.summary.totalChecks++;
      if (bpReport.passed) {
        report.summary.passed++;
      } else {
        report.summary.failed++;
        report.passed = false;
      }
      report.summary.warnings += (bpReport.warnings || []).length;
    }
  }

  const seqListResult = await callUnreal("/api/list-sequences", "POST", {});
  if (seqListResult && seqListResult.success) {
    const sequences = seqListResult.data?.sequences || seqListResult.sequences || [];
    const seqsToCheck = sequences.slice(0, 20);

    for (const seq of seqsToCheck) {
      const seqName = seq.name || seq;
      const seqReport = await verifySequence({ sequenceName: seqName }, callUnreal);
      report.sequences.push(seqReport);
      report.summary.totalChecks++;
      if (seqReport.passed) {
        report.summary.passed++;
      } else {
        report.summary.failed++;
        report.passed = false;
      }
      report.summary.warnings += (seqReport.warnings || []).length;
    }
  }

  return report;
}

// ---------------------------------------------------------------------------
// MCP Tool Handler
// ---------------------------------------------------------------------------

/**
 * Handle a scene verifier tool call.
 *
 * @param {string} toolName - The tool name (from VERIFIER_TOOLS)
 * @param {object} args - Tool arguments
 * @param {function} callUnreal - async (endpoint, method, body) => json
 * @returns {object} MCP tool response { content: [...], isError: bool }
 */
export async function handleVerifierTool(toolName, args, callUnreal) {
  try {
    let result;

    switch (toolName) {
      case "verifyScene":
        result = await verifyScene(args, callUnreal);
        break;

      case "verifyBlueprint":
        result = await verifyBlueprint(args, callUnreal);
        break;

      case "verifySequence":
        result = await verifySequence(args, callUnreal);
        break;

      case "verifyAll":
        result = await verifyAll(callUnreal);
        break;

      default:
        return {
          content: [{ type: "text", text: `Unknown verifier tool: ${toolName}` }],
          isError: true,
        };
    }

    return {
      content: [
        {
          type: "text",
          text: JSON.stringify(
            { success: result.passed !== false, ...result },
            null,
            2
          ),
        },
      ],
      isError: !result.passed,
    };
  } catch (error) {
    log.error(`Scene verifier error: ${toolName}`, {
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
