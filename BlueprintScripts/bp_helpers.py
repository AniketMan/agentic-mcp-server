# bp_helpers.py -- Reusable helpers for Blueprint graph construction via MCP HTTP API
# Paste this file into your UE5 project's Scripts folder, or paste inline before scene scripts.
# All functions call the Agentic MCP C++ plugin running on localhost:9847.
#
# USAGE:
#   import bp_helpers as bp
#   bp.check_connection()
#   node = bp.add_node("BP_MyActor", "Event", event_name="ReceiveBeginPlay")
#   print_node = bp.add_node("BP_MyActor", "CallFunction", function_name="PrintString", pos_x=300)
#   bp.connect_pins("BP_MyActor", node["nodeGuid"], "then", print_node["nodeGuid"], "execute")
#   bp.compile_blueprint("BP_MyActor")

import json
import sys

# ---------------------------------------------------------------------------
# HTTP transport -- works in UE5 Python console (no requests library needed)
# ---------------------------------------------------------------------------
try:
    import urllib.request
    import urllib.error
    import urllib.parse
except ImportError:
    print("[bp_helpers] ERROR: urllib not available. Cannot proceed.")
    sys.exit(1)

MCP_BASE_URL = "http://localhost:9847"
TIMEOUT_SECONDS = 30
_error_log = []


def _post(endpoint, data=None):
    """Send POST request to MCP plugin. Returns parsed JSON or raises."""
    url = "{}/api/{}".format(MCP_BASE_URL, endpoint)
    body = json.dumps(data or {}).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST"
    )
    try:
        resp = urllib.request.urlopen(req, timeout=TIMEOUT_SECONDS)
        raw = resp.read().decode("utf-8")
        if not raw.strip():
            return {"success": True}
        return json.loads(raw)
    except urllib.error.HTTPError as e:
        err_body = e.read().decode("utf-8") if e.fp else str(e)
        msg = "[bp_helpers] HTTP {} on /api/{}: {}".format(e.code, endpoint, err_body)
        _error_log.append(msg)
        print(msg)
        return {"success": False, "error": msg}
    except urllib.error.URLError as e:
        msg = "[bp_helpers] Connection failed for /api/{}: {}".format(endpoint, str(e.reason))
        _error_log.append(msg)
        print(msg)
        return {"success": False, "error": msg}
    except Exception as e:
        msg = "[bp_helpers] Unexpected error on /api/{}: {}".format(endpoint, str(e))
        _error_log.append(msg)
        print(msg)
        return {"success": False, "error": msg}


def _get(endpoint, params=None):
    """Send GET request to MCP plugin. Returns parsed JSON or raises."""
    url = "{}/api/{}".format(MCP_BASE_URL, endpoint)
    if params:
        query = urllib.parse.urlencode(params)
        url = "{}?{}".format(url, query)
    req = urllib.request.Request(url, method="GET")
    try:
        resp = urllib.request.urlopen(req, timeout=TIMEOUT_SECONDS)
        raw = resp.read().decode("utf-8")
        if not raw.strip():
            return {"success": True}
        return json.loads(raw)
    except urllib.error.HTTPError as e:
        err_body = e.read().decode("utf-8") if e.fp else str(e)
        msg = "[bp_helpers] HTTP {} on GET /api/{}: {}".format(e.code, endpoint, err_body)
        _error_log.append(msg)
        print(msg)
        return {"success": False, "error": msg}
    except urllib.error.URLError as e:
        msg = "[bp_helpers] Connection failed for GET /api/{}: {}".format(endpoint, str(e.reason))
        _error_log.append(msg)
        print(msg)
        return {"success": False, "error": msg}
    except Exception as e:
        msg = "[bp_helpers] Unexpected error on GET /api/{}: {}".format(endpoint, str(e))
        _error_log.append(msg)
        print(msg)
        return {"success": False, "error": msg}


# ---------------------------------------------------------------------------
# Connection check
# ---------------------------------------------------------------------------
def check_connection():
    """Verify MCP plugin is reachable. Returns True/False."""
    result = _get("listViewports")
    if result.get("success", True) and "error" not in result:
        print("[bp_helpers] MCP plugin connected at {}".format(MCP_BASE_URL))
        return True
    print("[bp_helpers] ERROR: Cannot reach MCP plugin at {}".format(MCP_BASE_URL))
    print("[bp_helpers] Make sure the Agentic MCP C++ plugin is running in UE5.")
    return False


# ---------------------------------------------------------------------------
# Blueprint CRUD
# ---------------------------------------------------------------------------
def create_blueprint(name, parent_class="Actor", path="/Game/Blueprints"):
    """Create a new Blueprint asset. Returns result dict."""
    return _post("createBlueprint", {
        "name": name,
        "parentClass": parent_class,
        "path": path
    })


def compile_blueprint(bp_name):
    """Compile a Blueprint. Returns result dict with any errors."""
    return _post("compileBlueprint", {"blueprintName": bp_name})


def get_blueprint(bp_name):
    """Get detailed Blueprint info (variables, graphs, components)."""
    return _get("blueprint", {"name": bp_name})


# ---------------------------------------------------------------------------
# Variable management
# ---------------------------------------------------------------------------
def add_variable(bp_name, var_name, var_type, default_value=None, is_exposed=False):
    """Add a variable to a Blueprint."""
    data = {
        "blueprintName": bp_name,
        "variableName": var_name,
        "variableType": var_type
    }
    if default_value is not None:
        data["defaultValue"] = str(default_value)
    if is_exposed:
        data["isExposed"] = True
    return _post("addVariable", data)


# ---------------------------------------------------------------------------
# Component management
# ---------------------------------------------------------------------------
def add_component(bp_name, component_class, component_name=None, parent=None):
    """Add a component to a Blueprint."""
    data = {
        "blueprintName": bp_name,
        "componentClass": component_class
    }
    if component_name:
        data["componentName"] = component_name
    if parent:
        data["parentComponent"] = parent
    return _post("addComponent", data)


# ---------------------------------------------------------------------------
# Node creation
# ---------------------------------------------------------------------------
def add_node(bp_name, node_type, **kwargs):
    """
    Add a node to a Blueprint graph. Returns dict with 'nodeGuid'.
    
    Common kwargs:
        graph_name: str     -- target graph (default: EventGraph)
        pos_x: float        -- X position
        pos_y: float        -- Y position
        function_name: str  -- for CallFunction nodes
        event_name: str     -- for Event/CustomEvent nodes
        variable_name: str  -- for VariableGet/VariableSet nodes
        target_class: str   -- for DynamicCast nodes
        struct_type: str    -- for BreakStruct/MakeStruct nodes
        comment_text: str   -- for Comment nodes
    """
    data = {
        "blueprintName": bp_name,
        "nodeType": node_type
    }
    # Map Python-style kwargs to API camelCase params
    param_map = {
        "graph_name": "graphName",
        "pos_x": "posX",
        "pos_y": "posY",
        "function_name": "functionName",
        "event_name": "eventName",
        "variable_name": "variableName",
        "target_class": "targetClass",
        "macro_name": "macroName",
        "struct_type": "structType",
        "comment_text": "commentText"
    }
    for py_key, api_key in param_map.items():
        if py_key in kwargs and kwargs[py_key] is not None:
            data[api_key] = kwargs[py_key]
    
    result = _post("addNode", data)
    if result.get("success", True) and "nodeGuid" not in result and "error" not in result:
        # Some responses nest the guid differently
        pass
    return result


def delete_node(bp_name, node_guid):
    """Delete a node from a Blueprint graph."""
    return _post("deleteNode", {
        "blueprintName": bp_name,
        "nodeGuid": node_guid
    })


# ---------------------------------------------------------------------------
# Pin connections
# ---------------------------------------------------------------------------
def connect_pins(bp_name, src_guid, src_pin, tgt_guid, tgt_pin):
    """Connect two pins between nodes."""
    return _post("connectPins", {
        "blueprintName": bp_name,
        "sourceNodeGuid": src_guid,
        "sourcePinName": src_pin,
        "targetNodeGuid": tgt_guid,
        "targetPinName": tgt_pin
    })


def disconnect_pin(bp_name, node_guid, pin_name):
    """Disconnect all connections from a pin."""
    return _post("disconnectPin", {
        "blueprintName": bp_name,
        "nodeGuid": node_guid,
        "pinName": pin_name
    })


def set_pin_default(bp_name, node_guid, pin_name, value):
    """Set the default value of a pin."""
    return _post("setPinDefault", {
        "blueprintName": bp_name,
        "nodeGuid": node_guid,
        "pinName": pin_name,
        "value": str(value)
    })


# ---------------------------------------------------------------------------
# Graph management
# ---------------------------------------------------------------------------
def get_graph(bp_name, graph_name="EventGraph"):
    """Get all nodes and connections in a Blueprint graph."""
    return _get("graph", {"name": bp_name, "graph": graph_name})


def create_graph(bp_name, graph_name, graph_type="function"):
    """Create a new function or macro graph."""
    return _post("createGraph", {
        "blueprintName": bp_name,
        "graphName": graph_name,
        "graphType": graph_type
    })


def get_pin_info(bp_name, node_guid, graph_name="EventGraph"):
    """Get detailed pin info for a specific node."""
    return _post("getPinInfo", {
        "blueprintName": bp_name,
        "nodeGuid": node_guid,
        "graphName": graph_name
    })


# ---------------------------------------------------------------------------
# Actor management
# ---------------------------------------------------------------------------
def spawn_actor(class_name, name=None, location=None, rotation=None, scale=None):
    """Spawn an actor in the current level."""
    data = {"className": class_name}
    if name:
        data["name"] = name
    if location:
        data["location"] = location
    if rotation:
        data["rotation"] = rotation
    if scale:
        data["scale"] = scale
    return _post("spawnActor", data)


def list_actors(class_filter=None, name_filter=None):
    """List actors in the current level."""
    data = {}
    if class_filter:
        data["classFilter"] = class_filter
    if name_filter:
        data["nameFilter"] = name_filter
    return _post("listActors", data)


def set_actor_property(name, prop, value):
    """Set a property on an actor."""
    return _post("setActorProperty", {
        "name": name,
        "property": prop,
        "value": str(value)
    })


def set_actor_transform(name, location=None, rotation=None, scale=None):
    """Set actor transform."""
    data = {"name": name}
    if location:
        data["location"] = location
    if rotation:
        data["rotation"] = rotation
    if scale:
        data["scale"] = scale
    return _post("setActorTransform", data)


# ---------------------------------------------------------------------------
# Level management
# ---------------------------------------------------------------------------
def load_level(name):
    """Load a level by name or path."""
    return _post("loadLevel", {"name": name})


def list_levels():
    """List all levels/sublevels."""
    return _get("listLevels")


def get_level_blueprint(level_name=None):
    """Get the level Blueprint for a level."""
    data = {}
    if level_name:
        data["levelName"] = level_name
    return _post("getLevelBlueprint", data)


# ---------------------------------------------------------------------------
# Level Sequence
# ---------------------------------------------------------------------------
def list_sequences():
    """List all Level Sequence assets."""
    return _post("listSequences", {})


def read_sequence(name):
    """Read tracks and keyframes of a Level Sequence."""
    return _post("readSequence", {"name": name})


# ---------------------------------------------------------------------------
# Audio
# ---------------------------------------------------------------------------
def play_sound(sound_name, location=None, volume=1.0):
    """Play a sound asset."""
    data = {"soundName": sound_name, "volume": volume}
    if location:
        data["location"] = location
    return _post("audioPlaySound", data)


# ---------------------------------------------------------------------------
# Python execution in editor
# ---------------------------------------------------------------------------
def execute_python(script):
    """Execute Python code inside the UE5 editor."""
    return _post("executePython", {"script": script})


# ---------------------------------------------------------------------------
# Transactions (undo support)
# ---------------------------------------------------------------------------
def begin_transaction(description):
    """Begin an undo transaction."""
    return _post("beginTransaction", {"description": description})


def end_transaction():
    """End the current undo transaction."""
    return _post("endTransaction", {})


# ---------------------------------------------------------------------------
# Snapshot / Rollback
# ---------------------------------------------------------------------------
def snapshot_graph(bp_name, graph_name=None):
    """Take a snapshot of a Blueprint graph."""
    data = {"blueprintName": bp_name}
    if graph_name:
        data["graphName"] = graph_name
    return _post("snapshotGraph", data)


def restore_graph(snapshot_id):
    """Restore a Blueprint graph from snapshot."""
    return _post("restoreGraph", {"snapshotId": snapshot_id})


# ---------------------------------------------------------------------------
# Utility: Build a complete interaction chain
# ---------------------------------------------------------------------------
def build_interaction_chain(bp_name, trigger_event, sequence_name=None, 
                            story_step=None, pos_x=0, pos_y=0):
    """
    Build a standard interaction chain:
    Event -> [Play Sequence] -> [Broadcast Story Step]
    
    Returns dict of all created node GUIDs.
    """
    nodes = {}
    x = pos_x
    y = pos_y
    
    # 1. Event node
    event = add_node(bp_name, "CustomEvent", event_name=trigger_event, pos_x=x, pos_y=y)
    nodes["event"] = event.get("nodeGuid", "")
    x += 300
    
    # 2. Play Level Sequence (if provided)
    if sequence_name:
        play_seq = add_node(bp_name, "CallFunction", 
                           function_name="CreateLevelSequencePlayer",
                           pos_x=x, pos_y=y)
        nodes["play_sequence"] = play_seq.get("nodeGuid", "")
        if nodes["event"] and nodes["play_sequence"]:
            connect_pins(bp_name, nodes["event"], "then", nodes["play_sequence"], "execute")
        x += 300
    
    # 3. Broadcast story step (if provided)
    if story_step:
        broadcast = add_node(bp_name, "CallFunction",
                            function_name="BroadcastMessage",
                            pos_x=x, pos_y=y)
        nodes["broadcast"] = broadcast.get("nodeGuid", "")
        prev_node = nodes.get("play_sequence", nodes["event"])
        if prev_node and nodes["broadcast"]:
            connect_pins(bp_name, prev_node, "then", nodes["broadcast"], "execute")
        x += 300
    
    return nodes


# ---------------------------------------------------------------------------
# Utility: Build cinematic auto-play chain
# ---------------------------------------------------------------------------
def build_cinematic_chain(bp_name, sequences, delays=None, pos_x=0, pos_y=0):
    """
    Build a BeginPlay -> Delay -> PlaySequence -> Delay -> PlaySequence chain.
    
    sequences: list of sequence asset names
    delays: list of delay durations (seconds) between sequences. 
            If None, uses 1.0s between each.
    
    Returns list of node GUIDs.
    """
    if not delays:
        delays = [1.0] * len(sequences)
    
    nodes = []
    x = pos_x
    y = pos_y
    
    # BeginPlay event
    begin_play = add_node(bp_name, "Event", event_name="ReceiveBeginPlay", pos_x=x, pos_y=y)
    begin_play_guid = begin_play.get("nodeGuid", "")
    nodes.append(("BeginPlay", begin_play_guid))
    prev_guid = begin_play_guid
    prev_pin = "then"
    x += 300
    
    for i, seq_name in enumerate(sequences):
        # Delay node
        delay = add_node(bp_name, "CallFunction", function_name="Delay", pos_x=x, pos_y=y)
        delay_guid = delay.get("nodeGuid", "")
        if prev_guid and delay_guid:
            connect_pins(bp_name, prev_guid, prev_pin, delay_guid, "execute")
        if delay_guid and i < len(delays):
            set_pin_default(bp_name, delay_guid, "Duration", str(delays[i]))
        nodes.append(("Delay_{}".format(i), delay_guid))
        x += 300
        
        # Play Sequence node
        play = add_node(bp_name, "CallFunction", 
                       function_name="PlayLevelSequence",
                       pos_x=x, pos_y=y)
        play_guid = play.get("nodeGuid", "")
        if delay_guid and play_guid:
            connect_pins(bp_name, delay_guid, "Completed", play_guid, "execute")
        # Set the sequence asset reference
        if play_guid:
            set_pin_default(bp_name, play_guid, "LevelSequence", seq_name)
        nodes.append(("Play_{}".format(seq_name), play_guid))
        
        prev_guid = play_guid
        prev_pin = "then"
        x += 300
    
    return nodes


# ---------------------------------------------------------------------------
# Error reporting
# ---------------------------------------------------------------------------
def get_errors():
    """Return list of all errors encountered during this session."""
    return list(_error_log)


def clear_errors():
    """Clear the error log."""
    _error_log.clear()


def print_summary():
    """Print a summary of the session."""
    if _error_log:
        print("\n[bp_helpers] === SESSION ERRORS ({}) ===".format(len(_error_log)))
        for err in _error_log:
            print("  " + err)
    else:
        print("\n[bp_helpers] === SESSION COMPLETE - NO ERRORS ===")
