/**
 * index.ts
 * ManusMCP - MCP server entry point.
 *
 * This is the TypeScript MCP server that wraps the ManusMCP C++ plugin
 * running inside the UE5 editor. It exposes all Blueprint manipulation,
 * actor management, and level editing tools to any MCP-compatible AI client.
 *
 * Architecture:
 *   AI Client (Claude, etc.) <-> MCP Protocol <-> This Server <-> HTTP <-> UE5 Plugin
 *
 * Usage:
 *   npx tsx src/index.ts [--port 9847]
 *
 * The server communicates with the UE5 plugin via HTTP on localhost.
 * The UE5 editor must be running with the ManusMCP plugin enabled.
 */

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { UEBridge } from "./ue-bridge.js";
import { registerReadTools } from "./tools/read.js";
import { registerMutationTools } from "./tools/mutation.js";
import { registerWorldTools } from "./tools/world.js";

// Parse command line args
const args = process.argv.slice(2);
let port = 9847;
const portIdx = args.indexOf("--port");
if (portIdx !== -1 && args[portIdx + 1]) {
  port = parseInt(args[portIdx + 1], 10);
  if (isNaN(port) || port < 1 || port > 65535) {
    console.error(`Invalid port: ${args[portIdx + 1]}. Using default 9847.`);
    port = 9847;
  }
}

// Create the UE bridge (HTTP client to the C++ plugin)
const bridge = new UEBridge(port);

// Create the MCP server
const server = new McpServer({
  name: "ManusMCP",
  version: "1.0.0",
  capabilities: {
    tools: {},
  },
});

// Register all tool categories
registerReadTools(server, bridge);
registerMutationTools(server, bridge);
registerWorldTools(server, bridge);

// Start the MCP server on stdio
async function main() {
  console.error(`ManusMCP: Starting MCP server (UE5 bridge on port ${port})...`);

  // Verify UE5 connection
  try {
    const health = await bridge.health();
    console.error(`ManusMCP: Connected to UE5 - ${JSON.stringify(health)}`);
  } catch (error) {
    console.error(
      `ManusMCP: WARNING - Could not connect to UE5 on port ${port}. ` +
      `The MCP server will start anyway, but tools will fail until the editor is running. ` +
      `Error: ${error instanceof Error ? error.message : String(error)}`
    );
  }

  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error("ManusMCP: MCP server running on stdio.");
}

main().catch((error) => {
  console.error(`ManusMCP: Fatal error - ${error}`);
  process.exit(1);
});
