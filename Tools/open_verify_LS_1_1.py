"""
Open LS_1_1 in Sequencer and verify contents
"""
import unreal

SEQ_PATH = "/Game/Sequences/Scene1/LS_1_1"

# Load and open the sequence
seq = unreal.load_asset(SEQ_PATH)
if not seq:
    print(f"ERROR: Could not load {SEQ_PATH}")
else:
    # Open in editor
    editor_subsystem = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
    editor_subsystem.open_editor_for_assets([seq])
    print(f"Opened: {seq.get_name()}")

    # Get movie scene data
    ms = seq.get_movie_scene()
    dur = ms.get_playback_end_seconds() - ms.get_playback_start_seconds()

    print(f"\n{'='*70}")
    print(f"LS_1_1 VERIFICATION")
    print(f"{'='*70}")
    print(f"Duration: {dur:.2f} seconds")

    # SCRIPT EXPECTATIONS:
    print(f"\n--- SCRIPT EXPECTATIONS ---")
    print("Character: HeatherChild")
    print("Actions: Dancing with teapot, hugs Susan's leg, skips to table")
    print("Audio: Susan VO, Child humming, Giggle SFX, Refrigerator hum")
    print("Interactions: Gaze highlight, Grip heartbeat")

    # ACTUAL BINDINGS
    print(f"\n--- ACTUAL BINDINGS ---")
    bindings = ms.get_bindings()
    heather_found = False

    for binding in bindings:
        b_name = binding.get_name()
        print(f"\nBinding: {b_name}")

        if "heather" in b_name.lower() or "child" in b_name.lower():
            heather_found = True
            print("  ** MATCHES HeatherChild **")

        for track in binding.get_tracks():
            track_class = track.get_class().get_name()

            if "SkeletalAnimation" in track_class:
                print(f"  Animation Track:")
                for section in track.get_sections():
                    try:
                        params = section.get_editor_property('params')
                        if params:
                            anim = params.get_editor_property('animation')
                            if anim:
                                print(f"    -> {anim.get_name()}")
                    except:
                        print(f"    -> (no animation)")

            elif "Transform" in track_class:
                print(f"  Transform Track")
            elif "Visibility" in track_class:
                print(f"  Visibility Track")
            elif "Event" in track_class:
                print(f"  Event Track (for interactions)")

    if not heather_found:
        print("\n!! WARNING: HeatherChild binding NOT FOUND !!")

    # AUDIO TRACKS
    print(f"\n--- AUDIO TRACKS ---")
    audio_found = False
    for track in ms.get_master_tracks():
        track_class = track.get_class().get_name()
        if "Audio" in track_class:
            audio_found = True
            print(f"Audio Track:")
            for section in track.get_sections():
                try:
                    sound = section.get_editor_property('sound')
                    if sound:
                        sname = sound.get_name()
                        print(f"  -> {sname}")
                        # Check for expected audio
                        if "susan" in sname.lower() or "vo" in sname.lower():
                            print(f"     (Susan VO - MATCHES SCRIPT)")
                        if "giggle" in sname.lower():
                            print(f"     (Giggle SFX - MATCHES SCRIPT)")
                        if "hum" in sname.lower():
                            print(f"     (Humming - MATCHES SCRIPT)")
                except:
                    pass
        elif "Fade" in track_class:
            print(f"Fade Track")
        elif "Event" in track_class:
            print(f"Event Track")

    if not audio_found:
        print("!! WARNING: No audio tracks found !!")

    # VERIFICATION SUMMARY
    print(f"\n{'='*70}")
    print("VERIFICATION SUMMARY")
    print(f"{'='*70}")
    print(f"HeatherChild Binding: {'FOUND' if heather_found else 'MISSING'}")
    print(f"Audio Tracks: {'FOUND' if audio_found else 'MISSING'}")
    print(f"Duration: {dur:.2f}s")
