# SL_Restaurant_Logic.umap Structure

## Key Exports for K2Node injection:
- Export[37] (38) EdGraph / EventGraph outer=55 -- THIS IS THE OUTER for all K2Nodes
- Export[54] (55) LevelScriptBlueprint / SL_Restaurant_Logic outer=54
- Export[53] (54) Level / PersistentLevel outer=105

## Existing K2Nodes (all outer=38 = EventGraph):
- [38] EdGraphNode_Comment / EdGraphNode_Comment_Scene5_HandPlaced
- [48] K2Node_CallFunction / K2Node_CallFunction_Broadcast_Scene5_HandPlaced (1425B)
- [49] K2Node_CallFunction / K2Node_DisableActor_Scene5_HandPlaced (723B)
- [50] K2Node_CustomEvent / K2Node_CustomEvent_Scene5_HandPlaced (424B)
- [51] K2Node_GetSubsystem / K2Node_GetSubsystem_Scene5_HandPlaced (224B)
- [52] K2Node_MakeStruct / K2Node_MakeStruct_Scene5_HandPlaced (446B)

## Strategy:
The paste text has 2 interaction chains:
1. Scene5_HandPlaced: CustomEvent -> PlayHaptic -> DisableActor -> GetSubsystem -> BroadcastMessage + MakeStruct
2. Scene5_TeleportToHeather: CustomEvent -> SpawnActor -> GetSubsystem -> BroadcastMessage + MakeStruct

Chain 1 already exists in the asset. Chain 2 needs to be injected.
But for the test, we'll create a FRESH .umap with ALL nodes injected from scratch.

Actually, better approach: use the existing asset as-is and verify we can READ the existing K2Nodes,
then ADD new nodes for Chain 2.
