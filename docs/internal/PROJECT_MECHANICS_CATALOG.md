# Project Mechanics Catalog
## Date: 2026-03-05

## Interaction Pattern (Every Scene Uses This)

Each interaction follows the same 4-node chain:

```
K2Node_CustomEvent (trigger)
  -> K2Node_GetSubsystem (get GameplayMessageSubsystem)
     -> K2Node_CallFunction (K2_BroadcastMessage)
        -> K2Node_MakeStruct (Msg_StoryStep with TagName)
```

Additional CallFunction nodes for:
- `DisableActor` (disable the trigger object after use)
- `EnableActor` (enable the next object)
- `BeginDeferredActorSpawnFromClass` (spawn teleport points, etc.)
- `PlayHapticEffect` (VR haptic feedback)

## Custom Events (21 Total)

### Scene 3 (Restaurant) - 4 events
- Scene3_DoorKnobGrabbed -> LS_3_1
- Scene3_FridgeDoorGrabbed -> LS_3_2
- Scene3_PitcherGrabbed -> LS_3_5
- Scene3_PitcherComplete -> LS_3_6

### Scene 4 (Restaurant) - 4 events
- Scene4_PhoneGrabbed -> LS_4_1
- Scene4_TextAdvance1 -> LS_4_2
- Scene4_TextAdvance2 -> LS_4_3
- Scene4_DoorHandleGrabbed -> LS_4_4

### Scene 5 (Trailer) - 2 events
- Scene5_HandPlaced -> heartbeat haptic + LS_5_1
- Scene5_TeleportToHeather -> spawn teleport + LS_5_2

### Scene 6 (Trailer) - 5 events
- Scene6_ComputerActivated -> LS_6_1
- Scene6_WeightPlaced -> LS_6_2
- Scene6_CradlePulled -> LS_6_3
- Scene6_SignGrabbed -> LS_6_4
- Scene6_PhoneGrabbed -> LS_6_5

### Scene 7 (Hospital) - 6 events
- Scene7_Arrival -> LS_7_1
- Scene7_NumberCardGrabbed -> LS_7_2
- Scene7_HallwayWalk1 -> LS_7_3
- Scene7_HallwayWalk2 -> LS_7_4
- Scene7_RoomEntry -> LS_7_5
- Scene7_End -> LS_7_6

## Key Systems
- **GameplayMessageSubsystem** - central message bus
- **Msg_StoryStep** - struct with TagName field (e.g., "Message.Event.StoryStep")
- **K2_BroadcastMessage** - sends story progression events
- **DisableActor/EnableActor** - toggle interactive objects
- **PlayHapticEffect** - VR controller feedback
- **BeginDeferredActorSpawnFromClass** - runtime actor spawning

## Files Per Level
- SL_Trailer_Logic: 8 interactions (Scenes 3-6)
- SL_Hospital_Logic: 6 interactions (Scene 7)
- SL_Restaurant_Logic: 2 interactions (Scene 3 subset)
- SL_Scene6_Logic: 5 interactions (Scene 6)
