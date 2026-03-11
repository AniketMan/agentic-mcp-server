#!/usr/bin/env node
/**
 * Audit: Cross-reference every C++ handler's actual parameter parsing
 * against the tool-registry.json parameter definitions.
 *
 * This script reads the C++ handler source files and checks:
 * 1. Does the handler parse from Body (JSON) or Params (query string)?
 * 2. What field names does it extract?
 * 3. Do those match the registry parameter names?
 */

import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";

const registry = JSON.parse(readFileSync("tool-registry.json", "utf-8"));
const regMap = new Map();
for (const tool of registry.tools) {
  regMap.set(tool.name, tool);
}

// Read all handler source files
const srcDir = join("..", "Source", "AgenticMCP", "Private");
const handlerFiles = readdirSync(srcDir).filter(
  (f) => f.startsWith("Handlers_") || f === "AgenticMCPServer.cpp"
);

let allSource = "";
for (const f of handlerFiles) {
  allSource += readFileSync(join(srcDir, f), "utf-8") + "\n";
}

// Read the main server to get HandlerMap registrations
const serverSrc = readFileSync(
  join(srcDir, "AgenticMCPServer.cpp"),
  "utf-8"
);

// Extract HandlerMap.Add entries to see which handlers use Params vs Body
const handlerMapRegex =
  /HandlerMap\.Add\(TEXT\("([^"]+)"\),\s*\[this\]\(const TMap<FString, FString>& Params, const FString& Body\)\s*\{\s*return\s+(\w+)\(([^)]*)\)/g;

const handlerDispatch = new Map();
let match;
while ((match = handlerMapRegex.exec(serverSrc)) !== null) {
  const [, toolName, funcName, args] = match;
  const usesParams = args.includes("Params");
  const usesBody = args.includes("Body");
  handlerDispatch.set(toolName, {
    funcName,
    usesParams,
    usesBody,
    argsStr: args.trim(),
  });
}

// Now extract what fields each handler reads from Body JSON
// Pattern: Json->GetStringField(TEXT("fieldName"))
// Pattern: Json->GetNumberField(TEXT("fieldName"))
// Pattern: Json->GetBoolField(TEXT("fieldName"))
// Pattern: Json->HasField(TEXT("fieldName"))
// Pattern: Json->GetArrayField(TEXT("fieldName"))
// Pattern: Json->GetObjectField(TEXT("fieldName"))

const errors = [];
const warnings = [];
const info = [];

for (const [toolName, dispatch] of handlerDispatch) {
  const reg = regMap.get(toolName);

  if (dispatch.usesParams && !dispatch.usesBody) {
    // This handler reads from query params. The /mcp/tool POST handler now
    // merges JSON body fields into QueryParams, so this is handled.
    // Log as info for awareness.
    if (reg && reg.parameters.length > 0) {
      info.push(
        `${toolName}: Params-only handler (${dispatch.funcName}). JSON body fields merged into QueryParams by /mcp/tool POST handler.`
      );
    }
  }

  if (!dispatch.usesParams && !dispatch.usesBody) {
    // Handler takes no args at all (like HandleRescan)
    if (reg && reg.parameters.length > 0) {
      warnings.push(
        `${toolName}: Handler takes no arguments but registry defines ${reg.parameters.length} parameters`
      );
    }
  }

  // For Body-based handlers, try to extract field names from the implementation
  if (dispatch.usesBody && reg) {
    // Find the function implementation
    const funcRegex = new RegExp(
      `FString FAgenticMCPServer::${dispatch.funcName}\\([^)]*\\)\\s*\\{([\\s\\S]*?)^\\}`,
      "m"
    );
    const funcMatch = funcRegex.exec(allSource);
    if (funcMatch) {
      const funcBody = funcMatch[1];
      // Extract field names from JSON parsing calls
      const fieldRegex =
        /(?:GetStringField|GetNumberField|GetBoolField|HasField|GetArrayField|GetObjectField|HasTypedField)\(TEXT\("([^"]+)"\)/g;
      const parsedFields = new Set();
      let fieldMatch;
      while ((fieldMatch = fieldRegex.exec(funcBody)) !== null) {
        parsedFields.add(fieldMatch[1]);
      }

      // Check registry params against parsed fields
      for (const param of reg.parameters) {
        if (!parsedFields.has(param.name)) {
          // Check if the handler might use a different field name
          const camelToSnake = param.name.replace(
            /[A-Z]/g,
            (c) => "_" + c.toLowerCase()
          );
          const snakeToCamel = param.name.replace(/_([a-z])/g, (_, c) =>
            c.toUpperCase()
          );
          if (
            parsedFields.has(camelToSnake) ||
            parsedFields.has(snakeToCamel)
          ) {
            warnings.push(
              `${toolName}: Registry param "${param.name}" may have case mismatch with C++ field`
            );
          } else if (param.required) {
            warnings.push(
              `${toolName}: Required registry param "${param.name}" not found in handler JSON parsing (may use different extraction method)`
            );
          }
        }
      }

      // Check for fields parsed in C++ but not in registry
      for (const field of parsedFields) {
        const inRegistry = reg.parameters.some((p) => p.name === field);
        if (!inRegistry) {
          info.push(
            `${toolName}: C++ parses field "${field}" but it is not in registry parameters`
          );
        }
      }
    }
  }
}

// Check for tools in registry but not in HandlerMap
for (const [toolName] of regMap) {
  if (!handlerDispatch.has(toolName)) {
    errors.push(
      `${toolName}: In registry but NOT in HandlerMap dispatch (tool will fail at runtime)`
    );
  }
}

console.log("=== HANDLER AUDIT RESULTS ===");
console.log(`Handlers checked: ${handlerDispatch.size}`);
console.log(`Registry tools: ${regMap.size}`);
console.log("");

console.log(`ERRORS (${errors.length}):`);
for (const e of errors) console.log(`  [ERROR] ${e}`);
console.log("");

console.log(`WARNINGS (${warnings.length}):`);
for (const w of warnings) console.log(`  [WARN] ${w}`);
console.log("");

console.log(`INFO (${info.length}):`);
for (const i of info.slice(0, 20)) console.log(`  [INFO] ${i}`);
if (info.length > 20)
  console.log(`  ... and ${info.length - 20} more info items`);

if (errors.length > 0) {
  console.log("\nFAIL: Critical issues found.");
  process.exit(1);
} else {
  console.log("\nAll critical checks passed.");
}
