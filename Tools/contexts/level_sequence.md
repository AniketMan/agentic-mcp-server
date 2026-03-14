<- Added audit scripts: audit-handlers.mjs, audit-test.mjs, WORKER REFERENCE: Load this file when executing level sequence tools -->
# Level Sequence Python API Reference

This document covers the complete Python API for creating and editing Level Sequences in UE5 via the `unreal` module. All operations require the Editor Python plugin to be enabled.

## Core Imports

```python
import unreal
```

## Subsystems

```python
# Level Sequence Editor Subsystem (requires Sequencer to be open)
ls_sub = unreal.get_editor_subsystem(unreal.LevelSequenceEditorSubsystem)

# Editor Actor Subsystem (for finding actors to bind)
actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# Static library functions (always available)
# unreal.LevelSequenceEditorBlueprintLibrary
```

## Creating a Level Sequence

```python
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.LevelSequenceFactoryNew()
seq = asset_tools.create_asset("LS_NewSequence", "/Game/Sequences", unreal.LevelSequence, factory)
```

## Opening a Level Sequence

```python
seq = unreal.load_asset("/Game/Sequences/LS_1_1")
unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
```

## Getting the Currently Open Sequence

```python
seq = unreal.LevelSequenceEditorBlueprintLibrary.get_current_level_sequence()
```

## Setting Playback Range

```python
seq = unreal.LevelSequenceEditorBlueprintLibrary.get_current_level_sequence()
# Set display rate (FPS)
seq.set_display_rate(unreal.FrameRate(30, 1))
# Set playback range in frames
seq.set_playback_start(0)
seq.set_playback_end(900)  # 30 seconds at 30fps
```

## Binding Actors (Possessable)

Possessable bindings reference actors already in the level.

```python
# Find actor by label
all_actors = actor_sub.get_all_level_actors()
target = None
for a in all_actors:
    if a.get_actor_label() == "MyActor":
        target = a
        break

# Bind to sequence
if target:
    bindings = ls_sub.add_actors([target])
    binding = bindings[0]  # SequencerBindingProxy
```

## Adding Spawnable Actors

Spawnable actors are owned by the sequence and spawned at runtime.

```python
# First add as possessable, then convert
bindings = ls_sub.add_actors([target])
ls_sub.convert_to_spawnable(bindings[0])
```

## Adding Tracks

```python
# Get binding
binding = seq.get_bindings()[0]

# Transform track
transform_track = binding.add_track(unreal.MovieScene3DTransformTrack)

# Skeletal animation track
anim_track = binding.add_track(unreal.MovieSceneSkeletalAnimationTrack)

# Audio track (on binding)
audio_track = binding.add_track(unreal.MovieSceneAudioTrack)

# Visibility track
vis_track = binding.add_track(unreal.MovieSceneVisibilityTrack)

# Event track
event_track = binding.add_track(unreal.MovieSceneEventTrack)
```

## Adding Master Tracks (Not Bound to Actor)

```python
movie_scene = seq.get_movie_scene()

# Camera cut track
cam_cut_track = movie_scene.add_master_track(unreal.MovieSceneCameraCutTrack)

# Audio master track
audio_master = movie_scene.add_master_track(unreal.MovieSceneAudioTrack)

# Fade track
fade_track = movie_scene.add_master_track(unreal.MovieSceneFadeTrack)

# Event master track
event_master = movie_scene.add_master_track(unreal.MovieSceneEventTrack)
```

## Adding Sections to Tracks

```python
# Add section
section = transform_track.add_section()

# Set range (in frames at display rate)
section.set_start_frame(0)
section.set_end_frame(300)  # 10 seconds at 30fps

# For animation sections
anim_section = anim_track.add_section()
anim_section.set_start_frame(0)
anim_section.set_end_frame(150)
anim_section.params.animation = unreal.load_asset("/Game/Animations/MyAnim")
```

## Setting Keyframes on Transform Track

```python
section = transform_track.add_section()
section.set_start_frame(0)
section.set_end_frame(300)
section.set_range(unreal.FrameNumberRange(
    unreal.FrameNumber(0),
    unreal.FrameNumber(300)
))

# Get channels (Location X, Y, Z, Rotation X, Y, Z, Scale X, Y, Z)
channels = section.get_all_channels()
# channels[0] = Location X
# channels[1] = Location Y
# channels[2] = Location Z
# channels[3] = Rotation X (Roll)
# channels[4] = Rotation Y (Pitch)
# channels[5] = Rotation Z (Yaw)
# channels[6] = Scale X
# channels[7] = Scale Y
# channels[8] = Scale Z

# Add keys
time_0 = unreal.FrameNumber(0)
time_150 = unreal.FrameNumber(150)

channels[0].add_key(time_0, 0.0)      # X=0 at frame 0
channels[0].add_key(time_150, 500.0)   # X=500 at frame 150
channels[1].add_key(time_0, 0.0)
channels[1].add_key(time_150, 200.0)
channels[2].add_key(time_0, 0.0)
channels[2].add_key(time_150, 0.0)
```

## Camera Cut Track

```python
movie_scene = seq.get_movie_scene()
cam_cut_track = movie_scene.add_master_track(unreal.MovieSceneCameraCutTrack)
cam_section = cam_cut_track.add_section()
cam_section.set_start_frame(0)
cam_section.set_end_frame(300)

# Bind camera actor
camera_binding = seq.get_bindings()[camera_index]
cam_section.set_camera_binding_id(camera_binding.get_binding_id())
```

## Creating Camera via Subsystem

```python
# Creates a cine camera actor and adds it to the sequence with a camera cut track
camera_data = ls_sub.create_camera(True)  # True = spawnable
# Returns (camera_actor, camera_binding)
```

## Audio Sections

```python
audio_track = binding.add_track(unreal.MovieSceneAudioTrack)
audio_section = audio_track.add_section()
audio_section.set_start_frame(0)
audio_section.set_end_frame(300)
audio_section.set_sound(unreal.load_asset("/Game/Audio/MySound"))
```

## Sub-Sequences

```python
# Add sub-sequence track
sub_track = movie_scene.add_master_track(unreal.MovieSceneSubTrack)
sub_section = sub_track.add_section()
sub_section.set_start_frame(0)
sub_section.set_end_frame(300)
sub_section.set_sequence(unreal.load_asset("/Game/Sequences/SubSequence"))

# Navigate into sub-sequence
unreal.LevelSequenceEditorBlueprintLibrary.focus_level_sequence(sub_seq)
# Navigate back to parent
unreal.LevelSequenceEditorBlueprintLibrary.focus_parent_sequence()
```

## Listing Bindings and Tracks

```python
seq = unreal.LevelSequenceEditorBlueprintLibrary.get_current_level_sequence()
bindings = seq.get_bindings()
for b in bindings:
    print(f"Binding: {b.get_name()} ({b.get_display_name()})")
    tracks = b.get_tracks()
    for t in tracks:
        print(f"  Track: {t.get_class().get_name()}")
        sections = t.get_sections()
        for s in sections:
            print(f"    Section: {s.get_start_frame()} - {s.get_end_frame()}")
```

## Event Tracks

```python
event_track = binding.add_track(unreal.MovieSceneEventTrack)
event_section = event_track.add_section()
event_section.set_start_frame(100)
event_section.set_end_frame(101)

# Event keys reference Blueprint functions
# The event name must match a function in the bound actor's Blueprint
channels = event_section.get_all_channels()
# Set event key via channel
```

## Common Patterns

### Full Scene Assembly

```python
import unreal

# 1. Create sequence
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.LevelSequenceFactoryNew()
seq = asset_tools.create_asset("LS_Scene5", "/Game/Sequences", unreal.LevelSequence, factory)

# 2. Open it
unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)

# 3. Set timing
seq.set_display_rate(unreal.FrameRate(30, 1))
seq.set_playback_start(0)
seq.set_playback_end(1800)  # 60 seconds

# 4. Get subsystem
ls_sub = unreal.get_editor_subsystem(unreal.LevelSequenceEditorSubsystem)
actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# 5. Bind actors
actors = actor_sub.get_all_level_actors()
character = next(a for a in actors if a.get_actor_label() == "MainCharacter")
bindings = ls_sub.add_actors([character])
char_binding = bindings[0]

# 6. Add animation track
anim_track = char_binding.add_track(unreal.MovieSceneSkeletalAnimationTrack)
anim_section = anim_track.add_section()
anim_section.set_start_frame(0)
anim_section.set_end_frame(900)
anim_section.params.animation = unreal.load_asset("/Game/Animations/Walk")

# 7. Add camera
camera_data = ls_sub.create_camera(True)

# 8. Add audio
movie_scene = seq.get_movie_scene()
audio_track = movie_scene.add_master_track(unreal.MovieSceneAudioTrack)
audio_section = audio_track.add_section()
audio_section.set_start_frame(0)
audio_section.set_end_frame(1800)
audio_section.set_sound(unreal.load_asset("/Game/Audio/Ambience"))
```
