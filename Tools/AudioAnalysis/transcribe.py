"""
Audio Analysis Service for AgenticMCP
Provides audio transcription using OpenAI Whisper for subtitle generation.

Usage:
    python transcribe.py <audio_file> [--output srt|json|txt] [--model base|small|medium|large]

Or as HTTP service:
    python transcribe.py --serve --port 9848
"""

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Optional, List, Dict, Any

# Check for required packages
try:
    import whisper
except ImportError:
    print("Installing whisper...")
    os.system(f"{sys.executable} -m pip install openai-whisper")
    import whisper

try:
    from flask import Flask, request, jsonify
except ImportError:
    print("Installing flask...")
    os.system(f"{sys.executable} -m pip install flask")
    from flask import Flask, request, jsonify


def format_timestamp(seconds: float) -> str:
    """Convert seconds to SRT timestamp format (HH:MM:SS,mmm)"""
    hours = int(seconds // 3600)
    minutes = int((seconds % 3600) // 60)
    secs = int(seconds % 60)
    millis = int((seconds % 1) * 1000)
    return f"{hours:02d}:{minutes:02d}:{secs:02d},{millis:03d}"


def transcribe_audio(
    audio_path: str,
    model_name: str = "base",
    language: Optional[str] = None
) -> Dict[str, Any]:
    """
    Transcribe audio file using Whisper.

    Args:
        audio_path: Path to audio file (.wav, .mp3, .ogg, etc.)
        model_name: Whisper model size (tiny, base, small, medium, large)
        language: Optional language code (e.g., 'en', 'es')

    Returns:
        Dictionary with transcription data including segments with timestamps
    """
    print(f"Loading Whisper model: {model_name}")
    model = whisper.load_model(model_name)

    print(f"Transcribing: {audio_path}")
    result = model.transcribe(
        audio_path,
        language=language,
        word_timestamps=True,
        verbose=False
    )

    return {
        "text": result["text"],
        "language": result["language"],
        "segments": [
            {
                "id": seg["id"],
                "start": seg["start"],
                "end": seg["end"],
                "text": seg["text"].strip(),
                "words": seg.get("words", [])
            }
            for seg in result["segments"]
        ]
    }


def to_srt(transcription: Dict[str, Any]) -> str:
    """Convert transcription to SRT subtitle format"""
    lines = []
    for i, seg in enumerate(transcription["segments"], 1):
        start = format_timestamp(seg["start"])
        end = format_timestamp(seg["end"])
        text = seg["text"]
        lines.append(f"{i}")
        lines.append(f"{start} --> {end}")
        lines.append(text)
        lines.append("")
    return "\n".join(lines)


def to_ue_datatable(transcription: Dict[str, Any], sequence_name: str = "") -> List[Dict]:
    """
    Convert transcription to Unreal Engine DataTable format.
    Can be imported as a DataTable with columns: Time, Duration, Speaker, Text
    """
    rows = []
    for seg in transcription["segments"]:
        rows.append({
            "RowName": f"Line_{seg['id']:03d}",
            "Time": seg["start"],
            "Duration": seg["end"] - seg["start"],
            "Speaker": "",  # Can be filled in manually or with speaker diarization
            "Text": seg["text"],
            "SequenceName": sequence_name
        })
    return rows


# ============================================================
# Flask HTTP Service
# ============================================================

app = Flask(__name__)
loaded_model = None
loaded_model_name = None


@app.route("/health", methods=["GET"])
def health():
    return jsonify({
        "status": "ok",
        "service": "AudioAnalysis",
        "model_loaded": loaded_model_name
    })


@app.route("/transcribe", methods=["POST"])
def transcribe_endpoint():
    """
    POST /transcribe
    Body (JSON):
        {
            "audioPath": "/path/to/audio.wav",
            "model": "base",  // optional: tiny, base, small, medium, large
            "language": "en",  // optional
            "outputFormat": "json"  // optional: json, srt, ue_datatable
            "sequenceName": "LS_1_1"  // optional: for ue_datatable format
        }
    """
    global loaded_model, loaded_model_name

    data = request.get_json()
    if not data:
        return jsonify({"error": "No JSON body provided"}), 400

    audio_path = data.get("audioPath")
    if not audio_path:
        return jsonify({"error": "Missing 'audioPath' field"}), 400

    if not os.path.exists(audio_path):
        return jsonify({"error": f"Audio file not found: {audio_path}"}), 404

    model_name = data.get("model", "base")
    language = data.get("language")
    output_format = data.get("outputFormat", "json")
    sequence_name = data.get("sequenceName", "")

    try:
        # Load model if needed
        if loaded_model is None or loaded_model_name != model_name:
            print(f"Loading Whisper model: {model_name}")
            loaded_model = whisper.load_model(model_name)
            loaded_model_name = model_name

        # Transcribe
        print(f"Transcribing: {audio_path}")
        result = loaded_model.transcribe(
            audio_path,
            language=language,
            word_timestamps=True,
            verbose=False
        )

        transcription = {
            "text": result["text"],
            "language": result["language"],
            "segments": [
                {
                    "id": seg["id"],
                    "start": seg["start"],
                    "end": seg["end"],
                    "text": seg["text"].strip(),
                }
                for seg in result["segments"]
            ]
        }

        # Format output
        if output_format == "srt":
            return jsonify({
                "success": True,
                "format": "srt",
                "content": to_srt(transcription)
            })
        elif output_format == "ue_datatable":
            return jsonify({
                "success": True,
                "format": "ue_datatable",
                "rows": to_ue_datatable(transcription, sequence_name)
            })
        else:
            return jsonify({
                "success": True,
                "format": "json",
                "transcription": transcription
            })

    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/batch-transcribe", methods=["POST"])
def batch_transcribe_endpoint():
    """
    POST /batch-transcribe
    Transcribe multiple audio files at once.

    Body (JSON):
        {
            "audioFiles": [
                {"path": "/path/to/audio1.wav", "sequenceName": "LS_1_1"},
                {"path": "/path/to/audio2.wav", "sequenceName": "LS_1_2"}
            ],
            "model": "base",
            "outputFormat": "ue_datatable"
        }
    """
    global loaded_model, loaded_model_name

    data = request.get_json()
    if not data:
        return jsonify({"error": "No JSON body provided"}), 400

    audio_files = data.get("audioFiles", [])
    if not audio_files:
        return jsonify({"error": "No audio files provided"}), 400

    model_name = data.get("model", "base")
    output_format = data.get("outputFormat", "json")

    # Load model once
    if loaded_model is None or loaded_model_name != model_name:
        print(f"Loading Whisper model: {model_name}")
        loaded_model = whisper.load_model(model_name)
        loaded_model_name = model_name

    results = []
    for item in audio_files:
        audio_path = item.get("path")
        sequence_name = item.get("sequenceName", "")

        if not audio_path or not os.path.exists(audio_path):
            results.append({
                "path": audio_path,
                "error": "File not found"
            })
            continue

        try:
            result = loaded_model.transcribe(
                audio_path,
                word_timestamps=True,
                verbose=False
            )

            transcription = {
                "text": result["text"],
                "language": result["language"],
                "segments": [
                    {
                        "id": seg["id"],
                        "start": seg["start"],
                        "end": seg["end"],
                        "text": seg["text"].strip(),
                    }
                    for seg in result["segments"]
                ]
            }

            if output_format == "ue_datatable":
                results.append({
                    "path": audio_path,
                    "sequenceName": sequence_name,
                    "success": True,
                    "rows": to_ue_datatable(transcription, sequence_name)
                })
            else:
                results.append({
                    "path": audio_path,
                    "sequenceName": sequence_name,
                    "success": True,
                    "transcription": transcription
                })

        except Exception as e:
            results.append({
                "path": audio_path,
                "error": str(e)
            })

    return jsonify({
        "success": True,
        "processed": len(results),
        "results": results
    })


# ============================================================
# CLI
# ============================================================

def main():
    parser = argparse.ArgumentParser(description="Audio transcription using Whisper")
    parser.add_argument("audio_file", nargs="?", help="Path to audio file")
    parser.add_argument("--model", default="base", choices=["tiny", "base", "small", "medium", "large"],
                        help="Whisper model size")
    parser.add_argument("--language", help="Language code (e.g., 'en')")
    parser.add_argument("--output", default="json", choices=["json", "srt", "txt"],
                        help="Output format")
    parser.add_argument("--serve", action="store_true", help="Run as HTTP service")
    parser.add_argument("--port", type=int, default=9848, help="HTTP service port")

    args = parser.parse_args()

    if args.serve:
        print(f"Starting Audio Analysis service on port {args.port}")
        print("Endpoints:")
        print(f"  GET  http://localhost:{args.port}/health")
        print(f"  POST http://localhost:{args.port}/transcribe")
        print(f"  POST http://localhost:{args.port}/batch-transcribe")
        app.run(host="0.0.0.0", port=args.port, debug=False)
    elif args.audio_file:
        if not os.path.exists(args.audio_file):
            print(f"Error: File not found: {args.audio_file}")
            sys.exit(1)

        transcription = transcribe_audio(args.audio_file, args.model, args.language)

        if args.output == "srt":
            print(to_srt(transcription))
        elif args.output == "txt":
            print(transcription["text"])
        else:
            print(json.dumps(transcription, indent=2))
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
