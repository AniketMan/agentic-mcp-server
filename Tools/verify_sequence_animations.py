"""
Verify Sequence Animations Match Script
Reads LS_1_1 through LS_1_4 and extracts animation/track data for comparison.
"""
import unreal
import json

OUTPUT_FILE = r"C:\Users\aniketbhatt\Desktop\SOH\Dev\Main\Docs\SEQUENCE_VERIFICATION.json"

SEQUENCES_TO_CHECK = [
    "/Game/Sequences/Scene1/LS_1_1",
    "/Game/Sequences/Scene1/LS_1_2",
    "/Game/Sequences/Scene1/LS_1_3",
    "/Game/Sequences/Scene1/LS_1_4",
]

# Script expectations
SCRIPT_EXPECTATIONS = {
    "LS_1_1": {
        "scene": "01 INT. HOME - DAY / LARGER THAN LIFE",
        "character": "HeatherChild",
        "expected_actions": [
            "Child enters dancing with teapot",
            "Hums Disney-like tune (Aladdin)",
            "Runs to Susan, rubs teapot",
            "Hugs Susan's leg",
            "Heartbeat haptic on grip",
            "Skips to kitchen table"
        ],
        "audio": ["Susan VO", "Child humming", "Giggle SFX"],
        "interactions": ["Gaze highlight on Heather", "Grip for heartbeat"]
    },
    "LS_1_2": {
        "scene": "02 INT. HOME - CONTINUOUS / STANDING UP FOR OTHERS",
        "character": "HeatherPreTeen",
        "expected_actions": [
            "Cross-fade from Child to PreTeen",
            "PreTeen at table illustrating",
            "Slides paper to Susan",
            "Animated illustration sequence (classroom)",
            "Grins, runs out door"
        ],
        "audio": ["Susan VO", "Pencil scratching", "Humming", "Giggle"],
        "interactions": ["Nav to table", "Grip paper for animation"]
    },
    "LS_1_3": {
        "scene": "03 INT. HOME - CONTINUOUS / RESCUERS",
        "character": "HeatherTeen + Friend1 + Friend2",
        "expected_actions": [
            "Muffled laughter outside door",
            "Teen enters with 2 friends",
            "Friends sit at table",
            "Heather gets glasses from cabinet",
            "Susan pours water",
            "Cheers and drink",
            "Retreat to hallway"
        ],
        "audio": ["Susan VO", "Laughter", "Water pouring", "Glass clink"],
        "interactions": ["Grip door knob", "Grip fridge", "Grip/Trigger pour water"]
    },
    "LS_1_4": {
        "scene": "04 INT. HOME - CONTINUOUS / STEPPING INTO ADULTHOOD",
        "character": "None visible (phone conversation)",
        "expected_actions": [
            "Phone dings with text",
            "Text message sequence (SSN/beneficiary conversation)",
            "Messages appear in 3D space",
            "Phone deactivates"
        ],
        "audio": ["Susan VO", "Text ding SFX", "Shwoop send SFX"],
        "interactions": ["Grip phone", "Trigger to send responses"]
    }
}

def get_sequence_data(seq_path):
    """Extract comprehensive data from a level sequence."""
    seq = unreal.load_asset(seq_path)
    if not seq:
        return {"error": f"Could not load {seq_path}"}

    seq_name = seq.get_name()
    movie_scene = seq.get_movie_scene()
    if not movie_scene:
        return {"name": seq_name, "error": "No MovieScene"}

    data = {
        "name": seq_name,
        "path": seq_path,
        "duration": movie_scene.get_playback_end_seconds() - movie_scene.get_playback_start_seconds(),
        "bindings": [],
        "master_tracks": [],
        "animations": [],
        "audio": [],
        "events": []
    }

    # Get master tracks (audio, events, etc.)
    for track in movie_scene.get_master_tracks():
        track_class = track.get_class().get_name()
        track_info = {"type": track_class, "sections": []}

        for section in track.get_sections():
            section_class = section.get_class().get_name()
            section_info = {"type": section_class}

            # Audio sections
            if "Audio" in section_class:
                try:
                    sound = section.get_editor_property('sound')
                    if sound:
                        section_info["sound"] = sound.get_name()
                        data["audio"].append(sound.get_name())
                except:
                    pass

            # Event sections
            if "Event" in section_class:
                section_info["is_event"] = True
                data["events"].append(section_class)

            track_info["sections"].append(section_info)

        data["master_tracks"].append(track_info)

    # Get object bindings (characters, props)
    for binding in movie_scene.get_bindings():
        binding_name = binding.get_name()
        binding_info = {
            "name": binding_name,
            "tracks": []
        }

        for track in binding.get_tracks():
            track_class = track.get_class().get_name()
            track_info = {"type": track_class}

            # Animation tracks
            if "Skeletal" in track_class or "Animation" in track_class:
                for section in track.get_sections():
                    try:
                        params = section.get_editor_property('params')
                        if params:
                            anim = params.get_editor_property('animation')
                            if anim:
                                anim_name = anim.get_name()
                                track_info["animation"] = anim_name
                                data["animations"].append({
                                    "binding": binding_name,
                                    "animation": anim_name
                                })
                    except:
                        pass

            binding_info["tracks"].append(track_info)

        data["bindings"].append(binding_info)

    return data

def verify_sequences():
    """Verify all sequences and compare to script expectations."""
    results = {"sequences": {}, "verification": {}}

    print("=" * 80)
    print("SEQUENCE VERIFICATION REPORT")
    print("=" * 80)

    for seq_path in SEQUENCES_TO_CHECK:
        seq_name = seq_path.split("/")[-1]
        print(f"\n--- {seq_name} ---")

        # Get sequence data
        data = get_sequence_data(seq_path)
        results["sequences"][seq_name] = data

        if "error" in data:
            print(f"  ERROR: {data['error']}")
            continue

        # Print findings
        print(f"  Duration: {data['duration']:.1f}s")
        print(f"  Bindings: {len(data['bindings'])}")
        for b in data["bindings"]:
            print(f"    - {b['name']}")
        print(f"  Animations: {len(data['animations'])}")
        for a in data["animations"]:
            print(f"    - {a['binding']}: {a['animation']}")
        print(f"  Audio: {len(data['audio'])}")
        for au in data["audio"]:
            print(f"    - {au}")

        # Compare to script expectations
        if seq_name in SCRIPT_EXPECTATIONS:
            expected = SCRIPT_EXPECTATIONS[seq_name]
            verification = {
                "scene": expected["scene"],
                "expected_character": expected["character"],
                "found_bindings": [b["name"] for b in data["bindings"]],
                "found_animations": [a["animation"] for a in data["animations"]],
                "found_audio": data["audio"],
                "issues": []
            }

            # Check for expected character
            char_found = any(expected["character"].split()[0].lower() in b["name"].lower()
                          for b in data["bindings"])
            if not char_found and expected["character"] != "None visible (phone conversation)":
                verification["issues"].append(f"Expected character '{expected['character']}' not found")

            # Check for animations
            if len(data["animations"]) == 0 and expected["character"] != "None visible (phone conversation)":
                verification["issues"].append("No animations found - may need animation tracks")

            results["verification"][seq_name] = verification

            if verification["issues"]:
                print(f"  ISSUES:")
                for issue in verification["issues"]:
                    print(f"    ! {issue}")
            else:
                print(f"  STATUS: OK")

    # Save results
    try:
        with open(OUTPUT_FILE, 'w') as f:
            json.dump(results, f, indent=2)
        print(f"\n\nReport saved to: {OUTPUT_FILE}")
    except Exception as e:
        print(f"\nFailed to save: {e}")

    return results

verify_sequences()
