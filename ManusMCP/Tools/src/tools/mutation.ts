/**
 * mutation.ts
 * MCP tool definitions for Blueprint write/mutation operations.
 * These tools create, modify, and delete Blueprint graph elements.
 */

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { UEBridge } from "../ue-bridge.js";

export function registerMutationTools(server: McpServer, bridge: UEBridge) {

  // ---- add_node ----
  server.tool(
    "add_node",
    "Add a new node to a Blueprint graph. Returns the node ID and full pin list.\n\n" +
    "Supported nodeTypes:\n" +
    "- CallFunction: Call a UFunction. Requires functionName, optional className.\n" +
    "- VariableGet/VariableSet: Get/set a Blueprint variable. Requires variableName.\n" +
    "- BreakStruct/MakeStruct: Break/make a struct. Requires typeName (e.g. 'FStoryStepData').\n" +
    "- DynamicCast: Cast to a class. Requires castTarget.\n" +
    "- OverrideEvent: Override a parent function as event. Requires functionName.\n" +
    "- CustomEvent: Create a custom event. Requires eventName.\n" +
    "- Branch: If/else branch node.\n" +
    "- Sequence: Execution sequence node.\n" +
    "- ForLoop/ForEachLoop/WhileLoop/ForLoopWithBreak: Loop macros.\n" +
    "- SpawnActorFromClass: Spawn actor. Optional actorClass.\n" +
    "- Select: Select node.\n" +
    "- Comment: Comment box. Optional comment, width, height.\n" +
    "- Reroute: Reroute/knot node.\n" +
    "- MacroInstance: Generic macro. Requires macroName, optional macroSource.\n" +
    "- CallParentFunction: Call parent implementation. Requires functionName.",
    {
      blueprint: z.string().describe("Blueprint or map name"),
      graph: z.string().describe("Target graph name (e.g. 'EventGraph')"),
      nodeType: z.string().describe("Node type (see description for supported types)"),
      posX: z.number().optional().describe("X position in graph"),
      posY: z.number().optional().describe("Y position in graph"),
      // Type-specific parameters
      functionName: z.string().optional().describe("For CallFunction/OverrideEvent/CallParentFunction"),
      className: z.string().optional().describe("For CallFunction: owning class"),
      variableName: z.string().optional().describe("For VariableGet/VariableSet"),
      typeName: z.string().optional().describe("For BreakStruct/MakeStruct: struct type name"),
      castTarget: z.string().optional().describe("For DynamicCast: target class name"),
      eventName: z.string().optional().describe("For CustomEvent: event name"),
      actorClass: z.string().optional().describe("For SpawnActorFromClass: actor class"),
      macroName: z.string().optional().describe("For MacroInstance: macro name"),
      macroSource: z.string().optional().describe("For MacroInstance: macro source Blueprint path"),
      comment: z.string().optional().describe("For Comment: comment text"),
      width: z.number().optional().describe("For Comment: box width"),
      height: z.number().optional().describe("For Comment: box height"),
    },
    async (args) => {
      const result = await bridge.post("add-node", args);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- delete_node ----
  server.tool(
    "delete_node",
    "Delete a node from a Blueprint graph. Breaks all connections first.",
    {
      blueprint: z.string().describe("Blueprint or map name"),
      nodeId: z.string().describe("Node GUID to delete"),
    },
    async ({ blueprint, nodeId }) => {
      const result = await bridge.post("delete-node", { blueprint, nodeId });
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- connect_pins ----
  server.tool(
    "connect_pins",
    "Connect two pins together. The schema validates type compatibility. " +
    "Returns updated node states with all connections. " +
    "Use get_pin_info first if unsure about pin names.",
    {
      blueprint: z.string().describe("Blueprint or map name"),
      sourceNodeId: z.string().describe("Source node GUID"),
      sourcePinName: z.string().describe("Source pin name (output pin)"),
      targetNodeId: z.string().describe("Target node GUID"),
      targetPinName: z.string().describe("Target pin name (input pin)"),
    },
    async (args) => {
      const result = await bridge.post("connect-pins", args);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- disconnect_pin ----
  server.tool(
    "disconnect_pin",
    "Break all connections on a specific pin.",
    {
      blueprint: z.string().describe("Blueprint or map name"),
      nodeId: z.string().describe("Node GUID"),
      pinName: z.string().describe("Pin name to disconnect"),
    },
    async (args) => {
      const result = await bridge.post("disconnect-pin", args);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- set_pin_default ----
  server.tool(
    "set_pin_default",
    "Set the default value of an input pin. For object/asset references, " +
    "use defaultObject with the full asset path instead of value.",
    {
      blueprint: z.string().describe("Blueprint or map name"),
      nodeId: z.string().describe("Node GUID"),
      pinName: z.string().describe("Pin name"),
      value: z.string().optional().describe("Default value as string"),
      defaultObject: z.string().optional().describe("Asset path for object references"),
    },
    async (args) => {
      const result = await bridge.post("set-pin-default", args);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- move_node ----
  server.tool(
    "move_node",
    "Move a node to a new position in the graph.",
    {
      blueprint: z.string().describe("Blueprint or map name"),
      nodeId: z.string().describe("Node GUID"),
      posX: z.number().describe("New X position"),
      posY: z.number().describe("New Y position"),
    },
    async (args) => {
      const result = await bridge.post("move-node", args);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- refresh_all_nodes ----
  server.tool(
    "refresh_all_nodes",
    "Refresh all nodes in a Blueprint. Useful after adding variables or " +
    "changing class hierarchy. Recompiles and saves.",
    {
      blueprint: z.string().describe("Blueprint or map name"),
    },
    async ({ blueprint }) => {
      const result = await bridge.post("refresh-all-nodes", { blueprint });
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- create_blueprint ----
  server.tool(
    "create_blueprint",
    "Create a new Blueprint asset. Defaults to Actor parent class.",
    {
      name: z.string().describe("Blueprint name (e.g. 'BP_MyActor')"),
      parentClass: z.string().optional().describe("Parent class (default: Actor)"),
      path: z.string().optional().describe("Content path (default: /Game/Blueprints)"),
    },
    async (args) => {
      const result = await bridge.post("create-blueprint", args);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- create_graph ----
  server.tool(
    "create_graph",
    "Create a new function or macro graph in a Blueprint.",
    {
      blueprint: z.string().describe("Blueprint or map name"),
      graphName: z.string().describe("New graph name"),
      graphType: z.enum(["function", "macro"]).optional().describe("Graph type (default: function)"),
    },
    async (args) => {
      const result = await bridge.post("create-graph", args);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- delete_graph ----
  server.tool(
    "delete_graph",
    "Delete a function or macro graph from a Blueprint.",
    {
      blueprint: z.string().describe("Blueprint or map name"),
      graphName: z.string().describe("Graph name to delete"),
    },
    async (args) => {
      const result = await bridge.post("delete-graph", args);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- add_variable ----
  server.tool(
    "add_variable",
    "Add a new variable to a Blueprint. Supported types: bool, byte, int, int64, " +
    "float, double, string, name, text, or any struct/class name.",
    {
      blueprint: z.string().describe("Blueprint or map name"),
      name: z.string().describe("Variable name"),
      type: z.string().describe("Variable type"),
    },
    async (args) => {
      const result = await bridge.post("add-variable", args);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- remove_variable ----
  server.tool(
    "remove_variable",
    "Remove a variable from a Blueprint.",
    {
      blueprint: z.string().describe("Blueprint or map name"),
      name: z.string().describe("Variable name to remove"),
    },
    async (args) => {
      const result = await bridge.post("remove-variable", args);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- compile_blueprint ----
  server.tool(
    "compile_blueprint",
    "Compile and save a Blueprint. Returns compilation status (up_to_date, error, dirty).",
    {
      blueprint: z.string().describe("Blueprint or map name"),
    },
    async ({ blueprint }) => {
      const result = await bridge.post("compile-blueprint", { blueprint });
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- set_node_comment ----
  server.tool(
    "set_node_comment",
    "Set or clear the comment bubble on a node.",
    {
      blueprint: z.string().describe("Blueprint or map name"),
      nodeId: z.string().describe("Node GUID"),
      comment: z.string().describe("Comment text (empty to clear)"),
    },
    async (args) => {
      const result = await bridge.post("set-node-comment", args);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );
}
