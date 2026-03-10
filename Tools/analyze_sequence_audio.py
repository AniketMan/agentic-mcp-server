"""
Analyze Level Sequences for Audio Tracks
Extracts audio track information from sequences for subtitle generation.
"""
import unreal
import os
import json

OUTPUT_FILE = r"C:\Users\aniketbhatt\Desktop\SOH\Dev\Main\Plugins\AgenticMCP\Tools\AnimationScreenshots\sequence_audio_data.json"

# All sequences to analyze
SEQUENCES = [
    "/Game/Sequences/Scene1/LS_1_1",
    "/Game/Sequences/Scene1/LS_1_2",
    "/Game/Sequences/Scene1/LS_1_3",
    "/Game/Sequences/Scene1/LS_1_4",
    "/Game/Sequences/Scene1/LS_HugLoop",
    "/Game/Sequences/Scene2/LS_2_1",
    "/Game/Sequences/Scene2/LS_2_2R",
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

def get_audio_tracks(seq_path):
    """Extract audio track information from a level sequence."""
    seq = unreal.load_asset(seq_path)
    if not seq:
        return None

    movie_scene = seq.get_movie_scene()
    if not movie_scene:
        return {"name": seq_path.split("/")[-1], "audio_tracks": []}

    audio_tracks = []

    # Check master tracks
    master_tracks = movie_scene.get_master_tracks()
    for track in master_tracks:
        track_class = track.get_class().get_name()
        if "Audio" in track_class:
            sections = track.get_sections()
            for section in sections:
                try:
                    sound = section.get_editor_property('sound')
                    if sound:
                        start = section.get_start_frame()
                        end = section.get_end_frame()
                        audio_tracks.append({
                            "type": "master",
                            "sound_name": sound.get_name(),
                            "sound_path": sound.get_path_name(),
                            "start_frame": start,
                            "end_frame": end,
                        })
                except:
                    pass

    # Check object binding tracks
    bindings = movie_scene.get_bindings()
    for binding in bindings:
        binding_name = binding.get_name()
        tracks = binding.get_tracks()
        for track in tracks:
            track_class = track.get_class().get_name()
            if "Audio" in track_class:
                sections = track.get_sections()
                for section in sections:
                    try:
                        sound = section.get_editor_property('sound')
                        if sound:
                            audio_tracks.append({
                                "type": "binding",
                                "binding_name": binding_name,
                                "sound_name": sound.get_name(),
                                "sound_path": sound.get_path_name(),
                            })
                    except:
                        pass

    return {
        "name": seq_path.split("/")[-1],
        "path": seq_path,
        "duration": movie_scene.get_playback_end_seconds() - movie_scene.get_playback_start_seconds(),
        "audio_tracks": audio_tracks
    }

def analyze_all_sequences():
    """Analyze all sequences for audio tracks."""
    results = {
        "sequences": [],
        "all_audio_assets": set()
    }

    print("=" * 80)
    print("ANALYZING SEQUENCES FOR AUDIO")
    print("=" * 80)

    for seq_path in SEQUENCES:
        info = get_audio_tracks(seq_path)
        if info:
            results["sequences"].append(info)
            for track in info["audio_tracks"]:
                results["all_audio_assets"].add(track["sound_path"])

            audio_count = len(info["audio_tracks"])
            if audio_count > 0:
                print(f"{info['name']}: {audio_count} audio tracks, {info['duration']:.1f}s")
                for track in info["audio_tracks"]:
                    print(f"  - {track['sound_name']}")
            else:
                print(f"{info['name']}: NO AUDIO")
        else:
            print(f"{seq_path.split('/')[-1]}: NOT FOUND")

    # Convert set to list for JSON serialization
    results["all_audio_assets"] = list(results["all_audio_assets"])

    # Save to file
    try:
        with open(OUTPUT_FILE, 'w') as f:
            json.dump(results, f, indent=2)
        print(f"\nSaved to: {OUTPUT_FILE}")
    except Exception as e:
        print(f"\nFailed to save: {e}")

    print(f"\nTotal unique audio assets: {len(results['all_audio_assets'])}")

analyze_all_sequences()
