"""
Load Scene 1 and verify sequences
"""
import unreal

# Open Scene 1 map
print("Loading Scene 1...")
level_path = "/Game/Maps/Game/Scene_1/ML_Scene1"
unreal.EditorLevelLibrary.load_level(level_path)
print(f"Loaded: {level_path}")

# Wait a moment for level to load
import time
# Note: can't actually sleep in UE Python, level loads synchronously

# Now check for sequences in the level
world = unreal.EditorLevelLibrary.get_editor_world()
print(f"Current World: {world.get_name()}")

# Find all LevelSequenceActors
actors = unreal.EditorLevelLibrary.get_all_level_actors()
seq_actors = [a for a in actors if "LevelSequence" in a.get_class().get_name()]

print(f"\nFound {len(seq_actors)} LevelSequenceActors:")
for sa in seq_actors:
    label = sa.get_actor_label()
    seq = sa.get_sequence()
    if seq:
        ms = seq.get_movie_scene()
        dur = ms.get_playback_end_seconds() - ms.get_playback_start_seconds() if ms else 0
        print(f"  {label}: {seq.get_name()} ({dur:.1f}s)")

        # List bindings
        for binding in ms.get_bindings():
            print(f"    Binding: {binding.get_name()}")
    else:
        print(f"  {label}: NO SEQUENCE ASSIGNED")
