# Animation Catalog for SOH (Summary of Heather)

## Project Overview
This project follows the story of Heather at different ages - Child, PreTeen, and Adult.
The mocap animations are organized by character age and scene number.

## Naming Convention Analysis

The mocap animation naming follows a pattern: `{SCENE}_{SHOT}_{TAKE}_ue5_{CHARACTER}_gm01`

- First number = Scene (e.g., 1 = Scene 1, 2 = Scene 2)
- Second number = Shot within scene
- Third number = Take number
- `ue5` = Unreal Engine 5 converted
- `gm01` = Gray Model version 01 (mocap mesh reference)

## Summary Statistics
- **Total Animations**: 19
- **HeatherAdult**: 5 animations
- **HeatherChild**: 8 animations
- **HeatherPreTeen**: 6 animations

---

## Scene-by-Scene Breakdown

### Scene 1 - Child Heather (Childhood memories/flashbacks)

**Character Used**: HeatherChild (young child version)
**Location**: Likely home environment, family setting

| Animation | Shot | Take | Description |
|-----------|------|------|-------------|
| `1_1_02_ue5_Heather_Child_gm01__1_` | 1 | 02 | Scene 1, Shot 1 - Opening/intro moment |
| `1_1_02b_ue5_Heather_Child_gm01__1_` | 1 | 02b | Alternate version of shot 1 |
| `1_3_01_ue5_Heather_Child_gm01_Anim` | 3 | 01 | Shot 3, Take 1 |
| `1_3_02_ue5_Heather_Child_gm01` | 3 | 02 | Shot 3, Take 2 |
| `1_3_03_ue5_Heather_Child_gm01` | 3 | 03 | Shot 3, Take 3 |
| `Full_Scene_1_V2_ue5_Anim` | ALL | V2 | **COMPLETE SCENE 1** - Full performance, Version 2 |

**Notes**: Scene 1 focuses on young Heather. The "Full_Scene" animation is likely the master take for the entire scene.

---

### Scene 2 - PreTeen Heather (Coming of age)

**Character Used**: HeatherPreTeen (young teenager version)
**Location**: Transitional period in story

| Animation | Shot | Take | Description |
|-----------|------|------|-------------|
| `2_1_01_ue5_Heather_Preteen_gm01` | 1 | 01 | Scene 2 opening, Take 1 |
| `2_1_02_ue5_Heather_Preteen_gm01` | 1 | 02 | Scene 2 opening, Take 2 |
| `2_1_03_ue5_Heather_Preteen_gm01` | 1 | 03 | Scene 2 opening, Take 3 |
| `2_1_04_ue5_Heather_Preteen_gm01` | 1 | 04 | Scene 2 opening, Take 4 |
| `2_1_05_ue5_Heather_Preteen_gm01` | 1 | 05 | Scene 2 opening, Take 5 (latest/best?) |
| `2_2_05_ue5_Heather_Preteen_gm01` | 2 | 05 | Shot 2, Take 5 |

**Also in Scene 2** (using HeatherChild skeleton):
| Animation | Shot | Take | Description |
|-----------|------|------|-------------|
| `2_1_04_ue5__1__Anim1` | 1 | 04 | HeatherChild version of Scene 2 shot (transition moment?) |

**Notes**: Multiple takes of shot 1 suggest this is an important dramatic moment with many performance options.

---

### Adult Scenes - HeatherAdult (Present day / Bar scenes)

**Character Used**: HeatherAdult (grown-up version)
**Location**: Bar, kitchen, domestic setting with Susan

| Animation | Type | Description |
|-----------|------|-------------|
| `HEATHER__ADULT__sits__excitedly_waiting-__2_ue5` | Idle | Adult Heather sitting and waiting excitedly (bar scene?) |
| `Heather_takes_a_sip_of_her_whisky_and_coke-__ue5` | Action | Drinking animation - whiskey and coke |
| `Livelyhands` | Gesture | Hand animation loop for talking/gesturing |
| `triggers_Heather_to_place_her_hand_in_Susan_ue5` | Interaction | Physical interaction with Susan character |
| `triggers_Heather_to_place_her_hand_in_Susan2_ue5` | Interaction | Alternative take of Susan interaction |

**Notes**: Adult scenes appear to be more character-driven with specific action descriptions rather than numbered scene/shot system. Susan appears to be another character Heather interacts with.

---

### Special Animations

| Animation | Character | Description |
|-----------|-----------|-------------|
| `Hug_Loop_2_Anim` | HeatherChild | Looping hug animation - emotional reunion/comfort scene |

---

## Asset Locations

```
Content/Assets/Characters/Heathers/
├── HeatherAdult/
│   └── Animations/         (5 animations)
├── HeatherChild/
│   └── Animations/         (8 animations)
└── HeatherPreTeen/
    └── Animations/         (6 animations)
```

## LiveLink Facial Capture Readiness

Based on previous analysis, the following characters have full ARKit 52 blend shape support:
- ✅ HeatherAdult - 52/52 ARKit shapes (READY)
- ✅ HeatherChild - 52/52 ARKit shapes (READY)
- ✅ HeatherPreTeen - 52/52 ARKit shapes (READY)

All characters have LiveLinkComponent added to their blueprints.

## Recommended Next Steps

1. **Select best takes**: Review multiple takes for each shot and select preferred performance
2. **Scene assembly**: Use Level Sequences to assemble final edit with selected animations
3. **Facial capture overlay**: Use LiveLink Face app to capture facial performance to layer on top of body mocap
4. **Audio sync**: Ensure dialogue audio tracks align with animation timing
