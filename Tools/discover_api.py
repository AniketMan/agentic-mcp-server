"""
Discover available methods on BlueprintEditorLibrary and related classes.
"""
import unreal

# List all methods on BlueprintEditorLibrary
unreal.log("=== BlueprintEditorLibrary methods ===")
for method in dir(unreal.BlueprintEditorLibrary):
    if not method.startswith('_'):
        unreal.log(f"  {method}")

# List all methods on SubobjectDataSubsystem
unreal.log("")
unreal.log("=== SubobjectDataSubsystem methods ===")
subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
for method in dir(subsystem):
    if not method.startswith('_') and callable(getattr(subsystem, method, None)):
        unreal.log(f"  {method}")

# Check if there's an ActorEditorUtils or similar
unreal.log("")
unreal.log("=== Checking for component-related classes ===")
classes_to_check = ['ActorEditorUtils', 'EditorActorSubsystem', 'ComponentEditorUtils', 'FKismetEditorUtilities']
for cls_name in classes_to_check:
    try:
        cls = getattr(unreal, cls_name, None)
        if cls:
            unreal.log(f"{cls_name} exists!")
    except:
        pass
