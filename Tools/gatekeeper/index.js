/**
 * AgenticMCP Gatekeeper
 * 
 * The validation and dispatch layer between the Planner (Claude)
 * and the C++ plugin. Enforces inference-gated determinism.
 * 
 * Components:
 * - Rule Engine: Zero-latency deterministic validation
 * - LLM Validator: Local model semantic validation + confidence scoring
 * - Dispatcher: Plan execution orchestrator
 */

export { validateStep, validatePlan, WorkflowState, loadRegistry } from './rule-engine.js';
export { isLLMAvailable, executeWithConfidenceGate, CONFIDENCE_THRESHOLD, MAX_RETRIES } from './llm-validator.js';
export { executePlan, sendToPlugin, buildWaves } from './dispatcher.js';
export { selectTechnique, recordOutcome, getTechniqueStats, TECHNIQUES } from './technique-selector.js';
