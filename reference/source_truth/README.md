# source_truth/ — User Override Layer

This folder is the **highest authority** in the AgenticMCP documentation hierarchy. Every file placed here is checked by Workers before executing any tool call. If a file here contradicts the Planner's instructions, this folder wins.

## How It Works

You can put nothing in this folder and the system will still work. The Planner generates plans, the Workers execute them using the tool registry and API docs. However, the more context you provide here, the more accurate and reliable the Workers become.

Think of it this way: an empty `source_truth/` means the Workers rely entirely on the Planner's judgment. A populated `source_truth/` means the Workers can independently verify the Planner's instructions against your actual project data.

## Recommended Files

The table below lists files that significantly improve Worker accuracy. None are required.

| File | Purpose | Impact |
|------|---------|--------|
| `ContentBrowser_Hierarchy.txt` | Full asset tree dump from UE Content Browser | Workers can verify asset paths instead of trusting the Planner's guesses. Run `unreal_list` tool or export from Content Browser. |
| `script.md` | The screenplay, script, or design document for the project | Workers understand the creative intent behind each step. Prevents wiring the wrong interactions. |
| `filesystem.txt` | Directory listing of the UE project on disk | Workers can verify file paths, plugin locations, and config files. Run `tree` or `dir /s` on the project root. |
| `naming_conventions.md` | Asset and actor naming rules (e.g., `BP_` prefix for Blueprints, `SM_` for static meshes) | Workers use correct prefixes when creating or searching for assets. |
| `level_map.md` | Which levels correspond to which scenes, and their sublevel structure | Workers load the correct levels and target the correct level Blueprints. |
| `interaction_map.md` | Which interactions exist in each scene, their triggers, and expected behavior | Workers wire the correct events, delegates, and function calls. |
| `node_type_reference.md` | Exact `nodeType` strings, required params, and C++ class mappings for `add_node` | Workers produce correct `add_node` calls without guessing. Already provided as `MCP_Node_Type_Reference.md`. |
| `known_issues.md` | Known bugs, workarounds, or things to avoid in the current project state | Workers avoid known pitfalls. The Planner can also read this to avoid generating plans that hit known issues. |

## How to Generate These Files

Most of these can be generated automatically:

```bash
# Content Browser hierarchy (run in UE Python console or via executePython tool)
unreal.EditorAssetLibrary.list_assets('/Game/', recursive=True)

# Filesystem dump (run in terminal)
tree /f /a "C:\Path\To\Project" > source_truth\filesystem.txt

# Actor inventory (run via MCP tool)
# Use the list_actors tool on each level and save the output
```

## Custom Files

You can add any file here. Workers check every file in this folder. If you have a specific rule, constraint, or reference that the Planner might not know about, put it here. Examples:

- A PDF of the art direction bible (converted to text)
- A list of approved third-party plugins and their APIs
- Hardware constraints (e.g., "Quest 3 target, max 750K triangles per scene")
- Team conventions (e.g., "all custom events must start with `CE_`")

The Workers treat every file in this folder as ground truth. If it's here, it's law.
