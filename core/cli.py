"""
cli.py — Command-line interface for UE 5.6 Level Logic Editor.

Usage:
    python -m core.cli info <file.umap>
    python -m core.cli actors <file.umap>
    python -m core.cli validate <file.umap>
    python -m core.cli props <file.umap> <actor_name>
    python -m core.cli functions <file.uasset>
    python -m core.cli graph <file.uasset> <function_export_index>
    python -m core.cli serve <file.umap> [--port 8080]
"""

import sys
import json
import argparse
from pathlib import Path


def cmd_info(args):
    """Show asset summary information."""
    from .uasset_bridge import AssetFile

    af = AssetFile(args.file, args.version)
    summary = af.summary()

    print(f"\n{'='*60}")
    print(f"  UE {args.version} Asset Summary")
    print(f"{'='*60}")
    for key, val in summary.items():
        print(f"  {key:25s}: {val}")
    print(f"{'='*60}\n")


def cmd_actors(args):
    """List all actors in a level."""
    from .uasset_bridge import AssetFile
    from .level_logic import LevelEditor

    af = AssetFile(args.file, args.version)
    editor = LevelEditor(af)
    actors = editor.get_actors()

    print(f"\n{'='*80}")
    print(f"  Level Actors ({len(actors)} total)")
    print(f"{'='*80}")
    print(f"  {'#':>4}  {'Class':30s}  {'Name':30s}  {'BP':>3}  {'Comp':>4}")
    print(f"  {'-'*4}  {'-'*30}  {'-'*30}  {'-'*3}  {'-'*4}")

    for i, actor in enumerate(actors):
        bp = "Yes" if actor.has_blueprint else ""
        print(f"  {i:4d}  {actor.class_name:30s}  {actor.object_name:30s}  {bp:>3}  {actor.component_count:4d}")

    print(f"{'='*80}\n")


def cmd_validate(args):
    """Run integrity validation on an asset."""
    from .uasset_bridge import AssetFile
    from .integrity import IntegrityValidator

    af = AssetFile(args.file, args.version)
    validator = IntegrityValidator(af)
    report = validator.validate_all()

    print(f"\n{'='*60}")
    print(f"  Validation Report: {Path(args.file).name}")
    print(f"{'='*60}")
    print(f"  {report.summary()}")
    print()

    if report.issues:
        for issue in report.issues:
            print(f"  {issue}")
    else:
        print("  No issues found.")

    print(f"{'='*60}\n")

    # Return exit code based on validation result
    return 0 if report.passed else 1


def cmd_props(args):
    """Show properties of a specific actor."""
    from .uasset_bridge import AssetFile
    from .level_logic import LevelEditor

    af = AssetFile(args.file, args.version)
    editor = LevelEditor(af)
    actor = editor.get_actor_by_name(args.actor_name)

    if actor is None:
        print(f"  ERROR: Actor '{args.actor_name}' not found in level.")
        return 1

    print(f"\n{'='*80}")
    print(f"  Actor: {actor.object_name} ({actor.class_name})")
    print(f"  Export Index: {actor.export_index}")
    print(f"  Has Blueprint: {actor.has_blueprint}")
    print(f"{'='*80}")

    if actor.properties:
        print(f"\n  Properties ({len(actor.properties)}):")
        print(f"  {'#':>4}  {'Name':30s}  {'Type':25s}  {'Value'}")
        print(f"  {'-'*4}  {'-'*30}  {'-'*25}  {'-'*30}")
        for prop in actor.properties:
            val = prop.get("value", "")
            if len(str(val)) > 50:
                val = str(val)[:47] + "..."
            print(f"  {prop['index']:4d}  {prop['name']:30s}  {prop['type']:25s}  {val}")
    else:
        print("\n  No properties found.")

    # Show components
    components = editor.get_actor_components(actor)
    if components:
        print(f"\n  Components ({len(components)}):")
        for comp in components:
            print(f"    - {comp['class_name']:30s}  {comp['object_name']}")

    print(f"\n{'='*80}\n")


def cmd_functions(args):
    """List all functions/bytecode in an asset."""
    from .uasset_bridge import AssetFile
    from .blueprint_editor import BlueprintInspector

    af = AssetFile(args.file, args.version)
    inspector = BlueprintInspector(af)
    functions = inspector.get_all_functions()

    print(f"\n{'='*80}")
    print(f"  Functions ({len(functions)} total)")
    print(f"{'='*80}")
    print(f"  {'#':>4}  {'Name':30s}  {'Type':25s}  {'BC':>3}  {'Size':>8}")
    print(f"  {'-'*4}  {'-'*30}  {'-'*25}  {'-'*3}  {'-'*8}")

    for func in functions:
        bc = "Yes" if func["has_bytecode"] else ""
        print(f"  {func['export_index']:4d}  {func['name']:30s}  {func['type']:25s}  {bc:>3}  {func['bytecode_size']:8d}")

    print(f"{'='*80}\n")


def cmd_graph(args):
    """Show Blueprint graph for a function."""
    from .uasset_bridge import AssetFile
    from .blueprint_editor import BlueprintInspector

    af = AssetFile(args.file, args.version)
    inspector = BlueprintInspector(af)
    graph = inspector.get_function_graph(args.export_index)

    print(f"\n{'='*80}")
    print(f"  Blueprint Graph: {graph.function_name}")
    print(f"  Export Index: {graph.export_index}")
    print(f"  Has Bytecode: {graph.has_bytecode}")
    print(f"  Bytecode Size: {graph.bytecode_size} bytes")
    print(f"  Nodes: {len(graph.nodes)}")
    print(f"{'='*80}")

    if graph.nodes:
        print(f"\n  {'#':>4}  {'Opcode':30s}  {'Category':10s}  {'Display Name'}")
        print(f"  {'-'*4}  {'-'*30}  {'-'*10}  {'-'*30}")
        for node in graph.nodes:
            print(f"  {node.id:4d}  {node.opcode:30s}  {node.category:10s}  {node.display_name}")
    elif graph.raw_available:
        print("\n  Bytecode parsing failed — raw bytes available.")
        print("  Use the web UI for hex inspection.")
    else:
        print("\n  No bytecode found.")

    print(f"\n{'='*80}\n")


def cmd_json(args):
    """Export asset data as JSON."""
    from .uasset_bridge import AssetFile
    from .level_logic import LevelEditor
    from .blueprint_editor import BlueprintInspector
    from .integrity import IntegrityValidator

    af = AssetFile(args.file, args.version)

    output = {
        "summary": af.summary(),
    }

    # Add level data if it's a map
    if af.is_map:
        try:
            editor = LevelEditor(af)
            actors = editor.get_actors()
            output["actors"] = [
                {
                    "export_index": a.export_index,
                    "class_name": a.class_name,
                    "object_name": a.object_name,
                    "has_blueprint": a.has_blueprint,
                    "component_count": a.component_count,
                    "properties": a.properties,
                }
                for a in actors
            ]
        except Exception as e:
            output["actors_error"] = str(e)

    # Add Blueprint data
    inspector = BlueprintInspector(af)
    output["functions"] = inspector.get_all_functions()
    output["blueprint_hierarchy"] = inspector.get_blueprint_hierarchy()

    # Add validation
    validator = IntegrityValidator(af)
    report = validator.validate_all()
    output["validation"] = report.to_dict()

    # Output
    if args.output:
        with open(args.output, "w") as f:
            json.dump(output, f, indent=2, default=str)
        print(f"  JSON exported to: {args.output}")
    else:
        print(json.dumps(output, indent=2, default=str))


def cmd_serve(args):
    """Start the web UI server."""
    # Import here to avoid loading Flask when not needed
    print(f"  Starting web UI server on port {args.port}...")
    print(f"  File: {args.file}")
    print(f"  Open http://localhost:{args.port} in your browser")

    # This will be implemented in the UI module
    from ui.server import create_app
    app = create_app(args.file, args.version)
    app.run(host="0.0.0.0", port=args.port, debug=False)


def main():
    parser = argparse.ArgumentParser(
        description="UE 5.6 Level Logic Editor — CLI",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python -m core.cli info MyLevel.umap
  python -m core.cli actors MyLevel.umap
  python -m core.cli validate MyLevel.umap
  python -m core.cli props MyLevel.umap StaticMeshActor_0
  python -m core.cli functions MyBlueprint.uasset
  python -m core.cli graph MyBlueprint.uasset 5
  python -m core.cli json MyLevel.umap -o output.json
  python -m core.cli serve MyLevel.umap --port 8080
        """
    )

    parser.add_argument("--version", "-v", default="5.6",
                        help="UE engine version (default: 5.6)")

    subparsers = parser.add_subparsers(dest="command", help="Command to run")

    # info
    p_info = subparsers.add_parser("info", help="Show asset summary")
    p_info.add_argument("file", help="Path to .uasset or .umap file")

    # actors
    p_actors = subparsers.add_parser("actors", help="List level actors")
    p_actors.add_argument("file", help="Path to .umap file")

    # validate
    p_validate = subparsers.add_parser("validate", help="Run integrity validation")
    p_validate.add_argument("file", help="Path to .uasset or .umap file")

    # props
    p_props = subparsers.add_parser("props", help="Show actor properties")
    p_props.add_argument("file", help="Path to .umap file")
    p_props.add_argument("actor_name", help="Actor object name")

    # functions
    p_funcs = subparsers.add_parser("functions", help="List Blueprint functions")
    p_funcs.add_argument("file", help="Path to .uasset file")

    # graph
    p_graph = subparsers.add_parser("graph", help="Show Blueprint graph")
    p_graph.add_argument("file", help="Path to .uasset file")
    p_graph.add_argument("export_index", type=int, help="Export index of the function")

    # json
    p_json = subparsers.add_parser("json", help="Export asset data as JSON")
    p_json.add_argument("file", help="Path to .uasset or .umap file")
    p_json.add_argument("-o", "--output", help="Output JSON file path")

    # serve
    p_serve = subparsers.add_parser("serve", help="Start web UI server")
    p_serve.add_argument("file", help="Path to .uasset or .umap file")
    p_serve.add_argument("--port", type=int, default=8080, help="Server port (default: 8080)")

    args = parser.parse_args()

    if args.command is None:
        parser.print_help()
        return 1

    # Dispatch to command handler
    commands = {
        "info": cmd_info,
        "actors": cmd_actors,
        "validate": cmd_validate,
        "props": cmd_props,
        "functions": cmd_functions,
        "graph": cmd_graph,
        "json": cmd_json,
        "serve": cmd_serve,
    }

    try:
        result = commands[args.command](args)
        return result or 0
    except FileNotFoundError as e:
        print(f"\n  ERROR: {e}\n", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"\n  ERROR: {e}\n", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
