"""
UE 5.6 Level Editor -- Generated Script
SOH Interaction Logic Injection -- All 4 Level Blueprints
=========================================================

Adds 22 interaction chains across 4 Level Script Blueprints:
  - SL_Trailer_Logic:     8 chains (Scenes 3-4, Steps 5-12)
  - SL_Restaurant_Logic:  3 chains (Scene 5, Steps 14-16)
  - SL_Scene6_Logic:      5 chains (Scene 6, Steps 17-21)
  - SL_Hospital_Logic:    6 chains (Scene 7, Steps 22-27)

Each chain consists of 4 nodes wired together:
  1. CustomEvent         -- Named trigger (e.g. "Scene3_DoorKnobGrabbed")
  2. GetSubsystem        -- Gets GameplayMessageSubsystem
  3. MakeStruct           -- Creates Msg_StoryStep with Step number
  4. BroadcastMessage    -- Sends the message on the StoryStep channel

Wiring:
  CustomEvent.then -> BroadcastMessage.execute
  GetSubsystem.ReturnValue -> BroadcastMessage.self (WorldSubsystem)
  MakeStruct.Msg_StoryStep -> BroadcastMessage.Message

Requires: JarvisEditor C++ plugin compiled into the project.
Verify with: print(hasattr(unreal, 'JarvisBlueprintLibrary'))

All operations are wrapped in undo transactions -- Ctrl+Z reverts everything.
"""
import unreal
import traceback


# =============================================================================
# Logging Helper
# =============================================================================

def log(msg, level="info"):
    """Log to UE output log and Python console."""
    prefix = "[JARVIS] "
    if level == "error":
        unreal.log_error(prefix + str(msg))
    elif level == "warning":
        unreal.log_warning(prefix + str(msg))
    else:
        unreal.log(prefix + str(msg))
    print(prefix + str(msg))


# =============================================================================
# Interaction Definitions
# =============================================================================
# Each tuple: (event_name, step_number)

TRAILER_INTERACTIONS = [
    ("Scene3_DoorKnobGrabbed",    5),
    ("Scene3_LightSwitchFlipped", 6),
    ("Scene3_WindowOpened",       7),
    ("Scene3_PhotoPickedUp",      8),
    ("Scene4_PhoneGrabbed",       9),
    ("Scene4_PhoneDialed",       10),
    ("Scene4_DoorHandleGrabbed", 11),
    ("Scene4_TVRemoteGrabbed",   12),
]

RESTAURANT_INTERACTIONS = [
    # Scene5_HandPlaced (Step 13) already exists in the level
    ("Scene5_TeleportToHeather",  14),
    ("Scene5_HeatherInteraction", 15),
    ("Scene5_End",                16),
]

SCENE6_INTERACTIONS = [
    ("Scene6_MirrorTouched",       17),
    ("Scene6_PhotoAlbumOpened",    18),
    ("Scene6_MusicBoxPlayed",      19),
    ("Scene6_WindowCurtainOpened", 20),
    ("Scene6_DoorOpened",          21),
]

HOSPITAL_INTERACTIONS = [
    ("Scene7_BedInteraction",     22),
    ("Scene7_MonitorChecked",     23),
    ("Scene7_DrawerOpened",       24),
    ("Scene7_PhotoFound",         25),
    ("Scene7_NurseCallPressed",   26),
    ("Scene7_FinalChoice",        27),
]

# Level asset paths -- match the Content directory structure
LEVEL_PATHS = {
    "Trailer":    "/Game/Maps/Game/Trailer/Levels/SLs/SL_Trailer_Logic",
    "Restaurant": "/Game/Maps/Game/Restaurant/Levels/SLs/SL_Restaurant_Logic",
    "Scene6":     "/Game/Maps/Game/Scene6/Levels/SLs/SL_Scene6_Logic",
    "Hospital":   "/Game/Maps/Game/Hospital/Levels/SLs/SL_Hospital_Logic",
}

# Graph name -- Level Script Blueprints use "EventGraph"
GRAPH = "EventGraph"

# Subsystem class name -- this is the UE class name for the subsystem
SUBSYSTEM_CLASS = "GameplayMessageSubsystem"

# Struct name for the story step message
STRUCT_NAME = "Msg_StoryStep"

# BroadcastMessage function reference
BROADCAST_FUNC = "GameplayMessageSubsystem.K2_BroadcastMessage"


# =============================================================================
# Core Injection Function
# =============================================================================

def add_interaction_chain(lib, bp, event_name, step_number, x_base, y_base):
    """
    Add a single interaction chain (4 nodes + wiring) to a Blueprint graph.

    Layout (horizontal, left to right):
        [CustomEvent]     @ (x_base, y_base)
        [GetSubsystem]    @ (x_base, y_base + 160)
        [MakeStruct]      @ (x_base + 400, y_base + 160)
        [BroadcastMessage] @ (x_base + 800, y_base)

    Wiring:
        CustomEvent.then -> BroadcastMessage.execute
        GetSubsystem.ReturnValue -> BroadcastMessage.self
        MakeStruct output -> BroadcastMessage.Message

    Args:
        lib:          unreal.JarvisBlueprintLibrary reference
        bp:           UBlueprint to modify
        event_name:   Name for the CustomEvent node
        step_number:  Integer step value for the Msg_StoryStep struct
        x_base:       X origin for this chain
        y_base:       Y origin for this chain

    Returns:
        True if all 4 nodes were created and wired successfully.
    """
    success = True
    warn_count = 0

    # --- Node 1: CustomEvent ---
    # Entry point -- called by gameplay code when the interaction fires.
    ce_node = lib.add_custom_event(bp, GRAPH, event_name, x_base, y_base)
    if not ce_node:
        log("  FAILED: Could not create CustomEvent: {}".format(event_name), "error")
        return False
    ce_guid = ce_node.node_guid.to_string() if hasattr(ce_node, 'node_guid') else str(ce_node.NodeGuid)
    log("  [1/4] CustomEvent '{}' -> GUID: {}".format(event_name, ce_guid))

    # --- Node 2: Get Gameplay Message Subsystem ---
    # Retrieves the world subsystem for message broadcasting.
    gs_node = lib.add_get_subsystem_node(
        bp, GRAPH, SUBSYSTEM_CLASS,
        x_base, y_base + 160
    )
    gs_guid = None
    if gs_node:
        gs_guid = gs_node.node_guid.to_string() if hasattr(gs_node, 'node_guid') else str(gs_node.NodeGuid)
        log("  [2/4] GetSubsystem '{}' -> GUID: {}".format(SUBSYSTEM_CLASS, gs_guid))
    else:
        log("  [2/4] WARNING: Could not create GetSubsystem -- wire manually", "warning")
        warn_count += 1

    # --- Node 3: Make Msg_StoryStep struct ---
    # Creates the message payload with the Step number.
    ms_node = lib.add_make_struct_node(
        bp, GRAPH, STRUCT_NAME,
        x_base + 400, y_base + 160
    )
    ms_guid = None
    if ms_node:
        ms_guid = ms_node.node_guid.to_string() if hasattr(ms_node, 'node_guid') else str(ms_node.NodeGuid)
        log("  [3/4] MakeStruct '{}' -> GUID: {}".format(STRUCT_NAME, ms_guid))

        # Set the Step default value on the MakeStruct node
        set_ok = lib.set_pin_default_value(bp, GRAPH, ms_guid, "Step", str(step_number))
        if set_ok:
            log("        Set Step = {}".format(step_number))
        else:
            log("        WARNING: Could not set Step={} -- set manually".format(step_number), "warning")
            warn_count += 1
    else:
        log("  [3/4] WARNING: Could not create MakeStruct -- wire manually", "warning")
        warn_count += 1

    # --- Node 4: Broadcast Message ---
    # Sends the Msg_StoryStep on the GameplayMessage channel.
    bm_node = lib.add_function_call(
        bp, GRAPH, BROADCAST_FUNC,
        x_base + 800, y_base
    )
    bm_guid = None
    if bm_node:
        bm_guid = bm_node.node_guid.to_string() if hasattr(bm_node, 'node_guid') else str(bm_node.NodeGuid)
        log("  [4/4] BroadcastMessage -> GUID: {}".format(bm_guid))
    else:
        log("  [4/4] FAILED: Could not create BroadcastMessage", "error")
        return False

    # --- Wiring ---
    # All connections use node GUIDs as required by ConnectPins.

    # Exec: CustomEvent.then -> BroadcastMessage.execute
    if ce_guid and bm_guid:
        ok = lib.connect_pins(bp, GRAPH, ce_guid, "then", bm_guid, "execute")
        if ok:
            log("        Wired: {}.then -> BroadcastMessage.execute".format(event_name))
        else:
            log("        WARNING: Could not wire exec flow", "warning")
            warn_count += 1

    # Data: GetSubsystem.ReturnValue -> BroadcastMessage.self
    if gs_guid and bm_guid:
        ok = lib.connect_pins(bp, GRAPH, gs_guid, "ReturnValue", bm_guid, "self")
        if not ok:
            # Try alternate pin names
            ok = lib.connect_pins(bp, GRAPH, gs_guid, "ReturnValue", bm_guid, "WorldContextObject")
            if not ok:
                log("        WARNING: Could not wire GetSubsystem -> BroadcastMessage", "warning")
                warn_count += 1
            else:
                log("        Wired: GetSubsystem -> BroadcastMessage.WorldContextObject")
        else:
            log("        Wired: GetSubsystem -> BroadcastMessage.self")

    # Data: MakeStruct output -> BroadcastMessage.Message
    if ms_guid and bm_guid:
        # The output pin name on MakeStruct is typically the struct name
        ok = lib.connect_pins(bp, GRAPH, ms_guid, STRUCT_NAME, bm_guid, "Message")
        if not ok:
            # Try alternate names
            ok = lib.connect_pins(bp, GRAPH, ms_guid, "Msg_StoryStep", bm_guid, "Message")
            if not ok:
                log("        WARNING: Could not wire MakeStruct -> BroadcastMessage.Message", "warning")
                warn_count += 1
            else:
                log("        Wired: MakeStruct -> BroadcastMessage.Message")
        else:
            log("        Wired: MakeStruct -> BroadcastMessage.Message")

    if warn_count > 0:
        log("  Chain '{}' completed with {} warnings".format(event_name, warn_count), "warning")
    else:
        log("  Chain '{}' completed successfully".format(event_name))

    return True


# =============================================================================
# Level Processing Function
# =============================================================================

def process_level(lib, level_name, level_path, interactions, y_start=3500):
    """
    Process a single level: load its Blueprint, add all interaction chains, compile.

    Args:
        lib:           unreal.JarvisBlueprintLibrary reference
        level_name:    Human-readable name (for logging)
        level_path:    UE asset path to the level
        interactions:  List of (event_name, step_number) tuples
        y_start:       Y offset to place new nodes below existing content

    Returns:
        (success_count, total_count) tuple
    """
    log("=" * 60)
    log("Processing: {} ({} interactions)".format(level_name, len(interactions)))
    log("  Level path: {}".format(level_path))

    # Get the Level Script Blueprint
    bp = lib.get_level_script_blueprint_by_path(level_path)
    if not bp:
        log("FAILED: Could not load Level Script Blueprint for {}".format(level_name), "error")
        log("  Make sure the level sublevel is loaded in the editor.", "error")
        log("  Trying current level fallback...", "warning")
        bp = lib.get_level_script_blueprint()
        if not bp:
            log("  Fallback also failed. Skipping {}.".format(level_name), "error")
            return (0, len(interactions))

    log("  Got Blueprint: {}".format(bp.get_name()))

    # Begin a single undo transaction for all chains in this level
    lib.begin_transaction("JARVIS: Add {} interaction chains to {}".format(
        len(interactions), level_name))

    success_count = 0
    for i, (event_name, step_number) in enumerate(interactions):
        # Layout: each chain gets 400px vertical space
        # X starts at 1600 (right of existing nodes to avoid overlap)
        x_base = 1600
        y_base = y_start + (i * 400)

        log("")
        log("  [{}/{}] {} (Step {}) at ({}, {})".format(
            i + 1, len(interactions), event_name, step_number, x_base, y_base))

        result = add_interaction_chain(lib, bp, event_name, step_number, x_base, y_base)
        if result:
            success_count += 1

    # End the undo transaction
    lib.end_transaction()

    # Compile the Blueprint
    log("")
    log("  Compiling {}...".format(level_name))
    compile_ok = lib.compile_blueprint(bp)
    if compile_ok:
        log("  Compilation SUCCEEDED")
    else:
        log("  Compilation had warnings/errors -- check Output Log (LogJarvis)", "warning")

    log("  Result: {}/{} chains added".format(success_count, len(interactions)))
    return (success_count, len(interactions))


# =============================================================================
# Main Entry Point
# =============================================================================

try:
    log("=" * 60)
    log("SOH Interaction Logic Injection")
    log("=" * 60)

    # --- Pre-flight check: verify JarvisEditor plugin ---
    if not hasattr(unreal, 'JarvisBlueprintLibrary'):
        log("FATAL: JarvisEditor plugin not found!", "error")
        log("", "error")
        log("The JarvisBlueprintLibrary class is not available in Python.", "error")
        log("", "error")
        log("Installation steps:", "error")
        log("  1. Copy ue_plugin/JarvisEditor/ to YourProject/Plugins/JarvisEditor/", "error")
        log("  2. Regenerate Visual Studio project files", "error")
        log("  3. Build the project (Development Editor)", "error")
        log("  4. Launch the editor and verify:", "error")
        log("     import unreal", "error")
        log("     print(hasattr(unreal, 'JarvisBlueprintLibrary'))", "error")
        log("  5. Output Log should show: LogJarvis: JarvisEditor module loaded", "error")
        raise RuntimeError("JarvisEditor plugin not loaded -- see instructions above")

    lib = unreal.JarvisBlueprintLibrary
    log("JarvisEditor plugin loaded successfully")

    # --- Pre-flight check: verify new functions exist ---
    required_funcs = [
        'add_custom_event', 'add_function_call', 'add_get_subsystem_node',
        'add_make_struct_node', 'set_pin_default_value', 'connect_pins',
        'compile_blueprint', 'begin_transaction', 'end_transaction',
        'get_level_script_blueprint_by_path'
    ]
    missing = [f for f in required_funcs if not hasattr(lib, f)]
    if missing:
        log("WARNING: Missing functions in JarvisBlueprintLibrary: {}".format(missing), "warning")
        log("You may need to rebuild the plugin with the latest source.", "warning")
        log("The script will attempt to continue but some operations may fail.", "warning")

    # Track overall results
    total_success = 0
    total_count = 0
    results = {}

    # --- Process each level ---

    # 1. Trailer (Scenes 3-4, Steps 5-12)
    s, t = process_level(
        lib, "Trailer", LEVEL_PATHS["Trailer"],
        TRAILER_INTERACTIONS, y_start=3500)
    total_success += s
    total_count += t
    results["Trailer"] = (s, t)

    # 2. Restaurant (Scene 5, Steps 14-16)
    s, t = process_level(
        lib, "Restaurant", LEVEL_PATHS["Restaurant"],
        RESTAURANT_INTERACTIONS, y_start=2000)
    total_success += s
    total_count += t
    results["Restaurant"] = (s, t)

    # 3. Scene 6 (Steps 17-21)
    s, t = process_level(
        lib, "Scene6", LEVEL_PATHS["Scene6"],
        SCENE6_INTERACTIONS, y_start=2000)
    total_success += s
    total_count += t
    results["Scene6"] = (s, t)

    # 4. Hospital (Scene 7, Steps 22-27)
    s, t = process_level(
        lib, "Hospital", LEVEL_PATHS["Hospital"],
        HOSPITAL_INTERACTIONS, y_start=500)
    total_success += s
    total_count += t
    results["Hospital"] = (s, t)

    # --- Summary ---
    log("")
    log("=" * 60)
    log("INJECTION COMPLETE")
    log("=" * 60)
    log("")
    for name, (s, t) in results.items():
        status = "OK" if s == t else "PARTIAL" if s > 0 else "FAILED"
        log("  {:15s}  {}/{} chains  [{}]".format(name, s, t, status))
    log("")
    log("  TOTAL: {}/{} chains".format(total_success, total_count))
    log("=" * 60)

    if total_success < total_count:
        log("")
        log("Some chains had issues. Check warnings above.", "warning")
        log("Nodes that failed to create can be added manually.", "warning")
        log("All successful operations are undoable via Ctrl+Z.", "info")

    # Save all modified assets
    log("")
    log("Saving all modified assets...")
    unreal.EditorAssetLibrary.save_loaded_assets()
    log("Done. All levels saved.")

except Exception as e:
    log("SCRIPT FAILED: {}".format(e), "error")
    log("Stack trace:", "error")
    traceback.print_exc()
    log("", "error")
    log("If this is a plugin issue, check:", "error")
    log("  1. JarvisEditor plugin is compiled and loaded", "error")
    log("  2. All 4 level sublevels are loaded in the editor", "error")
    log("  3. Output Log -> LogJarvis for detailed C++ diagnostics", "error")
