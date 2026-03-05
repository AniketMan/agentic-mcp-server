"""
Launch the UE 5.6 Level Logic Editor dashboard.
Usage: python run_dashboard.py [optional_file.umap] [--port 8080]
"""
import sys
import os
import argparse

# Ensure project root is on path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

def main():
    parser = argparse.ArgumentParser(description="UE 5.6 Level Editor Dashboard")
    parser.add_argument("file", nargs="?", default=None, help="Path to .uasset or .umap")
    parser.add_argument("--port", type=int, default=8080, help="Server port")
    parser.add_argument("--version", default="5.6", help="UE version")
    args = parser.parse_args()

    from ui.server import create_app
    app = create_app(args.file, args.version)

    print(f"")
    print(f"  UE 5.6 Level Logic Editor - Dashboard")
    print(f"  {'='*42}")
    if args.file:
        print(f"  File: {args.file}")
    else:
        print(f"  No file loaded (use /api/load to load one)")
    print(f"  Port: {args.port}")
    print(f"  URL:  http://0.0.0.0:{args.port}")
    print(f"  {'='*42}")
    print(f"")

    app.run(host="0.0.0.0", port=args.port, debug=False)

if __name__ == "__main__":
    main()
