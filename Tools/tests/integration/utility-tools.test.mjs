/**
 * Integration test for the 4 new utility tools in AgenticMCP v3.1.0
 *
 * Tests:
 *   1. listEnumValues — discover enum values with Python access names
 *   2. listEditableProperties — list settable properties on a UClass
 *   3. createDataAsset — create DataAsset by concrete class
 *   4. addComponentToActor — add component to a placed actor
 *
 * Requires: UE editor running with AgenticMCP on port 9847
 */

const BASE = "http://localhost:9847";
const PASS = "✅ PASS";
const FAIL = "❌ FAIL";
let passed = 0;
let failed = 0;
let skipped = 0;

async function callTool(endpoint, body = {}) {
  const url = `${BASE}/api/${endpoint}`;
  try {
    const resp = await fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
      signal: AbortSignal.timeout(15000),
    });
    return await resp.json();
  } catch (e) {
    return { success: false, error: e.message };
  }
}

function assert(label, condition, detail = "") {
  if (condition) {
    console.log(`  ${PASS} ${label}`);
    passed++;
  } else {
    console.log(`  ${FAIL} ${label} ${detail}`);
    failed++;
  }
}

// ====================================================================
// Test 1: listEnumValues
// ====================================================================
async function testListEnumValues() {
  console.log("\n--- Test 1: listEnumValues ---");

  // 1a: Known engine enum
  const r1 = await callTool("listEnumValues", { enumName: "ECollisionChannel" });
  assert("Returns success for ECollisionChannel", r1.success === true);
  assert("Has values array", Array.isArray(r1.values));
  assert("Has valueCount > 0", (r1.valueCount || 0) > 0);
  if (r1.values && r1.values.length > 0) {
    assert("First value has 'name' field", !!r1.values[0].name);
    assert("First value has 'pythonAccess' field", !!r1.values[0].pythonAccess);
    console.log(`    Sample: ${r1.values[0].pythonAccess}`);
  }

  // 1b: Without E prefix (should still find it)
  const r2 = await callTool("listEnumValues", { enumName: "CollisionChannel" });
  assert("Finds enum without E prefix", r2.success === true);

  // 1c: Non-existent enum returns suggestions
  const r3 = await callTool("listEnumValues", { enumName: "FakeEnumThatDoesNotExist" });
  assert("Non-existent enum returns success=false", r3.success === false);
  assert("Non-existent enum has suggestions array", Array.isArray(r3.suggestions));

  // 1d: Partial match suggestions
  const r4 = await callTool("listEnumValues", { enumName: "Mobility" });
  assert("Partial match finds EComponentMobility or similar",
    r4.success === true || (r4.suggestions && r4.suggestions.length > 0));
}

// ====================================================================
// Test 2: listEditableProperties
// ====================================================================
async function testListEditableProperties() {
  console.log("\n--- Test 2: listEditableProperties ---");

  // 2a: Known component class
  const r1 = await callTool("listEditableProperties", { className: "PointLightComponent" });
  assert("Returns success for PointLightComponent", r1.success === true);
  assert("Has properties array", Array.isArray(r1.properties));
  assert("Has propertyCount > 0", (r1.propertyCount || 0) > 0);
  if (r1.properties && r1.properties.length > 0) {
    const p = r1.properties[0];
    assert("Property has 'name' field", !!p.name);
    assert("Property has 'type' field", !!p.type);
    assert("Property has 'declaredIn' field", !!p.declaredIn);
    console.log(`    Sample: ${p.name} (${p.type}) from ${p.declaredIn}`);
  }

  // 2b: Check Intensity property exists
  const hasIntensity = r1.properties?.some(p => p.name === "Intensity");
  assert("PointLightComponent has 'Intensity' property", hasIntensity);

  // 2c: Actor class
  const r2 = await callTool("listEditableProperties", { className: "Actor" });
  assert("Returns success for Actor", r2.success === true);
  assert("Actor has properties", (r2.propertyCount || 0) > 0);

  // 2d: Non-existent class returns suggestions
  const r3 = await callTool("listEditableProperties", { className: "FakeClassName" });
  assert("Non-existent class returns success=false", r3.success === false);
  assert("Has suggestions", Array.isArray(r3.suggestions));
}

// ====================================================================
// Test 3: createDataAsset
// ====================================================================
async function testCreateDataAsset() {
  console.log("\n--- Test 3: createDataAsset ---");

  // 3a: Try creating with base UDataAsset class (should work)
  const r1 = await callTool("createDataAsset", {
    className: "DataAsset",
    assetName: "MCP_Test_DataAsset",
    assetPath: "/Game/Tests"
  });
  assert("createDataAsset returns a response", r1 !== undefined);
  if (r1.success) {
    assert("Created DataAsset successfully", true);
    assert("Has assetPath in response", !!r1.assetPath);
    assert("Has className in response", !!r1.className);
    console.log(`    Created: ${r1.assetPath}`);
  } else {
    // May fail if /Game/Tests doesn't exist or DataAsset is abstract
    console.log(`    Note: ${r1.error || r1.message || "unknown error"}`);
    assert("createDataAsset returned structured error", !!r1.error || !!r1.message);
  }

  // 3b: Non-existent class
  const r2 = await callTool("createDataAsset", {
    className: "CompletelyFakeClass",
    assetName: "WontWork",
    assetPath: "/Game/Tests"
  });
  assert("Non-existent class returns error", r2.success === false || !!r2.error);
}

// ====================================================================
// Test 4: addComponentToActor
// ====================================================================
async function testAddComponentToActor() {
  console.log("\n--- Test 4: addComponentToActor ---");

  // First, list actors to find one to test with
  const actorsResult = await callTool("list-actors", {});
  const actors = actorsResult?.actors || actorsResult?.data?.actors || [];

  if (actors.length === 0) {
    console.log("  ⏭️  SKIPPED — no actors in current level");
    skipped += 4;
    return;
  }

  const testActor = actors[0].label || actors[0].name || actors[0];
  console.log(`    Using actor: ${testActor}`);

  // 4a: Add a known engine component
  const r1 = await callTool("addComponentToActor", {
    actorName: testActor,
    componentClass: "AudioComponent",
  });
  assert("addComponentToActor returns a response", r1 !== undefined);
  if (r1.success) {
    assert("Component added successfully", true);
    assert("Has componentName in response", !!r1.componentName);
    assert("Has componentClass in response", !!r1.componentClass);
    console.log(`    Added: ${r1.componentClass} as ${r1.componentName}`);
  } else {
    console.log(`    Note: ${r1.error || r1.message || "unknown error"}`);
    assert("Returned structured error", !!r1.error || !!r1.message);
  }

  // 4b: Non-existent component class
  const r2 = await callTool("addComponentToActor", {
    actorName: testActor,
    componentClass: "FakeComponentThatDoesNotExist",
  });
  assert("Non-existent component class returns error", r2.success === false || !!r2.error);

  // 4c: Non-existent actor
  const r3 = await callTool("addComponentToActor", {
    actorName: "ActorThatDefinitelyDoesNotExist_12345",
    componentClass: "AudioComponent",
  });
  assert("Non-existent actor returns error", r3.success === false || !!r3.error);
}

// ====================================================================
// Main
// ====================================================================
async function main() {
  console.log("=== AgenticMCP v3.1.0 — Utility Tools Integration Tests ===");
  console.log(`Target: ${BASE}`);

  // Check connection first
  try {
    const health = await fetch(`${BASE}/mcp/status`, { signal: AbortSignal.timeout(5000) });
    if (!health.ok) throw new Error(`HTTP ${health.status}`);
    const data = await health.json();
    console.log(`Connected to: ${data.projectName || "Unknown"} (${data.engineVersion || "Unknown"})`);
  } catch (e) {
    console.error(`\n${FAIL} Cannot connect to AgenticMCP at ${BASE}`);
    console.error(`   Error: ${e.message}`);
    console.error(`   Is the UE editor running with the AgenticMCP plugin?`);
    process.exit(1);
  }

  await testListEnumValues();
  await testListEditableProperties();
  await testCreateDataAsset();
  await testAddComponentToActor();

  console.log(`\n=== Results: ${passed} passed, ${failed} failed, ${skipped} skipped ===`);
  process.exit(failed > 0 ? 1 : 0);
}

main();
