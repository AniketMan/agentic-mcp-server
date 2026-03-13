/**
 * Idempotency Guard for AgenticMCP
 * ==================================
 *
 * Checks if the intended result of a mutation already exists in the project.
 * If it does, the mutation is SKIPPED (not blocked) and Claude receives a
 * "SKIPPED: already done" response. This makes the entire pipeline resumable:
 *
 *   - Claude can crash, restart, and re-run the same commands
 *   - Completed work is never duplicated
 *   - Progress is preserved across sessions
 *
 * Architecture:
 *   - Sits between the project-state validator and the executor in index.js
 *   - Uses the same read-only tools as the validator to query current state
 *   - Each mutation type has an idempotency check that answers:
 *     "Does the thing I'm about to create/modify already exist in the
 *      exact state I want it in?"
 *   - If YES -> skip, return what already exists
 *   - If NO  -> proceed to execution
 *
 * This is NOT a cache. It queries live project state every time.
 * Accuracy at all costs.
 *
 * Author: JARVIS
 * Date: March 2026
 */

import { log } from "./lib.js";

// ---------------------------------------------------------------------------
// Idempotency Check Definitions
// ---------------------------------------------------------------------------
// Each mutation tool maps to a function that:
//   - Takes (args, executeReadTool) 
//   - Returns { alreadyDone: true/false, existing: <what exists>, reason: <why skipped> }
//
// If alreadyDone is true, the mutation is skipped.
// The 'existing' field is returned to Claude so it knows what's already there.

const IDEMPOTENCY_CHECKS = {

  // =========================================================================
  // Blueprint Node Operations
  // =========================================================================

  addNode: async (args, readTool) => {
    // Check if a node of the same class already exists in the graph
    // at the same position (or with the same function target)
    const bp = args.blueprintName || args.name || args.blueprint;
    const graph = args.graphName || "EventGraph";
    const nodeClass = args.nodeClass || args.className || "";
    const memberName = args.memberName || args.functionName || args.functionReference || "";

    if (!bp || !nodeClass) return { alreadyDone: false };

    try {
      const graphResult = await readTool("graph", {
        blueprintName: bp,
        graphName: graph,
      });

      if (!graphResult || !graphResult.success || !graphResult.nodes) {
        return { alreadyDone: false };
      }

      // Look for a node that matches class AND function/member name
      // This prevents duplicating "CallFunction: PlayAnimation" but allows
      // multiple nodes of the same class with different targets
      for (const node of graphResult.nodes) {
        const classMatch =
          node.class === nodeClass ||
          node.className === nodeClass ||
          (node.type || "").includes(nodeClass);

        if (!classMatch) continue;

        // If a memberName/functionName is specified, also match on that
        if (memberName) {
          const memberMatch =
            (node.memberName || "") === memberName ||
            (node.functionName || "") === memberName ||
            (node.functionReference || "") === memberName ||
            (node.title || "").includes(memberName) ||
            (node.name || "").includes(memberName);

          if (classMatch && memberMatch) {
            return {
              alreadyDone: true,
              existing: {
                nodeId: node.id || node.name,
                nodeClass: node.class || node.className,
                memberName: node.memberName || node.functionName || node.title,
                position: node.position,
              },
              reason: `Node '${nodeClass}' targeting '${memberName}' already exists ` +
                `in ${bp}/${graph} as '${node.id || node.name}'`,
            };
          }
        } else {
          // No memberName specified -- match on class + approximate position
          if (args.posX !== undefined && args.posY !== undefined && node.position) {
            const dx = Math.abs((node.position.x || node.position.X || 0) - args.posX);
            const dy = Math.abs((node.position.y || node.position.Y || 0) - args.posY);
            // Within 50 units = same node placement
            if (dx < 50 && dy < 50) {
              return {
                alreadyDone: true,
                existing: {
                  nodeId: node.id || node.name,
                  nodeClass: node.class || node.className,
                  position: node.position,
                },
                reason: `Node '${nodeClass}' already exists at approximately the same ` +
                  `position in ${bp}/${graph}`,
              };
            }
          }
        }
      }

      return { alreadyDone: false };
    } catch (e) {
      log.debug("Idempotency check failed for addNode, proceeding", { error: e.message });
      return { alreadyDone: false };
    }
  },

  connectPins: async (args, readTool) => {
    // Check if the two pins are already connected
    const bp = args.blueprintName || args.blueprint;
    const graph = args.graphName || "EventGraph";
    const sourceNode = args.sourceNode || args.sourceNodeName;
    const sourcePin = args.sourcePin || args.sourcePinName;
    const targetNode = args.targetNode || args.targetNodeName;
    const targetPin = args.targetPin || args.targetPinName;

    if (!bp || !sourceNode || !targetNode) return { alreadyDone: false };

    try {
      // Check source pin connections
      const pinResult = await readTool("pinInfo", {
        blueprintName: bp,
        graphName: graph,
        nodeName: sourceNode,
      });

      if (!pinResult || !pinResult.success || !pinResult.pins) {
        return { alreadyDone: false };
      }

      const pin = pinResult.pins.find(
        (p) => p.name === sourcePin || p.pinName === sourcePin
      );

      if (pin && pin.connections && pin.connections.length > 0) {
        // Check if already connected to the target
        const alreadyConnected = pin.connections.some((conn) => {
          const connNode = conn.node || conn.nodeName || conn.nodeId || "";
          const connPin = conn.pin || conn.pinName || "";
          return (
            (connNode === targetNode || connNode.includes(targetNode)) &&
            (connPin === targetPin || connPin.includes(targetPin))
          );
        });

        if (alreadyConnected) {
          return {
            alreadyDone: true,
            existing: {
              sourceNode,
              sourcePin,
              targetNode,
              targetPin,
              connections: pin.connections,
            },
            reason: `Pin '${sourcePin}' on '${sourceNode}' is already connected to ` +
              `'${targetPin}' on '${targetNode}'`,
          };
        }
      }

      return { alreadyDone: false };
    } catch (e) {
      log.debug("Idempotency check failed for connectPins, proceeding", { error: e.message });
      return { alreadyDone: false };
    }
  },

  disconnectPin: async (args, readTool) => {
    // Check if the pin is already disconnected
    const bp = args.blueprintName || args.blueprint;
    const graph = args.graphName || "EventGraph";
    const nodeName = args.nodeName || args.nodeId;
    const pinName = args.pinName || args.pin;

    if (!bp || !nodeName || !pinName) return { alreadyDone: false };

    try {
      const pinResult = await readTool("pinInfo", {
        blueprintName: bp,
        graphName: graph,
        nodeName: nodeName,
      });

      if (!pinResult || !pinResult.success || !pinResult.pins) {
        return { alreadyDone: false };
      }

      const pin = pinResult.pins.find(
        (p) => p.name === pinName || p.pinName === pinName
      );

      if (pin && (!pin.connections || pin.connections.length === 0)) {
        return {
          alreadyDone: true,
          existing: { nodeName, pinName, connections: [] },
          reason: `Pin '${pinName}' on '${nodeName}' is already disconnected`,
        };
      }

      return { alreadyDone: false };
    } catch (e) {
      log.debug("Idempotency check failed for disconnectPin, proceeding", { error: e.message });
      return { alreadyDone: false };
    }
  },

  setPinDefault: async (args, readTool) => {
    // Check if the pin already has the desired default value
    const bp = args.blueprintName || args.blueprint;
    const graph = args.graphName || "EventGraph";
    const nodeName = args.nodeName || args.nodeId;
    const pinName = args.pinName || args.pin;
    const desiredValue = args.value || args.defaultValue;

    if (!bp || !nodeName || !pinName || desiredValue === undefined) {
      return { alreadyDone: false };
    }

    try {
      const pinResult = await readTool("pinInfo", {
        blueprintName: bp,
        graphName: graph,
        nodeName: nodeName,
      });

      if (!pinResult || !pinResult.success || !pinResult.pins) {
        return { alreadyDone: false };
      }

      const pin = pinResult.pins.find(
        (p) => p.name === pinName || p.pinName === pinName
      );

      if (pin) {
        const currentValue = pin.defaultValue || pin.default || pin.value;
        // String comparison to handle type differences
        if (String(currentValue) === String(desiredValue)) {
          return {
            alreadyDone: true,
            existing: { nodeName, pinName, currentValue },
            reason: `Pin '${pinName}' on '${nodeName}' already has value '${desiredValue}'`,
          };
        }
      }

      return { alreadyDone: false };
    } catch (e) {
      log.debug("Idempotency check failed for setPinDefault, proceeding", { error: e.message });
      return { alreadyDone: false };
    }
  },

  deleteNode: async (args, readTool) => {
    // Check if the node is already gone
    const bp = args.blueprintName || args.name || args.blueprint;
    const graph = args.graphName || "EventGraph";
    const nodeId = args.nodeId || args.nodeName;

    if (!bp || !nodeId) return { alreadyDone: false };

    try {
      const graphResult = await readTool("graph", {
        blueprintName: bp,
        graphName: graph,
      });

      if (!graphResult || !graphResult.success || !graphResult.nodes) {
        return { alreadyDone: false };
      }

      const found = graphResult.nodes.some(
        (n) => n.id === nodeId || n.name === nodeId || n.title === nodeId
      );

      if (!found) {
        return {
          alreadyDone: true,
          existing: null,
          reason: `Node '${nodeId}' does not exist in ${bp}/${graph} -- already deleted or never created`,
        };
      }

      return { alreadyDone: false };
    } catch (e) {
      log.debug("Idempotency check failed for deleteNode, proceeding", { error: e.message });
      return { alreadyDone: false };
    }
  },

  // =========================================================================
  // Blueprint Structure Operations
  // =========================================================================

  createBlueprint: async (args, readTool) => {
    // Check if the Blueprint already exists
    const name = args.name || args.blueprintName;
    const path = args.path || "/Game/";

    if (!name) return { alreadyDone: false };

    try {
      const listResult = await readTool("list", { path, type: "Blueprint" });

      if (listResult && listResult.success && listResult.assets) {
        const exists = listResult.assets.some(
          (a) => (a.name || "").toLowerCase() === name.toLowerCase()
        );
        if (exists) {
          return {
            alreadyDone: true,
            existing: listResult.assets.find(
              (a) => (a.name || "").toLowerCase() === name.toLowerCase()
            ),
            reason: `Blueprint '${name}' already exists at '${path}'`,
          };
        }
      }

      return { alreadyDone: false };
    } catch (e) {
      log.debug("Idempotency check failed for createBlueprint, proceeding", { error: e.message });
      return { alreadyDone: false };
    }
  },

  createGraph: async (args, readTool) => {
    // Check if the graph already exists in the Blueprint
    const bp = args.blueprintName || args.name || args.blueprint;
    const graphName = args.graphName || args.graph;

    if (!bp || !graphName) return { alreadyDone: false };

    try {
      const bpResult = await readTool("blueprint", { blueprintName: bp });

      if (bpResult && bpResult.success && bpResult.graphs) {
        const exists = bpResult.graphs.some(
          (g) => (g.name || "").toLowerCase() === graphName.toLowerCase()
        );
        if (exists) {
          return {
            alreadyDone: true,
            existing: bpResult.graphs.find(
              (g) => (g.name || "").toLowerCase() === graphName.toLowerCase()
            ),
            reason: `Graph '${graphName}' already exists in Blueprint '${bp}'`,
          };
        }
      }

      return { alreadyDone: false };
    } catch (e) {
      log.debug("Idempotency check failed for createGraph, proceeding", { error: e.message });
      return { alreadyDone: false };
    }
  },

  deleteGraph: async (args, readTool) => {
    // Check if the graph is already gone
    const bp = args.blueprintName || args.name || args.blueprint;
    const graphName = args.graphName || args.graph;

    if (!bp || !graphName) return { alreadyDone: false };

    try {
      const bpResult = await readTool("blueprint", { blueprintName: bp });

      if (bpResult && bpResult.success && bpResult.graphs) {
        const exists = bpResult.graphs.some(
          (g) => (g.name || "").toLowerCase() === graphName.toLowerCase()
        );
        if (!exists) {
          return {
            alreadyDone: true,
            existing: null,
            reason: `Graph '${graphName}' does not exist in Blueprint '${bp}' -- already deleted`,
          };
        }
      }

      return { alreadyDone: false };
    } catch (e) {
      log.debug("Idempotency check failed for deleteGraph, proceeding", { error: e.message });
      return { alreadyDone: false };
    }
  },

  addVariable: async (args, readTool) => {
    // Check if the variable already exists with the same type
    const bp = args.blueprintName || args.name || args.blueprint;
    const varName = args.variableName || args.varName;
    const varType = args.variableType || args.type;

    if (!bp || !varName) return { alreadyDone: false };

    try {
      const bpResult = await readTool("blueprint", { blueprintName: bp });

      if (bpResult && bpResult.success && bpResult.variables) {
        const existing = bpResult.variables.find(
          (v) => (v.name || "").toLowerCase() === varName.toLowerCase()
        );
        if (existing) {
          // If type matches too, it's truly a duplicate
          const typeMatch = !varType ||
            (existing.type || "").toLowerCase() === varType.toLowerCase();
          if (typeMatch) {
            return {
              alreadyDone: true,
              existing,
              reason: `Variable '${varName}' (type: ${existing.type || "unknown"}) ` +
                `already exists in Blueprint '${bp}'`,
            };
          }
        }
      }

      return { alreadyDone: false };
    } catch (e) {
      log.debug("Idempotency check failed for addVariable, proceeding", { error: e.message });
      return { alreadyDone: false };
    }
  },

  removeVariable: async (args, readTool) => {
    // Check if the variable is already gone
    const bp = args.blueprintName || args.name || args.blueprint;
    const varName = args.variableName || args.varName;

    if (!bp || !varName) return { alreadyDone: false };

    try {
      const bpResult = await readTool("blueprint", { blueprintName: bp });

      if (bpResult && bpResult.success && bpResult.variables) {
        const exists = bpResult.variables.some(
          (v) => (v.name || "").toLowerCase() === varName.toLowerCase()
        );
        if (!exists) {
          return {
            alreadyDone: true,
            existing: null,
            reason: `Variable '${varName}' does not exist in Blueprint '${bp}' -- already removed`,
          };
        }
      }

      return { alreadyDone: false };
    } catch (e) {
      log.debug("Idempotency check failed for removeVariable, proceeding", { error: e.message });
      return { alreadyDone: false };
    }
  },

  // =========================================================================
  // Actor Operations
  // =========================================================================

  spawnActor: async (args, readTool) => {
    // Check if an actor with the same label/name already exists at the same location
    const actorName = args.actorLabel || args.actorName || args.label || args.name;
    const actorClass = args.actorClass || args.class || args.className;

    if (!actorName) return { alreadyDone: false };

    try {
      const actorsResult = await readTool("listActors", {});

      if (!actorsResult || !actorsResult.success || !actorsResult.actors) {
        return { alreadyDone: false };
      }

      const existing = actorsResult.actors.find(
        (a) =>
          (a.label || "").toLowerCase() === actorName.toLowerCase() ||
          (a.name || "").toLowerCase() === actorName.toLowerCase() ||
          (a.actorLabel || "").toLowerCase() === actorName.toLowerCase()
      );

      if (existing) {
        // If class also matches, it's the same actor
        const classMatch = !actorClass ||
          (existing.class || "").includes(actorClass) ||
          (existing.className || "").includes(actorClass);

        if (classMatch) {
          return {
            alreadyDone: true,
            existing: {
              name: existing.name || existing.label,
              class: existing.class || existing.className,
              location: existing.location || existing.transform,
            },
            reason: `Actor '${actorName}' (class: ${existing.class || actorClass}) ` +
              `already exists in the level`,
          };
        }
      }

      return { alreadyDone: false };
    } catch (e) {
      log.debug("Idempotency check failed for spawnActor, proceeding", { error: e.message });
      return { alreadyDone: false };
    }
  },

  deleteActor: async (args, readTool) => {
    // Check if the actor is already gone
    const actorName = args.actorName || args.actorLabel || args.name;

    if (!actorName) return { alreadyDone: false };

    try {
      const actorsResult = await readTool("listActors", {});

      if (!actorsResult || !actorsResult.success || !actorsResult.actors) {
        return { alreadyDone: false };
      }

      const found = actorsResult.actors.some(
        (a) =>
          (a.name || "").toLowerCase() === actorName.toLowerCase() ||
          (a.label || "").toLowerCase() === actorName.toLowerCase() ||
          (a.actorLabel || "").toLowerCase() === actorName.toLowerCase()
      );

      if (!found) {
        return {
          alreadyDone: true,
          existing: null,
          reason: `Actor '${actorName}' does not exist in the level -- already deleted or never spawned`,
        };
      }

      return { alreadyDone: false };
    } catch (e) {
      log.debug("Idempotency check failed for deleteActor, proceeding", { error: e.message });
      return { alreadyDone: false };
    }
  },

  setActorProperty: async (args, readTool) => {
    // Check if the property already has the desired value
    const actorName = args.actorName || args.actorLabel || args.name;
    const propName = args.propertyName || args.property;
    const desiredValue = args.value;

    if (!actorName || !propName || desiredValue === undefined) {
      return { alreadyDone: false };
    }

    try {
      const actorResult = await readTool("getActor", { actorName });

      if (!actorResult || !actorResult.success || !actorResult.properties) {
        return { alreadyDone: false };
      }

      // Case-insensitive property lookup
      const propKey = Object.keys(actorResult.properties).find(
        (k) => k.toLowerCase() === propName.toLowerCase()
      );

      if (propKey) {
        const currentValue = actorResult.properties[propKey];
        if (String(currentValue) === String(desiredValue)) {
          return {
            alreadyDone: true,
            existing: { actorName, property: propName, value: currentValue },
            reason: `Property '${propName}' on actor '${actorName}' already has value '${desiredValue}'`,
          };
        }
      }

      return { alreadyDone: false };
    } catch (e) {
      log.debug("Idempotency check failed for setActorProperty, proceeding", { error: e.message });
      return { alreadyDone: false };
    }
  },

  setActorTransform: async (args, readTool) => {
    // Check if the actor is already at the desired transform
    const actorName = args.actorName || args.actorLabel || args.name;
    const desiredLoc = args.location || args.position;

    if (!actorName || !desiredLoc) return { alreadyDone: false };

    try {
      const actorsResult = await readTool("listActors", {});

      if (!actorsResult || !actorsResult.success || !actorsResult.actors) {
        return { alreadyDone: false };
      }

      const actor = actorsResult.actors.find(
        (a) =>
          (a.name || "").toLowerCase() === actorName.toLowerCase() ||
          (a.label || "").toLowerCase() === actorName.toLowerCase()
      );

      if (actor && actor.location) {
        const dx = Math.abs((actor.location.x || actor.location.X || 0) - (desiredLoc.x || desiredLoc.X || 0));
        const dy = Math.abs((actor.location.y || actor.location.Y || 0) - (desiredLoc.y || desiredLoc.Y || 0));
        const dz = Math.abs((actor.location.z || actor.location.Z || 0) - (desiredLoc.z || desiredLoc.Z || 0));

        // Within 1 unit = same position (UE5 uses centimeters)
        if (dx < 1 && dy < 1 && dz < 1) {
          return {
            alreadyDone: true,
            existing: { actorName, location: actor.location },
            reason: `Actor '${actorName}' is already at the desired location`,
          };
        }
      }

      return { alreadyDone: false };
    } catch (e) {
      log.debug("Idempotency check failed for setActorTransform, proceeding", { error: e.message });
      return { alreadyDone: false };
    }
  },

  // =========================================================================
  // Level Sequence Operations
  // =========================================================================

  ls_create: async (args, readTool) => {
    // Check if the sequence already exists
    const name = args.name || args.sequenceName;
    const path = args.path || "/Game/";

    if (!name) return { alreadyDone: false };

    try {
      const listResult = await readTool("list", { path, type: "LevelSequence" });

      if (listResult && listResult.success && listResult.assets) {
        const existing = listResult.assets.find(
          (a) => (a.name || "").toLowerCase() === name.toLowerCase()
        );
        if (existing) {
          return {
            alreadyDone: true,
            existing,
            reason: `Level Sequence '${name}' already exists`,
          };
        }
      }

      return { alreadyDone: false };
    } catch (e) {
      log.debug("Idempotency check failed for ls_create, proceeding", { error: e.message });
      return { alreadyDone: false };
    }
  },

  ls_bind_actor: async (args, readTool) => {
    // Check if the actor is already bound to the sequence
    const seqName = args.sequenceName || args.sequence;
    const actorName = args.actorName || args.actor;

    if (!seqName || !actorName) return { alreadyDone: false };

    try {
      const bindingsResult = await readTool("ls_list_bindings", {
        sequenceName: seqName,
      });

      if (bindingsResult && bindingsResult.success && bindingsResult.bindings) {
        const alreadyBound = bindingsResult.bindings.some(
          (b) =>
            (b.actorName || "").toLowerCase() === actorName.toLowerCase() ||
            (b.name || "").toLowerCase() === actorName.toLowerCase() ||
            (b.actor || "").toLowerCase() === actorName.toLowerCase()
        );

        if (alreadyBound) {
          return {
            alreadyDone: true,
            existing: bindingsResult.bindings.find(
              (b) =>
                (b.actorName || "").toLowerCase() === actorName.toLowerCase() ||
                (b.name || "").toLowerCase() === actorName.toLowerCase()
            ),
            reason: `Actor '${actorName}' is already bound to sequence '${seqName}'`,
          };
        }
      }

      return { alreadyDone: false };
    } catch (e) {
      log.debug("Idempotency check failed for ls_bind_actor, proceeding", { error: e.message });
      return { alreadyDone: false };
    }
  },

  // compileBlueprint -- always run, never skip (compilation is always safe to re-run)
  // executePython -- always run, never skip (scripts may have side effects)
  // moveNode -- always run (position may need updating)
  // ls_add_track -- hard to check idempotently without deep track inspection
  // ls_add_section -- hard to check idempotently without deep section inspection
};

// ---------------------------------------------------------------------------
// Idempotency Check Execution
// ---------------------------------------------------------------------------

/**
 * Check if a mutation's intended result already exists in the project.
 *
 * @param {string} toolName - The mutation tool being called
 * @param {object} args - The tool arguments
 * @param {function} executeReadTool - Function to call read-only tools
 *   Signature: async (toolName, args) => result
 * @returns {Promise<object>}
 *   { alreadyDone: false } -- proceed with execution
 *   { alreadyDone: true, existing: <data>, reason: <string> } -- skip execution
 */
export async function checkIdempotency(toolName, args, executeReadTool) {
  const check = IDEMPOTENCY_CHECKS[toolName];

  // No idempotency check defined for this tool -- proceed
  if (!check) {
    return { alreadyDone: false };
  }

  try {
    const result = await check(args, executeReadTool);

    if (result.alreadyDone) {
      log.info("Idempotency check: SKIPPING (already done)", {
        tool: toolName,
        reason: result.reason,
      });
    }

    return result;
  } catch (error) {
    // If the idempotency check itself fails, proceed with execution
    // (fail-open -- we'd rather duplicate than block)
    log.error("Idempotency check failed, proceeding with execution", {
      tool: toolName,
      error: error.message,
    });
    return { alreadyDone: false };
  }
}

/**
 * Format a "skipped" response for Claude when a mutation is idempotent.
 *
 * @param {string} toolName - The skipped tool
 * @param {object} args - The tool arguments
 * @param {object} idempotencyResult - Result from checkIdempotency
 * @returns {object} Formatted response for MCP
 */
export function formatSkippedResponse(toolName, args, idempotencyResult) {
  return {
    skipped: true,
    reason: "ALREADY_DONE",
    tool: toolName,
    message: idempotencyResult.reason,
    existing: idempotencyResult.existing,
    action: "No mutation was executed. The intended result already exists in the project. " +
      "Proceed to the next step.",
  };
}

// ---------------------------------------------------------------------------
// Exports
// ---------------------------------------------------------------------------

export { IDEMPOTENCY_CHECKS };
