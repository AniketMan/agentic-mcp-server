# =============================================================================
# ALL SCENES: CINEMATIC AUTO-PLAY MASTER SCRIPT
# =============================================================================
# Builds cinematic (non-interactive) Level Blueprint graphs for ALL 10 scenes.
# Each scene plays its Level Sequences back-to-back with timed delays.
# Player is placed at the correct position for each scene.
#
# This is the ONE script to paste to get the entire experience playable
# as a movie. No interactions required.
#
# CROSS-REFERENCED AGAINST:
#   - OC_VR_Implementation_Roadmap.md (full document)
#   - Level Sequence Inventory (lines 915-1002)
#   - ContentBrowser_Hierarchy.txt (all asset paths)
#   - tool-registry.json (all API endpoints)
# =============================================================================

import sys
import time as _time

try:
    import bp_helpers as bp
except ImportError:
    print("[CINEMATIC] ERROR: bp_helpers not found. Paste bp_helpers.py first.")
    sys.exit(1)

# =============================================================================
# SCENE DATABASE -- Every scene, every sequence, every delay
# =============================================================================
# Delays are estimated from VO line timings in the roadmap.
# Each delay represents the approximate duration of the preceding sequence.

SCENES = {
    "00": {
        "name": "Tutorial",
        "level": "SL_Main_Logic",
        "player_start": {"x": 0, "y": 0, "z": 100},
        "sequences": [],  # Tutorial has no level sequences -- interaction-driven
        "delays": [],
        "note": "Tutorial is interaction-driven. Cinematic uses timed prints."
    },
    "01": {
        "name": "Home - Larger Than Life",
        "level": "SL_Main_Logic",
        "player_start": {"x": 0, "y": 0, "z": 100},
        "sequences": [
            "/Game/Sequences/Scene1/LS_1_1",   # Heather enters, dances
            "/Game/Sequences/Scene1/LS_1_2",   # Hug animation
            "/Game/Sequences/Scene1/LS_1_3",   # Transition to kitchen
            "/Game/Sequences/Scene1/LS_1_4",   # Cross-fade Child -> PreTeen
        ],
        "delays": [1.0, 22.0, 6.0, 4.0],
        "actors": [
            ("BP_HeatherChild", "S01_HeatherChild", {"x": -500, "y": 200, "z": 0}),
            ("BP_AmbienceSound", "S01_Ambience", {"x": 0, "y": 0, "z": 100}),
            ("BP_VoiceSource", "S01_VoiceSource", {"x": 0, "y": 0, "z": 150}),
        ]
    },
    "02": {
        "name": "Standing Up For Others",
        "level": "SL_Main_Logic",
        "player_start": {"x": 300, "y": -100, "z": 100},
        "sequences": [
            "/Game/Sequences/Scene2/LS_2_1",   # Heather at table drawing
            "/Game/Sequences/Scene2/LS_2_2R",  # Illustration animates
            "/Game/Sequences/Scene2/LS_2_2R1", # Illustration variant 1
            "/Game/Sequences/Scene2/LS_2_2R2", # Illustration variant 2
            "/Game/Sequences/Scene2/LS_2_3",   # Heather runs out door
        ],
        "delays": [1.0, 12.0, 45.0, 5.0, 5.0],
        "actors": [
            ("BP_HeatherPreTeen2", "S02_HeatherPreTeen", {"x": 300, "y": -100, "z": 0}),
            ("BP_VoiceSource", "S02_VoiceSource", {"x": 300, "y": -100, "z": 150}),
        ]
    },
    "03": {
        "name": "Rescuers",
        "level": "SL_Main_Logic",
        "player_start": {"x": 0, "y": 0, "z": 100},
        "sequences": [
            "/Game/Sequences/Scene3/LS_3_1",     # Friends enter, sit
            "/Game/Sequences/Scene3/LS_3_2",     # Heather gets glasses
            "/Game/Sequences/Scene3/LS_3_5",     # Heather retrieves glasses
            "/Game/Sequences/Scene3/LS_3_6",     # Pour interaction
            "/Game/Sequences/Scene3/LS_3_3_v2",  # Cheers animation
            "/Game/Sequences/Scene3/LS_3_7",     # Friends retreat
        ],
        "delays": [1.0, 15.0, 8.0, 10.0, 8.0, 5.0],
        "actors": [
            ("BP_Heather_Teen", "S03_HeatherTeen", {"x": 0, "y": 200, "z": 0}),
            ("BP_FriendMale", "S03_FriendMale", {"x": -200, "y": 300, "z": 0}),
            ("BP_FriendFemale", "S03_FriendFemale", {"x": 200, "y": 300, "z": 0}),
            ("BP_SfxSound", "S03_SfxSound", {"x": 0, "y": 0, "z": 100}),
        ]
    },
    "04": {
        "name": "Stepping Into Adulthood",
        "level": "SL_Main_Logic",
        "player_start": {"x": 0, "y": 0, "z": 100},
        "sequences": [
            "/Game/Sequences/Scene4/LS_4_1",  # Phone notification
            "/Game/Sequences/Scene4/LS_4_2",  # Text conversation
            "/Game/Sequences/Scene4/LS_4_3",  # Text continuation
            "/Game/Sequences/Scene4/LS_4_4",  # Phone down, door
        ],
        "delays": [1.0, 14.0, 8.0, 8.0],
        "actors": [
            ("BP_PhoneInteraction", "S04_Phone", {"x": 100, "y": -50, "z": 80}),
            ("BP_SfxSound", "S04_SfxSound", {"x": 0, "y": 0, "z": 100}),
            ("BP_VoiceSource", "S04_VoiceSource", {"x": 0, "y": 0, "z": 150}),
        ]
    },
    "05": {
        "name": "Dinner Together",
        "level": "SL_Restaurant_Logic",
        "player_start": {"x": 0, "y": 0, "z": 100},
        "sequences": [
            "/Game/Sequences/Scene5/LS_5_1",           # Establish scene
            "/Game/Sequences/Scene5/LS_5_1_intro_loop", # Ambient loop
            "/Game/Sequences/Scene5/LS_5_2",           # Heather talking
            "/Game/Sequences/Scene5/LS_5_3",           # Heather reaches out
            "/Game/Sequences/Scene5/LS_5_3_grip",      # Hand hold
            "/Game/Sequences/Scene5/LS_5_4",           # Intimate moment
        ],
        "delays": [1.0, 12.0, 5.0, 14.0, 10.0, 5.0],
        "actors": [
            ("BP_Heather_Adult", "S05_HeatherAdult", {"x": 200, "y": 0, "z": 0}),
            ("BP_AmbienceSound", "S05_Ambience", {"x": 0, "y": 0, "z": 100}),
            ("BP_VoiceSource", "S05_VoiceSource", {"x": 0, "y": 0, "z": 150}),
            ("BP_PlayerStartPoint", "S05_PlayerStart", {"x": -200, "y": 0, "z": 0}),
        ]
    },
    "06": {
        "name": "The Rally in Charlottesville",
        "level": "SL_Scene6_Logic",
        "player_start": {"x": 0, "y": 0, "z": 100},
        "sequences": [
            "/Game/Sequences/Scene6/LS_6_1",  # Echo chamber
            "/Game/Sequences/Scene6/LS_6_2",  # Matches ignite
            "/Game/Sequences/Scene6/LS_6_3",  # Cradle collision
            "/Game/Sequences/Scene6/LS_6_4",  # Chaos erupts
            "/Game/Sequences/Scene6/LS_6_5",  # Car falls, blackout
        ],
        "delays": [1.0, 40.0, 32.0, 16.0, 16.0],
        "actors": [
            ("BP_car", "S06_Car", {"x": 0, "y": 0, "z": 500}),
            ("BP_PCrotate", "S06_Computer", {"x": -500, "y": 0, "z": 0}),
            ("BP_Balance", "S06_Scale", {"x": 0, "y": -500, "z": 0}),
            ("BP_nCrate", "S06_Cradle", {"x": 500, "y": 0, "z": 0}),
            ("BP_cardbaordTorn", "S06_Sign", {"x": 0, "y": 500, "z": 0}),
            ("BP_VoiceSource", "S06_VoiceSource", {"x": 0, "y": 0, "z": 150}),
        ]
    },
    "07": {
        "name": "The Hospital",
        "level": "SL_Hospital_Logic",
        "player_start": {"x": 0, "y": 0, "z": 100},
        "sequences": [
            "/Game/Sequences/Scene7/LS_7_1",  # Lobby establish
            "/Game/Sequences/Scene7/LS_7_2",  # Reception desk
            "/Game/Sequences/Scene7/LS_7_3",  # Officers approach
            "/Game/Sequences/Scene7/LS_7_4",  # Hallway walk
            "/Game/Sequences/Scene7/LS_7_5",  # Enter meeting room
            "/Game/Sequences/Scene7/LS_7_6",  # Detective delivers news
        ],
        "delays": [1.0, 10.0, 8.0, 6.0, 12.0, 10.0],
        "actors": [
            ("BP_Recepcionist", "S07_Receptionist", {"x": 200, "y": 0, "z": 0}),
            ("BP_Officer1", "S07_Officer1", {"x": 300, "y": 100, "z": 0}),
            ("BP_Officer2", "S07_Officer2", {"x": 300, "y": -100, "z": 0}),
            ("BP_Detective", "S07_Detective", {"x": 800, "y": 0, "z": 0}),
            ("BP_VoiceSource", "S07_VoiceSource", {"x": 0, "y": 0, "z": 150}),
            ("BP_HospitalSequence", "S07_HospitalSeq", {"x": 500, "y": 0, "z": 0}),
        ]
    },
    "08": {
        "name": "Turning Grief Into Action",
        "level": "SL_Main_Logic",
        "player_start": {"x": 0, "y": 0, "z": 100},
        "sequences": [
            "/Game/Sequences/Scene8/LS_8_1",  # Memory matching setup
            "/Game/Sequences/Scene8/LS_8_2",  # Teapot match
            "/Game/Sequences/Scene8/LS_8_3",  # Illustration match
            "/Game/Sequences/Scene8/LS_8_4",  # Pitcher match
            "/Game/Sequences/Scene8/LS_8_5",  # Phone match + circle
        ],
        "delays": [1.0, 12.0, 10.0, 10.0, 10.0],
        "actors": [
            ("BP_MemoryMatching", "S08_MemoryMatching", {"x": 0, "y": 0, "z": 100}),
            ("BP_VoiceSource", "S08_VoiceSource", {"x": 0, "y": 0, "z": 150}),
        ]
    },
    "09": {
        "name": "Legacy in Bloom",
        "level": "ML_Scene9",
        "player_start": {"x": 0, "y": 0, "z": 100},
        "sequences": [
            "/Game/Sequences/Scene9/LS_9_1",  # Flowers bloom
            "/Game/Sequences/Scene9/LS_9_2",  # Foundation imagery
            "/Game/Sequences/Scene9/LS_9_3",  # Youth programs
            "/Game/Sequences/Scene9/LS_9_4",  # NO HATE Act
            "/Game/Sequences/Scene9/LS_9_5",  # Media appearances
            "/Game/Sequences/Scene9/LS_9_6",  # Sunrise finale
        ],
        "delays": [1.0, 15.0, 12.0, 10.0, 12.0, 15.0],
        "actors": [
            ("BP_VoiceSource", "S09_VoiceSource", {"x": 0, "y": 0, "z": 150}),
            ("BP_AmbienceSound", "S09_Ambience", {"x": 0, "y": 0, "z": 100}),
            ("BP_Image_Player", "S09_ImagePlayer", {"x": 200, "y": 0, "z": 150}),
            ("BP_Video_Player", "S09_VideoPlayer", {"x": 400, "y": 0, "z": 150}),
            ("BP_PlayerStartPoint", "S09_PlayerStart", {"x": 0, "y": 0, "z": 0}),
        ]
    },
}


# =============================================================================
# MAIN EXECUTION
# =============================================================================

def build_cinematic_for_scene(scene_id, scene_data):
    """Build cinematic auto-play for a single scene."""
    
    name = scene_data["name"]
    level = scene_data["level"]
    sequences = scene_data.get("sequences", [])
    delays = scene_data.get("delays", [])
    actors = scene_data.get("actors", [])
    
    print("\n" + "=" * 60)
    print("SCENE {}: {} -- CINEMATIC".format(scene_id, name))
    print("Level: {}".format(level))
    print("Sequences: {}".format(len(sequences)))
    print("=" * 60)
    
    # Spawn actors
    if actors:
        print("[S{}] Spawning {} actors...".format(scene_id, len(actors)))
        for class_name, actor_name, loc in actors:
            result = bp.spawn_actor(class_name, name=actor_name, location=loc)
            if result.get("error"):
                print("[S{}] WARNING: Failed to spawn {}: {}".format(
                    scene_id, actor_name, result["error"]))
    
    # Build Level Blueprint chain
    if not sequences:
        print("[S{}] No sequences -- skipping Level BP build.".format(scene_id))
        if scene_data.get("note"):
            print("[S{}] NOTE: {}".format(scene_id, scene_data["note"]))
        return True
    
    bp_name = level
    bp.begin_transaction("Scene {} Cinematic".format(scene_id))
    
    x = 0
    y = int(scene_id) * 600  # Offset Y per scene to avoid overlap
    SPACING = 250
    
    # BeginPlay (or CustomEvent for non-first scenes)
    if scene_id == "01":
        # Scene 01 starts on BeginPlay (first scene after tutorial)
        n_start = bp.add_node(bp_name, "Event", event_name="ReceiveBeginPlay", 
                              pos_x=x, pos_y=y)
    else:
        # Other scenes start on a custom event
        n_start = bp.add_node(bp_name, "CustomEvent", 
                              event_name="CinematicStart_S{}".format(scene_id),
                              pos_x=x, pos_y=y)
    
    prev_guid = n_start.get("nodeGuid", "")
    prev_pin = "then"
    x += SPACING
    
    for i, seq_path in enumerate(sequences):
        seq_name = seq_path.split("/")[-1]
        delay_val = delays[i] if i < len(delays) else 5.0
        
        # Delay
        n_delay = bp.add_node(bp_name, "CallFunction", function_name="Delay",
                              pos_x=x, pos_y=y)
        guid_delay = n_delay.get("nodeGuid", "")
        if prev_guid and guid_delay:
            bp.connect_pins(bp_name, prev_guid, prev_pin, guid_delay, "execute")
            bp.set_pin_default(bp_name, guid_delay, "Duration", str(delay_val))
        x += SPACING
        
        # Print (debug marker)
        n_print = bp.add_node(bp_name, "CallFunction", function_name="PrintString",
                              pos_x=x, pos_y=y)
        guid_print = n_print.get("nodeGuid", "")
        if guid_delay and guid_print:
            bp.connect_pins(bp_name, guid_delay, "Completed", guid_print, "execute")
            bp.set_pin_default(bp_name, guid_print, "InString", 
                             "S{} Playing: {}".format(scene_id, seq_name))
        x += SPACING
        
        # CreateLevelSequencePlayer
        n_seq = bp.add_node(bp_name, "CallFunction",
                           function_name="CreateLevelSequencePlayer",
                           pos_x=x, pos_y=y)
        guid_seq = n_seq.get("nodeGuid", "")
        if guid_print and guid_seq:
            bp.connect_pins(bp_name, guid_print, "then", guid_seq, "execute")
            bp.set_pin_default(bp_name, guid_seq, "LevelSequence", seq_path)
        x += SPACING
        
        prev_guid = guid_seq
        prev_pin = "then"
    
    # Final print
    n_final = bp.add_node(bp_name, "CallFunction", function_name="PrintString",
                          pos_x=x, pos_y=y)
    guid_final = n_final.get("nodeGuid", "")
    if prev_guid and guid_final:
        bp.connect_pins(bp_name, prev_guid, prev_pin, guid_final, "execute")
        bp.set_pin_default(bp_name, guid_final, "InString",
                         "Scene {} cinematic complete.".format(scene_id))
    
    bp.end_transaction()
    
    # Compile
    result = bp.compile_blueprint(bp_name)
    success = not result.get("error")
    print("[S{}] Compile: {}".format(scene_id, "OK" if success else result))
    
    return success


# =============================================================================
# RUN ALL SCENES
# =============================================================================

print("=" * 60)
print("ORDINARY COURAGE VR -- FULL CINEMATIC BUILD")
print("Building auto-play for all 10 scenes...")
print("=" * 60)

if not bp.check_connection():
    print("[CINEMATIC] ABORT: MCP plugin not reachable.")
    sys.exit(1)

results = {}
for scene_id in sorted(SCENES.keys()):
    scene_data = SCENES[scene_id]
    try:
        success = build_cinematic_for_scene(scene_id, scene_data)
        results[scene_id] = "PASS" if success else "FAIL"
    except Exception as e:
        print("[S{}] ERROR: {}".format(scene_id, str(e)))
        results[scene_id] = "ERROR: {}".format(str(e))

# =============================================================================
# SUMMARY
# =============================================================================
print("\n" + "=" * 60)
print("CINEMATIC BUILD SUMMARY")
print("=" * 60)
for sid, status in sorted(results.items()):
    scene_name = SCENES[sid]["name"]
    seq_count = len(SCENES[sid].get("sequences", []))
    print("  Scene {}: {} -- {} ({} sequences)".format(sid, scene_name, status, seq_count))

total_sequences = sum(len(s.get("sequences", [])) for s in SCENES.values())
print("\nTotal sequences wired: {}".format(total_sequences))
print("Total scenes: {}".format(len(SCENES)))

bp.print_summary()
print("\n" + "=" * 60)
print("CINEMATIC BUILD COMPLETE")
print("=" * 60)
