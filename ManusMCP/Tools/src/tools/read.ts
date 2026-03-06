/**
 * read.ts
 * MCP tool definitions for Blueprint read operations.
 * These tools inspect Blueprints, graphs, nodes, and pins without modifying them.
 */

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { UEBridge } from "../ue-bridge.js";

export function registerReadTools(server: McpServer, bridge: UEBridge) {

  // ---- list_blueprints ----
  server.tool(
    "list_blueprints",
    "List all Blueprint and Map assets in the project. Use filter to narrow results. " +
    "Maps contain level blueprints. Type can be 'blueprint', 'map', or 'all'.",
    {
      filter: z.string().optional().describe("Name filter pattern"),
      type: z.enum(["blueprint", "map", "all"]).optional().describe("Asset type to list"),
    },
    async ({ filter, type }) => {
      const result = await bridge.get("list", {
        ...(filter && { filter }),
        ...(type && { type }),
      });
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- get_blueprint ----
  server.tool(
    "get_blueprint",
    "Get detailed information about a Blueprint: graphs, variables, parent class. " +
    "For level blueprints, pass the map name (e.g. 'SL_Trailer_Logic').",
    {
      name: z.string().describe("Blueprint or map name"),
    },
    async ({ name }) => {
      const result = await bridge.get("blueprint", { name });
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- get_graph ----
  server.tool(
    "get_graph",
    "Get all nodes and connections in a Blueprint graph. Returns node IDs, types, " +
    "positions, pin names, pin types, default values, and connections. " +
    "If graph name is omitted, returns all graphs.",
    {
      blueprint: z.string().describe("Blueprint or map name"),
      graph: z.string().optional().describe("Graph name (omit for all graphs)"),
    },
    async ({ blueprint, graph }) => {
      const params: Record<string, string> = { blueprint };
      if (graph) params.graph = graph;
      const result = await bridge.get("graph", params);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- search_blueprints ----
  server.tool(
    "search_blueprints",
    "Search for Blueprints and Maps by name pattern.",
    {
      query: z.string().describe("Search pattern"),
      limit: z.number().optional().describe("Max results (default 50)"),
    },
    async ({ query, limit }) => {
      const params: Record<string, string> = { query };
      if (limit) params.limit = String(limit);
      const result = await bridge.get("search", params);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- find_references ----
  server.tool(
    "find_references",
    "Find all assets that reference a given Blueprint or Map.",
    {
      asset: z.string().describe("Asset name or path"),
    },
    async ({ asset }) => {
      const result = await bridge.get("references", { asset });
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- list_classes ----
  server.tool(
    "list_classes",
    "List UClasses available in the engine. Filter by name or parent class. " +
    "Useful for discovering what classes can be used in CallFunction or SpawnActor.",
    {
      filter: z.string().optional().describe("Name filter"),
      parentClass: z.string().optional().describe("Parent class to filter by (e.g. 'AActor')"),
      limit: z.number().optional().describe("Max results (default 100)"),
    },
    async ({ filter, parentClass, limit }) => {
      const result = await bridge.post("list-classes", { filter, parentClass, limit });
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- list_functions ----
  server.tool(
    "list_functions",
    "List all UFunctions on a class. Shows name, flags (BlueprintCallable, Pure, Static), " +
    "and parameters. Essential for knowing what functions can be called via add_node.",
    {
      className: z.string().describe("Class name (e.g. 'AActor', 'UGameplayStatics')"),
      filter: z.string().optional().describe("Function name filter"),
    },
    async ({ className, filter }) => {
      const result = await bridge.post("list-functions", { className, filter });
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- list_properties ----
  server.tool(
    "list_properties",
    "List all UProperties on a class. Shows name, type, and whether editable/blueprint-visible.",
    {
      className: z.string().describe("Class name"),
      filter: z.string().optional().describe("Property name filter"),
    },
    async ({ className, filter }) => {
      const result = await bridge.post("list-properties", { className, filter });
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- get_pin_info ----
  server.tool(
    "get_pin_info",
    "Get detailed pin information for a node. If pinName is omitted, returns all pins. " +
    "Shows pin type, direction, default value, and connections.",
    {
      blueprint: z.string().describe("Blueprint or map name"),
      nodeId: z.string().describe("Node GUID"),
      pinName: z.string().optional().describe("Specific pin name (omit for all pins)"),
    },
    async ({ blueprint, nodeId, pinName }) => {
      const result = await bridge.post("get-pin-info", { blueprint, nodeId, pinName });
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- rescan_assets ----
  server.tool(
    "rescan_assets",
    "Force rescan of the asset registry. Use after creating new assets or if " +
    "list_blueprints is missing recently added assets.",
    {},
    async () => {
      const result = await bridge.get("rescan");
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );
}
