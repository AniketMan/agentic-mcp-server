"""
Analyze Level Sequences to find which animations they reference.
Writes results to a file for easy reading.
"""
import unreal
import os
import json

OUTPUT_FILE = r"C:\Users\aniketbhatt\Desktop\SOH\Dev\Main\Plugins\AgenticMCP\Tools\AnimationScreenshots\sequence_animation_map.json"

# All sequences to check
SEQUENCES = [
    "/Game/Sequences/Scene1/LS_1_1",
    "/Game/Sequences/Scene1/LS_1_2",
    "/Game/Sequences/Scene1/LS_1_3",
    "/Game/Sequences/Scene1/LS_1_4",
    "/Game/Sequences/Scene1/LS_HugLoop",
    "/Game/Sequences/Scene2/LS_2_1",
    "/Game/Sequences/Scene2/LS_2_2R",
    "/Game/Sequences/Scene2/LS_2_2R1",
    "/Game/Sequences/Scene2/LS_2_2R2",
    "/Game/Sequences/Scene2/LS_2_3",
    "/Game/Sequences/Scene3/LS_3_1",
    "/Game/Sequences/Scene3/LS_3_2",
    "/Game/Sequences/Scene3/LS_3_5",
    "/Game/Sequences/Scene3/LS_3_6",
    "/Game/Sequences/Scene3/LS_3_7",
    "/Game/Sequences/Scene4/LS_4_1",
    "/Game/Sequences/Scene4/LS_4_2",
    "/Game/Sequences/Scene4/LS_4_3",
    "/Game/Sequences/Scene4/LS_4_4",
    "/Game/Sequences/Scene5/LS_5_1",
    "/Game/Sequences/Scene5/LS_5_2",
    "/Game/Sequences/Scene5/LS_5_3",
    "/Game/Sequences/Scene5/LS_5_4",
    "/Game/Sequences/Scene6/LS_6_1",
    "/Game/Sequences/Scene6/LS_6_2",
    "/Game/Sequences/Scene6/LS_6_3",
    "/Game/Sequences/Scene6/LS_6_4",
    "/Game/Sequences/Scene6/LS_6_5",
    "/Game/Sequences/Scene7/LS_7_1",
    "/Game/Sequences/Scene7/LS_7_2",
    "/Game/Sequences/Scene7/LS_7_3",
    "/Game/Sequences/Scene7/LS_7_4",
    "/Game/Sequences/Scene7/LS_7_5",
    "/Game/Sequences/Scene7/LS_7_6",
    "/Game/Sequences/Scene8/LS_8_1",
    "/Game/Sequences/Scene8/LS_8_2",
    "/Game/Sequences/Scene8/LS_8_3",
    "/Game/Sequences/Scene8/LS_8_4",
    "/Game/Sequences/Scene8/LS_8_5",
    "/Game/Sequences/Scene9/LS_9_1",
    "/Game/Sequences/Scene9/LS_9_2",
    "/Game/Sequences/Scene9/LS_9_3",
    "/Game/Sequences/Scene9/LS_9_4",
    "/Game/Sequences/Scene9/LS_9_5",
    "/Game/Sequences/Scene9/LS_9_6",
]

def get_sequence_info(seq_path):
    """Get detailed info about a level sequence including animation tracks."""
    seq = unreal.load_asset(seq_path)
    if not seq:
        return None

    info = {
        "name": seq.get_name(),
        "path": seq_path,
        "bindings": [],
        "animations": []
    }

    movie_scene = seq.get_movie_scene()
    if not movie_scene:
        return info

    # Get playback range
    info["duration"] = movie_scene.get_playback_end_seconds() - movie_scene.get_playback_start_seconds()

    # Get all bindings (characters/objects in the sequence)
    bindings = movie_scene.get_bindings()
    for binding in bindings:
        binding_name = binding.get_name()
        binding_id = str(binding.get_id())

        binding_info = {
            "name": binding_name,
            "tracks": []
        }

        tracks = binding.get_tracks()
        for track in tracks:
            track_class = track.get_class().get_name()
            track_info = {
                "type": track_class,
                "sections": []
            }

            # Get sections (keyframes/clips)
            sections = track.get_sections()
            for section in sections:
                section_class = section.get_class().get_name()
                section_info = {"type": section_class}

                # For skeletal animation sections, get the animation reference
                if "SkeletalAnimation" in section_class:
                    try:
                        params = section.get_editor_property('params')
                        if params:
                            anim = params.get_editor_property('animation')
                            if anim:
                                anim_name = anim.get_name()
                                anim_path = anim.get_path_name()
                                section_info["animation"] = anim_name
                                section_info["animation_path"] = anim_path
                                info["animations"].append({
                                    "binding": binding_name,
                                    "animation": anim_name,
                                    "path": anim_path
                                })
                    except Exception as e:
                        section_info["error"] = str(e)

                track_info["sections"].append(section_info)

            binding_info["tracks"].append(track_info)

        info["bindings"].append(binding_info)

    return info

def analyze_all():
    """Analyze all sequences and save to file."""
    results = {
        "sequences": {},
        "animation_to_sequence_map": {}
    }

    print("Analyzing sequences...")

    for seq_path in SEQUENCES:
        seq_name = seq_path.split("/")[-1]
        info = get_sequence_info(seq_path)

        if info:
            results["sequences"][seq_name] = info

            # Build reverse map: animation -> sequence
            for anim in info.get("animations", []):
                anim_name = anim["animation"]
                if anim_name not in results["animation_to_sequence_map"]:
                    results["animation_to_sequence_map"][anim_name] = []
                results["animation_to_sequence_map"][anim_name].append({
                    "sequence": seq_name,
                    "binding": anim["binding"]
                })

            print(f"  {seq_name}: {len(info.get('animations', []))} animations")
        else:
            print(f"  {seq_name}: NOT FOUND")

    # Save to file
    with open(OUTPUT_FILE, 'w') as f:
        json.dump(results, f, indent=2)

    print(f"\nResults saved to: {OUTPUT_FILE}")

    # Print summary
    print("\n=== ANIMATION TO SEQUENCE MAP ===")
    for anim, seqs in results["animation_to_sequence_map"].items():
        seq_list = ", ".join([s["sequence"] for s in seqs])
        print(f"  {anim} -> {seq_list}")

analyze_all()
