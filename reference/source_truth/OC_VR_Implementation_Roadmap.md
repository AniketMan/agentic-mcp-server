## Context
This document serves as a **technical implementation roadmap** for Scenes 0-9 (Tutorial through Chapter 3 Finale) of Ordinary Courage VR. It maps each script beat to:
- Content Browser assets (existing or `[makeTempBP]` for assets that need creation)
- Interaction component types from the C++ framework
- Spawn/despawn timing
- Level sequences that drive animations
- **VO/Subtitle lines** tied to each sequence

The programmer does not need to know the story—just what assets to place, what components they need, and when they activate.

**Subtitle System:** VO lines are embedded in SoundWave assets via the `Subtitles` property. When `BP_VoiceSource` plays the audio, `FSubtitleManager` broadcasts to `UVrSubtitlesComponent` which displays via `BP_SubtitlesActor`.

---

# SCENE 00: Tutorial / Dynamic Environment
**Level:** `ML_Main` or dedicated tutorial sublevel
**Player Start:** `BP_PlayerStartPoint`

## Sequence 00-A: Path to Door

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 1 | `BP_FloatingOrb` (array) | — | Level Start | Spawn along path | On door enter |
| 2 | `BP_LocationMarker` | `UTriggerBoxComponent` | Level Start | Visible at position 1 | On player overlap |
| 3 | `BP_VoiceSource` | — | Marker 1 overlap | Play Narrator VO | On VO end |

**On Marker 1 Overlap:**
- Disable `USohVrMovementComponent::SetTeleportationEnabled(false)` on `BP_PlayerPawn`

## Sequence 00-B: Gaze Words

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 4 | `BP_GazeText` ("Listen") | `UObservableComponent` | After Marker 1 | Enable observation | On gaze complete → particle dissolve |
| 5 | `BP_GazeText` ("Notice") | `UObservableComponent` | After Marker 1 | Enable observation | On gaze complete → particle dissolve |
| 6 | `BP_GazeText` ("Remember") | `UObservableComponent` | After Marker 1 | Enable observation | On gaze complete → particle dissolve |

**On All 3 Gaze Complete:**
- Enable `BP_ObjectOfLight`

## Sequence 00-C: Grip Object

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 7 | `BP_ObjectOfLight` | `UGrabbableComponent` | After gaze sequence | Enable grab | On trigger release |
| 8 | `Heartbeat` (Haptic) | — | `OnInteractionStart` | Play haptic loop | On drop |

**On Grip Held:**
- `BP_PlayerPawn` → `PlayHapticEffect(Heartbeat)` on active hand

**On Trigger Release (Drop):**
- Spawn particle burst via `NS_MemoryStream` `[makeTempBP]`
- Enable `BP_LocationMarker` #2

## Sequence 00-D: Walk to Door

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 9 | `BP_LocationMarker` #2 | `UTriggerBoxComponent` | After object release | Visible | On overlap |
| 10 | `BP_Door_Tutorial` `[makeTempBP]` | `UGrabbableComponent` (snap mode) | After Marker 2 | Highlight door | — |
| 11 | `BP_Trigger` (door threshold) | `UTriggerBoxComponent` | On door grab | — | — |

**On Door Threshold Enter:**
- `BP_PlayerCameraManager` → `FadeOut()`
- `USceneManager::SwitchToSceneLatent(1)` → Load Scene 01

---

# SCENE 01: Home - Larger Than Life
**Level:** `ML_Main` → Sublevel `SL_SusanHome_Logic`
**Player Start:** `BP_PlayerStartPoint` in kitchen

## Sequence 01-A: Heather Child Entrance

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 1 | `BP_HeatherChild` | `UObservableComponent` | Level Start (LS_1_1) | Spawn in hallway | After hug sequence |
| 2 | `BP_AmbienceSound` | — | Level Start | Refrigerator hum | — |
| 3 | `BP_VoiceSource` | — | Level Start | Susan VO | — |

**Level Sequence:** `LS_1_1` - Heather enters, dances, runs to Susan

## Sequence 01-B: Gaze at Heather

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 4 | `BP_HeatherChild` | `UObservableComponent` | During LS_1_1 | Gaze activates glowing aura VFX | — |

**On Gaze Accumulation Complete:**
- Trigger `NS_JoyfulAura` `[makeTempBP]` around `BP_HeatherChild`

## Sequence 01-C: Hug Interaction

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 5 | `BP_HeatherChild` | `UGrabbableComponent` (proxy on child) | After LS_1_1 | Glowing silhouette prompt | After grip release |
| 6 | `Heartbeat` (Haptic) | — | `OnInteractionStart` | Play heartbeat | On animation end |

**Level Sequence:** `LS_1_2` - Hug animation plays while grip held

**On Hug Sequence End:**
- Enable `BP_TeleportPoint` near kitchen table
- `BP_HeatherChild` runs to kitchen → Cross-fade to `BP_HeatherPreTeen2`

---

# SCENE 02: Standing Up For Others
**Level:** Same sublevel, continuous

## Sequence 02-A: Walk to Kitchen Table

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 1 | `BP_HeatherPreTeen2` | — | After Scene 01 | At table drawing | After door exit |
| 2 | `BP_TeleportPoint` (table) | `ATeleportPoint` | After hug | Enable | On arrival |
| 3 | `BP_LocationMarker` (table) | `UTriggerBoxComponent` | After hug | Visible | On overlap |

**On Marker Overlap:**
- Disable `USohVrMovementComponent::SetTeleportationEnabled(false)`

## Sequence 02-B: Illustration Interaction

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 4 | `BP_Illustration` `[makeTempBP]` | `UGrabbableComponent` | On table proximity | Highlight | Auto-return after sequence |
| 5 | `BP_VoiceSource` | — | `OnInteractionStart` | Susan VO | — |

**On Grip:**
- Play `LS_2_2_AnimatedIllustration` `[makeTempBP]` - 2D vignette animation on paper surface

**Level Sequence:** `LS_2_2` - Illustration animates (classroom scene)

**On Sequence End:**
- Paper auto-returns to table (`UGrabbableComponent::ForceEndInteraction()`)
- Enable `BP_TeleportPoint` (door)
- Highlight `BP_Door` with glowing silhouette

## Sequence 02-C: Door to Scene 03

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 6 | `BP_Door` | `UGrabbableComponent` (snap mode) | After illustration | Highlight | — |

**On Door Grab:**
- Play door open animation
- Spawn `BP_Heather_Teen`, `BP_FriendMale`, `BP_FriendFemale` behind door

---

# SCENE 03: Rescuers
**Level:** Same sublevel, continuous

## Sequence 03-A: Friends Enter

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 1 | `BP_Heather_Teen` | — | On door open | Enter with friends | After hallway exit |
| 2 | `BP_FriendMale` | — | On door open | Enter | After hallway exit |
| 3 | `BP_FriendFemale` | — | On door open | Enter | After hallway exit |
| 4 | `BP_Door` | `UGrabbableComponent` | — | Auto-closes | — |

**Level Sequence:** `LS_3_1` - Friends enter, sit at table

## Sequence 03-B: Fridge Interaction

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 5 | `BP_Fridge` `[makeTempBP]` | `UGrabbableComponent` (door only) | After friends sit | Highlight | — |
| 6 | `BP_SfxSound` | — | On fridge open | Fridge open SFX | — |

**On Fridge Grab:**
- Auto-open door (animated)
- Reveal `BP_PourablePitcher` inside

## Sequence 03-C: Pitcher Interaction

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 7 | `BP_PourablePitcher` | `UGrabbableComponent` | On fridge open | Highlight | After all glasses filled |
| 8 | `BP_Fridge` | — | On pitcher grab | Auto-close door | — |

**On Pitcher Grab:**
- Close fridge
- Enable pour interaction on `BP_PourablePitcher`

## Sequence 03-D: Pour Water

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 9 | `BP_Glass` x3 `[makeTempBP]` | `UActivatableComponent` (pour target) | On pitcher grab | Highlight | — |
| 10 | `BP_SfxSound` | — | On pour | Water pour SFX | — |

**Pour Logic (in `BP_PourablePitcher`):**
- Check trigger input + proximity to glass
- Fill glass mesh material parameter
- Increment `glassesFilledCount`

**On All 3 Glasses Filled:**
- Play `LS_3_3` - Cheers animation
- Friends + Heather retreat to hallway
- Enable Scene 04 progression

---

# SCENE 04: Stepping Into Adulthood
**Level:** Same sublevel, continuous

## Sequence 04-A: Phone Text

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 1 | `BP_PhoneInteraction` | `UGrabbableComponent` + `UActivatableComponent` | After friends leave | On side table, highlight | On drop |
| 2 | `BP_SfxSound` | — | Level Sequence | Text DING sound | — |
| 3 | `BP_SimpleWorldWidget` | — | On phone grab | 3D text messages | On phone drop |

**On Phone Grab:**
- Spawn 3D text widgets around phone
- Each trigger press advances message sequence

**Text Sequence (trigger to advance):**
1. HEATHER: "Hey Mom, what's you SSN?"
2. SUSAN: "Why do you need that?"
3. HEATHER: "I'm adding you as my beneficiary"
4. SUSAN: "Well I'd rather have you than money."
5. HEATHER: "Mom!"
6. SUSAN: "I love you."
7. HEATHER: "Love you more! Don't forget dinner next week."

**On Phone Drop:**
- Dismiss text widgets
- Highlight `BP_Door` (front door)

## Sequence 04-B: Exit to Restaurant

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 4 | `BP_Door` (front) | `UGrabbableComponent` | After phone | Highlight | — |

**On Door Grab:**
- `USceneManager::SwitchToSceneLatent()` → Load Restaurant

---

# SCENE 05: Dinner Together
**Level:** `ML_Restaurant`
**Player Start:** `BP_PlayerStartPoint` at entrance

## Sequence 05-A: Join Heather at Table

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 1 | `BP_Heather_Adult` | — | Level Start | Seated at table | Scene end |
| 2 | `BP_AmbienceSound` | — | Level Start | Restaurant ambience | — |
| 3 | `BP_TeleportPoint` (table) | `ATeleportPoint` | Level Start | Enabled | On arrival |
| 4 | `BP_LocationMarker` (chair) | `UTriggerBoxComponent` | Level Start | Visible | On overlap |

**On Marker Overlap:**
- Sit Susan down (camera animation)
- Disable teleportation

## Sequence 05-B: Hand Hold

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 5 | `BP_HandPlacement` | `UGrabbableComponent` (hand proxy) | After sit | Palm-up silhouette prompt | — |
| 6 | `Heartbeat` (Haptic) | — | On hand connect | Rapid → steady pulse | — |

**On Hand Placement Aligned:**
- `BP_Heather_Adult` places hand in Susan's
- Play `Heartbeat` haptic (fast)

**On Grip Held:**
- Heartbeat slows to steady
- Fade environment to black (isolate table)
- Play `LS_5_4` - intimate moment

## Sequence 05-C: Transition to Chapter 2

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 7 | `BP_VoiceSource` | — | After sequence | Narrator VO | — |

**On Sequence End:**
- Fade to black
- `USceneManager::SwitchToSceneLatent()` → Load Scene 06 (Chapter 2)

---

# SCENE 06: The Rally in Charlottesville (Chapter 2)
**Level:** `ML_Scene6` (Dynamic Environment / Void)
**Player Start:** `BP_PlayerStartPoint` in void

## Environment Setup

| Asset | Component | Position | Notes |
|-------|-----------|----------|-------|
| `BP_car` | — | Center, suspended | Dodge Challenger on chain |
| `BP_PCrotate` | — | Station 1 | Computer, rotates until approached |
| `BP_Balance` | — | Station 2 | Scale with matchsticks |
| `BP_nCrate` | — | Station 3 | Newton's cradle |
| `BP_cardbaordTorn` | — | Station 4 | Torn protest sign |

## Sequence 06-A: Computer / Echo Chamber

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 1 | `BP_FloatingOrb` (path) | — | Level Start | Path to computer | On arrival |
| 2 | `BP_LocationMarker` (computer) | `UTriggerBoxComponent` | Level Start | Visible | On overlap |
| 3 | `BP_PCrotate` | `UActivatableComponent` | On marker | Stop rotation, show screen | — |
| 4 | `BP_ShapeSelector` `[makeTempBP]` | `UActivatableComponent` x2 | On screen active | Triangle + Square buttons | On selection |

**On Shape Selection (Trigger):**
- Play `LS_6_1` - Echo chamber forms around player
- Spawn `BP_EchoChamber` `[makeTempBP]` (prism enclosure)

**On Echo Chamber Sequence End:**
- Open exit gap
- Reveal path to Station 2

## Sequence 06-B: Scale / Tiki Torch March

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 5 | `BP_LocationMarker` (scale) | `UTriggerBoxComponent` | After echo chamber | Visible | On overlap |
| 6 | `BP_Balance` | — | On marker | Lit match on raised end | — |
| 7 | `BP_Weight` `[makeTempBP]` | `UGrabbableComponent` | On marker | Grabbable weight | After placement |
| 8 | `BP_ScaleDropZone` `[makeTempBP]` | `UTriggerBoxComponent` | On marker | Drop target | — |

**On Weight Placed on Lit Match Side:**
- Play `LS_6_2` - Matches ignite domino effect
- Spawn `BP_TorchMarcher` `[makeTempBP]` (silhouettes, array) around player

**On Torch Sequence End:**
- Open exit gap
- Reveal path to Station 3

## Sequence 06-C: Newton's Cradle / Rally Confrontation

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 9 | `BP_LocationMarker` (cradle) | `UTriggerBoxComponent` | After torch march | Visible | On overlap |
| 10 | `BP_nCrate` | — | On marker | Cradle with 2 visible spheres | — |
| 11 | `BP_CradleSphere` `[makeTempBP]` | `UGrabbableComponent` | On marker | Pullable sphere | After release |

**On First Pull + Release:**
- Play `LS_6_3a` - Spheres collide
- Spawn 2 giant silhouettes (protester + counter-protester) on either side

**Cradle Reset → 3 Red Spheres:**

**On Second Pull + Release:**
- Play `LS_6_3b` - Stronger collision
- Spawn additional silhouettes
- Chaos erupts (shoving, debris)

**On Chaos Sequence End:**
- Open wind tunnel exit
- Reveal path to Station 4

## Sequence 06-D: Torn Sign / Car Attack

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 12 | `BP_LocationMarker` (sign) | `UTriggerBoxComponent` | After chaos | Visible | On overlap |
| 13 | `BP_cardbaordTorn` | `UGrabbableComponent` | On marker | Floating torn sign | On grab |

**On Sign Grab:**
- All spotlights flicker OFF
- `BP_car` chain releases (SFX: chain crank, engine rev, snap)
- Car falls toward center
- Blackout before impact

**After Blackout:**
- Single spotlight on `BP_PhoneInteraction` (floating)
- Phone rings

**On Phone Grab:**
- Play phone call audio: "Susan— The car— Heather had been hit— CLICK."
- Spawn `BP_Door_Hospital` `[makeTempBP]` with Susan silhouette

**On Walk Toward Door:**
- Pull player through doorway
- `USceneManager::SwitchToSceneLatent()` → Load Hospital

---

# SCENE 07: The Hospital (Chapter 2 continued)
**Level:** `ML_Hospital`
**Player Start:** Hospital lobby entrance

## Sequence 07-A: Reception

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 1 | `BP_Recepcionist` | — | Level Start | Behind desk | — |
| 2 | `BP_Officer1` | — | Level Start | Standing to right | — |
| 3 | `BP_Officer2` | — | Level Start | Standing to right | — |
| 4 | `BP_LocationMarker` (desk) | `UTriggerBoxComponent` | Level Start | Visible | On overlap |

**On Marker Overlap:**
- Disable teleportation
- Receptionist slides `BP_NumberCard` `[makeTempBP]` toward Susan

## Sequence 07-B: Number Card

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 5 | `BP_NumberCard` `[makeTempBP]` | `UGrabbableComponent` | On desk proximity | "20" card, highlight | Held through scene |

**On Card Grab:**
- Officers approach Susan
- Haptic feedback on arm touch
- Begin hallway walk (3DoF guided)

## Sequence 07-C: Hallway Walk

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 6 | `BP_HospitalSequence` | — | On card grab | Guided walk, tunneled vignette | — |
| 7 | `BP_Detective` | — | In meeting room | Waiting | — |

**Level Sequence:** `LS_7_Hallway` - Guided 3DoF walk with vignette effect

## Sequence 07-D: The News

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 8 | `BP_VoiceSource` | — | On room enter | Detective line + Susan wail | — |

**On Detective Line:**
- All ambient sound cuts
- Deafening ringing
- Susan's vision blurs, fades to black
- End Chapter 2

---

# SCENE 08: Turning Grief Into Action (Chapter 3)
**Level:** `ML_Main` → Same home sublevel
**Player Start:** Kitchen

## Sequence 08-A: Memory Matching Setup

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 1 | `BP_MemoryMatching` | — | Level Start | Manages matching logic | — |
| 2 | Photos x4 | — | On table | Child, PreTeen, Teen, Adult | — |
| 3 | `BP_Teapot_Grabbable` `[makeTempBP]` | `UGrabbableComponent` | Scattered in home | Highlight | On correct match |
| 4 | `BP_Illustration_Grabbable` `[makeTempBP]` | `UGrabbableComponent` | Scattered in home | Highlight | On correct match |
| 5 | `BP_PourablePitcher` (sealed) | `UGrabbableComponent` | Scattered in home | Highlight | On correct match |
| 6 | `BP_PhoneInteraction` | `UGrabbableComponent` | Scattered in home | Highlight | On correct match |

## Sequence 08-B: Object Matching

| Object | Target Photo | On Match Spawn |
|--------|--------------|----------------|
| `BP_Teapot_Grabbable` | Childhood | `BP_HeatherChild` at table |
| `BP_Illustration_Grabbable` | PreTeen | `BP_HeatherPreTeen2` at table |
| `BP_PourablePitcher` | Teen | `BP_Heather_Teen` at table |
| `BP_PhoneInteraction` | Adult | `BP_Heather_Adult` at table |

**Match Logic (in `BP_MemoryMatching`):**
- `UGrabbableComponent::OnInteractionEnd` → Check drop zone
- If correct zone: Play Susan VO, spawn Heather, `matchCount++`
- If wrong zone: Return object to original position

**On All 4 Matches Complete:**
- All Heathers hold hands in circle
- Extend hands toward Susan

## Sequence 08-C: Circle of Unity

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 7 | `BP_HandPlacement` (left) | `UGrabbableComponent` | After matches | Highlight | On grip |
| 8 | `BP_HandPlacement` (right) | `UGrabbableComponent` | After matches | Highlight | On grip |

**On Both Hands Gripped:**
- Play `LS_8_Final` - Circle connection
- Transition to Scene 09

---

# SCENE 09: Legacy in Bloom (Chapter 3 finale)
**Level:** `ML_Scene9`
**Player Start:** `BP_PlayerStartPoint`
**Level Sequences:** `LS_9_1`, `LS_9_2`, `LS_9_3`, `LS_9_4`, `LS_9_5`, `LS_9_6`

## Sequence 09-A: Environment Transforms → `LS_9_1`

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 1 | `FlowerOpen_clones` | — | Level Start | Purple flowers bloom around player | — |
| 2 | `LS_flowers_animation` | — | Level Start | Flower growth animation | — |
| 3 | `BP_VoiceSource` | — | Level Start | Susan VO | — |
| 4 | `BP_AmbienceSound` | — | Level Start | Ethereal ambient | — |

**Level Sequence:** `LS_9_1` - Home fades away, flowers begin sprouting

**VO Lines (LS_9_1):**
```
SUSAN: "After losing Heather, I didn't know how to move forward."
SUSAN: "I knew I couldn't replace her, but I could honor her."
SUSAN: "And slowly, with support from so many others, I learned to transform pain into purpose."
```

## Sequence 09-B: Foundation Imagery → `LS_9_2`

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 5 | `BP_Image_Player` | — | After LS_9_1 | Foundation scholarship recipients | — |

**Level Sequence:** `LS_9_2` - Scholarship recipient images emerge

**VO Lines (LS_9_2):**
```
SUSAN: "We created the Heather Heyer Foundation, supporting passionate individuals dedicated to making positive change."
SUSAN: "Scholarships helped students pursuing justice, equality, and compassion."
```

## Sequence 09-C: Youth Programs → `LS_9_3`

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 6 | `BP_Image_Player` | — | After LS_9_2 | Youth activism groups, Rose Bowl Parade, Smithsonian | — |

**Level Sequence:** `LS_9_3` - Partnership and youth program images

**VO Lines (LS_9_3):**
```
SUSAN: "Through partnerships and community programs, we empowered youth to raise their voices against hate, inspired by Heather's bravery and compassion."
```

## Sequence 09-D: NO HATE Act → `LS_9_4`

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 7 | `BP_Image_Player` | — | After LS_9_3 | Legislative signing imagery | — |

**Level Sequence:** `LS_9_4` - Animated signatures form into documents

**VO Lines (LS_9_4):**
```
SUSAN: "Together, we advocated for change— real, lasting change."
SUSAN: "Laws that improve how hate crimes are reported and responded to, ensuring no one's suffering goes unseen."
```

## Sequence 09-E: Media & Legacy → `LS_9_5`

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 8 | `BP_Video_Player` | — | After LS_9_4 | MTV VMAs, talk shows, community gatherings | — |

**Level Sequence:** `LS_9_5` - Media snippets flow through space

**VO Lines (LS_9_5):**
```
SUSAN: "The world wanted to understand who Heather was."
SUSAN: "Through conversations, through education, and through community— we made sure they saw the heart of who she was."
SUSAN: "Her legacy lives on in every person inspired to act."
```

## Sequence 09-F: Sunrise Finale → `LS_9_6`

| Order | Asset | Component | Trigger | Action | Despawn |
|-------|-------|-----------|---------|--------|---------|
| 9 | Sun (Directional Light) | — | After LS_9_5 | Sun reaches zenith | — |
| 10 | `BP_PlayerCameraManager` | — | On VO end | Fade to white | — |

**Level Sequence:** `LS_9_6` - Flowers and images swirl into mosaic, sun rises

**VO Lines (LS_9_6):**
```
SUSAN: "Heather showed me that every small action matters, every voice counts."
SUSAN: "And when we choose love over hate, compassion over fear, we plant something powerful— something lasting."
SUSAN: "Ordinary people can do extraordinary things."
SUSAN: "These are the seeds Heather planted, the legacy she left behind."
SUSAN: "A gentle reminder that HOPE— still exists."
```

**On Sequence End:**
- Fade to white
- End experience

---

# Assets to Create `[makeTempBP]` with Logic

## Tutorial (Scene 00)

### `BP_Door_Tutorial`
**Components:** `UGrabbableComponent` (EGrabMode::Snap), `UStaticMeshComponent`, `UEnablerComponent`
**Logic:**
```
BeginPlay:
  - UEnablerComponent::DisableActor()
  - UGrabbableComponent::SetInteractable(false)

EnableDoor (called from Level BP):
  - UEnablerComponent::EnableActor()
  - UGrabbableComponent::SetInteractable(true)
  - Play highlight material

OnInteractionStart:
  - Play door open animation (Timeline)
  - Disable UGrabbableComponent
```

### `NS_MemoryStream`
**Type:** Niagara System
**Visual:** Swirling light particles rising upward, dissipating
**Trigger:** Spawned on `BP_ObjectOfLight` trigger release location

### `NS_JoyfulAura`
**Type:** Niagara System
**Visual:** Soft glowing aura particles around character
**Trigger:** Spawned around `BP_HeatherChild` when `UObservableComponent` accumulation complete

---

## Scene 02 - Illustration

### `BP_Illustration`
**Components:** `UGrabbableComponent`, `UStaticMeshComponent` (SM_IllustrationPaper), `UWidgetComponent`
**Logic:**
```
BeginPlay:
  - UGrabbableComponent::SetInteractable(false)

EnableIllustration (called from Level BP):
  - UGrabbableComponent::SetInteractable(true)
  - Play highlight material

OnInteractionStart:
  - Start Level Sequence: LS_2_2R (bound to paper surface widget)
  - Play BP_VoiceSource (Susan VO lines)

OnSequenceEnd (bound in Level BP):
  - UGrabbableComponent::ForceEndInteraction()
  - Lerp paper back to table position
  - Enable BP_TeleportPoint (door)
  - Highlight BP_Door
```

---

## Scene 03 - Kitchen

### `BP_Fridge`
**Components:** `UGrabbableComponent` (on door child actor), `UStaticMeshComponent`, skeletal or static door
**Logic:**
```
BeginPlay:
  - UGrabbableComponent::SetInteractable(false)
  - bDoorOpen = false

EnableFridge (called from Level BP after LS_3_1):
  - UGrabbableComponent::SetInteractable(true)
  - Play highlight material on door handle

OnInteractionStart:
  - Play door open animation (Timeline rotating door mesh)
  - BP_SfxSound::PlaySound(FridgeOpen)
  - UGrabbableComponent::SetInteractable(false)
  - Enable BP_PourablePitcher inside
  - bDoorOpen = true

CloseDoor (called when pitcher grabbed):
  - Play door close animation
  - BP_SfxSound::PlaySound(FridgeClose)
```

### `BP_Glass` (x3 instances)
**Components:** `UActivatableComponent`, `UStaticMeshComponent`, Material with FillLevel parameter
**Logic:**
```
Variables:
  - FillLevel: float (0.0 → 1.0)
  - bIsFilled: bool

BeginPlay:
  - FillLevel = 0.0
  - UActivatableComponent::SetInteractable(false)

EnableGlass (called from Level BP):
  - UActivatableComponent::SetInteractable(true)
  - Play highlight material

ReceivePour (called from BP_PourablePitcher when trigger + proximity):
  - If FillLevel < 1.0:
    - FillLevel += DeltaTime * PourRate
    - Update material parameter "FillLevel"
    - BP_SfxSound::PlaySound(WaterPour)
  - If FillLevel >= 1.0:
    - bIsFilled = true
    - UActivatableComponent::SetInteractable(false)
    - Broadcast OnGlassFilled delegate
```

---

## Scene 06 - Dynamic Environment

### `BP_ShapeSelector`
**Components:** `UActivatableComponent` x2 (Triangle, Square), `UStaticMeshComponent` x2
**Logic:**
```
BeginPlay:
  - UActivatableComponent::SetInteractable(false) on both

EnableSelector (called from Level BP):
  - UActivatableComponent::SetInteractable(true) on both
  - Play glow material on shapes

OnTriangleInteractionStart / OnSquareInteractionStart:
  - SelectedShape = "Triangle" or "Square"
  - UActivatableComponent::SetInteractable(false) on both
  - Broadcast OnShapeSelected(SelectedShape)
  - [Level BP catches this, plays LS_6_1]
```

### `BP_EchoChamber`
**Components:** `UStaticMeshComponent` (prism walls), `UEnablerComponent`, Niagara particles
**Logic:**
```
SpawnChamber (called from Level BP after shape selection):
  - Spawn at player location
  - Play formation animation (walls closing in)
  - Spawn social media post widgets on walls (BP_SimpleWorldWidget)

OpenExit (called from Level BP after LS_6_1):
  - Play wall dissolve animation on one side
  - Spawn BP_FloatingOrb path to next station
```

### `BP_Weight`
**Components:** `UGrabbableComponent` (EGrabMode::Attach, EReleaseMode::Simulate), `UStaticMeshComponent`
**Logic:**
```
BeginPlay:
  - UGrabbableComponent::SetInteractable(false)

EnableWeight (called from Level BP):
  - UGrabbableComponent::SetInteractable(true)
  - Play highlight

OnInteractionEnd:
  - Check overlap with BP_ScaleDropZone
  - If overlapping correct zone:
    - Broadcast OnWeightPlaced
    - [Level BP catches, plays LS_6_2]
```

### `BP_ScaleDropZone`
**Components:** `UTriggerBoxComponent`
**Logic:**
```
OnComponentBeginOverlap:
  - If OtherActor is BP_Weight:
    - bWeightInZone = true

OnComponentEndOverlap:
  - bWeightInZone = false
```

### `BP_TorchMarcher`
**Components:** `UStaticMeshComponent` (silhouette mesh), `USplineComponent` (walk path)
**Logic:**
```
SpawnMarchers (called from Level BP after LS_6_2):
  - Spawn array of silhouettes around player
  - Each follows spline path, carrying tiki torch mesh
  - Loop walking animation
  - Play ambient chanting audio
```

### `BP_CradleSphere`
**Components:** `UGrabbableComponent` (custom physics constraint), `UStaticMeshComponent`
**Logic:**
```
BeginPlay:
  - UGrabbableComponent::SetInteractable(false)
  - Constrain to pendulum pivot point

EnableSphere (called from Level BP):
  - UGrabbableComponent::SetInteractable(true)

OnInteractionStart:
  - Allow player to pull back (constrained to arc)
  - Track pull distance

OnInteractionEnd:
  - Release sphere (physics takes over)
  - On collision with other spheres:
    - BP_SfxSound::PlaySound(CradleClack)
    - Broadcast OnCradleCollision(PullDistance)
    - [Level BP catches, plays LS_6_3 or LS_6_4 based on count]
```

### `BP_Door_Hospital`
**Components:** `UStaticMeshComponent`, `UTriggerBoxComponent` (threshold)
**Logic:**
```
SpawnDoor (called after phone call in Scene 06):
  - Fade in door mesh with Susan silhouette
  - Enable BP_Trigger threshold

OnThresholdEnter:
  - Pull player through (lerp transform)
  - BP_PlayerCameraManager::FadeOut()
  - USceneManager::SwitchToSceneLatent(7)
```

---

## Scene 07 - Hospital

### `BP_NumberCard`
**Components:** `UGrabbableComponent`, `UStaticMeshComponent` (card with "20"), `UWidgetComponent`
**Logic:**
```
BeginPlay:
  - UGrabbableComponent::SetInteractable(false)
  - Hidden

SlideToPlayer (called from Level BP after desk marker):
  - SetActorHiddenInGame(false)
  - Play slide animation toward Susan
  - UGrabbableComponent::SetInteractable(true)

OnInteractionStart:
  - Broadcast OnCardGrabbed
  - [Level BP catches, starts LS_7_4 hallway walk]
  - Card stays attached to hand through scene
```

---

## Scene 08 - Memory Matching

### `BP_Teapot_Grabbable`
**Components:** `UGrabbableComponent`, `UStaticMeshComponent` (Teapot mesh)
**Logic:**
```
Variables:
  - CorrectDropZone: BP_PhotoDropZone (Childhood)
  - OriginalTransform: FTransform

BeginPlay:
  - Store OriginalTransform
  - UGrabbableComponent::SetInteractable(true)

OnInteractionEnd:
  - Check overlap with BP_PhotoDropZone actors
  - If overlapping CorrectDropZone:
    - Snap to photo position
    - UGrabbableComponent::SetInteractable(false)
    - Broadcast OnCorrectMatch("Teapot")
    - [Level BP spawns BP_HeatherChild, plays VO]
  - Else:
    - Lerp back to OriginalTransform
```

### `BP_Illustration_Grabbable`
**Same logic as BP_Teapot_Grabbable, CorrectDropZone = PreTeen photo**

### `BP_PhotoDropZone` (x4 instances)
**Components:** `UTriggerBoxComponent`, `UStaticMeshComponent` (photo frame)
**Logic:**
```
Variables:
  - ExpectedObjectTag: FName ("Teapot", "Illustration", "Pitcher", "Phone")
  - bMatched: bool

OnComponentBeginOverlap:
  - If OtherActor has matching tag:
    - bMatched = true
    - Play glow effect

OnComponentEndOverlap:
  - bMatched = false
```

---

# Component Reference Quick Guide

| Component | C++ Class | Use Case | Key Delegates |
|-----------|-----------|----------|---------------|
| **Grabbable** | `UGrabbableComponent` | Pick up objects with grip | `OnInteractionStart`, `OnInteractionEnd` |
| **Activatable** | `UActivatableComponent` | Button press interactions | `OnInteractionStart`, `OnInteractionEnd` |
| **Touchable** | `UTouchableComponent` | Touch/overlap interactions | `OnInteractionStart`, `OnInteractionEnd` |
| **Observable** | `UObservableComponent` | Gaze-based interactions | `OnInteractionStart` (after accumulation) |
| **TriggerBox** | `UTriggerBoxComponent` | Player enter/exit zones | `OnInteractionStart`, `OnInteractionEnd` |
| **Enabler** | `UEnablerComponent` | Enable/disable actors | `OnEnableStateChanged` |

---

# Key System Calls Reference

| Action | System | Call |
|--------|--------|------|
| Switch scene | `USceneManager` | `SwitchToSceneLatent(SceneIndex)` |
| Disable teleport | `USohVrMovementComponent` | `SetTeleportationEnabled(false)` |
| Enable teleport | `USohVrMovementComponent` | `SetTeleportationEnabled(true)` |
| Fade out | `BP_PlayerCameraManager` | `FadeOut()` |
| Fade in | `BP_PlayerCameraManager` | `FadeIn()` |
| Play haptic | `BP_PlayerPawn` | `PlayHapticEffect(HapticAsset, Hand)` |
| Enable interaction | `UInteractableComponent` | `SetInteractable(true)` |
| Force drop | `UGrabbableComponent` | `ForceEndInteraction()` |
| Play sound | `BP_AudioSubsystem` | `PlaySound(SoundAsset)` |

---

# Level Sequence Inventory (All Existing Sequences)

## Scene 1 - Home/Child
| Sequence | Description | VO Content |
|----------|-------------|------------|
| `LS_1_1` | Heather Child enters, dances, runs to Susan | SUSAN: "This is my home— And THAT little whirlwind is my daughter..." |
| `LS_1_2` | Hug animation plays while grip held | SUSAN: "These moments—these little, everyday moments—are what stay with you." |
| `LS_1_3` | Transition, Heather runs to kitchen | [Heather GIGGLES] |
| `LS_1_4` | Cross-fade Child → PreTeen | — |
| `LS_HugLoop` | Looping hug if grip held longer | — |

## Scene 2 - Home/PreTeen
| Sequence | Description | VO Content |
|----------|-------------|------------|
| `LS_2_1` | Heather at table drawing | SUSAN: "Whenever the house grew quiet, I always had to check on Heather..." |
| `LS_2_2R` | Illustration animates (classroom vignette) | SUSAN: "I didn't know at the time, but she got in trouble for this..." [full detention story] |
| `LS_2_2R1` | Illustration variant 1 | — |
| `LS_2_2R2` | Illustration variant 2 | — |
| `LS_2_3` | Heather runs out door | SUSAN: "Sometimes doing the right thing isn't easy, but that was her way." |

## Scene 3 - Home/Teen
| Sequence | Description | VO Content |
|----------|-------------|------------|
| `LS_3_1` | Friends enter, sit at table | SUSAN: "She always had this way of bringing people in—friends, strays..." |
| `LS_3_1_v2` | Friends enter (v2) | — |
| `LS_3_2` | Heather gets glasses from cabinet | SUSAN: "We didn't have much, but Heather made sure everyone felt like they belonged." |
| `LS_3_2_v2` | Cabinet (v2) | — |
| `LS_3_3_v2` | Cheers animation | [Glass CLINK over LAUGHTER] |
| `LS_3_5` | Heather retrieves glasses | SUSAN: "Sometimes I worried she was taking on too much—" |
| `LS_3_5_V2` | Glasses (v2) | — |
| `LS_3_6` | Pour interaction | [Water FILLING SFX] |
| `LS_3_7` | Cheers, friends retreat | SUSAN: "Even in the hardest times, we believed that it wasn't about what you have..." |

## Scene 4 - Home/Adult (Phone)
| Sequence | Description | VO Content |
|----------|-------------|------------|
| `LS_4_1` | Phone notification, setup | SUSAN: "Now don't get me wrong. Heather wasn't anything special..." |
| `LS_4_2` | Text conversation active | SUSAN: "She'd moved out, gotten her own place..." |
| `LS_4_3` | Text conversation continuation | SUSAN: "My heart swelled, but like any parent..." |
| `LS_4_4` | Phone down, door transition | SUSAN: "It's hard to let go, but seeing her become her own person— That was a gift." |

## Scene 5 - Restaurant
| Sequence | Description | VO Content |
|----------|-------------|------------|
| `LS_5_1` | Establish scene, Heather waiting | SUSAN: "Heather and I would meet for dinner just about every other week..." |
| `LS_5_1_intro_loop` | Ambient waiting loop | — |
| `LS_5_2` | Heather talking energetically | SUSAN: "She was talking a mile a minute—" |
| `LS_5_3` | Heather sips, reaches out | SUSAN: "I wanted to tell her everything would be fine, but I couldn't lie." |
| `LS_5_3_grip` | Hand hold interaction | — |
| `LS_5_4` | Intimate moment, fade | SUSAN: "Holding on to her whisky laugh, infectious smile..." + NARRATOR transition |

## Scene 6 - Dynamic Environment (Rally)
| Sequence | Description | VO Content |
|----------|-------------|------------|
| `LS_6_1` | Echo chamber forms | NARRATOR: "In these echo chambers, illusions become truth—fueled by fear..." |
| `LS_6_2` | Matches ignite domino | NARRATOR: "On August 11, hundreds marched by torchlight..." |
| `LS_6_3` | Cradle collision + chaos | NARRATOR: "By midday, momentum built with every angry voice..." |
| `LS_6_4` | Chaos erupts | — |
| `LS_6_5` | Car falls, blackout | NARRATOR: "At 1:42 PM, a car drove into a group of counter-protesters..." |

## Scene 7 - Hospital
| Sequence | Description | VO Content |
|----------|-------------|------------|
| `LS_7_1` | Lobby establish | SUSAN: "I got the call while I was out with a friend. No one knew where Heather was." |
| `LS_7_2` | Reception desk | SUSAN: "When I finally got there, they handed me a number—" |
| `LS_7_3` | Officers approach | SUSAN: "A NUMBER— Where is my daughter?" |
| `LS_7_4` | Hallway walk (guided 3DoF) | SUSAN: "They took me by the arms." + heartbeat audio |
| `LS_7_5` | Enter meeting room | SUSAN: "I kept hoping she was just hurt, just unconscious. My baby girl—" |
| `LS_7_6` | Detective delivers news | DETECTIVE: "Ma'am, your daughter has been pronounced." + SUSAN wail |

## Scene 8 - Home/Memory Matching
| Sequence | Description | VO Content |
|----------|-------------|------------|
| `LS_8_1` | Memory matching setup | SUSAN: "In those first days, the whole world wanted to know who Heather was..." |
| `LS_8_2` | Teapot match → Child Heather | SUSAN: "Heather was always full of life, imagination pouring from her like magic." |
| `LS_8_3` | Illustration match → PreTeen | SUSAN: "Heather's fairness defined her early on." |
| `LS_8_4` | Pitcher match → Teen | SUSAN: "Heather saw the good in everyone, sharing whatever we had." |
| `LS_8_5` | Phone match → Adult + circle | SUSAN: "Heather became someone I deeply admired—responsible, thoughtful..." |

## Scene 9 - Legacy/Finale
| Sequence | Description | VO Content |
|----------|-------------|------------|
| `LS_9_1` | Flowers begin blooming | SUSAN: "After losing Heather, I didn't know how to move forward." |
| `LS_9_2` | Foundation imagery | SUSAN: "We created the Heather Heyer Foundation..." |
| `LS_9_3` | Youth programs | SUSAN: "Through partnerships and community programs..." |
| `LS_9_4` | NO HATE Act signing | SUSAN: "Together, we advocated for change— real, lasting change." |
| `LS_9_5` | Media appearances | SUSAN: "The world wanted to understand who Heather was." |
| `LS_9_6` | Sunrise finale | SUSAN: "These are the seeds Heather planted, the legacy she left behind..." |

---

# Haptic Assets

| Asset | Location | Use |
|-------|----------|-----|
| `Heartbeat` | `Content/Assets/VRTemplate/Haptics/Heartbeat.uasset` | Emotional connection moments |
| `GrabHapticEffect` | `Content/Assets/VRTemplate/Haptics/GrabHapticEffect.uasset` | Default grab feedback |

---

# Audio Assets Reference

| Asset | Type | Use |
|-------|------|-----|
| `BP_VoiceSource` | VO | Susan narration, character dialogue |
| `BP_AmbienceSound` | Ambience | Environment loops (fridge, restaurant) |
| `BP_SfxSound` | SFX | Interaction feedback (ding, pour, door) |
| `BP_MusicSource` | Music | Score, emotional cues |

---

# Summary: Scenes 0-3 Flow

```
SCENE 00 (Tutorial)
├── Walk to marker → Gaze words → Grip object → Walk to door
└── → SCENE 01

SCENE 01 (Home - Child) [LS_1_1 → LS_1_4]
├── Heather enters → Gaze at her → Hug interaction
└── → SCENE 02

SCENE 02 (Home - PreTeen) [LS_2_1 → LS_2_3]
├── Walk to table → Grab illustration → Door opens
└── → SCENE 03

SCENE 03 (Home - Teen) [LS_3_1 → LS_3_7]
├── Friends enter → Fridge → Pitcher → Pour 3 glasses → Cheers
└── → SCENE 04

SCENE 04 (Home - Adult) [LS_4_1 → LS_4_4]
├── Phone text sequence → Front door
└── → SCENE 05

SCENE 05 (Restaurant) [LS_5_1 → LS_5_4]
├── Walk to table → Hand hold → Fade to black
└── → SCENE 06 (Chapter 2)

SCENE 06 (Void / Rally) [LS_6_1 → LS_6_5]
├── Computer (echo chamber) → Scale (torches) → Cradle (chaos) → Sign (car)
└── → SCENE 07

SCENE 07 (Hospital) [LS_7_1 → LS_7_6]
├── Reception → Number card → Hallway walk → Detective news
└── → SCENE 08 (Chapter 3)

SCENE 08 (Home - Memory) [LS_8_1 → LS_8_5]
├── Match 4 objects to 4 photos → All Heathers appear → Hold hands
└── → SCENE 09

SCENE 09 (Legacy) [LS_9_1 → LS_9_6]
├── Flowers bloom → Foundation montage → Fade to white
└── END
```

---

# VO Lines → SoundWave Subtitle Mapping

This section maps each VO line to its Level Sequence for subtitle embedding. Each line should be added to the SoundWave asset's `Subtitles` array with appropriate timing.

## Chapter 1: Mother and Daughter

### Scene 01 (LS_1_1)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "This is my home—" |
| 2.0s | SUSAN | "And THAT little whirlwind is my daughter, Heather Heyer." |
| 6.0s | SUSAN | "She could fill a room with her energy— unstoppable, imaginative, and just a little bit loud." |
| 12.0s | SUSAN | "She had been born with hearing completely blocked on one side, but that never stopped her." |
| 18.0s | SUSAN | "Heather listened to the world in her own way and made sure the world listened to her." |

### Scene 01 (LS_1_2)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "These moments—these little, everyday moments—are what stay with you." |

### Scene 02 (LS_2_1)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "Whenever the house grew quiet, I always had to check on Heather." |
| 4.0s | SUSAN | "Silence usually meant she was up to something." |
| 7.0s | SUSAN | "But sometimes, it was moments like this—just her, a pencil, and a head full of ideas." |

### Scene 02 (LS_2_2R) - Animated Illustration
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "I didn't know at the time, but she got in trouble for this." |
| 4.0s | SUSAN | "I was a teacher myself at the time, and I always told Heather's teachers to treat her like every other kid because I wanted Heather to learn the good and the bad of fairness." |
| 14.0s | SUSAN | "There she was sitting in class being a kid when the teacher threatened one of her classmates with detention." |
| 20.0s | SUSAN | "The boy started to cry and that's when Heather stood up." |
| 25.0s | SUSAN | "She went over and said, 'Don't worry. Detention isn't so bad. I'll go with you.'" |
| 32.0s | SUSAN | "And they went to detention together." |
| 36.0s | SUSAN | "Heather wasn't afraid to stand up for someone—even if it meant detention." |
| 42.0s | SUSAN | "Her sense of fairness was always stronger than her fear of consequences." |

### Scene 02 (LS_2_3)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "Sometimes doing the right thing isn't easy, but that was her way." |

### Scene 03 (LS_3_1)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "Heather never hesitated to make someone else's burden a little lighter." |
| 5.0s | SUSAN | "She always had this way of bringing people in—friends, strays, you name it." |
| 10.0s | SUSAN | "She saw someone who needed help, and suddenly, they were family." |

### Scene 03 (LS_3_2)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "We didn't have much, but Heather made sure everyone felt like they belonged." |

### Scene 03 (LS_3_5)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "Sometimes I worried she was taking on too much—" |
| 4.0s | SUSAN | "But that was my fault. I made sure Heather knew what it meant to be a neighbor— And she never forgot." |

### Scene 03 (LS_3_7)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "Even in the hardest times, we believed that it wasn't about what you have— It's about what you're willing to share." |

### Scene 04 (LS_4_1)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "Now don't get me wrong. Heather wasn't anything special." |
| 4.0s | SUSAN | "She took the long way to adulthood, making plenty of mistakes like everyone else—" |
| 9.0s | SUSAN | "But when she got there, she made sure it was on her own terms." |

### Scene 04 (LS_4_2)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "She'd moved out, gotten her own place, and started working as a waitress and a bartender, until she got a job working as a paralegal for a law firm." |

### Scene 04 (LS_4_3)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "My heart swelled, but like any parent, more than anything— I wanted her to be safe and happy." |

### Scene 04 (LS_4_4)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "It's hard to let go, but seeing her become her own person— That was a gift." |
| 5.0s | SUSAN | "She figured out that growing up isn't just about independence. It's about deciding who we want to be, and she chose to be someone who cares." |

### Scene 05 (LS_5_1)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "Heather and I would meet for dinner just about every other week, and it was always an event." |
| 6.0s | SUSAN | "She'd save up stories, and I would just listen to her, but— That night, though, something felt different—" |

### Scene 05 (LS_5_2)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "She was talking a mile a minute— like she was trying to tell me everything all at once." |
| 6.0s | SUSAN | "It felt like the world was only growing louder, and not for the better." |
| 11.0s | SUSAN | "Everywhere you looked, there was festering anger swelling beneath the surface." |

### Scene 05 (LS_5_3)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "I wanted to tell her everything would be fine, but I couldn't lie." |
| 5.0s | SUSAN | "So, we just stayed, talking— longer than usual that night." |

### Scene 05 (LS_5_4)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "Holding on to her whisky laugh, infectious smile— And her eyes— Filled with love and sadness." |
| 8.0s | NARRATOR | "Hold this moment… but know it's only part of the story." |
| 13.0s | NARRATOR | "You must step beyond Susan's perspective to see how August 12 came to be." |

---

## Chapter 2: The Rally in Charlottesville

### Scene 06 (LS_6_1)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | NARRATOR | "This is a space between Susan's memories—where unseen forces quietly shaped a day she never foresaw." |
| 7.0s | NARRATOR | "Because hatred doesn't arrive in a single moment—it grows slowly, fueled by fear, falsehoods, and the pull of belonging." |
| 16.0s | NARRATOR | "In these echo chambers, illusions become truth—fueled by fear, falsehoods, and the need to belong." |
| 24.0s | NARRATOR | "Weeks before the rally, white supremacist groups organized online, pushing conspiracies about 'white genocide' and 'replacement.'" |
| 32.0s | NARRATOR | "Under the guise of preserving history, they secured permits—while, behind private screens, hatred quietly multiplied." |

### Scene 06 (LS_6_2)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | NARRATOR | "A single flame can crawl through many matches, just as a few hateful voices can ignite an entire crowd." |
| 8.0s | NARRATOR | "On August 11, hundreds marched by torchlight, turning a lone spark of anger into a blazing show of intimidation." |
| 16.0s | NARRATOR | "Fueled by the power of ritual, intimidation took shape—a lone flame magnified into a sea of torches." |
| 24.0s | NARRATOR | "Emboldened chants replaced caution, as unity in darkness gave rise to a false courage none would dare alone." |

### Scene 06 (LS_6_3)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | NARRATOR | "In these moments, a single nudge can tip strangers toward confrontation—or hold them in uneasy peace." |
| 8.0s | NARRATOR | "By August 12's morning, tensions loomed quietly at Emancipation Park." |

### Scene 06 (LS_6_4)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | NARRATOR | "By midday, momentum built with every angry voice. Shouts became shoves." |
| 6.0s | NARRATOR | "Voices emboldened by each other, drowning out restraint." |
| 10.0s | NARRATOR | "In that crush, caution and conscience gave way to force, as August 12 tipped toward violence." |

### Scene 06 (LS_6_5)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | NARRATOR | "When empathy is destroyed, anyone can become a target— an obstacle in the path of hatred." |
| 8.0s | NARRATOR | "At 1:42 PM, a car drove into a group of counter-protesters, injuring 35 people and killing one— Heather Heyer." |
| 18.0s | NARRATOR | "Susan believed Heather had decided to avoid the rally, until the phone rang." |
| 23.0s | NARRATOR | "Go now. Return to Susan and witness her story." |

### Scene 07 (LS_7_1)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "I got the call while I was out with a friend. No one knew where Heather was. I was frantic, calling every hospital." |

### Scene 07 (LS_7_2)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "When I finally got there, they handed me a number—" |
| 4.0s | SUSAN | "A NUMBER— Where is my daughter?" |

### Scene 07 (LS_7_3)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | OFFICER | "Ms. Bro, please come with us." |
| 3.0s | SUSAN | "They took me by the arms." |

### Scene 07 (LS_7_4)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "I kept hoping she was just hurt, just unconscious. My baby girl—" |

### Scene 07 (LS_7_5)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | DETECTIVE | "Ma'am, your daughter has been pronounced." |

### Scene 07 (LS_7_6)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "It didn't make sense. Everything just— stopped." |
| 5.0s | SUSAN | "I didn't get to see her— Couldn't hold her— Couldn't even say goodbye—" |

---

## Chapter 3: Seeds of Hope

### Scene 08 (LS_8_1)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "In those first days, the whole world wanted to know who Heather was." |
| 5.0s | SUSAN | "Reporters camped outside, day and night, asking questions I barely knew how to answer. I didn't even have time to grieve." |

### Scene 08 (LS_8_2) - Teapot Match
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "Heather was always full of life, imagination pouring from her like magic." |
| 5.0s | SUSAN | "Even then, she brought joy and curiosity to everything she did." |

### Scene 08 (LS_8_3) - Illustration Match
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "Heather's fairness defined her early on." |
| 4.0s | SUSAN | "She stood up for others, quietly inspiring everyone around her with bravery." |

### Scene 08 (LS_8_4) - Pitcher Match
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "Heather saw the good in everyone, sharing whatever we had." |
| 5.0s | SUSAN | "Her kindness touched more lives than she ever knew." |

### Scene 08 (LS_8_5) - Phone Match + Circle
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "Heather became someone I deeply admired—responsible, thoughtful, dedicated to justice." |
| 6.0s | SUSAN | "She taught us all how to care and listen." |
| 10.0s | SUSAN | "Heather showed me how a single person, living authentically, can inspire countless others." |
| 16.0s | SUSAN | "Now, it's my turn to continue what she started—" |

### Scene 09 (LS_9_1)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "After losing Heather, I didn't know how to move forward." |
| 5.0s | SUSAN | "I knew I couldn't replace her, but I could honor her." |
| 9.0s | SUSAN | "And slowly, with support from so many others, I learned to transform pain into purpose." |

### Scene 09 (LS_9_2)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "We created the Heather Heyer Foundation, supporting passionate individuals dedicated to making positive change." |
| 7.0s | SUSAN | "Scholarships helped students pursuing justice, equality, and compassion." |

### Scene 09 (LS_9_3)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "Through partnerships and community programs, we empowered youth to raise their voices against hate, inspired by Heather's bravery and compassion." |

### Scene 09 (LS_9_4)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "Together, we advocated for change— real, lasting change." |
| 5.0s | SUSAN | "Laws that improve how hate crimes are reported and responded to, ensuring no one's suffering goes unseen." |

### Scene 09 (LS_9_5)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "The world wanted to understand who Heather was." |
| 4.0s | SUSAN | "Through conversations, through education, and through community— we made sure they saw the heart of who she was." |
| 10.0s | SUSAN | "Her legacy lives on in every person inspired to act." |

### Scene 09 (LS_9_6)
| Time | Speaker | Line |
|------|---------|------|
| 0.0s | SUSAN | "Heather showed me that every small action matters, every voice counts." |
| 6.0s | SUSAN | "And when we choose love over hate, compassion over fear, we plant something powerful— something lasting." |
| 13.0s | SUSAN | "Ordinary people can do extraordinary things." |
| 17.0s | SUSAN | "These are the seeds Heather planted, the legacy she left behind." |
| 22.0s | SUSAN | "A gentle reminder that HOPE— still exists." |
