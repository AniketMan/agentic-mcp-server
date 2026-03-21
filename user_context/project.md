# Ordinary Courage VR — Project Context for MCP Worker

## What This Project Is

A 9-scene VR narrative experience about Susan Bro and Heather Heyer, built for Meta Quest 2/Pro/3 on Unreal Engine 5.6.1 (Meta Oculus Fork). The player embodies Susan Bro in first person and relives memories of her daughter Heather across 3 chapters, culminating in the Charlottesville rally events and Susan's transformation of grief into activism through the Heather Heyer Foundation.

## Architecture

- **Engine:** UE 5.6.1 Meta Oculus Fork at `C:\VRUnreal`
- **Project:** `C:\Users\aniketbhatt\Desktop\SOH\Dev\Narrative\OrdinaryCourage.uproject`
- **Module:** `OrdinaryCourage` — minimal (5 files, no game logic in C++)
- **Core Plugin:** `VRNarrativeKit` — 34 runtime + 18 editor files
- **MCP Plugin:** `AgenticMCP` — 394 tools, 66 handler files
- **Meta SDKs:** ISDK (OculusInteraction), Movement SDK, MetaXR Audio, MetaXR Haptics, OculusPlatform

## Scene Map

| Scene | Title | Map | Key Interactions |
|-------|-------|-----|-----------------|
| 00 | Tutorial | Tutorial_Void | Learn nav/gaze/grip/trigger in void |
| 01 | Larger Than Life | ML_Main | Nav markers, gaze words, grab Heather's hand |
| 02 | Standing Up For Others | ML_Main | Gaze floating words, pick up school items |
| 03 | Rescuers | ML_Main | Gaze aura around friends, grab friendship tokens |
| 04 | Stepping Into Adulthood | ML_Main | Trigger-send text messages, gaze phone screen |
| 05 | Dinner Together | ML_Restaurant | Grip pitcher to pour water, grip menu, trigger toast |
| 06 | Charlottesville Rally | ML_Scene6 | Complex: computer, scale, Newton's cradle, car |
| 07 | The Hospital | ML_Hospital | Nav to reception, gaze at clock, grip phone |
| 08 | Turning Grief Into Action | ML_TrailerScene8 | Match objects to photos, grip foundation items |
| 09 | Legacy In Bloom | ML_Scene9 | Gaze at achievements, flowers bloom |

## Interaction Signal Flow

```
ISDK Event (grab/poke/ray/gaze dwell)
    → InteractionSignalComponent.FireSignal("SignalName")
    → NarrativeDirector.CompleteInteraction(InteractionID)
    → Scene advances when all required interactions complete
    → SceneStreamingManager loads next scene
```

## Key Classes

| Class | Type | Purpose |
|-------|------|---------|
| `UNarrativeDirector` | GameInstanceSubsystem | Experience brain — chapter/scene/interaction state |
| `USceneStreamingManager` | WorldSubsystem | Level streaming with 5 strategies |
| `UNarrativeData` | DataAsset | Experience definition (chapters → scenes → interactions) |
| `AVRNarrativePawn` | Pawn | Base VR pawn with ISDK hand/controller switching |
| `UGazeInteractionSubsystem` | WorldSubsystem | Eye tracking + HMD fallback with dwell |
| `UInteractionSignalComponent` | ActorComponent | Bridges ISDK → narrative/audio/VFX |
| `UGazeTargetComponent` | ActorComponent | Per-actor gaze dwell target |
| `UNarrationManager` | ActorComponent | VO playback + subtitle events |
| `UComfortSettings` | GameInstanceSubsystem | Accessibility (seated, vignette, subtitles) |
| `UPromptableComponent` | ActorComponent | Interaction prompt system |

## Key Asset Paths

| Asset | Path |
|-------|------|
| NarrativeData | `/Game/Data/DA_NarrativeData` |
| Input Mapping Context | `/Game/Input/IMC_Default` |
| GameMode | `/Game/OrdinaryCourage/Blueprints/Core/VRGameMode` |
| MetaToolkit | `/Game/MetaToolkit/` |
| Characters (Heather) | `/Game/Characters/Heathers/` |
| Characters (Susan, NPCs) | `/Game/Assets/Characters/` |
| Maps | `/Game/Maps/Game/Main/`, `Restaurant/`, `Scene6/`, `Hospital/` |
| Sequences | `/Game/Sequences/Scene1/` through `Scene9/` |
| Audio VO | `/Game/Sounds/VO/` |
| Audio Music | `/Game/Sounds/Music/` |
| Audio SFX | `/Game/Sounds/SFX/` |
| Haptics | `/Game/VR/Haptics/` |

## Audio Status (from audit)

- **74 VO cues** needed across 10 scenes (9 unsplit files exist, Scene 06 all missing)
- **60 SFX cues** in script (14 exist, 8 TEMP, 29 missing — mostly Scene 06/07)
- **Music:** 8/10 scenes have final music (Tutorial and Scene 06 missing)
- **P0 Blocker:** Scene 06 has NO audio at all (VO, SFX, or music)

## Naming Conventions

| Thing | Pattern | Example |
|-------|---------|---------|
| Component | `U` + Name + `Component` | `UInteractionSignalComponent` |
| Subsystem | `U` + Name | `UNarrativeDirector` |
| Actor | `A` + Name | `AVRNarrativePawn` |
| Blueprint | `BP_` + Name | `BP_OCVRPawn` |
| DataAsset | `DA_` + Name | `DA_NarrativeData` |
| Signal Name | Verb + Object | `GrabPitcher`, `GazeDissolve` |
| VO File | `VO_S{XX}_{Speaker}_{NN}` | `VO_S01_Narrator_01` |
| SFX File | `sfx_S{XX}_{description}` | `sfx_S06_chain_snap` |
| Map | `ML_` + Name | `ML_Main`, `ML_Restaurant` |
| Sublevel | `SL_` + Scene + `_` + Type | `SL_Main_Art`, `SL_Main_Lighting` |

## Performance Budget (Quest 3)

| Metric | Budget |
|--------|--------|
| Frame time | < 11.1ms (90fps) |
| Memory | < 6GB |
| CPU level | SustainedLow |
| GPU level | SustainedHigh |
| Shadow budget | ~1.5ms |
| Draw calls | Minimize (use SkeletalMerging, AnimToTexture for crowds) |
