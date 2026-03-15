/**
 * Adaptive Technique Selector (ARCHON-inspired)
 * 
 * Dynamically selects the inference technique per step based on
 * task complexity, tool type, and historical performance.
 * 
 * Implements the core insight from ARCHON (arxiv: 2409.15254):
 * different tasks benefit from different inference strategies.
 * Simple tasks waste compute on multi-pass; complex tasks fail
 * on single-pass. This module classifies each step and selects
 * the optimal technique.
 * 
 * Techniques:
 *   1. DIRECT     -- Plan params used as-is, no Worker inference (read-only tools)
 *   2. SINGLE     -- One Worker inference pass (simple modifying tools)
 *   3. MULTI_PASS -- Up to N retries with increasing context (complex tools)
 *   4. ENSEMBLE   -- Run inference on Validator + Worker, take consensus (critical tools)
 *   5. SPATIAL    -- Route to Cosmos Reason2 for visual/spatial analysis
 */

// ---------------------------------------------------------------------------
// Tool Complexity Classification
// ---------------------------------------------------------------------------

// Tools that need no inference -- just pass plan params through
const DIRECT_TOOLS = new Set([
  'get_level_actors', 'asset_search', 'blueprint_query',
  'asset_dependencies', 'asset_referencers', 'capture_viewport',
  'get_output_log', 'screenshot', 'list', 'listActors',
  'getActorProperties', 'snapshotGraph', 'get_scene_plan',
  'get_all_scene_plans', 'get_wiring_status', 'quantize_project',
  'xrGetHapticCapabilities', 'xrGetSpatialAudioStatus',
]);

// Tools with simple, well-defined params -- single pass is enough
const SIMPLE_TOOLS = new Set([
  'spawn_actor', 'move_actor', 'delete_actor', 'set_actor_property',
  'set_actor_transform', 'set_actor_label', 'add_component',
  'open_level', 'run_console_command', 'begin_transaction',
  'end_transaction', 'xrPlayHapticEffect', 'xrStopHapticEffect',
  'xrSetSpatialAudioEnabled', 'xrConfigureAudioAttenuation',
]);

// Tools that modify complex structures -- need multi-pass with context
const COMPLEX_TOOLS = new Set([
  'add_node', 'connect_pins', 'disconnect_pin', 'set_pin_default',
  'create_blueprint', 'create_graph', 'compile_blueprint',
  'anim_blueprint_modify', 'material_create', 'material_modify',
  'enhanced_input', 'seq_create', 'seq_add_track',
  'pcg_execute', 'executePythonCapture',
]);

// Tools where errors are costly -- use ensemble (Validator + Worker agree)
const CRITICAL_TOOLS = new Set([
  'delete_actors', 'execute_script', 'cleanup_scripts',
  'compile_blueprint', 'asset_import',
]);

// Tools that benefit from spatial/visual analysis
const SPATIAL_TOOLS = new Set([
  'screenshot', 'capture_viewport', 'move_camera',
]);

// ---------------------------------------------------------------------------
// Technique Definitions
// ---------------------------------------------------------------------------

const TECHNIQUES = {
  DIRECT: {
    name: 'DIRECT',
    description: 'Pass plan params directly to plugin. No inference.',
    retries: 0,
    confidence_threshold: 1.0,  // Always passes -- no inference
    roles: [],
  },
  SINGLE: {
    name: 'SINGLE',
    description: 'Single Worker inference pass.',
    retries: 1,
    confidence_threshold: 0.90,  // Slightly lower bar for simple tools
    roles: ['worker'],
  },
  MULTI_PASS: {
    name: 'MULTI_PASS',
    description: 'Up to 3 Worker inference passes with escalating context.',
    retries: 3,
    confidence_threshold: 0.95,
    roles: ['worker'],
  },
  ENSEMBLE: {
    name: 'ENSEMBLE',
    description: 'Worker + Validator both infer. Execute only if they agree.',
    retries: 2,
    confidence_threshold: 0.95,
    roles: ['worker', 'validator'],
    require_consensus: true,
  },
  SPATIAL: {
    name: 'SPATIAL',
    description: 'Route to Cosmos Reason2 for visual/spatial analysis.',
    retries: 1,
    confidence_threshold: 0.80,  // Vision models are less precise on token probs
    roles: ['spatial'],
  },
};

// ---------------------------------------------------------------------------
// Performance History (in-memory, resets per session)
// ---------------------------------------------------------------------------

const performanceHistory = {
  // tool_name -> { attempts: number, successes: number, avg_confidence: number }
};

function recordOutcome(toolName, success, confidence) {
  if (!performanceHistory[toolName]) {
    performanceHistory[toolName] = { attempts: 0, successes: 0, total_confidence: 0 };
  }
  const h = performanceHistory[toolName];
  h.attempts++;
  if (success) h.successes++;
  h.total_confidence += confidence;
}

function getSuccessRate(toolName) {
  const h = performanceHistory[toolName];
  if (!h || h.attempts === 0) return null;
  return h.successes / h.attempts;
}

function getAvgConfidence(toolName) {
  const h = performanceHistory[toolName];
  if (!h || h.attempts === 0) return null;
  return h.total_confidence / h.attempts;
}

// ---------------------------------------------------------------------------
// Technique Selection
// ---------------------------------------------------------------------------

/**
 * Select the optimal inference technique for a given plan step.
 * 
 * Selection priority:
 * 1. Static classification (tool type)
 * 2. Performance history override (if a "simple" tool keeps failing, upgrade)
 * 3. Step hints (plan can request a specific technique)
 * 
 * @param {object} step - The plan step
 * @param {object} options - Optional overrides
 * @returns {object} The selected technique definition
 */
function selectTechnique(step, options = {}) {
  const toolName = step.tool;

  // 1. Check for explicit technique hint in the plan step
  if (step.technique && TECHNIQUES[step.technique]) {
    return { ...TECHNIQUES[step.technique], reason: 'explicit_hint' };
  }

  // 2. Check performance history -- upgrade technique if tool is underperforming
  const successRate = getSuccessRate(toolName);
  const avgConfidence = getAvgConfidence(toolName);

  if (successRate !== null && successRate < 0.5) {
    // This tool fails more than half the time -- upgrade to ENSEMBLE
    return { ...TECHNIQUES.ENSEMBLE, reason: 'performance_upgrade' };
  }

  if (avgConfidence !== null && avgConfidence < 0.85 && !DIRECT_TOOLS.has(toolName)) {
    // Average confidence is low -- upgrade from SINGLE to MULTI_PASS
    return { ...TECHNIQUES.MULTI_PASS, reason: 'confidence_upgrade' };
  }

  // 3. Static classification
  if (DIRECT_TOOLS.has(toolName)) {
    return { ...TECHNIQUES.DIRECT, reason: 'read_only' };
  }

  if (CRITICAL_TOOLS.has(toolName)) {
    return { ...TECHNIQUES.ENSEMBLE, reason: 'critical_tool' };
  }

  if (COMPLEX_TOOLS.has(toolName)) {
    return { ...TECHNIQUES.MULTI_PASS, reason: 'complex_tool' };
  }

  if (SIMPLE_TOOLS.has(toolName)) {
    return { ...TECHNIQUES.SINGLE, reason: 'simple_tool' };
  }

  // 4. Check if the step involves spatial/visual analysis
  if (step.spatial_check || SPATIAL_TOOLS.has(toolName)) {
    return { ...TECHNIQUES.SPATIAL, reason: 'spatial_analysis' };
  }

  // Default: MULTI_PASS for unknown tools (safe default)
  return { ...TECHNIQUES.MULTI_PASS, reason: 'unknown_tool_default' };
}

/**
 * Get a summary of current technique selection state.
 * Useful for debugging and reporting.
 */
function getTechniqueStats() {
  return {
    tool_classifications: {
      direct: [...DIRECT_TOOLS],
      simple: [...SIMPLE_TOOLS],
      complex: [...COMPLEX_TOOLS],
      critical: [...CRITICAL_TOOLS],
      spatial: [...SPATIAL_TOOLS],
    },
    performance_history: { ...performanceHistory },
    techniques: Object.keys(TECHNIQUES),
  };
}

export {
  selectTechnique,
  recordOutcome,
  getTechniqueStats,
  getSuccessRate,
  getAvgConfidence,
  TECHNIQUES,
  DIRECT_TOOLS,
  SIMPLE_TOOLS,
  COMPLEX_TOOLS,
  CRITICAL_TOOLS,
  SPATIAL_TOOLS,
};
