# Scene 7: The Hospital

## Level Information
- **Level Path:** `/Game/Maps/Game/Hospital/Levels/SLs/SL_Hospital_Logic`
- **Display Name:** Hospital (Scene 7)
- **Total Interactions:** 6

## Notes
Steps 26-27 are AUTO/3DoF (hallway walk). Step 29 is automated.

## Interaction Definitions

| Step | Event Name | Trigger | Actor | Description |
|------|-----------|---------|-------|-------------|
| 24 | `Scene7_Arrival` | Nav | TBD | NAV to reception desk (LS_7_1) |
| 25 | `Scene7_NumberCardGrabbed` | Grip | TBD | GRIP number 20 card (LS_7_2) |
| 26 | `Scene7_HallwayWalk1` | Auto | TBD | NAV hallway walk part 1 (LS_7_3) - AUTO/3DoF |
| 27 | `Scene7_HallwayWalk2` | Auto | TBD | NAV hallway walk part 2 (LS_7_4) - AUTO/3DoF |
| 28 | `Scene7_RoomEntry` | Nav | TBD | NAV room entry (LS_7_5) |
| 29 | `Scene7_End` | Auto | TBD | Scene 7 end - detective delivers news (LS_7_6) - AUTOMATED |


## Wiring Workflow

For each interaction in the table above, execute the following sequence using AgenticMCP tools:

### Phase 1: Read Current State
1. Call `unreal_status` to verify the editor is connected.
2. Call `get_project_state` to understand current project state.
3. Call `load_level` with path `/Game/Maps/Game/Hospital/Levels/SLs/SL_Hospital_Logic`.
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
5. Run `p4 submit -d "Scene 7: wired 6 interactions"` to submit the changelist.
