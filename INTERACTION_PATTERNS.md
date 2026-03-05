# Interaction Patterns — SL_Trailer_Logic Analysis

> Reverse-engineered from the working SL_Trailer_Logic paste text (526 lines, ~40 nodes).
> This documents the exact Blueprint patterns used for the VR experience's interaction system.

## Architecture Overview

The level uses a **message-driven architecture** built on the GameplayMessage plugin:

1. **GameplayMessageSubsystem** — UGameInstanceSubsystem for broadcasting/listening to messages
2. **GameplayTag channels** — Messages are routed by tag (e.g., `Message.Event.TeleportPoint`, `Message.Event.StoryStep`)
3. **User-defined message structs** — `Msg_TeleportPoint`, `Msg_StoryStep` carry payload data
4. **EnablerComponent** — Controls actor visibility/interactability (enable/disable pattern)
5. **TeleportPoint** — ActivatableActor with EnablerComponent, fires `OnPlayerTeleported` delegate

## Pattern 1: TeleportPoint Message Listener

**Purpose**: Listen for teleport events, resolve the teleport point, enable it, and bind its delegate.

### Node Chain (left to right)

```
BeginPlay → Call ListenMessages (self) → BroadcastMessage (TeleportPoint channel)
                                              ↓
                                    [separate chain below]

ListenMessages (CustomEvent) → ListenForGameplayMessages (async)
                                     ↓ OnMessageReceived
                                BreakStruct (Msg_TeleportPoint)
                                     ↓ Teleport (soft ref)
                                LoadAsset (async)
                                     ↓ Completed
                                Cast to TeleportPoint
                                     ↓ success
                                EnableActor (on EnablerComponent)
                                     ↓
                                AssignDelegate (OnPlayerTeleported)
                                     ← CreateDelegate (→ OnPlayerTeleported_Event_0)
```

### Key Details

1. **BeginPlay** fires `ListenMessages` (a custom event on self), then broadcasts a `Msg_TeleportPoint` message with a soft reference to a specific `BP_TeleportPoint` actor.

2. **ListenMessages** sets up an async listener on channel `Message.Event.TeleportPoint`. When a message arrives:
   - Break the `Msg_TeleportPoint` struct to get the `Teleport` soft object reference
   - Async load the asset
   - Cast to `TeleportPoint`
   - Call `EnableActor()` on its `EnablerComponent`
   - Bind `OnPlayerTeleported` delegate to `OnPlayerTeleported_Event_0` custom event

3. The **MakeStruct → BroadcastMessage** pattern constructs the message payload inline:
   - `K2Node_Literal` references the specific `BP_TeleportPoint_C_0` actor in the level
   - `K2Node_ConvertAsset` converts the hard reference to a soft reference
   - `K2Node_MakeStruct` wraps it in `Msg_TeleportPoint`
   - `K2Node_CallFunction` calls `K2_BroadcastMessage` on `GameplayMessageSubsystem`

## Pattern 2: OnPlayerTeleported Handler (MultiGate)

**Purpose**: When a teleport point is used, disable it, check which one it was, and advance the story.

### Node Chain

```
OnPlayerTeleported_Event_0 (CustomEvent, outputs TeleportPoint)
     ↓ exec
MultiGate (Out 0, Out 1)
     ↓ Out 0                              ↓ Out 1
IsValid (macro)                      IsValid (macro)
     ↓ Is Valid                           ↓ Is Valid
DisableActor (EnablerComponent)      DisableActor (EnablerComponent)
     ↓                                    ↓
Branch (== BP_TeleportPoint_C_0?)    Branch (== BP_TeleportPoint_C_0?)
     ↓ true                              ↓ true
BroadcastMessage                     BroadcastMessage
  Channel: Message.Event.StoryStep     Channel: Message.Event.StoryStep
  Msg_StoryStep { Step = 3 }           Msg_StoryStep { Step = 7 }
```

### Key Details

1. **MultiGate** routes sequential teleport events to different handlers. First teleport → Out 0, second → Out 1, etc.

2. Each gate output follows the same sub-pattern:
   - **IsValid** macro checks the TeleportPoint reference
   - **DisableActor** on the TeleportPoint's EnablerComponent (prevents re-use)
   - **Branch** compares the teleport point to a known `K2Node_Literal` reference
   - If match → **BroadcastMessage** on `Message.Event.StoryStep` with a step index

3. The **Knot** nodes (K2Node_Knot) are reroute nodes that fan out the TeleportPoint reference to multiple consumers (IsValid, VariableGet, comparison).

4. **Story step values**: Each gate broadcasts a different step index (3, 7, 6, 3 observed). These correspond to entries in the StoryStep DataTable.

## Pattern 3: Story Step Broadcast

**Purpose**: Advance the narrative by broadcasting a step index.

### Nodes Required

```
K2Node_GetSubsystem (GameplayMessageSubsystem)
     ↓ ReturnValue
K2Node_CallFunction (K2_BroadcastMessage)
     ← Channel: (TagName="Message.Event.StoryStep")
     ← Message: Msg_StoryStep struct
          ← K2Node_MakeStruct (Msg_StoryStep)
               ← Step: <integer>
```

### Msg_StoryStep Struct

| Field | Type | Description |
|-------|------|-------------|
| `Step_4_9162A20A46E747E6062EFAAD47D66DAA` | int | Story step index (maps to DataTable row) |

The `PersistentGuid=9162A20A46E747E6062EFAAD47D66DAA` on the Step pin is the **struct member GUID** from the UserDefinedStruct — it must match exactly.

### Msg_TeleportPoint Struct

| Field | Type | Description |
|-------|------|-------------|
| `Teleport_2_9162A20A46E747E6062EFAAD47D66DAA` | TSoftObjectPtr<ATeleportPoint> | Soft reference to teleport point actor |

Same PersistentGuid pattern — `9162A20A46E747E6062EFAAD47D66DAA` is the struct member GUID.

## Pattern 4: Enable/Disable Actor

**Purpose**: Toggle an actor's interactability.

### Enable
```
K2Node_VariableGet (TeleportPoint.EnablerComponent)
     ↓ EnablerComponent
K2Node_CallFunction (EnablerComponent.EnableActor)
```

### Disable
```
K2Node_VariableGet (TeleportPoint.EnablerComponent)
     ↓ EnablerComponent
K2Node_CallFunction (EnablerComponent.DisableActor)
```

The VariableGet requires:
- `VariableReference=(MemberParent=".../SohVr.TeleportPoint'",MemberName="EnablerComponent")`
- `SelfContextInfo=NotSelfContext`
- A `self` input pin linked to the TeleportPoint object reference

## Asset Paths (Project-Specific)

| Asset | Path |
|-------|------|
| BP_TeleportPoint Blueprint | `/Game/Blueprints/Game/BP_TeleportPoint` |
| BP_TeleportPoint Generated Class | `/Game/Blueprints/Game/BP_TeleportPoint.BP_TeleportPoint_C` |
| Msg_TeleportPoint struct | `/Game/Blueprints/Data/Message/Msg_TeleportPoint.Msg_TeleportPoint` |
| Msg_StoryStep struct | `/Game/Blueprints/Data/Message/Msg_StoryStep.Msg_StoryStep` |
| TeleportPoint C++ class | `/Script/SohVr.TeleportPoint` |
| EnablerComponent C++ class | `/Script/SohVr.EnablerComponent` |
| GameplayMessageSubsystem | `/Script/GameplayMessageRuntime.GameplayMessageSubsystem` |
| IsValid macro | `/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:IsValid` |
| StandardMacros blueprint | `/Engine/EditorBlueprintResources/StandardMacros.StandardMacros` |
| IsValid macro GUID | `64422BCD430703FF5CAEA8B79A32AA65` |

## C++ Class Hierarchy

```
AActor
  └─ AActivatableActor (has SceneRoot + ActivatableComponent)
       └─ ATeleportPoint (has EnablerComponent, OnPlayerTeleported delegate)

UActorComponent
  └─ UEnablerComponent (EnableActor/DisableActor, OnEnableStateChanged)

UInteractableComponent : UActorComponent
  └─ UActivatableComponent
  └─ UTriggerBoxComponent (overlap-based trigger)

UGameInstanceSubsystem
  └─ USceneManager (scene loading/switching)

UBlueprintFunctionLibrary
  └─ UStoryHelpers (LoadStepsFromDataTable, TeleportPlayer, FindPlayerStartPoint)
```

## Screenplay Interaction Types → Blueprint Mapping

| Script Notation | Blueprint Pattern |
|----------------|-------------------|
| `//NAVIGATION INTERACTIVITY: ... LOCATION MARKER ...//` | TeleportPoint enable → OnPlayerTeleported → story step |
| `//GAZE INTERACTIVITY: ...//` | TBD — likely ActivatableComponent with gaze overlap |
| `//GRIP INTERACTIVITY: ...//` | TBD — likely ActivatableComponent with grip input |
| `//TRIGGER INTERACTIVITY: ...//` | TBD — likely TriggerBoxComponent or custom input |
| `//GRIP + TRIGGER INTERACTIVITY: ...//` | TBD — combined input pattern |
