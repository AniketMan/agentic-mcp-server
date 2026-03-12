# Scene 3: Rescuers

## Level Information
- **Level Path:** `/Game/Maps/Game/Trailer/Levels/SLs/SL_Trailer_Logic`
- **Display Name:** Trailer (Scenes 3-4)
- **Total Interactions:** 4

## DEPENDENCY WARNING
This scene shares the level blueprint `/Game/Maps/Game/Trailer/Levels/SLs/SL_Trailer_Logic` with Scene 4.
**Before starting:** Check `../scene_4/STATUS.json`.
- If Scene 4 is "in_progress", STOP. Tell the user: "Scene 4 is currently being wired on the same level blueprint. I must wait."
- If Scene 4 is "complete" or "not_started", proceed.
- When checking out the level blueprint via `p4 edit`, verify no other agent has it locked.

## Notes
Shares SL_Trailer_Logic level blueprint with Scene 4.

## Interaction Definitions

| Step | Event Name | Trigger | Actor | Description |
|------|-----------|---------|-------|-------------|
| 7 | `Scene3_DoorKnobGrabbed` | Grip | TBD | GRIP door knob -> Heather+friends enter (LS_3_1) |
| 8 | `Scene3_FridgeDoorGrabbed` | Grip | TBD | GRIP fridge door -> fridge opens (LS_3_2) |
| 9 | `Scene3_PitcherGrabbed` | Grip | TBD | GRIP+TRIGGER pitcher pour (LS_3_5) |
| 10 | `Scene3_PitcherComplete` | Grip | TBD | All 3 glasses filled (LS_3_6) |


## Wiring Workflow

For each interaction in the table above, execute the following sequence using AgenticMCP tools:

### Phase 1: Read Current State
1. Call `unreal_status` to verify the editor is connected.
2. Call `get_project_state` to understand current project state.
3. Call `load_level` with path `/Game/Maps/Game/Trailer/Levels/SLs/SL_Trailer_Logic`.
4. Call `get_level_blueprint` to inspect existing logic.
5. Call `list_actors` to find the trigger actors listed in the table.

### Phase 2: Check What Already Exists
1. Call `get_graph` on the level blueprint's EventGraph.
2. Search for existing CustomEvent nodes matching the event names in the table.
3. If an interaction already exists (CustomEvent + BroadcastMessage chain), mark it as complete in STATUS.json and skip it.

### Phase 3: Wire Missing Interactions
For each interaction that does NOT already exist:

1. **Snapshot:** `snapshot_graph` on the level blueprint.
2. **Perforce:** Run `p4 edit` on the level blueprint `.uasset` file.
3. **Create CustomEvent:** `add_node(nodeType="CustomEvent", eventName="<EventName>")`.
4. **Create GetSubsystem:** `add_node(nodeType="CallFunction", functionName="GetSubsystem", className="UGameplayMessageSubsystem")`.
5. **Create BroadcastMessage:** `add_node(nodeType="CallFunction", functionName="BroadcastMessage")`.
6. **Create MakeStruct:** `add_node(nodeType="MakeStruct", typeName="Msg_StoryStep")`.
7. **Set Step Value:** `set_pin_default` on the MakeStruct's "Step" pin to the step number from the table.
8. **Wire the chain:**
   - CustomEvent.then -> GetSubsystem.execute
   - GetSubsystem.ReturnValue -> BroadcastMessage.Target
   - MakeStruct.output -> BroadcastMessage.Message
   - GetSubsystem.then -> BroadcastMessage.execute
9. **Validate:** `validate_blueprint`.
10. **Compile:** `compile_blueprint`.

### Phase 4: Wire Triggers (if actor is specified)
For interactions where the Actor column is not "TBD":

1. Call `list_actors` to find the trigger actor by name.
2. If the trigger type is "Grip":
   - `add_node(nodeType="AddDelegate", delegateName="OnGrabbed", ownerClass="GrabbableComponent")`
   - Wire the delegate to the corresponding CustomEvent.
3. If the trigger type is "Nav":
   - Find the navigation trigger volume/teleport point actor.
   - Wire `OnComponentBeginOverlap` to the CustomEvent.
4. If the trigger type is "Gaze":
   - Find the gaze target actor.
   - Wire the gaze delegate to the CustomEvent.
5. If the trigger type is "Auto":
   - Skip trigger wiring. These are automated by Level Sequences.

### Phase 5: Finalize
1. Call `validate_blueprint` one final time.
2. Call `compile_blueprint`.
3. Update `STATUS.json` to "complete".
4. Remove `LOCK.json`.
5. Run `p4 submit -d "Scene 3: wired 4 interactions"` to submit the changelist.
