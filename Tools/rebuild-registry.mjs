#!/usr/bin/env node
/**
 * Rebuild tool-registry.json from the actual C++ handler source code.
 *
 * For each handler in HandlerMap, this script:
 * 1. Finds the handler function implementation
 * 2. Extracts all JSON field names it parses (GetStringField, GetNumberField, etc.)
 * 3. Determines which are required vs optional
 * 4. For Params-only handlers, extracts query param names
 * 5. Preserves existing descriptions and annotations where possible
 * 6. Outputs a corrected tool-registry.json
 */

import { readFileSync, writeFileSync, readdirSync } from "node:fs";
import { join } from "node:path";

// Load existing registry for descriptions/annotations
const existingRegistry = JSON.parse(readFileSync("tool-registry.json", "utf-8"));
const existingMap = new Map();
for (const tool of existingRegistry.tools) {
  existingMap.set(tool.name, tool);
}

// Read all C++ source files
const srcDir = join("..", "Source", "AgenticMCP", "Private");
const sourceFiles = readdirSync(srcDir).filter((f) => f.endsWith(".cpp"));
const allSources = {};
let combinedSource = "";
for (const f of sourceFiles) {
  const content = readFileSync(join(srcDir, f), "utf-8");
  allSources[f] = content;
  combinedSource += content + "\n";
}

const serverSrc = allSources["AgenticMCPServer.cpp"];

// Extract HandlerMap.Add entries
const handlerMapRegex =
  /HandlerMap\.Add\(TEXT\("([^"]+)"\),\s*\[this\]\(const TMap<FString, FString>& Params, const FString& Body\)\s*\{\s*return\s+(\w+)\(([^)]*)\)/g;

const handlers = [];
let match;
while ((match = handlerMapRegex.exec(serverSrc)) !== null) {
  const [, toolName, funcName, argsStr] = match;
  const usesParams = argsStr.includes("Params");
  const usesBody = argsStr.includes("Body");
  handlers.push({ toolName, funcName, usesParams, usesBody, argsStr });
}

// For each handler, find its implementation and extract field names
const tools = [];

for (const h of handlers) {
  const existing = existingMap.get(h.toolName);

  // Find the function implementation across all source files
  // Match: FString FAgenticMCPServer::FuncName(...)  {  ...  }
  // We need to handle multi-line function bodies
  let funcBody = null;
  let funcSignature = null;

  for (const [filename, src] of Object.entries(allSources)) {
    // Try to find the function
    const funcStartRegex = new RegExp(
      `FString\\s+FAgenticMCPServer::${h.funcName}\\(([^)]*)\\)`,
      "g"
    );
    const startMatch = funcStartRegex.exec(src);
    if (startMatch) {
      funcSignature = startMatch[1];
      // Find the opening brace
      let braceStart = src.indexOf("{", startMatch.index + startMatch[0].length);
      if (braceStart === -1) continue;

      // Count braces to find the end
      let depth = 1;
      let pos = braceStart + 1;
      while (depth > 0 && pos < src.length) {
        if (src[pos] === "{") depth++;
        else if (src[pos] === "}") depth--;
        pos++;
      }
      funcBody = src.substring(braceStart + 1, pos - 1);
      break;
    }
  }

  const parameters = [];

  if (funcBody) {
    if (h.usesParams && !h.usesBody) {
      // Params-only handler - extract from Params.Contains(TEXT("..."))
      const paramRegex = /Params\.Contains\(TEXT\("([^"]+)"\)\)|Params\[TEXT\("([^"]+)"\)\]/g;
      const paramNames = new Set();
      let pm;
      while ((pm = paramRegex.exec(funcBody)) !== null) {
        paramNames.add(pm[1] || pm[2]);
      }

      // Determine which are required (check for error on missing)
      for (const name of paramNames) {
        const isRequired =
          funcBody.includes(`Missing required parameter: ${name}`) ||
          funcBody.includes(`Missing required parameter: ${name.toLowerCase()}`);

        // Infer type from usage
        let type = "string";
        if (funcBody.includes(`Atoi(*Params[TEXT("${name}")]`) ||
            funcBody.includes(`Atoi(*Params["${name}"]`)) {
          type = "number";
        }

        parameters.push({
          name,
          type,
          description: getParamDescription(h.toolName, name, existing),
          required: isRequired,
        });
      }
    } else if (h.usesBody) {
      // Body-based handler - extract JSON field names
      const fieldRegex =
        /(?:Get(String|Number|Bool|Integer|Array|Object)Field|Has(?:Typed)?Field)\(TEXT\("([^"]+)"\)/g;
      const fields = new Map(); // name -> { type, required }
      let fm;
      while ((fm = fieldRegex.exec(funcBody)) !== null) {
        const fieldType = fm[1];
        const fieldName = fm[2];
        if (!fields.has(fieldName)) {
          let type = "string";
          if (fieldType === "Number" || fieldType === "Integer") type = "number";
          else if (fieldType === "Bool") type = "boolean";
          else if (fieldType === "Array") type = "array";
          else if (fieldType === "Object") type = "object";
          fields.set(fieldName, { type });
        }
      }

      // Also check for TryGetStringField, TryGetNumberField patterns
      const tryFieldRegex = /TryGet(String|Number|Bool|Integer|Array|Object)Field\(TEXT\("([^"]+)"\)/g;
      while ((fm = tryFieldRegex.exec(funcBody)) !== null) {
        const fieldType = fm[1];
        const fieldName = fm[2];
        if (!fields.has(fieldName)) {
          let type = "string";
          if (fieldType === "Number" || fieldType === "Integer") type = "number";
          else if (fieldType === "Bool") type = "boolean";
          else if (fieldType === "Array") type = "array";
          else if (fieldType === "Object") type = "object";
          fields.set(fieldName, { type });
        }
      }

      // Determine which are required
      for (const [name, info] of fields) {
        const isRequired =
          funcBody.includes(`Missing required field: ${name}`) ||
          funcBody.includes(`Missing required field: '${name}'`) ||
          funcBody.includes(`Missing required field: ${name.toLowerCase()}`) ||
          funcBody.includes(`Missing required parameter: ${name}`) ||
          (funcBody.includes(`${name}.IsEmpty()`) &&
            funcBody.includes("MakeErrorJson"));

        parameters.push({
          name,
          type: info.type,
          description: getParamDescription(h.toolName, name, existing),
          required: isRequired,
        });
      }
    }
    // else: no-arg handler, parameters stays empty
  }

  // Build the tool entry
  const tool = {
    name: h.toolName,
    description: existing ? existing.description : h.funcName.replace("Handle", ""),
    parameters,
    annotations: existing
      ? existing.annotations
      : { readOnlyHint: false, destructiveHint: true },
  };

  tools.push(tool);
}

// Sort tools by name for consistency
tools.sort((a, b) => a.name.localeCompare(b.name));

const output = { tools };
writeFileSync("tool-registry.json", JSON.stringify(output, null, 2) + "\n");

console.log(`Registry rebuilt: ${tools.length} tools`);
console.log(
  `  With parameters: ${tools.filter((t) => t.parameters.length > 0).length}`
);
console.log(
  `  No parameters: ${tools.filter((t) => t.parameters.length === 0).length}`
);

// Helper: get a description for a parameter, preferring existing registry
function getParamDescription(toolName, paramName, existing) {
  if (existing) {
    const existingParam = existing.parameters.find((p) => p.name === paramName);
    if (existingParam && existingParam.description) {
      return existingParam.description;
    }
  }

  // Generate a reasonable description from the param name
  const descriptions = {
    blueprint: "Blueprint asset name or path",
    blueprintName: "Blueprint asset name or path",
    name: "Asset or object name",
    graph: "Graph name within the blueprint",
    filter: "Filter string to narrow results",
    type: "Type filter (e.g. 'blueprint', 'map', 'all')",
    query: "Search query string",
    limit: "Maximum number of results to return",
    asset: "Asset name or path",
    className: "UE class name to use",
    label: "Display label for the object",
    x: "X coordinate",
    y: "Y coordinate",
    z: "Z coordinate",
    location: "Location vector {x, y, z}",
    rotation: "Rotation vector {pitch, yaw, roll}",
    scale: "Scale vector {x, y, z}",
    actorName: "Name of the actor in the level",
    propertyName: "Name of the property to modify",
    propertyValue: "Value to set on the property",
    script: "Python script code to execute",
    file: "Path to a Python file to execute",
    command: "Console command to execute",
    enabled: "Whether to enable or disable the feature",
    active: "Whether to activate or deactivate",
    nodeId: "Unique identifier of the blueprint node",
    pinName: "Name of the pin on the node",
    value: "Value to set",
    description: "Description text",
    task_id: "Task queue task identifier",
    tool_name: "Name of the tool to execute",
    params: "Parameters for the tool",
    timeout_ms: "Timeout in milliseconds",
    format: "Output format (e.g. 'jpeg', 'png')",
    width: "Width in pixels",
    height: "Height in pixels",
    quality: "Image quality (1-100)",
    levelName: "Name of the level or sublevel",
    sublevelName: "Name of the sublevel",
    sequenceName: "Name of the level sequence",
    frame: "Frame number",
    playRate: "Playback rate multiplier",
    shapes: "Array of debug shapes to draw",
    stateId: "State snapshot identifier",
    target: "Target identifier or name",
    rows: "Array of row data",
    animationName: "Name of the animation asset",
    soundName: "Name of the sound asset",
    paramName: "Name of the parameter",
    volume: "Volume level (0.0 - 1.0)",
  };

  return descriptions[paramName] || `${paramName} parameter`;
}
