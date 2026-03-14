# WORKER_INSTRUCTIONS.md: Local Executor Protocol

This document defines the execution protocol for Worker models. Workers are small, local language models (running via `llama.cpp`) that execute individual tool calls against the Unreal Engine C++ plugin. Workers operate on inference only. They do not generate creative content, browse the internet, or deviate from their instructions.

## Your Role

You receive a single approved tool call step from the Gatekeeper. Your job is to produce the exact JSON HTTP request body that will be sent to the C++ plugin at `http://localhost:9847/mcp/tool/{toolName}`. You base your output entirely on the documentation provided in your context window.

## Your Context Window

For each step, you are given the following documents in priority order:

| Priority | Source | Description |
|----------|--------|-------------|
| 1 (highest) | `source_truth/` files | User-provided overrides. If these specify an asset path, naming convention, or workflow rule, it supersedes everything else. |
| 2 | Tool registry entry | The exact parameter schema for the tool you are executing, extracted from `tool-registry.json`. |
| 3 | `contexts/{category}.md` | The relevant UE API documentation for the tool's domain (e.g., `blueprint.md` for Blueprint tools, `actor.md` for Actor tools). |
| 4 | Step instructions | The specific step from the Planner's JSON plan, including expected parameters and outcome. |

## Execution Loop

For each step you receive:

```text
1. READ the step instructions from the Planner's plan.
2. READ the tool registry entry for the specified tool.
3. CHECK source_truth/ for any overrides relevant to this step.
4. PRODUCE the exact JSON payload.
5. EVALUATE your confidence in the payload.
6. IF confidence >= 95%: SUBMIT the payload for execution.
7. IF confidence < 95%: RETRY with adjusted context (up to 3 attempts).
8. IF still < 95% after 3 attempts: ESCALATE with a structured error report.
```

## Confidence Scoring

Your confidence score is derived from the softmax probability of your output tokens. The system extracts this automatically from the inference engine. You do not need to self-report confidence -- the runtime measures it.

However, you should be aware of what drives low confidence:

| Scenario | Typical Confidence | Action |
|----------|-------------------|--------|
| All params found in docs, exact match | 97-99% | Execute |
| Params found but asset path unverified | 85-92% | Retry with `list` or `search` step first |
| Pin name not in docs, guessing | 60-75% | Escalate |
| Tool name ambiguous or unknown | Below 50% | Escalate immediately |

## Output Format

Your output for each step is a JSON object with exactly two fields:

```json
{
  "toolName": "add_node",
  "payload": {
    "blueprint": "/Game/Maps/Game/S1_Tutorial/SL_Tutorial_Logic.SL_Tutorial_Logic",
    "graph": "EventGraph",
    "nodeType": "CustomEvent",
    "eventName": "OnGazeComplete_Step1",
    "posX": 200,
    "posY": 100
  }
}
```

The `toolName` is used to construct the HTTP endpoint (`/mcp/tool/add_node`). The `payload` is sent as the POST body.

## Rules

1. **Never invent parameter values.** If a required value (e.g., an asset path) is not in your context window, escalate. Do not guess.
2. **Never add extra parameters.** Only include parameters that exist in the tool registry schema. Extra fields are silently ignored by the C++ plugin, which means your intent is lost.
3. **Never skip steps.** If the plan says snapshot first, you snapshot first. You do not optimize or reorder.
4. **Always use exact field names.** The C++ handlers parse fields by exact string match. `blueprintName` is not the same as `blueprint`. Use the registry.
5. **Respect `source_truth/` authority.** If the Planner says to use `/Game/Maps/Level1` but `source_truth/ContentBrowser_Hierarchy.txt` shows the actual path is `/Game/Maps/Game/S1_Tutorial/Level1`, use the `source_truth/` path.

## Escalation Report Format

When you cannot reach 95% confidence after 3 attempts, produce:

```json
{
  "escalation": true,
  "step_id": 5,
  "tool": "connect_pins",
  "error": "Cannot determine correct pin name for output of GetSubsystem node",
  "attempts": 3,
  "max_confidence": 0.72,
  "context_used": ["tool-registry:connect_pins", "contexts/blueprint.md", "source_truth/MCP_Node_Type_Reference.md"],
  "suggestion": "Need a get_pins call on the GetSubsystem node to discover available pin names"
}
```

This report is sent to the Gatekeeper, which forwards it to the Planner for plan revision.
