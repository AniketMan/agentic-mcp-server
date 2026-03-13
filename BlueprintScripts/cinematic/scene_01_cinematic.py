# =============================================================================
# SCENE 01: HOME - LARGER THAN LIFE -- CINEMATIC AUTO-PLAY
# =============================================================================
# Plays Scene 01 as a non-interactive movie.
# All 5 level sequences play back-to-back with appropriate delays.
#
# LEVEL SEQUENCES (from roadmap):
#   LS_1_1 -- Heather Child enters, dances, runs to Susan
#   LS_1_2 -- Hug animation plays
#   LS_1_3 -- Transition, Heather runs to kitchen
#   LS_1_4 -- Cross-fade Child -> PreTeen
#   LS_HugLoop -- Looping hug (skipped in cinematic, plays once)
#
# CROSS-REFERENCED AGAINST:
#   - OC_VR_Implementation_Roadmap.md: Scene 01 (lines 69-104)
#   - Level Sequence Inventory (lines 917-924)
#   - ContentBrowser_Hierarchy.txt: /Game/Sequences/Scene1/
# =============================================================================

import sys

try:
    import bp_helpers as bp
except ImportError:
    print("[SCENE 01 CINEMATIC] ERROR: bp_helpers not found.")
    sys.exit(1)

SCENE_ID = "01"
SCENE_NAME = "Home - Larger Than Life"
LEVEL_NAME = "SL_Main_Logic"

# Verified sequence paths
SEQUENCES = [
    "/Game/Sequences/Scene1/LS_1_1",
    "/Game/Sequences/Scene1/LS_1_2",
    "/Game/Sequences/Scene1/LS_1_3",
    "/Game/Sequences/Scene1/LS_1_4",
]
# Delays between sequences (seconds) -- based on VO timing from roadmap
# LS_1_1: ~20s of VO, LS_1_2: ~5s, LS_1_3: ~3s, LS_1_4: ~2s transition
DELAYS = [1.0, 20.0, 5.0, 3.0]

print("=" * 60)
print("SCENE {}: {} -- CINEMATIC AUTO-PLAY".format(SCENE_ID, SCENE_NAME))
print("=" * 60)

if not bp.check_connection():
    print("[SCENE 01] ABORT: MCP plugin not reachable.")
    sys.exit(1)

# Step 1: Spawn required actors
print("\n[SCENE 01] Step 1: Spawning actors...")

# BP_HeatherChild -- hallway entrance
bp.spawn_actor("BP_HeatherChild", name="S01_HeatherChild",
               location={"x": -500, "y": 200, "z": 0})

# BP_AmbienceSound -- fridge hum
bp.spawn_actor("BP_AmbienceSound", name="S01_Ambience",
               location={"x": 0, "y": 0, "z": 100})

# BP_VoiceSource -- Susan VO
bp.spawn_actor("BP_VoiceSource", name="S01_VoiceSource",
               location={"x": 0, "y": 0, "z": 150})

# BP_TeleportPoint -- near kitchen table (enabled after hug)
bp.spawn_actor("BP_TeleportPoint", name="S01_TeleportKitchen",
               location={"x": 300, "y": -100, "z": 0})

print("[SCENE 01] Actors spawned.")

# Step 2: Build cinematic chain in Level Blueprint
print("\n[SCENE 01] Step 2: Building cinematic Level Blueprint...")

bp_name = LEVEL_NAME
bp.begin_transaction("Scene 01 Cinematic Auto-Play")

x = 0
y = 0
SPACING = 300

# BeginPlay
n_begin = bp.add_node(bp_name, "Event", event_name="ReceiveBeginPlay", pos_x=x, pos_y=y)
guid_begin = n_begin.get("nodeGuid", "")
prev_guid = guid_begin
prev_pin = "then"
x += SPACING

# For each sequence: Delay -> CreateLevelSequencePlayer -> Play
for i, seq_path in enumerate(SEQUENCES):
    seq_name = seq_path.split("/")[-1]
    
    # Delay
    n_delay = bp.add_node(bp_name, "CallFunction", function_name="Delay", pos_x=x, pos_y=y)
    guid_delay = n_delay.get("nodeGuid", "")
    if prev_guid and guid_delay:
        bp.connect_pins(bp_name, prev_guid, prev_pin, guid_delay, "execute")
        bp.set_pin_default(bp_name, guid_delay, "Duration", str(DELAYS[i]))
    x += SPACING
    
    # Print sequence name (debug)
    n_print = bp.add_node(bp_name, "CallFunction", function_name="PrintString", pos_x=x, pos_y=y)
    guid_print = n_print.get("nodeGuid", "")
    if guid_delay and guid_print:
        bp.connect_pins(bp_name, guid_delay, "Completed", guid_print, "execute")
        bp.set_pin_default(bp_name, guid_print, "InString", "Playing: {}".format(seq_name))
    x += SPACING
    
    # Create Level Sequence Player
    n_create = bp.add_node(bp_name, "CallFunction", 
                           function_name="CreateLevelSequencePlayer",
                           pos_x=x, pos_y=y)
    guid_create = n_create.get("nodeGuid", "")
    if guid_print and guid_create:
        bp.connect_pins(bp_name, guid_print, "then", guid_create, "execute")
        # Set the sequence asset
        bp.set_pin_default(bp_name, guid_create, "LevelSequence", seq_path)
    x += SPACING
    
    prev_guid = guid_create
    prev_pin = "then"

# Final: Print "Scene 01 Complete"
n_final = bp.add_node(bp_name, "CallFunction", function_name="PrintString", pos_x=x, pos_y=y)
guid_final = n_final.get("nodeGuid", "")
if prev_guid and guid_final:
    bp.connect_pins(bp_name, prev_guid, prev_pin, guid_final, "execute")
    bp.set_pin_default(bp_name, guid_final, "InString", "Scene 01 cinematic complete. Transitioning to Scene 02...")

bp.end_transaction()

# Compile
print("\n[SCENE 01] Step 3: Compiling...")
compile_result = bp.compile_blueprint(bp_name)
print("[SCENE 01] Compile result: {}".format(compile_result))

bp.print_summary()
print("\n" + "=" * 60)
print("SCENE 01 CINEMATIC: COMPLETE")
print("Sequences: LS_1_1 -> LS_1_2 -> LS_1_3 -> LS_1_4")
print("=" * 60)
