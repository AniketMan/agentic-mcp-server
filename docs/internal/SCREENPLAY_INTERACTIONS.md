# Screenplay Interaction Catalog

> Extracted from "Ordinary Courage VR" screenplay v2 (3.24.25).
> Every `//INTERACTIVITY//` block mapped to scene, type, and required Blueprint logic.

## Summary

| Interaction Type | Count |
|-----------------|-------|
| NAVIGATION | 17 |
| GAZE | 2 |
| GRIP | 17 |
| TRIGGER | 3 |
| GRIP + TRIGGER | 2 |
| NAVIGATION + GRIP | 1 |
| **Total** | **42** |

## Scene 00: Tutorial (Dynamic Environment)

| # | Type | Description | Blueprint Pattern |
|---|------|-------------|-------------------|
| 1 | NAVIGATION | Glowing LOCATION MARKER ahead. Step toward marker deactivates teleportation, activates movement. | TeleportPoint enable/disable + story step |
| 2 | GAZE | Words float: "Listen." "Notice." "Remember." Gaze on each dissolves into particles. | ActivatableComponent (gaze overlap) + particle trigger |
| 3 | GRIP | Glowing OBJECT OF LIGHT. Grip button holds it. Heartbeat vibration haptic. | ActivatableComponent (grip input) + haptic feedback |
| 4 | TRIGGER | Trigger button sends object flying upward, bursts into memory stream. | Trigger input + launch + particle burst |
| 5 | NAVIGATION | Another LOCATION MARKER. Walk forward. | TeleportPoint enable + story step |
| 6 | NAVIGATION | Walking toward door disables other interactions and teleportation. | TeleportPoint + disable interactions |

## Scene 01: Home - Mother and Daughter

| # | Type | Description | Blueprint Pattern |
|---|------|-------------|-------------------|
| 7 | GAZE | Focus gaze on Heather (Child) activates glowing aura. | ActivatableComponent (gaze) + material/particle |
| 8 | GRIP | Grip button on Heather's body triggers heartbeat haptic. Auto-sequence ends with animation, reactivates teleportation. | ActivatableComponent (grip) + haptic + animation sequence + re-enable teleport |

## Scene 02: Home - Standing Up For Others

| # | Type | Description | Blueprint Pattern |
|---|------|-------------|-------------------|
| 9 | NAVIGATION | LOCATION MARKER near kitchen table. Step deactivates teleportation. | TeleportPoint + story step |
| 10 | GRIP | Grab paper with GRIP. Activates vignette animation on paper. | ActivatableComponent (grip) + animation sequence |
| 11 | GRIP | Paper auto-returns to table after animation. | Auto-return (timeline/animation) |
| 12 | NAVIGATION | Teleportation reactivates. DOOR and KNOB highlighted. | Re-enable teleport + highlight |

## Scene 03: Home - Rescuers

| # | Type | Description | Blueprint Pattern |
|---|------|-------------|-------------------|
| 13 | GRIP | Grab DOOR KNOB. Initiates Heather (Teen) + friends entry. | ActivatableComponent (grip) + animation sequence |
| 14 | GRIP | Grab REFRIGERATOR DOOR. Auto-opens. | ActivatableComponent (grip) + door animation |
| 15 | GRIP+TRIGGER | Grab PITCHER (grip), pour WATER (trigger). Removing pitcher closes door. | Dual-input interaction + pour physics/anim |
| 16 | GRIP+TRIGGER | Hold PITCHER (grip), pour WATER (trigger) into 3 glasses. Filling all activates next sequence. | Dual-input + fill counter + sequence trigger |

## Scene 04: Home - Stepping Into Adulthood

| # | Type | Description | Blueprint Pattern |
|---|------|-------------|-------------------|
| 17 | GRIP+TRIGGER | Grab PHONE (grip). Text message sequence. TRIGGER advances texts. | ActivatableComponent (grip) + sequential trigger input |
| 18 | GRIP | Release grip drops phone. | Grip release detection |
| 19 | GRIP | Grab front DOOR HANDLE opens door. | ActivatableComponent (grip) + door animation |

## Scene 05: Restaurant - Dinner Together

| # | Type | Description | Blueprint Pattern |
|---|------|-------------|-------------------|
| 20 | NAVIGATION | LOCATION MARKER at table. Step deactivates teleportation, sits Susan. | TeleportPoint + sit animation |
| 21 | GRIP | Place hand on table palm up. Heather places hand. Grip activates heartbeat haptic. | ActivatableComponent (grip) + haptic + animation |

## Scene 06: Dynamic Environment - Charlottesville Rally

| # | Type | Description | Blueprint Pattern |
|---|------|-------------|-------------------|
| 22 | NAVIGATION | Glowing path to computer. LOCATION MARKER limits teleportation to spotlight radius. | TeleportPoint + radius constraint |
| 23 | TRIGGER | Select glowing shape on computer screen. Trigger button initiates echo chamber animation. | Trigger input + animation sequence |
| 24 | NAVIGATION | Exit echo chamber through opening. Prism closes behind. | TeleportPoint + animation trigger |
| 25 | NAVIGATION | Path to balance beam scale. LOCATION MARKER limits teleportation. | TeleportPoint + radius constraint |
| 26 | GRIP | Pick up WEIGHT. Grip holds, release drops. Place on lit match side triggers animation. | ActivatableComponent (grip) + physics placement + animation |
| 27 | NAVIGATION | Exit through gap. Flames fizzle. | TeleportPoint + animation |
| 28 | NAVIGATION | Path to Newton's Cradle. LOCATION MARKER limits teleportation. | TeleportPoint + radius constraint |
| 29 | GRIP | Pull back WEIGHT on cradle. Release swings, initiates animation. | ActivatableComponent (grip) + physics + animation |
| 30 | GRIP | Pull back WEIGHTS (second time, more balls). Release initiates escalated animation. | Same pattern, escalated |
| 31 | NAVIGATION | Exit through opening. | TeleportPoint |
| 32 | NAVIGATION | Path to spotlight. LOCATION MARKER limits teleportation. | TeleportPoint + radius constraint |
| 33 | GRIP | Grab PROTEST SIGN. Holding initiates animation. Release drops. | ActivatableComponent (grip) + animation |
| 34 | GRIP | Grab PHONE. Initiates phone call sequence. | ActivatableComponent (grip) + audio sequence |
| 35 | NAVIGATION | Move toward Susan in doorway. Phone dissolves, pulled through to hospital. | TeleportPoint + dissolve + transition |

## Scene 07: Hospital

| # | Type | Description | Blueprint Pattern |
|---|------|-------------|-------------------|
| 36 | NAVIGATION | LOCATION MARKER at reception desk. Step deactivates teleportation, initiates Number 20 Card sequence. | TeleportPoint + story step |
| 37 | GRIP | Grab NUMBER 20 CARD. Initiates officer sequence. | ActivatableComponent (grip) + animation |
| 38 | NAVIGATION | Officers guide in 3DoF. Haptic touch. Tunneled vignette. Teleportation deactivated. | Guided movement + haptic + vignette |
| 39 | NAVIGATION | Officers release. Teleportation remains deactivated. | State management |

## Scene 08: Home - Turning Grief Into Action

| # | Type | Description | Blueprint Pattern |
|---|------|-------------|-------------------|
| 40 | NAV+GRIP | Navigate to objects, grip picks up. Match to correct photo by hovering + grip. Any order. | TeleportPoint + ActivatableComponent (grip) + matching logic |
| 41 | GRIP | Pick up each of 4 objects (teapot, illustration, pitcher, cellphone) and match to photos. | 4x ActivatableComponent (grip) + match validation |
| 42 | GRIP | Hold hands with Heathers. Grip buttons while reaching. Initiates transition. | ActivatableComponent (grip) + transition |

## Scene 09: Legacy in Bloom

No explicit interactions. Passive visual experience with narration.

## Blueprint Pattern Requirements Summary

| Pattern | Scenes Using It | Estimated Node Count Per Instance |
|---------|----------------|----------------------------------|
| TeleportPoint enable/disable + story step | 00, 01, 02, 03, 04, 05, 06, 07, 08 | ~17 nodes (from SL_Trailer_Logic) |
| ActivatableComponent gaze trigger | 00, 01 | ~8 nodes (estimated) |
| ActivatableComponent grip trigger | 01-08 | ~10 nodes (estimated) |
| Trigger button input | 00, 04, 06 | ~8 nodes (estimated) |
| Dual grip+trigger | 03, 04 | ~14 nodes (estimated) |
| Haptic feedback trigger | 00, 01, 05, 07 | ~4 nodes (add-on) |
| Animation sequence trigger | 01-08 | ~6 nodes (add-on) |
| Story step broadcast | All | ~5 nodes (MakeStruct + BroadcastMessage) |
