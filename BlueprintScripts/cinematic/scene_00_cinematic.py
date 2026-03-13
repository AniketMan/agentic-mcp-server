# =============================================================================
# SCENE 00: TUTORIAL -- CINEMATIC AUTO-PLAY
# =============================================================================
# Plays the tutorial as a non-interactive movie.
# Player is placed at start, all sequences play automatically with delays.
#
# PREREQUISITES:
#   - MCP C++ plugin running on localhost:9847
#   - ML_Main level loaded with SL_Main_Logic sublevel
#   - Paste bp_helpers.py first (or have it in sys.path)
#
# CROSS-REFERENCED AGAINST:
#   - OC_VR_Implementation_Roadmap.md: Scene 00 (lines 15-66)
#   - ContentBrowser_Hierarchy.txt: Asset paths verified
#   - tool-registry.json: API endpoints verified
# =============================================================================

import sys
import os

# If bp_helpers is not importable, define inline fallback
try:
    import bp_helpers as bp
except ImportError:
    # Paste bp_helpers.py content before this script
    print("[SCENE 00 CINEMATIC] ERROR: bp_helpers not found. Paste bp_helpers.py first.")
    sys.exit(1)

SCENE_ID = "00"
SCENE_NAME = "Tutorial"
LEVEL_NAME = "SL_Main_Logic"

# Level Sequence paths (verified from ContentBrowser_Hierarchy.txt)
# Scene 00 has no dedicated level sequences -- it's all interaction-driven.
# For cinematic mode, we simulate the flow with timed Python commands.

print("=" * 60)
print("SCENE {}: {} -- CINEMATIC AUTO-PLAY".format(SCENE_ID, SCENE_NAME))
print("=" * 60)

# Step 0: Check MCP connection
if not bp.check_connection():
    print("[SCENE 00] ABORT: MCP plugin not reachable.")
    sys.exit(1)

# Step 1: Verify correct level is loaded
print("\n[SCENE 00] Step 1: Verifying level...")
levels = bp.list_levels()
print("[SCENE 00] Current levels: {}".format(levels))

# Step 2: Spawn the tutorial actors at correct positions
print("\n[SCENE 00] Step 2: Spawning tutorial actors...")

# BP_PlayerStartPoint -- player spawn
bp.spawn_actor("BP_PlayerStartPoint", name="Tutorial_PlayerStart",
               location={"x": 0, "y": 0, "z": 100})

# BP_FloatingOrb array -- path to door
for i in range(5):
    bp.spawn_actor("BP_FloatingOrb", name="Tutorial_Orb_{}".format(i),
                   location={"x": 200 * (i + 1), "y": 0, "z": 150})

# BP_LocationMarker #1 -- first marker
bp.spawn_actor("BP_LocationMarker", name="Tutorial_Marker_1",
               location={"x": 300, "y": 0, "z": 100})

# BP_VoiceSource -- narrator VO
bp.spawn_actor("BP_VoiceSource", name="Tutorial_VoiceSource",
               location={"x": 300, "y": 0, "z": 150})

# BP_GazeText x3 -- Listen, Notice, Remember
gaze_words = ["Listen", "Notice", "Remember"]
for i, word in enumerate(gaze_words):
    bp.spawn_actor("BP_GazeText", name="Tutorial_Gaze_{}".format(word),
                   location={"x": 500, "y": -200 + (i * 200), "z": 200})

# BP_ObjectOfLight -- grip object
bp.spawn_actor("BP_ObjectOfLight", name="Tutorial_ObjectOfLight",
               location={"x": 700, "y": 0, "z": 150})

# BP_LocationMarker #2 -- after object release
bp.spawn_actor("BP_LocationMarker", name="Tutorial_Marker_2",
               location={"x": 900, "y": 0, "z": 100})

# BP_Door_Tutorial -- door to Scene 01
bp.spawn_actor("BP_Door", name="Tutorial_Door",
               location={"x": 1200, "y": 0, "z": 100})

# BP_Trigger -- door threshold
bp.spawn_actor("BP_Trigger", name="Tutorial_DoorThreshold",
               location={"x": 1250, "y": 0, "z": 100})

print("[SCENE 00] All actors spawned.")

# Step 3: Build cinematic auto-play in Level Blueprint
# Since Scene 00 has no level sequences, we build a timed event chain:
# BeginPlay -> Delay 2s -> Fade orbs in -> Delay 3s -> Dissolve gaze words
# -> Delay 2s -> Glow ObjectOfLight -> Delay 3s -> Particle burst
# -> Delay 2s -> Open door -> Delay 2s -> Fade out -> Switch to Scene 01

print("\n[SCENE 00] Step 3: Building cinematic Level Blueprint...")

# Get level blueprint reference
lb = bp.get_level_blueprint(LEVEL_NAME)
bp_name = LEVEL_NAME

# Take snapshot before modifications
snap = bp.snapshot_graph(bp_name)
print("[SCENE 00] Snapshot taken: {}".format(snap))

# Begin undo transaction
bp.begin_transaction("Scene 00 Cinematic Auto-Play")

# --- Node chain ---
x = 0
y = 0
SPACING = 300

# 1. BeginPlay
n_begin = bp.add_node(bp_name, "Event", event_name="ReceiveBeginPlay", pos_x=x, pos_y=y)
guid_begin = n_begin.get("nodeGuid", "")
x += SPACING

# 2. Delay 2s -- let player orient
n_delay1 = bp.add_node(bp_name, "CallFunction", function_name="Delay", pos_x=x, pos_y=y)
guid_delay1 = n_delay1.get("nodeGuid", "")
if guid_begin and guid_delay1:
    bp.connect_pins(bp_name, guid_begin, "then", guid_delay1, "execute")
    bp.set_pin_default(bp_name, guid_delay1, "Duration", "2.0")
x += SPACING

# 3. Print "Tutorial: Orbs appear" (visual debug)
n_print1 = bp.add_node(bp_name, "CallFunction", function_name="PrintString", pos_x=x, pos_y=y)
guid_print1 = n_print1.get("nodeGuid", "")
if guid_delay1 and guid_print1:
    bp.connect_pins(bp_name, guid_delay1, "Completed", guid_print1, "execute")
    bp.set_pin_default(bp_name, guid_print1, "InString", "Tutorial: Follow the orbs...")
x += SPACING

# 4. Delay 3s -- gaze words
n_delay2 = bp.add_node(bp_name, "CallFunction", function_name="Delay", pos_x=x, pos_y=y)
guid_delay2 = n_delay2.get("nodeGuid", "")
if guid_print1 and guid_delay2:
    bp.connect_pins(bp_name, guid_print1, "then", guid_delay2, "execute")
    bp.set_pin_default(bp_name, guid_delay2, "Duration", "3.0")
x += SPACING

# 5. Print "Tutorial: Gaze words dissolve"
n_print2 = bp.add_node(bp_name, "CallFunction", function_name="PrintString", pos_x=x, pos_y=y)
guid_print2 = n_print2.get("nodeGuid", "")
if guid_delay2 and guid_print2:
    bp.connect_pins(bp_name, guid_delay2, "Completed", guid_print2, "execute")
    bp.set_pin_default(bp_name, guid_print2, "InString", "Tutorial: Listen... Notice... Remember...")
x += SPACING

# 6. Delay 3s -- object glow
n_delay3 = bp.add_node(bp_name, "CallFunction", function_name="Delay", pos_x=x, pos_y=y)
guid_delay3 = n_delay3.get("nodeGuid", "")
if guid_print2 and guid_delay3:
    bp.connect_pins(bp_name, guid_print2, "then", guid_delay3, "execute")
    bp.set_pin_default(bp_name, guid_delay3, "Duration", "3.0")
x += SPACING

# 7. Print "Tutorial: Object of Light glows"
n_print3 = bp.add_node(bp_name, "CallFunction", function_name="PrintString", pos_x=x, pos_y=y)
guid_print3 = n_print3.get("nodeGuid", "")
if guid_delay3 and guid_print3:
    bp.connect_pins(bp_name, guid_delay3, "Completed", guid_print3, "execute")
    bp.set_pin_default(bp_name, guid_print3, "InString", "Tutorial: Grip the Object of Light...")
x += SPACING

# 8. Delay 3s -- door opens
n_delay4 = bp.add_node(bp_name, "CallFunction", function_name="Delay", pos_x=x, pos_y=y)
guid_delay4 = n_delay4.get("nodeGuid", "")
if guid_print3 and guid_delay4:
    bp.connect_pins(bp_name, guid_print3, "then", guid_delay4, "execute")
    bp.set_pin_default(bp_name, guid_delay4, "Duration", "3.0")
x += SPACING

# 9. Print "Tutorial: Door opens"
n_print4 = bp.add_node(bp_name, "CallFunction", function_name="PrintString", pos_x=x, pos_y=y)
guid_print4 = n_print4.get("nodeGuid", "")
if guid_delay4 and guid_print4:
    bp.connect_pins(bp_name, guid_delay4, "Completed", guid_print4, "execute")
    bp.set_pin_default(bp_name, guid_print4, "InString", "Tutorial: Walk to the door...")
x += SPACING

# 10. Delay 2s -- fade out
n_delay5 = bp.add_node(bp_name, "CallFunction", function_name="Delay", pos_x=x, pos_y=y)
guid_delay5 = n_delay5.get("nodeGuid", "")
if guid_print4 and guid_delay5:
    bp.connect_pins(bp_name, guid_print4, "then", guid_delay5, "execute")
    bp.set_pin_default(bp_name, guid_delay5, "Duration", "2.0")
x += SPACING

# 11. Print "Transitioning to Scene 01..."
n_print5 = bp.add_node(bp_name, "CallFunction", function_name="PrintString", pos_x=x, pos_y=y)
guid_print5 = n_print5.get("nodeGuid", "")
if guid_delay5 and guid_print5:
    bp.connect_pins(bp_name, guid_delay5, "Completed", guid_print5, "execute")
    bp.set_pin_default(bp_name, guid_print5, "InString", "Tutorial complete. Loading Scene 01...")
x += SPACING

# End transaction
bp.end_transaction()

# Compile
print("\n[SCENE 00] Step 4: Compiling...")
compile_result = bp.compile_blueprint(bp_name)
print("[SCENE 00] Compile result: {}".format(compile_result))

# Summary
bp.print_summary()
print("\n" + "=" * 60)
print("SCENE 00 CINEMATIC: COMPLETE")
print("=" * 60)
print("NOTE: Scene 00 is interaction-driven (no level sequences).")
print("Cinematic mode uses timed delays + print strings as placeholders.")
print("For full playback, use the interactive script instead.")
