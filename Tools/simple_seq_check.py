"""
Simple Sequence Reader - outputs to UE log only
"""
import unreal

SEQUENCES = [
    "/Game/Sequences/Scene1/LS_1_1",
    "/Game/Sequences/Scene1/LS_1_2",
    "/Game/Sequences/Scene1/LS_1_3",
    "/Game/Sequences/Scene1/LS_1_4",
]

for seq_path in SEQUENCES:
    seq = unreal.load_asset(seq_path)
    if not seq:
        print(f"{seq_path}: NOT FOUND")
        continue

    ms = seq.get_movie_scene()
    dur = ms.get_playback_end_seconds() - ms.get_playback_start_seconds() if ms else 0

    print(f"\n{'='*60}")
    print(f"{seq.get_name()} ({dur:.1f}s)")
    print(f"{'='*60}")

    # Bindings
    for binding in ms.get_bindings():
        print(f"  BINDING: {binding.get_name()}")
        for track in binding.get_tracks():
            tc = track.get_class().get_name()
            print(f"    Track: {tc}")
            if "Skeletal" in tc:
                for sec in track.get_sections():
                    try:
                        p = sec.get_editor_property('params')
                        a = p.get_editor_property('animation')
                        if a:
                            print(f"      -> ANIM: {a.get_name()}")
                    except:
                        pass

    # Master tracks (audio)
    for track in ms.get_master_tracks():
        tc = track.get_class().get_name()
        if "Audio" in tc:
            print(f"  AUDIO TRACK:")
            for sec in track.get_sections():
                try:
                    s = sec.get_editor_property('sound')
                    if s:
                        print(f"    -> {s.get_name()}")
                except:
                    pass

print("\n" + "="*60)
print("VERIFICATION COMPLETE")
print("="*60)
