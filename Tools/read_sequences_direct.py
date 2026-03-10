"""
Read sequence assets directly (without needing level loaded)
"""
import unreal

SEQUENCES = [
    "/Game/Sequences/Scene1/LS_1_1",
    "/Game/Sequences/Scene1/LS_1_2",
    "/Game/Sequences/Scene1/LS_1_3",
    "/Game/Sequences/Scene1/LS_1_4",
]

print("="*70)
print("SEQUENCE VERIFICATION: LS_1_1 through LS_1_4")
print("="*70)

for seq_path in SEQUENCES:
    seq = unreal.load_asset(seq_path)
    if not seq:
        print(f"\n{seq_path.split('/')[-1]}: NOT FOUND")
        continue

    seq_name = seq.get_name()
    ms = seq.get_movie_scene()

    if not ms:
        print(f"\n{seq_name}: NO MOVIE SCENE")
        continue

    dur = ms.get_playback_end_seconds() - ms.get_playback_start_seconds()

    print(f"\n{'='*70}")
    print(f"SEQUENCE: {seq_name}")
    print(f"Duration: {dur:.2f} seconds")
    print(f"{'='*70}")

    # Get bindings (characters/objects)
    bindings = ms.get_bindings()
    print(f"\nBINDINGS ({len(bindings)}):")

    for binding in bindings:
        b_name = binding.get_name()
        print(f"  [{b_name}]")

        tracks = binding.get_tracks()
        for track in tracks:
            track_class = track.get_class().get_name()

            # Check for animation tracks
            if "SkeletalAnimation" in track_class:
                print(f"    Animation Track:")
                for section in track.get_sections():
                    try:
                        params = section.get_editor_property('params')
                        if params:
                            anim = params.get_editor_property('animation')
                            if anim:
                                print(f"      -> {anim.get_name()}")
                    except Exception as e:
                        print(f"      -> (error: {e})")

            # Check for transform tracks
            elif "Transform" in track_class:
                print(f"    Transform Track")

            # Check for visibility
            elif "Visibility" in track_class:
                print(f"    Visibility Track")

            # Check for events
            elif "Event" in track_class:
                print(f"    Event Track")

    # Get master tracks (audio, events)
    master_tracks = ms.get_master_tracks()
    if master_tracks:
        print(f"\nMASTER TRACKS ({len(master_tracks)}):")
        for track in master_tracks:
            track_class = track.get_class().get_name()

            if "Audio" in track_class:
                print(f"  Audio Track:")
                for section in track.get_sections():
                    try:
                        sound = section.get_editor_property('sound')
                        if sound:
                            print(f"    -> {sound.get_name()}")
                    except:
                        pass

            elif "Event" in track_class:
                print(f"  Event Track")

            elif "Fade" in track_class:
                print(f"  Fade Track")

            else:
                print(f"  {track_class}")

print("\n" + "="*70)
print("VERIFICATION COMPLETE - Check Output Log above")
print("="*70)
