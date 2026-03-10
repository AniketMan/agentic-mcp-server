# Scene Awareness Guide

This document describes how the AI agent should maintain awareness of the UE5 project state, infer scene mappings, and make contextual placement decisions.

## Persistent State

The project state file at `Content/AgenticMCP/project_state.json` is the AI's memory. It contains:

- All levels and their sublevels
- All actors with world positions
- Blueprint graph status (node count, compile state)
- Scene-to-level mapping
- Level Sequence inventory

**Always read this file first on connection.** If it does not exist, run a full project scan to create it.

## Scene Inference Rules

When a screenplay or script references a scene, use these rules to find the corresponding level:

1. **Direct name match:** "Scene 5 — Restaurant" → search for folders/levels containing "Restaurant" or "S5"
2. **Codename match:** If the folder is `S8_CNA` and the screenplay says "Scene 8 — Pluma," check `DA_GameData` for the scene name at index 8
3. **Folder pattern:** Levels follow `S{N}_{Codename}/` structure under `/Game/Maps/Game/`
4. **Master level:** Each scene has `ML_{Name}` as the master level
5. **Logic level:** Each scene has `SL_{Name}_Logic` as the level script level
6. **Art level:** Each scene has `SL_{Name}_Art` for environment art
7. **Lighting level:** Each scene has `SL_{Name}_Lighting` for lights

## Contextual Placement

When placing interaction actors based on screenplay descriptions:

### Teleport Points

- "She walks to the couch" → Find actor with "couch" in name → Place teleport point 150 units in front of couch, facing the couch
- "He enters the room" → Find the door actor → Place teleport point 200 units inside the room from the door
- "They stand at the window" → Find window actor → Place teleport point 100 units in front of window

### Spatial Reasoning

- Use the spatial grid (1000-unit cells) to find nearby actors
- When the script says "near" something, offset by 100-200 units
- When the script says "at" something, offset by 50-100 units
- When the script says "across from" something, mirror the position across the room center
- Teleport points should face the object of interest (rotation toward the target)

### Interaction Placement

- **Grabbable objects:** Place at the actor's location, ensure collision volume covers the mesh bounds
- **Touchable objects:** Place at the actor's location, ensure the touch collision is on the interactive face
- **Gaze targets:** Place at the actor's location, no offset needed
- **Trigger volumes:** Size to the area described in the script ("when the player enters the kitchen" → trigger volume covering the kitchen area)

## Actor Naming Conventions

When searching for actors, use fuzzy matching:

- Strip prefixes: `SM_`, `BP_`, `SK_` → search the remainder
- Case insensitive
- Partial match: "couch" matches "SM_Couch_01", "BP_InteractiveCouch", "Couch_Living_Room"
- Class match: If name search fails, search by class (e.g., all StaticMeshActors with "couch" in their mesh asset name)

## Level Load Procedure

When opening a level for the first time:

1. Load the master level (`ML_*`)
2. Wait for all sublevels to stream in
3. Run `list_actors` to get the full actor inventory
4. Update `project_state.json` with all actor positions
5. Report the inventory to the user

## State Update Triggers

The project state should be updated when:

- A level is loaded or unloaded
- An actor is spawned, deleted, or moved
- A Blueprint is compiled
- A Level Sequence is created or modified
- The user explicitly requests a state refresh
