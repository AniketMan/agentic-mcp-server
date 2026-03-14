# Plan Directory

**This directory is managed by the Planner (Claude).**

This is where Claude writes its outputs after reading your `user_context/` files.

## Files generated here:

1. **`plan.json`** - The structured, step-by-step execution plan that the local workers will follow.
2. **`project_config.md`** - Any project-specific mappings (like scene-to-level tables, animation catalogs, or interaction patterns) that Claude extracted from your context files to help the workers.

**Users:** You can read these files to see what Claude intends to do, but you generally shouldn't edit them manually. If the plan is wrong, update your files in `user_context/` and tell Claude to regenerate the plan.
