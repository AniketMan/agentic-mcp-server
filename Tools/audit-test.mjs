#!/usr/bin/env node
/**
 * End-to-end audit test for the AgenticMCP tool discovery chain.
 * Validates: tool-registry.json -> convertToMCPSchema -> valid MCP ListTools response
 */

import { readFileSync } from "node:fs";
import { convertToMCPSchema, convertAnnotations } from "./lib.js";

const registry = JSON.parse(readFileSync("tool-registry.json", "utf-8"));
const errors = [];
const warnings = [];

for (const tool of registry.tools) {
  // Check required fields
  if (typeof tool.name !== "string" || tool.name.length === 0) {
    errors.push("Tool missing name");
    continue;
  }
  if (typeof tool.description !== "string" || tool.description.length === 0) {
    errors.push(`${tool.name}: missing description`);
  }
  if (tool.parameters === undefined || tool.parameters === null) {
    errors.push(`${tool.name}: parameters is null/undefined`);
    continue;
  }
  if (!Array.isArray(tool.parameters)) {
    errors.push(`${tool.name}: parameters not array`);
    continue;
  }

  // Test convertToMCPSchema
  let schema;
  try {
    schema = convertToMCPSchema(tool.parameters);
  } catch (e) {
    errors.push(`${tool.name}: convertToMCPSchema threw: ${e.message}`);
    continue;
  }

  if (schema.type !== "object") {
    errors.push(`${tool.name}: schema type not object, got ${schema.type}`);
  }
  if (schema.properties === undefined || typeof schema.properties !== "object") {
    errors.push(`${tool.name}: no properties object in schema`);
    continue;
  }

  // Check each parameter
  for (const param of tool.parameters) {
    if (typeof param.name !== "string" || param.name.length === 0) {
      errors.push(`${tool.name}: param missing name`);
      continue;
    }
    if (typeof param.type !== "string" || param.type.length === 0) {
      errors.push(`${tool.name}: param ${param.name} missing type`);
    }
    if (typeof param.description !== "string" || param.description.length === 0) {
      warnings.push(`${tool.name}.${param.name}: no description`);
    }

    // Verify the param made it into the schema
    if (schema.properties[param.name] === undefined) {
      errors.push(`${tool.name}: param ${param.name} not in schema output`);
    }
  }

  // Check required params are in required array
  const requiredParams = tool.parameters
    .filter((p) => p.required)
    .map((p) => p.name);
  if (requiredParams.length > 0 && schema.required === undefined) {
    errors.push(
      `${tool.name}: has required params [${requiredParams.join(", ")}] but schema.required is undefined`
    );
  }
  if (schema.required) {
    for (const r of requiredParams) {
      if (schema.required.indexOf(r) === -1) {
        errors.push(`${tool.name}: required param ${r} not in schema.required`);
      }
    }
  }

  // Test annotations
  let ann;
  try {
    ann = convertAnnotations(tool.annotations);
  } catch (e) {
    errors.push(`${tool.name}: convertAnnotations threw: ${e.message}`);
    continue;
  }
  if (typeof ann.readOnlyHint !== "boolean") {
    errors.push(`${tool.name}: readOnlyHint not boolean`);
  }
  if (typeof ann.destructiveHint !== "boolean") {
    errors.push(`${tool.name}: destructiveHint not boolean`);
  }
}

console.log(`Tools checked: ${registry.tools.length}`);
console.log(`Errors: ${errors.length}`);
for (const e of errors) console.log(`  ERROR: ${e}`);
console.log(`Warnings: ${warnings.length}`);
for (const w of warnings.slice(0, 10)) console.log(`  WARN: ${w}`);
if (warnings.length > 10) console.log(`  ... and ${warnings.length - 10} more`);

if (errors.length > 0) {
  process.exit(1);
}
console.log("\nPASS: All tools have valid registry entries and produce valid MCP schemas.");
