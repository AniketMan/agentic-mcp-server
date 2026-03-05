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

    # ====================================================================
    # WRITE ENDPOINTS — Direct binary editing via UAssetAPI
    # These are the PRIMARY endpoints for the Manus workflow.
    # See GROUND_TRUTH.md for architecture details.
    # ====================================================================

    @app.route("/api/write/add-import", methods=["POST"])
    def api_write_add_import():
        """Add a new import (class/package reference) to the import table.

        Body: {
            "class_package": "/Script/Engine",
            "class_name": "Package",
            "object_name": "/Script/Engine",
            "outer_index": 0  // optional, default 0
        }
        Returns: {"success": true, "import_index": -22, "negative_index": -22}
        """
        af = _require_asset(app)
        data = request.get_json()
        if not data:
            return jsonify({"error": "Missing request body"}), 400

        required = ["class_package", "class_name", "object_name"]
        missing = [k for k in required if k not in data]
        if missing:
            return jsonify({"error": f"Missing required fields: {missing}"}), 400

        try:
            idx = af.add_import(
                class_package=data["class_package"],
                class_name=data["class_name"],
                object_name=data["object_name"],
                outer_index=data.get("outer_index", 0),
            )
            return jsonify({"success": True, "import_index": idx, "negative_index": idx})
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    @app.route("/api/write/find-or-add-import", methods=["POST"])
    def api_write_find_or_add_import():
        """Find an existing import by object_name, or add it if missing.

        Body: {
            "class_package": "/Script/Engine",
            "class_name": "Package",
            "object_name": "/Script/Engine"
        }
        Returns: {"success": true, "import_index": -21, "was_existing": true}
        """
        af = _require_asset(app)
        data = request.get_json()
        if not data:
            return jsonify({"error": "Missing request body"}), 400

        required = ["class_package", "class_name", "object_name"]
        missing = [k for k in required if k not in data]
        if missing:
            return jsonify({"error": f"Missing required fields: {missing}"}), 400

        try:
            existing = af.find_import(data["object_name"])
            was_existing = existing is not None
            idx = af.find_or_add_import(
                class_package=data["class_package"],
                class_name=data["class_name"],
                object_name=data["object_name"],
            )
            return jsonify({"success": True, "import_index": idx, "was_existing": was_existing})
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    @app.route("/api/write/add-export", methods=["POST"])
    def api_write_add_export():
        """Create a new NormalExport (actor, component, etc.).

        Body: {
            "object_name": "MyActor_0",
            "class_index": -5,        // FPackageIndex (negative = import)
            "outer_index": 1,         // FPackageIndex (positive = export, 0 = root)
            "super_index": 0,         // optional, default 0
            "template_index": 0,      // optional, default 0
            "object_flags": 8        // optional, default RF_Public (8)
        }
        Returns: {"success": true, "export_index": 14, "package_index": 15}
        """
        af = _require_asset(app)
        data = request.get_json()
        if not data:
            return jsonify({"error": "Missing request body"}), 400

        required = ["object_name", "class_index", "outer_index"]
        missing = [k for k in required if k not in data]
        if missing:
            return jsonify({"error": f"Missing required fields: {missing}"}), 400

        try:
            exp_idx, pkg_idx = af.add_export(
                object_name=data["object_name"],
                class_index=data["class_index"],
                outer_index=data["outer_index"],
                super_index=data.get("super_index", 0),
                template_index=data.get("template_index", 0),
                object_flags=data.get("object_flags", 8),
            )
            return jsonify({"success": True, "export_index": exp_idx, "package_index": pkg_idx})
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    @app.route("/api/write/add-actor", methods=["POST"])
    def api_write_add_actor():
        """Add an export to the LevelExport.Actors list.

        Body: {"export_package_index": 15}
        Returns: {"success": true, "actor_count": 14}
        """
        af = _require_asset(app)
        data = request.get_json()
        if not data or "export_package_index" not in data:
            return jsonify({"error": "Missing 'export_package_index'"}), 400

        try:
            result = af.add_actor_to_level(data["export_package_index"])
            if not result:
                return jsonify({"error": "Failed to add actor. Not a .umap or no LevelExport."}), 400
            # Get updated actor count
            level_exp = af.get_level_export()
            count = level_exp.Actors.Count if level_exp else 0
            return jsonify({"success": True, "actor_count": count})
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    @app.route("/api/write/remove-actor", methods=["POST"])
    def api_write_remove_actor():
        """Remove an actor from the LevelExport.Actors list.

        Body: {"export_package_index": 15}
        Returns: {"success": true, "actor_count": 12}
        """
        af = _require_asset(app)
        data = request.get_json()
        if not data or "export_package_index" not in data:
            return jsonify({"error": "Missing 'export_package_index'"}), 400

        try:
            result = af.remove_actor_from_level(data["export_package_index"])
            if not result:
                return jsonify({"error": "Actor not found in level or not a .umap."}), 400
            level_exp = af.get_level_export()
            count = level_exp.Actors.Count if level_exp else 0
            return jsonify({"success": True, "actor_count": count})
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    @app.route("/api/write/add-property", methods=["POST"])
    def api_write_add_property():
        """Add a new property to an export's Data list.

        Body: {
            "export_index": 5,
            "property_name": "bIsEnabled",
            "property_type": "bool",   // bool, int, float, str, name, object, byte, text, enum, softobject
            "value": true              // optional, type-appropriate default used if omitted
        }
        Returns: {"success": true, "property_count": 7}
        """
        af = _require_asset(app)
        data = request.get_json()
        if not data:
            return jsonify({"error": "Missing request body"}), 400

        required = ["export_index", "property_name", "property_type"]
        missing = [k for k in required if k not in data]
        if missing:
            return jsonify({"error": f"Missing required fields: {missing}"}), 400

        try:
            af.add_property(
                export_index=data["export_index"],
                prop_name=data["property_name"],
                prop_type=data["property_type"],
                value=data.get("value"),
            )
            # Return updated property count
            from core.uasset_bridge import NormalExport
            exp = af.get_export(data["export_index"])
            count = exp.Data.Count if isinstance(exp, NormalExport) and exp.Data else 0
            return jsonify({"success": True, "property_count": count})
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    @app.route("/api/write/remove-property", methods=["POST"])
    def api_write_remove_property():
        """Remove a property from an export by name.

        Body: {"export_index": 5, "property_name": "bIsEnabled"}
        Returns: {"success": true, "removed": true, "property_count": 6}
        """
        af = _require_asset(app)
        data = request.get_json()
        if not data:
            return jsonify({"error": "Missing request body"}), 400

        required = ["export_index", "property_name"]
        missing = [k for k in required if k not in data]
        if missing:
            return jsonify({"error": f"Missing required fields: {missing}"}), 400

        try:
            removed = af.remove_property(data["export_index"], data["property_name"])
            from core.uasset_bridge import NormalExport
            exp = af.get_export(data["export_index"])
            count = exp.Data.Count if isinstance(exp, NormalExport) and exp.Data else 0
            return jsonify({"success": True, "removed": removed, "property_count": count})
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    @app.route("/api/write/validate", methods=["GET"])
    def api_write_validate():
        """Run pre-save validation on the loaded asset.

        Returns: {"valid": true, "issues": []}
        or:      {"valid": false, "issues": ["Export 5 has null ClassIndex", ...]}
        """
        af = _require_asset(app)
        try:
            issues = af.validate()
            return jsonify({"valid": len(issues) == 0, "issues": issues})
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    @app.route("/api/write/backup", methods=["POST"])
    def api_write_backup():
        """Create a backup of the current asset file.

        Returns: {"success": true, "backup_path": "/path/to/file.umap.bak"}
        """
        af = _require_asset(app)
        try:
            backup_path = af.backup()
            return jsonify({"success": True, "backup_path": str(backup_path)})
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    @app.route("/api/write/save", methods=["POST"])
    def api_write_save():
        """Save the modified asset to disk with validation and backup.

        Body: {
            "output_path": "/path/to/output.umap",  // optional, overwrites original if omitted
            "validate": true,                         // optional, default true
            "backup": true                            // optional, default true
        }
        Returns: {"success": true, "saved_path": "...", "validation": {"valid": true, "issues": []}}
        """
        af = _require_asset(app)
        data = request.get_json() or {}
        output_path = data.get("output_path")
        do_validate = data.get("validate", True)
        do_backup = data.get("backup", True)

        try:
            # Pre-save validation
            if do_validate:
                issues = af.validate()
                if issues:
                    return jsonify({
                        "error": "Pre-save validation failed",
                        "validation": {"valid": False, "issues": issues},
                    }), 400

            saved_path = af.save(output_path, do_backup)
            return jsonify({
                "success": True,
                "saved_path": str(saved_path),
                "validation": {"valid": True, "issues": []},
            })
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    # ---- Multi-Level Routes ----

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

    # ---- Script Generation Routes ----
    # ========================================================================
    # LOCAL-ONLY: These endpoints are for Workflow B (local AI with Unreal
    # Editor open). Manus agents must NEVER call these endpoints. The Manus
    # workflow uses direct binary editing via the /api/write/* endpoints.
    # See GROUND_TRUTH.md for the full architecture.
    # ========================================================================

    @app.route("/api/script/generate", methods=["POST"])
    def api_script_generate():
        """[LOCAL-ONLY] Generate a UE5.6 Python script for a given operation.
        WARNING: This endpoint is for Workflow B only. Manus agents must use
        the /api/write/* endpoints for direct binary editing instead.
        
        Body: {"domain": "actors", "method": "spawn", "params": {"actor_class": "StaticMeshActor"}}
        OR:   {"operation": "actors.spawn", "params": {"actor_class": "StaticMeshActor"}}
        """
        data = request.get_json()
        if not data:
            return jsonify({"error": "Missing request body"}), 400

        try:
            from core.script_generator import ScriptGenerator
            gen = ScriptGenerator()
            params = data.get("params", {})

            # Support both formats
            if "domain" in data and "method" in data:
                domain = data["domain"]
                method = data["method"]
            elif "operation" in data:
                parts = data["operation"].split(".", 1)
                if len(parts) == 2:
                    domain, method = parts
                else:
                    return jsonify({"error": "Operation must be 'domain.method' format, e.g. 'actors.spawn'"}), 400
            else:
                return jsonify({"error": "Missing 'domain'+'method' or 'operation' in request body"}), 400

            script = gen.generate_from_request(domain, method, params)
            return jsonify({
                "success": True,
                "operation": f"{domain}.{method}",
                "script": script,
                "line_count": len(script.strip().split('\n')),
            })
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    @app.route("/api/script/operations")
    def api_script_operations():
        """List all available script generation operations grouped by domain."""
        try:
            from core.script_generator import ScriptGenerator
            import inspect
            gen = ScriptGenerator()
            raw = gen.list_domains()
            domains = []
            for domain_name, info in raw.items():
                domain_obj = getattr(gen, domain_name)
                ops = []
                for method_name in info['methods']:
                    method_fn = getattr(domain_obj, method_name)
                    sig = inspect.signature(method_fn)
                    params = []
                    for pname, param in sig.parameters.items():
                        if pname == 'self':
                            continue
                        params.append({
                            'name': pname,
                            'required': param.default is inspect.Parameter.empty,
                            'default': str(param.default) if param.default is not inspect.Parameter.empty else None,
                        })
                    ops.append({
                        'id': f'{domain_name}.{method_name}',
                        'method': method_name,
                        'description': (method_fn.__doc__ or '').strip().split('\n')[0],
                        'params': params,
                    })
                domains.append({
                    'name': domain_name.replace('_', ' ').title(),
                    'key': domain_name,
                    'description': info['doc'],
                    'operations': ops,
                })
            return jsonify({'domains': domains})
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    @app.route("/api/script/save", methods=["POST"])
    def api_script_save():
        """Generate a script and save it to a file."""
        data = request.get_json()
        if not data or "output_path" not in data:
            return jsonify({"error": "Missing 'output_path'"}), 400

        try:
            from core.script_generator import ScriptGenerator
            gen = ScriptGenerator()
            params = data.get("params", {})
            output_path = data["output_path"]

            if "domain" in data and "method" in data:
                domain, method = data["domain"], data["method"]
            elif "operation" in data:
                parts = data["operation"].split(".", 1)
                domain, method = parts[0], parts[1] if len(parts) == 2 else ''
            else:
                return jsonify({"error": "Missing 'domain'+'method' or 'operation'"}), 400

            script = gen.generate_from_request(domain, method, params)
            with open(output_path, 'w', encoding='utf-8') as f:
                f.write(script)
            return jsonify({"success": True, "path": output_path, "line_count": len(script.strip().split('\n'))})
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    # ---- Project Scanning Routes ----

    @app.route("/api/project/scan", methods=["POST"])
    def api_project_scan():
        """Scan a full UE project directory."""
        data = request.get_json()
        if not data or "project_root" not in data:
            return jsonify({"error": "Missing 'project_root' in request body"}), 400

        try:
            from core.project_scanner import ProjectScanner
            scanner = ProjectScanner(data["project_root"])
            # Use bridge if available for deep inspection
            bridge = app.config.get("ASSET_FILE")
            report = scanner.full_scan(bridge=None)  # bridge for deep scan is optional
            app.config["PROJECT_SCANNER"] = scanner
            app.config["PROJECT_REPORT"] = report
            return jsonify(report)
        except Exception as e:
            return jsonify({"error": str(e), "trace": traceback.format_exc()}), 500

    @app.route("/api/project/info")
    def api_project_info():
        """Get cached project scan results."""
        report = app.config.get("PROJECT_REPORT")
        if report is None:
            return jsonify({"error": "No project scanned. POST to /api/project/scan first."}), 400
        return jsonify(report)

    @app.route("/api/project/assets")
    def api_project_assets():
        """Get the asset registry from the last project scan."""
        scanner = app.config.get("PROJECT_SCANNER")
        if scanner is None or scanner.asset_registry is None:
            return jsonify({"error": "No project scanned."}), 400
        reg = scanner.asset_registry
        # Return paginated asset list
        page = request.args.get('page', 1, type=int)
        per_page = request.args.get('per_page', 100, type=int)
        asset_type = request.args.get('type', None)

        assets = list(reg.assets.values())
        if asset_type:
            assets = [a for a in assets if a.asset_type == asset_type]

        total = len(assets)
        start = (page - 1) * per_page
        end = start + per_page
        page_assets = assets[start:end]

        return jsonify({
            "total": total,
            "page": page,
            "per_page": per_page,
            "assets": [
                {"name": a.name, "path": a.path, "type": a.asset_type,
                 "size_bytes": a.size_bytes, "has_uexp": a.has_uexp}
                for a in page_assets
            ],
        })

    @app.route("/api/project/levels")
    def api_project_levels():
        """Get all levels from the last project scan."""
        scanner = app.config.get("PROJECT_SCANNER")
        if scanner is None:
            return jsonify({"error": "No project scanned."}), 400
        return jsonify([
            {
                "name": l.name, "path": l.path,
                "size_kb": round(l.size_bytes / 1024, 1),
                "has_logic": l.has_logic, "is_persistent": l.is_persistent,
                "actor_count": l.actor_count, "function_count": l.function_count,
                "referenced_blueprints": l.referenced_blueprints,
            }
            for l in scanner.levels
        ])

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
