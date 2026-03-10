"""
Export Sound Assets and Build Subtitles Track
Exports audio from Unreal Sound assets to WAV for transcription.
"""
import unreal
import os

OUTPUT_DIR = r"C:\Users\aniketbhatt\Desktop\SOH\Dev\Main\Plugins\AgenticMCP\Tools\AnimationScreenshots\AudioExports"

# VO assets to export
VO_ASSETS = [
    "/Game/Sounds/VO/MVP_Susan_DCVO_Draft_050725",
    "/Game/Sounds/VO/Scene1_Mix02_VOPrint_",
    "/Game/Sounds/VO/Scene2_Mix02_VOPrint",
    "/Game/Sounds/Test/Test_Sounds/DX/vo_s1c1_Susan_VO_mono",
]

def export_sound_to_wav(sound_path, output_dir):
    """Export a USoundWave to WAV file."""
    sound = unreal.load_asset(sound_path)
    if not sound:
        print(f"NOT FOUND: {sound_path}")
        return None

    sound_name = sound.get_name()
    output_path = os.path.join(output_dir, f"{sound_name}.wav")

    # Get sound wave properties
    duration = sound.duration if hasattr(sound, 'duration') else 0
    sample_rate = sound.get_sample_rate_for_current_platform() if hasattr(sound, 'get_sample_rate_for_current_platform') else 0
    num_channels = sound.num_channels if hasattr(sound, 'num_channels') else 0

    print(f"\n{sound_name}:")
    print(f"  Duration: {duration:.2f}s")
    print(f"  Sample Rate: {sample_rate}")
    print(f"  Channels: {num_channels}")
    print(f"  Path: {sound_path}")

    # Note: Direct WAV export from USoundWave requires the SoundExporter
    # For now, let's identify what audio we have

    return {
        "name": sound_name,
        "path": sound_path,
        "duration": duration,
        "sample_rate": sample_rate,
        "channels": num_channels,
    }

def analyze_vo_assets():
    """Analyze all VO assets."""
    # Create output directory
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    print("=" * 80)
    print("ANALYZING VO AUDIO ASSETS")
    print("=" * 80)

    results = []
    for asset_path in VO_ASSETS:
        info = export_sound_to_wav(asset_path, OUTPUT_DIR)
        if info:
            results.append(info)

    print("\n" + "=" * 80)
    print("SUMMARY")
    print("=" * 80)
    print(f"Found {len(results)} VO assets")

    total_duration = sum(r["duration"] for r in results)
    print(f"Total Duration: {total_duration:.1f}s ({total_duration/60:.1f} minutes)")

    print("\nTo transcribe, you need to:")
    print("1. Right-click each sound asset in UE5 Content Browser")
    print("2. Select 'Asset Actions' -> 'Export'")
    print("3. Save as WAV to:", OUTPUT_DIR)
    print("4. Then run the transcription service")

analyze_vo_assets()
