/**
 * world.ts
 * MCP tool definitions for actor management, level management, and validation.
 */

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import { UEBridge } from "../ue-bridge.js";

export function registerWorldTools(server: McpServer, bridge: UEBridge) {

  // ============================================================
  // Actor Management
  // ============================================================

  // ---- list_actors ----
  server.tool(
    "list_actors",
    "List actors in the current editor world. Filter by class, name, or level.",
    {
      classFilter: z.string().optional().describe("Filter by class name (e.g. 'BP_TeleportPoint')"),
      nameFilter: z.string().optional().describe("Filter by actor name or label"),
      level: z.string().optional().describe("Filter by level name"),
    },
    async (args) => {
      const result = await bridge.post("list-actors", args);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- get_actor ----
  server.tool(
    "get_actor",
    "Get detailed information about a specific actor: transform, components, " +
    "and all editable properties with current values.",
    {
      name: z.string().describe("Actor name or label"),
    },
    async ({ name }) => {
      const result = await bridge.post("get-actor", { name });
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- spawn_actor ----
  server.tool(
    "spawn_actor",
    "Spawn a new actor in the current world. Supports both Blueprint classes " +
    "(by Blueprint name) and native classes (by class name).",
    {
      className: z.string().describe("Actor class or Blueprint name"),
      label: z.string().optional().describe("Actor label"),
      locationX: z.number().optional().describe("X location"),
      locationY: z.number().optional().describe("Y location"),
      locationZ: z.number().optional().describe("Z location"),
      rotationPitch: z.number().optional().describe("Pitch rotation"),
      rotationYaw: z.number().optional().describe("Yaw rotation"),
      rotationRoll: z.number().optional().describe("Roll rotation"),
      scaleX: z.number().optional().describe("X scale (default 1)"),
      scaleY: z.number().optional().describe("Y scale (default 1)"),
      scaleZ: z.number().optional().describe("Z scale (default 1)"),
    },
    async (args) => {
      const result = await bridge.post("spawn-actor", args);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- delete_actor ----
  server.tool(
    "delete_actor",
    "Delete an actor from the current world.",
    {
      name: z.string().describe("Actor name or label"),
    },
    async ({ name }) => {
      const result = await bridge.post("delete-actor", { name });
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- set_actor_property ----
  server.tool(
    "set_actor_property",
    "Set a property value on an actor or its components. " +
    "Searches actor properties first, then component properties.",
    {
      name: z.string().describe("Actor name or label"),
      property: z.string().describe("Property name"),
      value: z.string().describe("New value as string"),
    },
    async (args) => {
      const result = await bridge.post("set-actor-property", args);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- set_actor_transform ----
  server.tool(
    "set_actor_transform",
    "Set an actor's transform. Only provided fields are updated; " +
    "omitted fields keep their current values.",
    {
      name: z.string().describe("Actor name or label"),
      locationX: z.number().optional(),
      locationY: z.number().optional(),
      locationZ: z.number().optional(),
      rotationPitch: z.number().optional(),
      rotationYaw: z.number().optional(),
      rotationRoll: z.number().optional(),
      scaleX: z.number().optional(),
      scaleY: z.number().optional(),
      scaleZ: z.number().optional(),
    },
    async (args) => {
      const result = await bridge.post("set-actor-transform", args);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ============================================================
  // Level Management
  // ============================================================

  // ---- list_levels ----
  server.tool(
    "list_levels",
    "List all levels in the current world: persistent level and streaming sublevels. " +
    "Shows actor count, level blueprint info, and load state.",
    {},
    async () => {
      const result = await bridge.post("list-levels", {});
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- load_level ----
  server.tool(
    "load_level",
    "Load a sublevel into the current world. Use the full content path.",
    {
      levelPath: z.string().describe("Level content path (e.g. '/Game/Maps/Game/Trailer/Levels/SLs/SL_Trailer_Logic')"),
    },
    async ({ levelPath }) => {
      const result = await bridge.post("load-level", { levelPath });
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- get_level_blueprint ----
  server.tool(
    "get_level_blueprint",
    "Get the level blueprint for a specific level. Returns graphs, variables, " +
    "and node details. Use 'persistent' for the persistent level.",
    {
      level: z.string().describe("Level name or 'persistent'"),
    },
    async ({ level }) => {
      const result = await bridge.post("get-level-blueprint", { level });
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ============================================================
  // Validation and Safety
  // ============================================================

  // ---- validate_blueprint ----
  server.tool(
    "validate_blueprint",
    "Compile a Blueprint and report validation results: status, warnings " +
    "(disconnected nodes, type mismatches), node/connection counts.",
    {
      blueprint: z.string().describe("Blueprint or map name"),
    },
    async ({ blueprint }) => {
      const result = await bridge.post("validate-blueprint", { blueprint });
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- snapshot_graph ----
  server.tool(
    "snapshot_graph",
    "Take a snapshot of all graphs in a Blueprint for potential rollback. " +
    "Captures node types, positions, and connections. Max 50 snapshots stored.",
    {
      blueprint: z.string().describe("Blueprint or map name"),
      description: z.string().optional().describe("Human-readable description of the snapshot"),
    },
    async (args) => {
      const result = await bridge.post("snapshot-graph", args);
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );

  // ---- restore_graph ----
  server.tool(
    "restore_graph",
    "Restore a graph from a snapshot. WARNING: This clears existing nodes. " +
    "The snapshot data is returned for manual reconstruction via add_node/connect_pins.",
    {
      snapshotId: z.string().describe("Snapshot ID from snapshot_graph"),
    },
    async ({ snapshotId }) => {
      const result = await bridge.post("restore-graph", { snapshotId });
      return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
    }
  );
}
