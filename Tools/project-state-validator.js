/**
 * Project State Validator for AgenticMCP
 * ========================================
 *
 * Cross-validates every mutation against the live UE5 project state before
 * allowing execution. Accuracy at all costs.
 *
 * Architecture:
 *   - Before any mutation fires, the validator makes read-only calls to the
 *     UE5 editor to verify that what Claude claims exists actually exists.
 *   - If the project state doesn't match, the mutation is blocked and Claude
 *     receives a detailed diff (what it claimed vs what actually exists) plus
 *     the specific read-only tool to call to get the real data, and the
 *     relevant engine doc path.
 *   - Claude reads the real data, adjusts, and retries with a call that
 *     aligns with the actual project.
 *
 * This turns the confidence gate from a score-based system into a
 * ground-truth validator. Claude cannot hallucinate assets that don't exist.
 *
 * Author: JARVIS
 * Date: March 2026
 */

import { log } from "./lib.js";
import { readFileSync, existsSync } from "fs";
import { join, dirname } from "path";
import { fileURLToPath } from "url";

const __dirname = dirname(fileURLToPath(import.meta.url));

// Engine documentation path (relative to engine root)
const ENGINE_DOCS_RELATIVE_PATH = "Engine/Documentation/Builds";

// ---------------------------------------------------------------------------
// Validation Rule Definitions
// ---------------------------------------------------------------------------
// Each mutation tool maps to a set of validation checks.
// Each check specifies:
//   - readTool: the read-only MCP tool to call for verification
//   - buildArgs: function that extracts the read-tool args from the mutation args
//   - validate: function that checks the read-tool result against the mutation args
//   - failMessage: function that builds a human-readable error when validation fails
//   - docCategory: relevant Blueprint API category for the engine docs

const VALIDATION_RULES = {

  // =========================================================================
  // Blueprint Node Operations
  // =========================================================================

  addNode: [
    {
      name: "blueprint_exists",
      description: "Verify the target Blueprint exists in the project",
      readTool: "blueprint",
      buildArgs: (args) => ({
        blueprintName: args.blueprintName || args.name || args.blueprint,
      }),
      validate: (readResult, args) => {
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: args.blueprintName || args.name || args.blueprint,
            actual: "Blueprint not found",
            fix: `Call unreal_list to search for Blueprints matching this name. ` +
              `The Blueprint may have a different name or path than expected.`,
          };
        }
        return { valid: true };
      },
      docCategory: "Blueprint",
    },
    {
      name: "graph_exists",
      description: "Verify the target graph exists within the Blueprint",
      readTool: "graph",
      buildArgs: (args) => ({
        blueprintName: args.blueprintName || args.name || args.blueprint,
        graphName: args.graphName || "EventGraph",
      }),
      validate: (readResult, args) => {
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: args.graphName || "EventGraph",
            actual: "Graph not found in Blueprint",
            fix: `Call unreal_blueprint('${args.blueprintName || args.name}') to list ` +
              `all available graphs, then use the correct graph name.`,
          };
        }
        return { valid: true };
      },
      docCategory: "AnimationGraph",
    },
    {
      name: "node_class_valid",
      description: "Verify the node class is a valid UE5 node type",
      readTool: "listClasses",
      buildArgs: (args) => ({
        search: args.nodeClass || args.className || "",
      }),
      validate: (readResult, args) => {
        const nodeClass = args.nodeClass || args.className || "";
        if (!nodeClass) {
          return {
            valid: false,
            claimed: "(empty)",
            actual: "No node class specified",
            fix: `Specify a valid nodeClass. Call unreal_listClasses to search ` +
              `for available node types.`,
          };
        }
        // If listClasses returned results, check if the class is in there
        if (readResult && readResult.success && readResult.classes) {
          const found = readResult.classes.some(
            (c) => c.name === nodeClass || c.className === nodeClass
          );
          if (!found) {
            return {
              valid: false,
              claimed: nodeClass,
              actual: `Class '${nodeClass}' not found in available classes`,
              fix: `Call unreal_listClasses(search: '${nodeClass.replace(/^K2Node_/, "")}') ` +
                `to find the correct class name. Common UE5 node classes: ` +
                `K2Node_CallFunction, K2Node_Event, K2Node_IfThenElse, ` +
                `K2Node_DynamicCast, K2Node_VariableGet, K2Node_VariableSet.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Blueprint",
    },
  ],

  deleteNode: [
    {
      name: "node_exists",
      description: "Verify the node exists in the graph before deletion",
      readTool: "graph",
      buildArgs: (args) => ({
        blueprintName: args.blueprintName || args.name || args.blueprint,
        graphName: args.graphName || "EventGraph",
      }),
      validate: (readResult, args) => {
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: `Node '${args.nodeId || args.nodeName}' in graph`,
            actual: "Graph not found or inaccessible",
            fix: `Call unreal_blueprint to verify the Blueprint exists, then ` +
              `unreal_graph to list all nodes in the graph.`,
          };
        }
        // Check if the specific node exists
        const nodeId = args.nodeId || args.nodeName;
        if (nodeId && readResult.nodes) {
          const found = readResult.nodes.some(
            (n) => n.id === nodeId || n.name === nodeId || n.title === nodeId
          );
          if (!found) {
            const availableNodes = (readResult.nodes || [])
              .slice(0, 10)
              .map((n) => n.name || n.title || n.id)
              .join(", ");
            return {
              valid: false,
              claimed: nodeId,
              actual: `Node not found. Available nodes: ${availableNodes}`,
              fix: `Call unreal_graph to get the current list of nodes. ` +
                `The node may have been renamed or already deleted.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Blueprint",
    },
  ],

  connectPins: [
    {
      name: "source_node_exists",
      description: "Verify the source node exists",
      readTool: "graph",
      buildArgs: (args) => ({
        blueprintName: args.blueprintName || args.blueprint,
        graphName: args.graphName || "EventGraph",
      }),
      validate: (readResult, args) => {
        if (!readResult || !readResult.success || !readResult.nodes) {
          return {
            valid: false,
            claimed: `Source node '${args.sourceNode || args.sourceNodeName}'`,
            actual: "Graph not found or has no nodes",
            fix: `Call unreal_graph to verify the graph exists and list its nodes.`,
          };
        }
        const sourceNode = args.sourceNode || args.sourceNodeName;
        if (sourceNode) {
          const found = readResult.nodes.some(
            (n) => n.id === sourceNode || n.name === sourceNode || n.title === sourceNode
          );
          if (!found) {
            return {
              valid: false,
              claimed: sourceNode,
              actual: `Source node not found in graph`,
              fix: `Call unreal_graph to list all nodes. Available: ` +
                readResult.nodes.slice(0, 8).map((n) => n.name || n.id).join(", "),
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Blueprint",
    },
    {
      name: "target_node_exists",
      description: "Verify the target node exists",
      readTool: "graph",
      buildArgs: (args) => ({
        blueprintName: args.blueprintName || args.blueprint,
        graphName: args.graphName || "EventGraph",
      }),
      validate: (readResult, args) => {
        if (!readResult || !readResult.success || !readResult.nodes) {
          return { valid: true }; // Already caught by source_node_exists
        }
        const targetNode = args.targetNode || args.targetNodeName;
        if (targetNode) {
          const found = readResult.nodes.some(
            (n) => n.id === targetNode || n.name === targetNode || n.title === targetNode
          );
          if (!found) {
            return {
              valid: false,
              claimed: targetNode,
              actual: `Target node not found in graph`,
              fix: `Call unreal_graph to list all nodes. Available: ` +
                readResult.nodes.slice(0, 8).map((n) => n.name || n.id).join(", "),
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Blueprint",
    },
    {
      name: "pins_exist",
      description: "Verify both source and target pins exist and are compatible",
      readTool: "pinInfo",
      buildArgs: (args) => ({
        blueprintName: args.blueprintName || args.blueprint,
        graphName: args.graphName || "EventGraph",
        nodeName: args.sourceNode || args.sourceNodeName,
      }),
      validate: (readResult, args) => {
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: `Pin '${args.sourcePin || args.sourcePinName}'`,
            actual: "Could not retrieve pin info for source node",
            fix: `Call unreal_pinInfo for the source node to list available pins.`,
          };
        }
        const sourcePin = args.sourcePin || args.sourcePinName;
        if (sourcePin && readResult.pins) {
          const found = readResult.pins.some(
            (p) => p.name === sourcePin || p.displayName === sourcePin
          );
          if (!found) {
            const availablePins = (readResult.pins || [])
              .map((p) => `${p.name}(${p.type || p.category || "?"})`)
              .join(", ");
            return {
              valid: false,
              claimed: sourcePin,
              actual: `Pin not found on source node. Available pins: ${availablePins}`,
              fix: `Call unreal_pinInfo for the source node. Pin names are ` +
                `case-sensitive and may differ from what you expect.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Blueprint",
    },
  ],

  disconnectPin: [
    {
      name: "node_and_pin_exist",
      description: "Verify the node and pin exist before disconnecting",
      readTool: "pinInfo",
      buildArgs: (args) => ({
        blueprintName: args.blueprintName || args.blueprint,
        graphName: args.graphName || "EventGraph",
        nodeName: args.nodeName || args.nodeId,
      }),
      validate: (readResult, args) => {
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: `Pin '${args.pinName}' on node '${args.nodeName || args.nodeId}'`,
            actual: "Node not found or pin info unavailable",
            fix: `Call unreal_graph to verify the node exists, then unreal_pinInfo ` +
              `to list its pins.`,
          };
        }
        const pinName = args.pinName;
        if (pinName && readResult.pins) {
          const pin = readResult.pins.find(
            (p) => p.name === pinName || p.displayName === pinName
          );
          if (!pin) {
            return {
              valid: false,
              claimed: pinName,
              actual: `Pin not found. Available: ` +
                readResult.pins.map((p) => p.name).join(", "),
              fix: `Call unreal_pinInfo to get the exact pin names.`,
            };
          }
          if (pin && !pin.connected && !pin.linkedTo?.length) {
            return {
              valid: false,
              claimed: `Pin '${pinName}' is connected`,
              actual: `Pin '${pinName}' is not connected to anything`,
              fix: `This pin has no connections to disconnect. Call unreal_pinInfo ` +
                `to see which pins are actually connected.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Blueprint",
    },
  ],

  setPinDefault: [
    {
      name: "pin_exists_and_type_matches",
      description: "Verify the pin exists and the value type is compatible",
      readTool: "pinInfo",
      buildArgs: (args) => ({
        blueprintName: args.blueprintName || args.blueprint,
        graphName: args.graphName || "EventGraph",
        nodeName: args.nodeName || args.nodeId,
      }),
      validate: (readResult, args) => {
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: `Pin '${args.pinName}' on node '${args.nodeName || args.nodeId}'`,
            actual: "Node not found or pin info unavailable",
            fix: `Call unreal_graph to verify the node, then unreal_pinInfo for pins.`,
          };
        }
        const pinName = args.pinName;
        if (pinName && readResult.pins) {
          const pin = readResult.pins.find(
            (p) => p.name === pinName || p.displayName === pinName
          );
          if (!pin) {
            return {
              valid: false,
              claimed: pinName,
              actual: `Pin not found. Available: ` +
                readResult.pins.map((p) => `${p.name}(${p.type || "?"})`).join(", "),
              fix: `Call unreal_pinInfo to get exact pin names and types.`,
            };
          }
          // Check if pin is an input (can have defaults set)
          if (pin.direction === "output" || pin.direction === "Output") {
            return {
              valid: false,
              claimed: `Setting default on '${pinName}'`,
              actual: `Pin '${pinName}' is an output pin -- defaults can only be set on input pins`,
              fix: `Only input pins accept default values. Check pin direction with unreal_pinInfo.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Blueprint",
    },
  ],

  moveNode: [
    {
      name: "node_exists",
      description: "Verify the node exists before moving",
      readTool: "graph",
      buildArgs: (args) => ({
        blueprintName: args.blueprintName || args.blueprint,
        graphName: args.graphName || "EventGraph",
      }),
      validate: (readResult, args) => {
        const nodeId = args.nodeId || args.nodeName;
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: nodeId,
            actual: "Graph not found",
            fix: `Call unreal_graph to verify the graph and list nodes.`,
          };
        }
        if (nodeId && readResult.nodes) {
          const found = readResult.nodes.some(
            (n) => n.id === nodeId || n.name === nodeId
          );
          if (!found) {
            return {
              valid: false,
              claimed: nodeId,
              actual: "Node not found in graph",
              fix: `Call unreal_graph to list available nodes.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Blueprint",
    },
  ],

  // =========================================================================
  // Blueprint CRUD Operations
  // =========================================================================

  createBlueprint: [
    {
      name: "blueprint_does_not_exist",
      description: "Verify the Blueprint does NOT already exist (prevent duplicates)",
      readTool: "list",
      buildArgs: (args) => ({
        path: args.path || "/Game/",
        type: "Blueprint",
      }),
      validate: (readResult, args) => {
        const targetName = args.name || args.blueprintName;
        if (readResult && readResult.success && readResult.assets) {
          const exists = readResult.assets.some(
            (a) => (a.name || "").toLowerCase() === (targetName || "").toLowerCase()
          );
          if (exists) {
            return {
              valid: false,
              claimed: `Creating new Blueprint '${targetName}'`,
              actual: `Blueprint '${targetName}' already exists at this path`,
              fix: `A Blueprint with this name already exists. Use unreal_blueprint ` +
                `to inspect it, or choose a different name.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Blueprint",
    },
  ],

  createGraph: [
    {
      name: "blueprint_exists_graph_does_not",
      description: "Verify Blueprint exists and graph name is not taken",
      readTool: "blueprint",
      buildArgs: (args) => ({
        blueprintName: args.blueprintName || args.name || args.blueprint,
      }),
      validate: (readResult, args) => {
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: args.blueprintName || args.name,
            actual: "Blueprint not found",
            fix: `Call unreal_list to find the correct Blueprint name.`,
          };
        }
        const graphName = args.graphName || args.graph;
        if (graphName && readResult.graphs) {
          const exists = readResult.graphs.some(
            (g) => (g.name || "").toLowerCase() === graphName.toLowerCase()
          );
          if (exists) {
            return {
              valid: false,
              claimed: `Creating graph '${graphName}'`,
              actual: `Graph '${graphName}' already exists in this Blueprint`,
              fix: `Use unreal_graph to inspect the existing graph, or choose a different name.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Blueprint",
    },
  ],

  deleteGraph: [
    {
      name: "graph_exists",
      description: "Verify the graph exists before deletion",
      readTool: "blueprint",
      buildArgs: (args) => ({
        blueprintName: args.blueprintName || args.name || args.blueprint,
      }),
      validate: (readResult, args) => {
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: args.blueprintName || args.name,
            actual: "Blueprint not found",
            fix: `Call unreal_list to find the correct Blueprint name.`,
          };
        }
        const graphName = args.graphName || args.graph;
        if (graphName && readResult.graphs) {
          const exists = readResult.graphs.some(
            (g) => (g.name || "").toLowerCase() === graphName.toLowerCase()
          );
          if (!exists) {
            const available = readResult.graphs.map((g) => g.name).join(", ");
            return {
              valid: false,
              claimed: graphName,
              actual: `Graph not found. Available graphs: ${available}`,
              fix: `Call unreal_blueprint to list all graphs in this Blueprint.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Blueprint",
    },
  ],

  addVariable: [
    {
      name: "blueprint_exists_var_unique",
      description: "Verify Blueprint exists and variable name is not taken",
      readTool: "blueprint",
      buildArgs: (args) => ({
        blueprintName: args.blueprintName || args.name || args.blueprint,
      }),
      validate: (readResult, args) => {
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: args.blueprintName || args.name,
            actual: "Blueprint not found",
            fix: `Call unreal_list to find the correct Blueprint name.`,
          };
        }
        const varName = args.variableName || args.varName;
        if (varName && readResult.variables) {
          const exists = readResult.variables.some(
            (v) => (v.name || "").toLowerCase() === varName.toLowerCase()
          );
          if (exists) {
            return {
              valid: false,
              claimed: `Adding variable '${varName}'`,
              actual: `Variable '${varName}' already exists`,
              fix: `Use unreal_blueprint to see existing variables. Choose a unique name.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Blueprint",
    },
  ],

  removeVariable: [
    {
      name: "variable_exists",
      description: "Verify the variable exists before removal",
      readTool: "blueprint",
      buildArgs: (args) => ({
        blueprintName: args.blueprintName || args.name || args.blueprint,
      }),
      validate: (readResult, args) => {
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: args.blueprintName || args.name,
            actual: "Blueprint not found",
            fix: `Call unreal_list to find the correct Blueprint name.`,
          };
        }
        const varName = args.variableName || args.varName;
        if (varName && readResult.variables) {
          const exists = readResult.variables.some(
            (v) => (v.name || "").toLowerCase() === varName.toLowerCase()
          );
          if (!exists) {
            const available = readResult.variables.map((v) => v.name).join(", ");
            return {
              valid: false,
              claimed: varName,
              actual: `Variable not found. Available: ${available}`,
              fix: `Call unreal_blueprint to list all variables.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Blueprint",
    },
  ],

  compileBlueprint: [
    {
      name: "blueprint_exists",
      description: "Verify the Blueprint exists before compilation",
      readTool: "blueprint",
      buildArgs: (args) => ({
        blueprintName: args.blueprintName || args.name || args.blueprint,
      }),
      validate: (readResult, args) => {
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: args.blueprintName || args.name || args.blueprint,
            actual: "Blueprint not found",
            fix: `Call unreal_list to search for the correct Blueprint name.`,
          };
        }
        return { valid: true };
      },
      docCategory: "Blueprint",
    },
  ],

  // =========================================================================
  // Actor Operations
  // =========================================================================

  spawnActor: [
    {
      name: "level_loaded",
      description: "Verify a level is loaded before spawning",
      readTool: "listLevels",
      buildArgs: () => ({}),
      validate: (readResult) => {
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: "Spawning actor in current level",
            actual: "No level loaded or level info unavailable",
            fix: `Call unreal_loadLevel to load the target level first, ` +
              `then call unreal_listLevels to confirm it's loaded.`,
          };
        }
        if (readResult.levels && readResult.levels.length === 0) {
          return {
            valid: false,
            claimed: "Spawning actor in current level",
            actual: "No levels are currently loaded",
            fix: `Call unreal_loadLevel to load the target level first.`,
          };
        }
        return { valid: true };
      },
      docCategory: "Actor",
    },
    {
      name: "actor_class_valid",
      description: "Verify the actor class is valid",
      readTool: "listClasses",
      buildArgs: (args) => ({
        search: args.actorClass || args.class || args.className || "",
      }),
      validate: (readResult, args) => {
        const actorClass = args.actorClass || args.class || args.className;
        if (!actorClass) {
          return {
            valid: false,
            claimed: "(empty)",
            actual: "No actor class specified",
            fix: `Specify an actorClass. Common actor classes: StaticMeshActor, ` +
              `PointLight, TriggerBox, CameraActor, PlayerStart.`,
          };
        }
        if (readResult && readResult.success && readResult.classes) {
          const found = readResult.classes.some(
            (c) => c.name === actorClass || c.className === actorClass
          );
          if (!found) {
            return {
              valid: false,
              claimed: actorClass,
              actual: `Actor class '${actorClass}' not found`,
              fix: `Call unreal_listClasses(search: '${actorClass}') to find ` +
                `the correct class name.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Actor",
    },
    {
      name: "transform_sane",
      description: "Verify transform values are within reasonable bounds",
      readTool: null, // No read call needed -- pure validation
      buildArgs: () => null,
      validate: (_, args) => {
        const loc = args.location || args.position;
        if (loc) {
          const coords = [loc.x || loc.X, loc.y || loc.Y, loc.z || loc.Z].filter(
            (v) => v !== undefined
          );
          for (const c of coords) {
            if (typeof c === "number" && (Math.abs(c) > 1000000)) {
              return {
                valid: false,
                claimed: `Location: (${coords.join(", ")})`,
                actual: `Coordinate value ${c} is extremely large (>1M units)`,
                fix: `UE5 uses centimeters. A typical room is ~500-1000 units. ` +
                  `Call unreal_listActors to see existing actor positions for reference.`,
              };
            }
          }
        }
        return { valid: true };
      },
      docCategory: "Actor",
    },
  ],

  deleteActor: [
    {
      name: "actor_exists",
      description: "Verify the actor exists before deletion",
      readTool: "listActors",
      buildArgs: () => ({}),
      validate: (readResult, args) => {
        const actorName = args.actorName || args.actorLabel || args.name;
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: actorName,
            actual: "Could not retrieve actor list",
            fix: `Call unreal_listActors to verify the level is loaded and actors exist.`,
          };
        }
        if (actorName && readResult.actors) {
          const found = readResult.actors.some(
            (a) =>
              (a.name || "").toLowerCase() === actorName.toLowerCase() ||
              (a.label || "").toLowerCase() === actorName.toLowerCase() ||
              (a.actorLabel || "").toLowerCase() === actorName.toLowerCase()
          );
          if (!found) {
            const available = (readResult.actors || [])
              .slice(0, 15)
              .map((a) => a.label || a.name)
              .join(", ");
            return {
              valid: false,
              claimed: actorName,
              actual: `Actor not found. Some actors in level: ${available}`,
              fix: `Call unreal_listActors to get the full list. Actor names ` +
                `are case-sensitive.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Actor",
    },
  ],

  setActorProperty: [
    {
      name: "actor_exists",
      description: "Verify the actor exists before setting property",
      readTool: "getActor",
      buildArgs: (args) => ({
        actorName: args.actorName || args.actorLabel || args.name,
      }),
      validate: (readResult, args) => {
        const actorName = args.actorName || args.actorLabel || args.name;
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: actorName,
            actual: "Actor not found",
            fix: `Call unreal_listActors to find the correct actor name.`,
          };
        }
        // Check if the property exists on the actor
        const propName = args.propertyName || args.property;
        if (propName && readResult.properties) {
          const found = Object.keys(readResult.properties).some(
            (p) => p.toLowerCase() === propName.toLowerCase()
          );
          if (!found) {
            const available = Object.keys(readResult.properties).slice(0, 15).join(", ");
            return {
              valid: false,
              claimed: `Property '${propName}' on actor '${actorName}'`,
              actual: `Property not found. Available: ${available}`,
              fix: `Call unreal_getActor('${actorName}') to see all properties.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Actor",
    },
  ],

  setActorTransform: [
    {
      name: "actor_exists",
      description: "Verify the actor exists before transforming",
      readTool: "listActors",
      buildArgs: () => ({}),
      validate: (readResult, args) => {
        const actorName = args.actorName || args.actorLabel || args.name;
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: actorName,
            actual: "Could not retrieve actor list",
            fix: `Call unreal_listActors to verify actors.`,
          };
        }
        if (actorName && readResult.actors) {
          const found = readResult.actors.some(
            (a) =>
              (a.name || "").toLowerCase() === actorName.toLowerCase() ||
              (a.label || "").toLowerCase() === actorName.toLowerCase()
          );
          if (!found) {
            return {
              valid: false,
              claimed: actorName,
              actual: "Actor not found in level",
              fix: `Call unreal_listActors to get the correct actor name.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Actor",
    },
    {
      name: "transform_sane",
      description: "Verify transform values are within reasonable bounds",
      readTool: null,
      buildArgs: () => null,
      validate: (_, args) => {
        const loc = args.location || args.position;
        if (loc) {
          const coords = [loc.x || loc.X, loc.y || loc.Y, loc.z || loc.Z].filter(
            (v) => v !== undefined
          );
          for (const c of coords) {
            if (typeof c === "number" && Math.abs(c) > 1000000) {
              return {
                valid: false,
                claimed: `Location: (${coords.join(", ")})`,
                actual: `Coordinate ${c} is extremely large (>1M units)`,
                fix: `UE5 uses centimeters. Call unreal_listActors to see ` +
                  `existing actor positions for spatial reference.`,
              };
            }
          }
        }
        return { valid: true };
      },
      docCategory: "Actor",
    },
  ],

  // =========================================================================
  // Level Sequence Operations
  // =========================================================================

  ls_bind_actor: [
    {
      name: "sequence_exists",
      description: "Verify the level sequence exists",
      readTool: "ls_open",
      buildArgs: (args) => ({
        sequenceName: args.sequenceName || args.sequence,
      }),
      validate: (readResult, args) => {
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: args.sequenceName || args.sequence,
            actual: "Level sequence not found or could not be opened",
            fix: `Call unreal_list(type: 'LevelSequence') to find available sequences, ` +
              `or call unreal_ls_create to create a new one.`,
          };
        }
        return { valid: true };
      },
      docCategory: "Sequence",
    },
    {
      name: "actor_exists_for_binding",
      description: "Verify the actor to bind exists in the level",
      readTool: "listActors",
      buildArgs: () => ({}),
      validate: (readResult, args) => {
        const actorName = args.actorName || args.actor;
        if (!actorName) return { valid: true };
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: actorName,
            actual: "Could not retrieve actor list",
            fix: `Call unreal_listActors to verify the actor exists.`,
          };
        }
        if (readResult.actors) {
          const found = readResult.actors.some(
            (a) =>
              (a.name || "").toLowerCase() === actorName.toLowerCase() ||
              (a.label || "").toLowerCase() === actorName.toLowerCase()
          );
          if (!found) {
            return {
              valid: false,
              claimed: actorName,
              actual: "Actor not found in level for binding",
              fix: `Call unreal_listActors to find the correct actor name.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Actor",
    },
  ],

  ls_add_track: [
    {
      name: "sequence_and_binding_exist",
      description: "Verify the sequence exists and the binding is valid",
      readTool: "ls_open",
      buildArgs: (args) => ({
        sequenceName: args.sequenceName || args.sequence,
      }),
      validate: (readResult, args) => {
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: args.sequenceName || args.sequence,
            actual: "Level sequence not found",
            fix: `Call unreal_list(type: 'LevelSequence') to find sequences.`,
          };
        }
        return { valid: true };
      },
      docCategory: "Sequence",
    },
  ],

  ls_add_section: [
    {
      name: "sequence_exists",
      description: "Verify the level sequence exists",
      readTool: "ls_open",
      buildArgs: (args) => ({
        sequenceName: args.sequenceName || args.sequence,
      }),
      validate: (readResult, args) => {
        if (!readResult || !readResult.success) {
          return {
            valid: false,
            claimed: args.sequenceName || args.sequence,
            actual: "Level sequence not found",
            fix: `Call unreal_list(type: 'LevelSequence') to find sequences.`,
          };
        }
        return { valid: true };
      },
      docCategory: "Sequence",
    },
  ],

  ls_create: [
    {
      name: "sequence_does_not_exist",
      description: "Verify the sequence does NOT already exist",
      readTool: "list",
      buildArgs: (args) => ({
        path: args.path || "/Game/",
        type: "LevelSequence",
      }),
      validate: (readResult, args) => {
        const name = args.name || args.sequenceName;
        if (readResult && readResult.success && readResult.assets) {
          const exists = readResult.assets.some(
            (a) => (a.name || "").toLowerCase() === (name || "").toLowerCase()
          );
          if (exists) {
            return {
              valid: false,
              claimed: `Creating new sequence '${name}'`,
              actual: `Sequence '${name}' already exists`,
              fix: `Use unreal_ls_open to inspect the existing sequence, ` +
                `or choose a different name.`,
            };
          }
        }
        return { valid: true };
      },
      docCategory: "Sequence",
    },
  ],

  // =========================================================================
  // Dangerous Operations -- extra scrutiny
  // =========================================================================

  executePython: [
    {
      name: "python_safety_check",
      description: "AST-tokenization-based safety check on Python code execution",
      readTool: null,
      buildArgs: () => null,
      validate: (_, args) => {
        const code = args.code || args.script || "";

        if (!code.trim()) {
          return { valid: true };
        }

        // ---------------------------------------------------------------
        // Phase 1: Normalize code to defeat encoding-based bypasses
        // ---------------------------------------------------------------
        const normalizedCode = code
          .replace(/\\x[0-9a-fA-F]{2}/g, "X")   // Hex escapes
          .replace(/\\u[0-9a-fA-F]{4}/g, "X")    // Unicode escapes
          .replace(/\\[0-7]{1,3}/g, "X")          // Octal escapes
          .replace(/\bbase64\b/gi, "BASE64_BLOCKED")
          .replace(/\bcodecs\b/gi, "CODECS_BLOCKED");

        // ---------------------------------------------------------------
        // Phase 2: Tokenize and check for dangerous patterns
        //   Unlike simple string matching, this catches obfuscated calls
        //   like __import__('os'), getattr(builtins, 'exec'), etc.
        // ---------------------------------------------------------------

        // Dangerous module imports (direct and dynamic)
        const dangerousModules = [
          "os", "sys", "subprocess", "shutil", "socket", "http",
          "ftplib", "smtplib", "ctypes", "signal", "multiprocessing",
          "importlib", "pathlib", "tempfile", "glob", "io",
          "pickle", "shelve", "marshal", "code", "codeop",
          "compileall", "py_compile", "zipimport", "pkgutil",
        ];

        // Dangerous builtins and attribute access patterns
        const dangerousPatterns = [
          // Direct dangerous calls
          /\b(?:eval|exec|compile|execfile)\s*\(/i,
          // Dynamic import mechanisms
          /\b__import__\s*\(/i,
          /\bimportlib\s*\.\s*import_module\s*\(/i,
          // Builtin access tricks
          /\bgetattr\s*\(\s*(?:__builtins__|builtins)/i,
          /\b__builtins__\s*\[/i,
          /\b__builtins__\s*\.\s*__dict__/i,
          // Dangerous dunder access
          /\b__subclasses__\s*\(/i,
          /\b__globals__/i,
          /\b__code__/i,
          /\b__class__\s*\.\s*__bases__/i,
          /\b__mro__/i,
          // File system destructors
          /\bshutil\s*\.\s*rmtree/i,
          /\bos\s*\.\s*(?:remove|unlink|rmdir|system|popen|exec[lv]?[pe]?)\s*\(/i,
          /\bos\s*\.\s*(?:rename|replace|makedirs|chmod|chown)\s*\(/i,
          // Subprocess variations
          /\bsubprocess\s*\.\s*(?:call|run|Popen|check_output|check_call|getoutput|getstatusoutput)\s*\(/i,
          // Network operations
          /\bsocket\s*\.\s*socket\s*\(/i,
          /\burllib\s*\.\s*request/i,
          /\brequests\s*\.\s*(?:get|post|put|delete|patch)\s*\(/i,
          // Pickle deserialization (arbitrary code execution)
          /\bpickle\s*\.\s*(?:loads?|Unpickler)\s*\(/i,
          // open() for writing (allow read-only)
          /\bopen\s*\([^)]*['"]\s*[wax+]\s*['"]/i,
          // Code compilation
          /\bcompile\s*\([^)]*['"]\s*exec\s*['"]/i,
        ];

        // Check for dangerous import statements
        const importRegex = /\b(?:import|from)\s+(\S+)/gi;
        let match;
        while ((match = importRegex.exec(normalizedCode)) !== null) {
          const moduleName = match[1].split(".")[0].toLowerCase();
          if (dangerousModules.includes(moduleName)) {
            return {
              valid: false,
              claimed: `Executing Python with 'import ${match[1]}'`,
              actual: `Blocked import of restricted module: '${match[1]}'`,
              fix: `Module '${moduleName}' is not allowed in MCP Python execution. ` +
                `Use UE5's built-in Python API (unreal module) instead. ` +
                `For file operations, use unreal.EditorAssetLibrary. ` +
                `For logging, use unreal.log().`,
            };
          }
        }

        // Check for dangerous code patterns
        for (const pattern of dangerousPatterns) {
          const patternMatch = normalizedCode.match(pattern);
          if (patternMatch) {
            return {
              valid: false,
              claimed: `Executing Python containing '${patternMatch[0].trim()}'`,
              actual: `Blocked: potentially dangerous code pattern detected`,
              fix: `The pattern '${patternMatch[0].trim()}' is not allowed. ` +
                `Avoid: eval/exec, __import__, getattr on builtins, ` +
                `subprocess, os.system, file deletion, network calls, ` +
                `and pickle deserialization. Use the unreal module API.`,
            };
          }
        }

        // Phase 3: Check for string concatenation / char-code tricks that
        // try to reconstruct blocked function names
        const charCodePatterns = [
          /chr\s*\(\s*\d+\s*\)\s*\+\s*chr/i,         // chr(111)+chr(115) = "os"
          /\bbytearray\s*\(\s*\[/i,                    // bytearray([111,115])
          /\b(?:join|format)\s*\([^)]*(?:\\x|\\u)/i,   // "".join with escapes
        ];

        for (const pattern of charCodePatterns) {
          if (pattern.test(normalizedCode)) {
            return {
              valid: false,
              claimed: `Executing Python with character code construction`,
              actual: `Blocked: code appears to construct strings from character codes (potential obfuscation)`,
              fix: `Character code construction patterns (chr(), bytearray, hex escapes) ` +
                `are not allowed as they can be used to bypass security checks. ` +
                `Write code directly using the unreal Python API.`,
            };
          }
        }

        // Phase 4: Script size limit to prevent resource exhaustion
        if (code.length > 50000) {
          return {
            valid: false,
            claimed: `Executing Python script (${code.length} chars)`,
            actual: `Script exceeds maximum allowed size (50KB)`,
            fix: `Break the script into smaller units or use pythonExecFile ` +
              `to execute a .py file from the project directory.`,
          };
        }

        return { valid: true };
      },
      docCategory: "Blueprint",
    },
  ],
};

// ---------------------------------------------------------------------------
// Validation Execution
// ---------------------------------------------------------------------------

/**
 * Run all validation checks for a mutation tool.
 *
 * @param {string} toolName - The mutation tool being called
 * @param {object} args - The tool arguments
 * @param {function} executeReadTool - Function to call read-only tools on the editor
 *   Signature: async (toolName, args) => result
 * @returns {Promise<object>} Validation result:
 *   { valid: true } -- all checks passed
 *   { valid: false, failures: [...], engineDocs: [...] } -- one or more checks failed
 */
export async function validateMutation(toolName, args, executeReadTool) {
  const rules = VALIDATION_RULES[toolName];

  // No rules defined for this tool -- pass through
  if (!rules || rules.length === 0) {
    log.debug("No validation rules for tool", { tool: toolName });
    return { valid: true, checks: 0 };
  }

  const failures = [];
  const engineDocs = new Set();
  let checksRun = 0;

  for (const rule of rules) {
    checksRun++;
    log.debug("Running validation check", {
      tool: toolName,
      check: rule.name,
      description: rule.description,
    });

    let readResult = null;

    // Execute the read-only tool if one is specified
    if (rule.readTool && executeReadTool) {
      const readArgs = rule.buildArgs(args);
      if (readArgs !== null) {
        try {
          readResult = await executeReadTool(rule.readTool, readArgs);
        } catch (error) {
          log.error("Validation read-tool failed", {
            tool: toolName,
            check: rule.name,
            readTool: rule.readTool,
            error: error.message,
          });
          // If the read tool fails, we can't validate -- treat as failure
          failures.push({
            check: rule.name,
            description: rule.description,
            claimed: "Unable to verify",
            actual: `Read tool '${rule.readTool}' failed: ${error.message}`,
            fix: `The editor may not be connected. Check unreal_status first.`,
          });
          continue;
        }
      }
    }

    // Run the validation function
    const result = rule.validate(readResult, args);

    if (!result.valid) {
      failures.push({
        check: rule.name,
        description: rule.description,
        claimed: result.claimed,
        actual: result.actual,
        fix: result.fix,
      });

      // Collect relevant engine doc paths
      if (rule.docCategory) {
        engineDocs.add(
          `${ENGINE_DOCS_RELATIVE_PATH}/BlueprintAPI/${rule.docCategory}/index.html`
        );
      }
    }
  }

  if (failures.length > 0) {
    log.info("Mutation validation FAILED", {
      tool: toolName,
      checksRun,
      failures: failures.length,
      failedChecks: failures.map((f) => f.check),
    });

    return {
      valid: false,
      checksRun,
      failures,
      engineDocs: [...engineDocs],
    };
  }

  log.debug("Mutation validation PASSED", {
    tool: toolName,
    checksRun,
  });

  return { valid: true, checksRun };
}

/**
 * Format validation failures into a structured response for Claude.
 * This is what Claude sees when a mutation is blocked by the validator.
 *
 * @param {string} toolName - The blocked tool
 * @param {object} args - The tool arguments
 * @param {object} validationResult - Result from validateMutation
 * @returns {object} Formatted response
 */
export function formatValidationFailure(toolName, args, validationResult) {
  const response = {
    blocked: true,
    blocker: "project-state-validator",
    tool: toolName,
    checksRun: validationResult.checksRun,
    failureCount: validationResult.failures.length,
    failures: validationResult.failures.map((f) => ({
      check: f.check,
      description: f.description,
      what_you_claimed: f.claimed,
      what_actually_exists: f.actual,
      how_to_fix: f.fix,
    })),
    engineDocs: validationResult.engineDocs || [],
    message:
      `Mutation '${toolName}' blocked: ${validationResult.failures.length} validation ` +
      `check(s) failed against the live project state. Read the failures above -- ` +
      `each one tells you exactly what you claimed, what actually exists, and how ` +
      `to fix it. Call the suggested read-only tools to get the real data, adjust ` +
      `your call, and retry.`,
  };

  return response;
}

// ---------------------------------------------------------------------------
// Exports
// ---------------------------------------------------------------------------

export { VALIDATION_RULES, ENGINE_DOCS_RELATIVE_PATH };
