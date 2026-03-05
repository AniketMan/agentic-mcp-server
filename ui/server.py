"""
server.py — Flask web server for the UE 5.6 Level Logic Editor.

Provides a REST API and serves the web UI for:
  - Asset inspection (exports, imports, names)
  - Level actor browsing and editing
  - Blueprint graph visualization
  - Property editing
  - Reference integrity validation
  - Safe save with backup

All endpoints return JSON. The frontend is a single-page app
served from static files.
"""

import os
import sys
import json
import traceback
from pathlib import Path
from flask import Flask, jsonify, request, send_from_directory, abort

# Ensure core module is importable
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))


def create_app(filepath: str = None, engine_version: str = "5.6"):
    """
    Create and configure the Flask application.
    
    Args:
        filepath: Path to the .uasset/.umap file to edit
        engine_version: UE version string
    """
    app = Flask(
        __name__,
        static_folder=str(Path(__file__).parent / "static"),
        static_url_path="/static"
    )

    # Store state
    app.config["ASSET_FILE"] = None
    app.config["LEVEL_EDITOR"] = None
    app.config["BP_INSPECTOR"] = None
    app.config["VALIDATOR"] = None
    app.config["ENGINE_VERSION"] = engine_version
    # Multi-file state for Level Logic view
    app.config["LOADED_LEVELS"] = {}  # filepath -> {asset, editor, inspector, summary}
    # Plugin validation state
    app.config["LOADED_PLUGINS"] = {}  # path -> PluginReport dict

    if filepath:
        _load_asset(app, filepath, engine_version)

    # ---- Routes ----

    @app.route("/")
    def index():
        """Serve the main UI page."""
        return send_from_directory(app.static_folder, "index.html")

    @app.route("/api/status")
    def api_status():
        """Check if an asset is loaded."""
        af = app.config["ASSET_FILE"]
        if af is None:
            return jsonify({"loaded": False, "message": "No asset loaded"})
        return jsonify({
            "loaded": True,
            "filepath": str(af.filepath),
            "engine_version": af.engine_version_str,
            "is_map": af.is_map,
        })

    @app.route("/api/load", methods=["POST"])
    def api_load():
        """Load a new asset file."""
        data = request.get_json()
        if not data or "filepath" not in data:
            return jsonify({"error": "Missing 'filepath' in request body"}), 400

        version = data.get("engine_version", app.config["ENGINE_VERSION"])
        try:
            _load_asset(app, data["filepath"], version)
            af = app.config["ASSET_FILE"]
            return jsonify({
                "success": True,
                "summary": af.summary(),
            })
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    @app.route("/api/summary")
    def api_summary():
        """Get asset summary."""
        af = _require_asset(app)
        return jsonify(af.summary())

    @app.route("/api/exports")
    def api_exports():
        """List all exports with metadata."""
        af = _require_asset(app)
        exports = []
        for i in range(af.export_count):
            exp = af.get_export(i)
            exports.append({
                "index": i,
                "class_name": af.get_export_class_name(exp),
                "object_name": af.get_export_name(exp),
                "type": type(exp).__name__,
                "serial_size": int(exp.SerialSize) if hasattr(exp, "SerialSize") else 0,
            })
        return jsonify(exports)

    @app.route("/api/imports")
    def api_imports():
        """List all imports."""
        af = _require_asset(app)
        imports = []
        for i, imp in enumerate(af.imports):
            imports.append({
                "index": i,
                "class_name": str(imp.ClassName),
                "class_package": str(imp.ClassPackage),
                "object_name": str(imp.ObjectName),
            })
        return jsonify(imports)

    @app.route("/api/names")
    def api_names():
        """List the name map."""
        af = _require_asset(app)
        return jsonify(af.name_map)

    @app.route("/api/export/<int:index>")
    def api_export_detail(index):
        """Get detailed info about a specific export."""
        af = _require_asset(app)
        try:
            exp = af.get_export(index)
        except IndexError:
            return jsonify({"error": f"Export index {index} out of range"}), 404

        from core.uasset_bridge import NormalExport
        result = {
            "index": index,
            "class_name": af.get_export_class_name(exp),
            "object_name": af.get_export_name(exp),
            "type": type(exp).__name__,
            "outer_index": exp.OuterIndex.Index,
            "class_index": exp.ClassIndex.Index,
            "super_index": exp.SuperIndex.Index,
            "template_index": exp.TemplateIndex.Index,
            "serial_size": int(exp.SerialSize),
            "serial_offset": int(exp.SerialOffset),
            "properties": af.get_export_properties(exp) if isinstance(exp, NormalExport) else [],
        }
        return jsonify(result)

    @app.route("/api/actors")
    def api_actors():
        """List all level actors (requires .umap)."""
        editor = _require_level_editor(app)
        actors = editor.get_actors()
        return jsonify([
            {
                "export_index": a.export_index,
                "package_index": a.package_index,
                "class_name": a.class_name,
                "object_name": a.object_name,
                "outer_name": a.outer_name,
                "has_blueprint": a.has_blueprint,
                "component_count": a.component_count,
                "property_count": len(a.properties),
            }
            for a in actors
        ])

    @app.route("/api/actor/<name>")
    def api_actor_detail(name):
        """Get detailed actor info including properties and components."""
        editor = _require_level_editor(app)
        actor = editor.get_actor_by_name(name)
        if actor is None:
            return jsonify({"error": f"Actor '{name}' not found"}), 404

        components = editor.get_actor_components(actor)
        return jsonify({
            "export_index": actor.export_index,
            "package_index": actor.package_index,
            "class_name": actor.class_name,
            "object_name": actor.object_name,
            "outer_name": actor.outer_name,
            "has_blueprint": actor.has_blueprint,
            "component_count": actor.component_count,
            "properties": actor.properties,
            "components": components,
        })

    @app.route("/api/actor/<name>/references")
    def api_actor_references(name):
        """Find all references to an actor."""
        editor = _require_level_editor(app)
        actor = editor.get_actor_by_name(name)
        if actor is None:
            return jsonify({"error": f"Actor '{name}' not found"}), 404
        if actor.export_index < 0:
            return jsonify({"error": "Actor is not an export"}), 400

        refs = editor.find_references_to_export(actor.export_index)
        return jsonify(refs)

    @app.route("/api/functions")
    def api_functions():
        """List all Blueprint functions."""
        inspector = _require_bp_inspector(app)
        return jsonify(inspector.get_all_functions())

    @app.route("/api/graph/<int:export_index>")
    def api_graph(export_index):
        """Get Blueprint graph for a function."""
        inspector = _require_bp_inspector(app)
        try:
            graph = inspector.get_function_graph(export_index)
            return jsonify(inspector.graph_to_json(graph))
        except Exception as e:
            return jsonify({"error": str(e)}), 500

    @app.route("/api/blueprint/hierarchy")
    def api_bp_hierarchy():
        """Get Blueprint class hierarchy."""
        inspector = _require_bp_inspector(app)
        return jsonify(inspector.get_blueprint_hierarchy())

    @app.route("/api/validate")
    def api_validate():
        """Run integrity validation."""
        validator = _require_validator(app)
        report = validator.validate_all()
        return jsonify(report.to_dict())

    @app.route("/api/property", methods=["POST"])
    def api_set_property():
        """Set a property value on an export."""
        af = _require_asset(app)
        data = request.get_json()
        if not data:
            return jsonify({"error": "Missing request body"}), 400

        export_index = data.get("export_index")
        prop_name = data.get("property_name")
        new_value = data.get("value")

        if export_index is None or prop_name is None:
            return jsonify({"error": "Missing export_index or property_name"}), 400

        try:
            af.set_property_value(export_index, prop_name, new_value)
            return jsonify({"success": True})
        except Exception as e:
            return jsonify({"error": str(e)}), 500

    @app.route("/api/load-multi", methods=["POST"])
    def api_load_multi():
        """Load multiple .umap files for the Level Logic view."""
        data = request.get_json()
        if not data or "filepaths" not in data:
            return jsonify({"error": "Missing 'filepaths' in request body"}), 400

        version = data.get("engine_version", app.config["ENGINE_VERSION"])
        results = []
        for fp in data["filepaths"]:
            try:
                _load_level(app, fp, version)
                info = app.config["LOADED_LEVELS"][fp]
                results.append({"filepath": fp, "success": True, "summary": info["summary"]})
            except Exception as e:
                results.append({"filepath": fp, "success": False, "error": str(e)})
        return jsonify({"results": results, "total_loaded": len(app.config["LOADED_LEVELS"])})

    @app.route("/api/levels")
    def api_levels():
        """List all loaded levels with their logic summary."""
        levels = []
        for fp, info in app.config["LOADED_LEVELS"].items():
            lvl = {
                "filepath": fp,
                "filename": os.path.basename(fp),
                "summary": info["summary"],
                "has_logic": info["has_logic"],
                "functions": info["functions"],
                "actors": info["actors"],
                "blueprint_class": info["blueprint_class"],
                "k2_nodes": info.get("k2_nodes", []),
            }
            levels.append(lvl)
        return jsonify(levels)

    @app.route("/api/level/<path:filename>/graph/<int:export_index>")
    def api_level_graph(filename, export_index):
        """Get Blueprint graph for a function in a specific level."""
        info = _find_level_by_filename(app, filename)
        if info is None:
            return jsonify({"error": f"Level '{filename}' not loaded"}), 404
        try:
            graph = info["inspector"].get_function_graph(export_index)
            return jsonify(info["inspector"].graph_to_json(graph))
        except Exception as e:
            return jsonify({"error": str(e)}), 500

    @app.route("/api/level/<path:filename>/actors")
    def api_level_actors(filename):
        """Get actors for a specific level."""
        info = _find_level_by_filename(app, filename)
        if info is None:
            return jsonify({"error": f"Level '{filename}' not loaded"}), 404
        return jsonify(info["actors"])

    @app.route("/api/level/<path:filename>/validate")
    def api_level_validate(filename):
        """Validate a specific level."""
        info = _find_level_by_filename(app, filename)
        if info is None:
            return jsonify({"error": f"Level '{filename}' not loaded"}), 404
        try:
            report = info["validator"].validate_all()
            return jsonify(report.to_dict())
        except Exception as e:
            return jsonify({"error": str(e)}), 500

    @app.route("/api/save", methods=["POST"])
    def api_save():
        """Save the asset with validation."""
        af = _require_asset(app)
        data = request.get_json() or {}
        output_path = data.get("output_path")
        validate = data.get("validate", True)
        backup = data.get("backup", True)

        try:
            if af.is_map:
                editor = _require_level_editor(app)
                saved_path, report = editor.save(output_path, validate, backup)
            else:
                if validate:
                    validator = _require_validator(app)
                    report = validator.validate_all()
                    if not report.passed:
                        return jsonify({
                            "error": "Validation failed",
                            "validation": report.to_dict(),
                        }), 400
                else:
                    report = None
                saved_path = af.save(output_path, backup)

            result = {"success": True, "saved_path": str(saved_path)}
            if report:
                result["validation"] = report.to_dict()
            return jsonify(result)
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    # ---- Plugin Validation Routes ----

    @app.route("/api/plugin/validate", methods=["POST"])
    def api_plugin_validate():
        """Validate a plugin directory or .uplugin file."""
        data = request.get_json()
        if not data or "path" not in data:
            return jsonify({"error": "Missing 'path' in request body"}), 400

        plugin_path = data["path"]
        try:
            from core.plugin_validator import PluginValidator
            validator = PluginValidator(plugin_path)
            report = validator.validate()
            result = report.to_dict()
            # Store the report
            app.config["LOADED_PLUGINS"][plugin_path] = result
            return jsonify(result)
        except FileNotFoundError as e:
            return jsonify({"error": str(e)}), 404
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    @app.route("/api/plugin/validate-multiple", methods=["POST"])
    def api_plugin_validate_multiple():
        """Validate multiple plugins at once."""
        data = request.get_json()
        if not data or "paths" not in data:
            return jsonify({"error": "Missing 'paths' in request body"}), 400

        from core.plugin_validator import PluginValidator
        results = []
        for path in data["paths"]:
            try:
                validator = PluginValidator(path)
                report = validator.validate()
                result = report.to_dict()
                app.config["LOADED_PLUGINS"][path] = result
                results.append({"path": path, "success": True, "report": result})
            except Exception as e:
                results.append({"path": path, "success": False, "error": str(e)})
        return jsonify({"results": results, "total": len(results)})

    @app.route("/api/plugin/scan", methods=["POST"])
    def api_plugin_scan():
        """Scan a directory for all .uplugin files and validate them."""
        data = request.get_json()
        if not data or "directory" not in data:
            return jsonify({"error": "Missing 'directory' in request body"}), 400

        scan_dir = Path(data["directory"])
        if not scan_dir.is_dir():
            return jsonify({"error": f"Directory not found: {scan_dir}"}), 404

        from core.plugin_validator import PluginValidator
        results = []
        for uplugin_file in scan_dir.rglob("*.uplugin"):
            try:
                validator = PluginValidator(str(uplugin_file))
                report = validator.validate()
                result = report.to_dict()
                app.config["LOADED_PLUGINS"][str(uplugin_file.parent)] = result
                results.append({"path": str(uplugin_file.parent), "success": True, "report": result})
            except Exception as e:
                results.append({"path": str(uplugin_file), "success": False, "error": str(e)})
        return jsonify({"results": results, "total": len(results)})

    @app.route("/api/plugins")
    def api_plugins():
        """List all validated plugins."""
        return jsonify(list(app.config["LOADED_PLUGINS"].values()))

    return app


# ---- Helpers ----

def _load_asset(app, filepath, version):
    """Load an asset and initialize all editors."""
    from core.uasset_bridge import AssetFile
    from core.integrity import IntegrityValidator
    from core.blueprint_editor import BlueprintInspector

    af = AssetFile(filepath, version)
    app.config["ASSET_FILE"] = af
    app.config["BP_INSPECTOR"] = BlueprintInspector(af)
    app.config["VALIDATOR"] = IntegrityValidator(af)

    if af.is_map:
        from core.level_logic import LevelEditor
        try:
            app.config["LEVEL_EDITOR"] = LevelEditor(af)
        except ValueError:
            app.config["LEVEL_EDITOR"] = None
    else:
        app.config["LEVEL_EDITOR"] = None


def _require_asset(app):
    af = app.config.get("ASSET_FILE")
    if af is None:
        abort(400, description="No asset loaded. POST to /api/load first.")
    return af


def _require_level_editor(app):
    _require_asset(app)
    editor = app.config.get("LEVEL_EDITOR")
    if editor is None:
        abort(400, description="Not a level file or no LevelExport found.")
    return editor


def _require_bp_inspector(app):
    _require_asset(app)
    inspector = app.config.get("BP_INSPECTOR")
    if inspector is None:
        abort(500, description="Blueprint inspector not initialized.")
    return inspector


def _require_validator(app):
    _require_asset(app)
    validator = app.config.get("VALIDATOR")
    if validator is None:
        abort(500, description="Validator not initialized.")
    return validator


def _load_level(app, filepath, version):
    """Load a level file into the multi-file store."""
    from core.uasset_bridge import AssetFile
    from core.integrity import IntegrityValidator
    from core.blueprint_editor import BlueprintInspector

    af = AssetFile(filepath, version)
    inspector = BlueprintInspector(af)
    validator = IntegrityValidator(af)

    editor = None
    if af.is_map:
        from core.level_logic import LevelEditor
        try:
            editor = LevelEditor(af)
        except ValueError:
            pass

    # Get functions
    functions = inspector.get_all_functions()
    has_logic = any(f["has_bytecode"] for f in functions) or len(functions) > 0

    # Get actors
    actors = []
    if editor:
        raw_actors = editor.get_actors()
        actors = [
            {
                "export_index": a.export_index,
                "class_name": a.class_name,
                "object_name": a.object_name,
                "has_blueprint": a.has_blueprint,
                "component_count": a.component_count,
                "property_count": len(a.properties),
            }
            for a in raw_actors
        ]

    # Get blueprint class info
    bp_class = None
    hierarchy = inspector.get_blueprint_hierarchy()
    if hierarchy:
        bp_class = hierarchy[0]

    # Get K2 nodes with editor-matching display names
    k2_nodes_raw = inspector.get_k2_nodes()
    k2_nodes = [
        {
            "export_index": k.export_index,
            "class_name": k.class_name,
            "object_name": k.object_name,
            "node_type": k.node_type,
            "readable_name": k.readable_name,
            "pos_x": k.pos_x,
            "pos_y": k.pos_y,
            "properties": k.properties,
        }
        for k in k2_nodes_raw
    ]

    app.config["LOADED_LEVELS"][filepath] = {
        "asset": af,
        "editor": editor,
        "inspector": inspector,
        "validator": validator,
        "summary": af.summary(),
        "has_logic": has_logic,
        "functions": functions,
        "actors": actors,
        "blueprint_class": bp_class,
        "k2_nodes": k2_nodes,
    }


def _find_level_by_filename(app, filename):
    """Find a loaded level by its filename."""
    for fp, info in app.config["LOADED_LEVELS"].items():
        if os.path.basename(fp) == filename:
            return info
    return None
