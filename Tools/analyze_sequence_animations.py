"""
Analyze Level Sequences to find which animations they reference.
This will help map animations to their correct sequence names.
"""
import unreal

# All sequences to check
SEQUENCES = [
    # Scene 1
    "/Game/Sequences/Scene1/LS_1_1",
    "/Game/Sequences/Scene1/LS_1_2",
    "/Game/Sequences/Scene1/LS_1_3",
    "/Game/Sequences/Scene1/LS_1_4",
    "/Game/Sequences/Scene1/LS_HugLoop",
    # Scene 2
    "/Game/Sequences/Scene2/LS_2_1",
    "/Game/Sequences/Scene2/LS_2_2R",
    "/Game/Sequences/Scene2/LS_2_3",
    # Scene 3
    "/Game/Sequences/Scene3/LS_3_1",
    "/Game/Sequences/Scene3/LS_3_2",
    "/Game/Sequences/Scene3/LS_3_5",
    "/Game/Sequences/Scene3/LS_3_6",
    "/Game/Sequences/Scene3/LS_3_7",
    # Scene 4
    "/Game/Sequences/Scene4/LS_4_1",
    "/Game/Sequences/Scene4/LS_4_2",
    "/Game/Sequences/Scene4/LS_4_3",
    "/Game/Sequences/Scene4/LS_4_4",
    # Scene 5
    "/Game/Sequences/Scene5/LS_5_1",
    "/Game/Sequences/Scene5/LS_5_2",
    "/Game/Sequences/Scene5/LS_5_3",
    "/Game/Sequences/Scene5/LS_5_4",
]

def get_sequence_animations(seq_path):
    """Get all animation references in a level sequence."""
    seq = unreal.load_asset(seq_path)
    if not seq:
        return None

    animations = []
    movie_scene = seq.get_movie_scene()
    if not movie_scene:
        return animations

    # Get all bindings
    bindings = movie_scene.get_bindings()
    for binding in bindings:
        binding_name = binding.get_name()
        tracks = binding.get_tracks()

        for track in tracks:
            track_name = track.get_display_name()
            track_class = track.get_class().get_name()

            # Look for animation tracks
            if "Animation" in track_class or "Skeletal" in track_class:
                sections = track.get_sections()
                for section in sections:
                    section_class = section.get_class().get_name()
                    # Try to get the animation asset reference
                    if hasattr(section, 'get_editor_property'):
                        try:
                            params = section.get_editor_property('params')
                            if params and hasattr(params, 'animation'):
                                anim = params.animation
                                if anim:
                                    animations.append({
                                        "binding": binding_name,
                                        "animation": anim.get_name(),
                                        "path": anim.get_path_name()
                                    })
                        except:
                            pass

    return animations

def analyze_all_sequences():
    """Analyze all sequences and print animation mappings."""
    print("=" * 80)
    print("SEQUENCE -> ANIMATION MAPPING")
    print("=" * 80)

    for seq_path in SEQUENCES:
        seq_name = seq_path.split("/")[-1]
        animations = get_sequence_animations(seq_path)

        if animations is None:
            print(f"\n{seq_name}: [NOT FOUND]")
        elif len(animations) == 0:
            print(f"\n{seq_name}: [NO ANIMATIONS]")
        else:
            print(f"\n{seq_name}:")
            for anim in animations:
                print(f"  - {anim['binding']}: {anim['animation']}")

analyze_all_sequences()
