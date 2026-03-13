---
displayName: Wire Scene (Quantized Path)
description: Uses the quantized inference layer to wire a scene with pre-computed, deterministic MCP call plans.
placeholder: "Enter scene number (1-8)"
---

# Wire Scene {{input}} via Quantized Inference Path

This command uses the quantized spatial data architecture to wire scene interactions
deterministically. Instead of discovering assets on the fly, it reads from a cached
manifest and executes pre-computed call sequences.

## Execution Steps

### Step 1: Verify Project State
Call `unreal_quantize_project` to ensure the asset manifest is fresh.
If the editor is connected, this will refresh the cache. If offline, it uses the last cached version.

### Step 2: Get the Wiring Plan
Call `unreal_get_scene_plan` with `sceneId: {{input}}`.
This returns the exact MCP call sequence for every interaction in Scene {{input}},
with confidence scores per operation.

### Step 3: Review Confidence
Before executing, check the confidence scores:
- **1.0**: All referenced actors and sequences found in manifest. Safe to execute.
- **0.6-0.9**: Some references missing. The level may not be loaded or assets may need spawning. Proceed with caution.
- **Below 0.5**: Critical references missing. Do NOT auto-execute. Report to user.

### Step 4: Execute Each Step
For each pending operation in the plan:

1. Call `unreal_execute_scene_step` with `sceneId: {{input}}` and the step number.
2. This returns the exact call sequence. Execute each call in order:
   - `unreal_snapshot_graph` (safety snapshot)
   - `unreal_add_node` (capture the returned GUID)
   - Substitute GUIDs into subsequent `unreal_connect_pins` and `unreal_set_pin_default` calls
3. After all calls for a step succeed, call `unreal_mark_step_complete` with the step number.

### Step 5: Compile
After all steps are wired, call `unreal_compileBlueprint` on the level blueprint.

### Step 6: Report
Call `unreal_get_wiring_status` to report the updated project status.

## Error Recovery
- If any call fails, the snapshot from Step 4.1 allows rollback via `unreal_restore_graph`.
- If a step is blocked, call `unreal_mark_scene_blocked` with the reason.
- Progress is saved per-step. If the process is interrupted, resume from the last incomplete step.

## Key Principle
No generative AI is used. The data IS the model. Claude is the logic router.
The manifest provides the weights. The wiring plan provides the inference.
The human reviews the confidence scores and makes the final call.
