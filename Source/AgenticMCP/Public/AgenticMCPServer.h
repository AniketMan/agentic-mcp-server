// AgenticMCPServer.h
// Core HTTP server for AgenticMCP. Plain C++ class (not a UCLASS) that owns all
// HTTP serving logic. Both the editor subsystem and the standalone commandlet
// delegate to an instance of this class.
//
// Architecture:
//   HTTP requests arrive on the HTTP thread -> queued as FPendingRequest ->
//   dequeued and processed on the game thread via ProcessOneRequest().
//   This ensures thread-safe access to all UE5 engine APIs.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "AssetRegistry/AssetData.h"
#include "HttpResultCallback.h"
#include "EdGraph/EdGraphPin.h"

// Forward declarations
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UBlueprint;
class UWorld;
class AActor;

// ============================================================
// Snapshot data structures — for graph state backup/restore
// ============================================================

struct FAgenticPinConnectionRecord
{
	FString SourceNodeGuid;
	FString SourcePinName;
	FString TargetNodeGuid;
	FString TargetPinName;
};

struct FAgenticNodeRecord
{
	FString NodeGuid;
	FString NodeClass;
	FString NodeTitle;
	FString StructType; // For Break/Make struct nodes
};

struct FAgenticGraphSnapshotData
{
	TArray<FAgenticNodeRecord> Nodes;
	TArray<FAgenticPinConnectionRecord> Connections;
};

struct FAgenticGraphSnapshot
{
	FString SnapshotId;
	FString BlueprintName;
	FString BlueprintPath;
	FDateTime CreatedAt;
	TMap<FString, FAgenticGraphSnapshotData> Graphs; // graphName -> data
};

// ============================================================
// FAgenticMCPServer
// ============================================================

class FAgenticMCPServer
{
public:
	/**
	 * Scan asset registry, bind HTTP routes, start listener on the given port.
	 * @param InPort       TCP port to listen on (default 9847)
	 * @param bEditorMode  True when hosted inside the editor (disables /api/shutdown)
	 * @return True if the server started successfully
	 */
	bool Start(int32 InPort, bool bEditorMode = false);

	/** Stop the HTTP listener and clean up. */
	void Stop();

	/**
	 * Dequeue and handle ONE pending HTTP request on the calling (game) thread.
	 * Call this every tick from whichever host owns this server.
	 * @return True if a request was processed
	 */
	bool ProcessOneRequest();

	/** Whether the HTTP server is currently listening. */
	bool IsRunning() const { return bRunning; }

	/** Port the server is listening on. */
	int32 GetPort() const { return Port; }

	/** Number of indexed Blueprint assets. */
	int32 GetBlueprintCount() const { return AllBlueprintAssets.Num(); }

	/** Number of indexed Map assets. */
	int32 GetMapCount() const { return AllMapAssets.Num(); }

private:
	// ---- TMap-based request dispatch ----
	using FRequestHandler = TFunction<FString(const TMap<FString, FString>&, const FString&)>;
	TMap<FString, FRequestHandler> HandlerMap;
	void RegisterHandlers();

	// ---- Queued request model ----
	struct FPendingRequest
	{
		FString Endpoint;
		TMap<FString, FString> QueryParams;
		FString Body;
		FHttpResultCallback OnComplete;
	};

	TQueue<TSharedPtr<FPendingRequest>> RequestQueue;

	// ---- Cached asset lists ----
	TArray<FAssetData> AllBlueprintAssets;
	TArray<FAssetData> AllMapAssets;

	// ---- Server state ----
	int32 Port = 9847;
	bool bRunning = false;
	bool bIsEditor = false;

	// ============================================================
	// Handler declarations — organized by category
	// ============================================================

	// ---- Asset registry rescan ----
	FString HandleRescan();

	// ---- Blueprint read handlers (Handlers_Read.cpp) ----
	FString HandleList(const TMap<FString, FString>& Params);
	FString HandleGetBlueprint(const TMap<FString, FString>& Params);
	FString HandleGetGraph(const TMap<FString, FString>& Params);
	FString HandleSearch(const TMap<FString, FString>& Params);
	FString HandleFindReferences(const TMap<FString, FString>& Params);
	FString HandleListClasses(const FString& Body);
	FString HandleListFunctions(const FString& Body);
	FString HandleListProperties(const FString& Body);
	FString HandleGetPinInfo(const FString& Body);

	// ---- Blueprint mutation handlers (Handlers_Mutation.cpp) ----
	FString HandleAddNode(const FString& Body);
	FString HandleDeleteNode(const FString& Body);
	FString HandleConnectPins(const FString& Body);
	FString HandleDisconnectPin(const FString& Body);
	FString HandleSetPinDefault(const FString& Body);
	FString HandleMoveNode(const FString& Body);
	FString HandleRefreshAllNodes(const FString& Body);
	FString HandleCreateBlueprint(const FString& Body);
	FString HandleCreateGraph(const FString& Body);
	FString HandleDeleteGraph(const FString& Body);
	FString HandleAddVariable(const FString& Body);
	FString HandleRemoveVariable(const FString& Body);
	FString HandleCompileBlueprint(const FString& Body);
	FString HandleDuplicateNodes(const FString& Body);
	FString HandleSetNodeComment(const FString& Body);
	FString HandleAddComponent(const FString& Body);

	// ---- Actor management handlers (Handlers_Actors.cpp) ----
	FString HandleListActors(const FString& Body);
	FString HandleGetActor(const FString& Body);
	FString HandleSpawnActor(const FString& Body);
	FString HandleDeleteActor(const FString& Body);
	FString HandleSetActorProperty(const FString& Body);
	FString HandleSetActorTransform(const FString& Body);

	// ---- Level management handlers (Handlers_Level.cpp) ----
	FString HandleListLevels(const FString& Body);
	FString HandleLoadLevel(const FString& Body);
	FString HandleRemoveSublevel(const FString& Body);
	FString HandleGetLevelBlueprint(const FString& Body);
	FString HandleSetStreamingLevelVisibility(const FString& Body);
	FString HandleGetOutputLog(const FString& Body);

	// ---- Validation and safety handlers (Handlers_Validation.cpp) ----
	FString HandleValidateBlueprint(const FString& Body);
	FString HandleSnapshotGraph(const FString& Body);
	FString HandleRestoreGraph(const FString& Body);

	// ---- VisualAgent automation handlers (Handlers_VisualAgent.cpp) ----
	FString HandleSceneSnapshot(const FString& Body);
	FString HandleScreenshot(const FString& Body);
	FString HandleFocusActor(const FString& Body);
	FString HandleSelectActor(const FString& Body);
	FString HandleSetViewport(const FString& Body);
	FString HandleWaitReady(const FString& Body);
	FString HandleResolveRef(const FString& Body);
	FString HandleGetCamera(const FString& Body);
	FString HandleListViewports(const FString& Body);
	FString HandleGetSelection(const FString& Body);

	// ---- Level Sequences ----
	FString HandleListSequences(const FString& Body);
	FString HandleReadSequence(const FString& Body);
	FString HandleRemoveAudioTracks(const FString& Body);

	// ---- Debug Visualization ----
	FString HandleDrawDebug(const FString& Body);
	FString HandleClearDebug(const FString& Body);

	// ---- Blueprint Graph Snapshot ----
	FString HandleBlueprintSnapshot(const FString& Body);

	// ---- Undo/Redo Transactions ----
	FString HandleBeginTransaction(const FString& Body);
	FString HandleEndTransaction(const FString& Body);
	FString HandleUndo(const FString& Body);
	FString HandleRedo(const FString& Body);

	// ---- Diff/Compare Mode ----
	FString HandleSaveState(const FString& Body);
	FString HandleDiffState(const FString& Body);
	FString HandleRestoreState(const FString& Body);
	FString HandleListStates(const FString& Body);

	// ---- Python Execution ----
	FString HandleExecutePython(const FString& Body);

	// ---- Asset Editor ----
	FString HandleOpenAsset(const FString& Body);

	// ---- PIE Control (Handlers_PIE.cpp) ----
	FString HandleStartPIE(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleStopPIE(const TMap<FString, FString>& Params, const FString& Body);
	FString HandlePausePIE(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleStepPIE(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleGetPIEState(const TMap<FString, FString>& Params, const FString& Body);

	// ---- Console Commands (Handlers_Console.cpp) ----
	FString HandleExecuteConsole(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleGetCVar(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleSetCVar(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleListCVars(const TMap<FString, FString>& Params, const FString& Body);

	// ---- Input Simulation (Handlers_Input.cpp) ----
	FString HandleSimulateInput(const TMap<FString, FString>& Params, const FString& Body);

	// ---- Meta XR / OculusXR (Handlers_MetaXR.cpp) ----
	FString HandleXRStatus(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRGuardian(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRSetGuardianVisibility(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRHandTracking(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRControllers(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRPassthrough(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRSetPassthrough(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRSetDisplayFrequency(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRSetPerformanceLevels(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRRecenter(const TMap<FString, FString>& Params, const FString& Body);

	// ---- Story Handlers (Handlers_Story.cpp) ----
	FString HandleStoryState(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleStoryAdvance(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleStoryGoto(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleStoryPlay(const TMap<FString, FString>& Params, const FString& Body);

	// ---- DataTable Handlers (Handlers_DataTable.cpp) ----
	FString HandleDataTableRead(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleDataTableWrite(const TMap<FString, FString>& Params, const FString& Body);

	// ---- Animation Handlers (Handlers_Animation.cpp) ----
	FString HandleAnimationPlay(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleAnimationStop(const TMap<FString, FString>& Params, const FString& Body);

	// ---- Material Handlers (Handlers_Materials.cpp) ----
	FString HandleMaterialSetParam(const TMap<FString, FString>& Params, const FString& Body);

	// ---- Collision Handlers (Handlers_Collision.cpp) ----
	FString HandleCollisionTrace(const TMap<FString, FString>& Params, const FString& Body);

	// ---- Audio Handlers (Handlers_Audio.cpp) ----
	FString HandleAudioGetStatus(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleAudioListActiveSounds(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleAudioGetDeviceInfo(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleAudioListSoundClasses(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleAudioSetVolume(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleAudioGetStats(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleAudioPlaySound(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleAudioStopSound(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleAudioSetListener(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleAudioDebugVisualize(const TMap<FString, FString>& Params, const FString& Body);

	// ---- Niagara Handlers (Handlers_Niagara.cpp) ----
	FString HandleNiagaraGetStatus(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleNiagaraListSystems(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleNiagaraGetSystemInfo(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleNiagaraGetEmitters(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleNiagaraSetParameter(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleNiagaraGetParameters(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleNiagaraActivateSystem(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleNiagaraSetEmitterEnable(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleNiagaraResetSystem(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleNiagaraGetStats(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleNiagaraDebugHUD(const TMap<FString, FString>& Params, const FString& Body);

	// ---- PixelStreaming Handlers (Handlers_PixelStreaming.cpp) ----
	FString HandlePixelStreamingGetStatus(const TMap<FString, FString>& Params, const FString& Body);
	FString HandlePixelStreamingStart(const TMap<FString, FString>& Params, const FString& Body);
	FString HandlePixelStreamingStop(const TMap<FString, FString>& Params, const FString& Body);
	FString HandlePixelStreamingListStreamers(const TMap<FString, FString>& Params, const FString& Body);
	FString HandlePixelStreamingGetCodec(const TMap<FString, FString>& Params, const FString& Body);
	FString HandlePixelStreamingSetCodec(const TMap<FString, FString>& Params, const FString& Body);
	FString HandlePixelStreamingListPlayers(const TMap<FString, FString>& Params, const FString& Body);

	// ---- RenderDoc Handlers (Handlers_RenderDoc.cpp) ----
	FString HandleRenderDocCapture(const TMap<FString, FString>& Params, const FString& Body);

	// ---- PCG Handlers (Handlers_PCG.cpp) ----
	FString HandlePCGListGraphs(const FString& Body);
	FString HandlePCGGetGraphInfo(const FString& Body);
	FString HandlePCGExecuteGraph(const FString& Body);
	FString HandlePCGGetNodeSettings(const FString& Body);
	FString HandlePCGSetNodeSettings(const FString& Body);
	FString HandlePCGListComponents(const FString& Body);

	// ---- Animation Blueprint Handlers (Handlers_AnimBlueprint.cpp) ----
	FString HandleAnimBPList(const FString& Body);
	FString HandleAnimBPGetGraph(const FString& Body);
	FString HandleAnimBPGetStates(const FString& Body);
	FString HandleAnimBPGetTransitions(const FString& Body);
	FString HandleAnimBPGetSlotGroups(const FString& Body);
	FString HandleAnimBPGetMontages(const FString& Body);

	// ---- Animation Blueprint Mutation Handlers (Handlers_AnimBP_Mutation.cpp) ----
	FString HandleAnimBPAddState(const FString& Body);
	FString HandleAnimBPRemoveState(const FString& Body);
	FString HandleAnimBPAddTransition(const FString& Body);
	FString HandleAnimBPSetTransitionRule(const FString& Body);
	FString HandleAnimBPSetStateAnimation(const FString& Body);
	FString HandleAnimBPGetStateMachine(const FString& Body);
	FString HandleAnimBPAddBlendNode(const FString& Body);

	// ---- Scene Hierarchy Handlers (Handlers_SceneHierarchy.cpp) ----
	FString HandleSceneGetHierarchy(const FString& Body);
	FString HandleSceneSetActorFolder(const FString& Body);
	FString HandleSceneAttachActor(const FString& Body);
	FString HandleSceneDetachActor(const FString& Body);
	FString HandleSceneRenameActor(const FString& Body);

	// ---- Sequencer Editing Handlers (Handlers_SequencerEdit.cpp) ----
	FString HandleSequencerCreate(const FString& Body);
	FString HandleSequencerAddTrack(const FString& Body);
	FString HandleSequencerAddSection(const FString& Body);
	FString HandleSequencerSetKeyframe(const FString& Body);
	FString HandleSequencerDeleteSection(const FString& Body);
	FString HandleSequencerBindActor(const FString& Body);
	FString HandleSequencerAddCameraCut(const FString& Body);
	FString HandleSequencerGetTracks(const FString& Body);
	FString HandleSequencerSetPlayRange(const FString& Body);
	FString HandleSequencerRender(const FString& Body);
	FString HandleSequencerDeleteTrack(const FString& Body);
	FString HandleSequencerGetKeyframes(const FString& Body);
	FString HandleSequencerAddSpawnable(const FString& Body);
	FString HandleSequencerMoveSection(const FString& Body);
	FString HandleSequencerDuplicateSection(const FString& Body);
	FString HandleSequencerSetTrackMute(const FString& Body);
	FString HandleSequencerAddSubSequence(const FString& Body);
	FString HandleSequencerAddFade(const FString& Body);
	FString HandleSequencerSetAudioSection(const FString& Body);
	FString HandleSequencerSetEventPayload(const FString& Body);
	FString HandleSequencerRenderStatus(const FString& Body);

	// ---- Landscape / Foliage Handlers (Handlers_Landscape.cpp) ----
	FString HandleLandscapeList(const FString& Body);
	FString HandleLandscapeGetInfo(const FString& Body);
	FString HandleLandscapeGetLayers(const FString& Body);
	FString HandleFoliageList(const FString& Body);
	FString HandleFoliageGetStats(const FString& Body);

	// ---- Physics Handlers (Handlers_Physics.cpp) ----
	FString HandlePhysicsGetBodyInfo(const FString& Body);
	FString HandlePhysicsSetSimulate(const FString& Body);
	FString HandlePhysicsApplyForce(const FString& Body);
	FString HandlePhysicsListConstraints(const FString& Body);
	FString HandlePhysicsGetOverlaps(const FString& Body);

	// ---- AI / Behavior Tree Handlers (Handlers_AI.cpp) ----
	FString HandleAIListBehaviorTrees(const FString& Body);
	FString HandleAIGetBehaviorTree(const FString& Body);
	FString HandleAIListBlackboards(const FString& Body);
	FString HandleAIGetBlackboard(const FString& Body);
	FString HandleAIListControllers(const FString& Body);
	FString HandleAIGetEQSQueries(const FString& Body);

	// ---- AI / Behavior Tree Mutation Handlers (Handlers_AI_Mutation.cpp) ----
	FString HandleBTAddTask(const FString& Body);
	FString HandleBTAddComposite(const FString& Body);
	FString HandleBTRemoveNode(const FString& Body);
	FString HandleBTAddDecorator(const FString& Body);
	FString HandleBTAddService(const FString& Body);
	FString HandleBTSetBlackboardValue(const FString& Body);
	FString HandleBTWireNodes(const FString& Body);
	FString HandleBTGetTree(const FString& Body);

	// ---- Material Editing Handlers (Handlers_MaterialEdit.cpp) ----
	FString HandleMaterialList(const FString& Body);
	FString HandleMaterialGetInfo(const FString& Body);
	FString HandleMaterialCreate(const FString& Body);
	FString HandleMaterialListInstances(const FString& Body);

	// ---- Material Graph Mutation Handlers (Handlers_MaterialGraphEdit.cpp) ----
	FString HandleMaterialAddNode(const FString& Body);
	FString HandleMaterialDeleteNode(const FString& Body);
	FString HandleMaterialConnectPins(const FString& Body);
	FString HandleMaterialDisconnectPin(const FString& Body);
	FString HandleMaterialSetTextureParam(const FString& Body);
	FString HandleMaterialCreateInstance(const FString& Body);
	FString HandleMaterialAssignToActor(const FString& Body);

	// ---- Asset Import / Management Handlers (Handlers_AssetImport.cpp) ----
	FString HandleAssetImport(const FString& Body);
	FString HandleAssetGetInfo(const FString& Body);
	FString HandleAssetDuplicate(const FString& Body);
	FString HandleAssetRename(const FString& Body);
	FString HandleAssetDelete(const FString& Body);
	FString HandleAssetListByType(const FString& Body);

	// ---- Editor Settings Handlers (Handlers_EditorSettings.cpp) ----
	FString HandleSettingsGetProject(const FString& Body);
	FString HandleSettingsGetEditor(const FString& Body);
	FString HandleSettingsGetRendering(const FString& Body);
	FString HandleSettingsGetPlugins(const FString& Body);

	// ---- UMG / Widget Handlers (Handlers_UMG.cpp) ----
	FString HandleUMGListWidgets(const FString& Body);
	FString HandleUMGGetWidgetTree(const FString& Body);
	FString HandleUMGGetWidgetProperties(const FString& Body);
	FString HandleUMGListHUDs(const FString& Body);

	// ---- UMG Widget Mutation Handlers (Handlers_UMG_Mutation.cpp) ----
	FString HandleUMGCreateWidget(const FString& Body);
	FString HandleUMGAddChild(const FString& Body);
	FString HandleUMGRemoveChild(const FString& Body);
	FString HandleUMGSetWidgetProperty(const FString& Body);
	FString HandleUMGBindEvent(const FString& Body);
	FString HandleUMGGetWidgetChildren(const FString& Body);

	// ---- Skeletal Mesh Handlers (Handlers_SkeletalMesh.cpp) ----
	FString HandleSkelMeshList(const FString& Body);
	FString HandleSkelMeshGetInfo(const FString& Body);
	FString HandleSkelMeshGetBones(const FString& Body);
	FString HandleSkelMeshGetMorphTargets(const FString& Body);
	FString HandleSkelMeshGetSockets(const FString& Body);

	// ---- Build / Packaging Handlers (Handlers_Packaging.cpp) ----
	FString HandleBuildGetStatus(const FString& Body);
	FString HandleBuildLighting(const FString& Body);
	FString HandleSourceControlGetStatus(const FString& Body);
	FString HandleSourceControlCheckout(const FString& Body);

	// ---- Component Manipulation Handlers (Handlers_ComponentEdit.cpp) ----
	FString HandleComponentList(const FString& Body);
	FString HandleComponentRemove(const FString& Body);
	FString HandleComponentSetProperty(const FString& Body);
	FString HandleComponentSetTransform(const FString& Body);
	FString HandleComponentSetVisibility(const FString& Body);
	FString HandleComponentSetCollision(const FString& Body);

	// ---- Python Execution with Output Capture (Handlers_PythonFix.cpp) ----
	FString HandleExecutePythonCapture(const FString& Body);

	// ============================================================
	// Serialization helpers
	// ============================================================

	TSharedRef<FJsonObject> SerializeBlueprint(UBlueprint* BP);
	TSharedPtr<FJsonObject> SerializeGraph(UEdGraph* Graph);
	TSharedPtr<FJsonObject> SerializeNode(UEdGraphNode* Node);
	TSharedPtr<FJsonObject> SerializePin(UEdGraphPin* Pin);
	FString JsonToString(TSharedRef<FJsonObject> JsonObj);

	// ============================================================
	// Common helpers
	// ============================================================

	/** Find a Blueprint asset by name or package path. */
	FAssetData* FindBlueprintAsset(const FString& NameOrPath);

	/** Find a Map asset by name or package path. */
	FAssetData* FindMapAsset(const FString& NameOrPath);

	/**
	 * Load a Blueprint by name. Handles both regular Blueprints and level Blueprints (from .umap).
	 * @param NameOrPath  Asset name or package path
	 * @param OutError    Error message if loading fails
	 * @return The loaded Blueprint, or nullptr on failure
	 */
	UBlueprint* LoadBlueprintByName(const FString& NameOrPath, FString& OutError);

	/**
	 * Find a node in a Blueprint by its GUID string.
	 * @param BP          The Blueprint to search
	 * @param GuidString  The node GUID as a string
	 * @param OutGraph    Optional output: the graph containing the node
	 * @return The node, or nullptr if not found
	 */
	UEdGraphNode* FindNodeByGuid(UBlueprint* BP, const FString& GuidString, UEdGraph** OutGraph = nullptr);

	/** Parse a JSON string body into a FJsonObject. Returns nullptr on parse failure. */
	TSharedPtr<FJsonObject> ParseBodyJson(const FString& Body);

	/** Create a JSON error response string. */
	FString MakeErrorJson(const FString& Message);

	/** Save a Blueprint's package to disk (compiles first). Returns true on success. */
	bool SaveBlueprintPackage(UBlueprint* BP);

	/** URL-decode a percent-encoded string. */
	static FString UrlDecode(const FString& EncodedString);

	/** Resolve a type name string (e.g. "float", "Vector", "MyStruct") into an FEdGraphPinType. */
	bool ResolveTypeFromString(const FString& TypeName, FEdGraphPinType& OutPinType, FString& OutError);

	/** Find a UClass by name, searching all loaded modules. */
	static UClass* FindClassByName(const FString& ClassName);

	// ---- Safe Blueprint modification (crash-proof) ----
	/**
	 * Safely mark a Blueprint as structurally modified with full crash protection.
	 * Wraps MarkBlueprintAsStructurallyModified in an editor transaction
	 * (provides valid FText scope) and SEH handler (catches any remaining crashes).
	 * Returns true if compilation succeeded, false if it crashed (node was still created).
	 */
	static bool SafeMarkStructurallyModified(UBlueprint* BP, const TCHAR* TransactionDesc = TEXT("MCP Mutation"));

	/**
	 * Safely mark a Blueprint as modified (non-structural, no recompile).
	 * Same crash protection as above but for lightweight modifications.
	 */
	static bool SafeMarkModified(UBlueprint* BP, const TCHAR* TransactionDesc = TEXT("MCP Mutation"));

	// ---- Snapshot storage ----
	TMap<FString, FAgenticGraphSnapshot> Snapshots;
	static const int32 MaxSnapshots = 50;
	FString GenerateSnapshotId(const FString& BlueprintName);
	FAgenticGraphSnapshotData CaptureGraphSnapshot(UEdGraph* Graph);
	void PruneOldSnapshots();

	// ---- AnimBlueprint (auto-generated declarations) ----
	FString HandleAnimBPGetBlendSpace(const FString& Body);
	FString HandleAnimBPListMontages(const FString& Body);

	// ---- Animation (auto-generated declarations) ----
	FString HandleAnimationPlay(const FString& Body);
	FString HandleAnimationStop(const FString& Body);

	// ---- Audio (auto-generated declarations) ----
	FString HandleAudioDebugVisualize(const FString& Body);
	FString HandleAudioGetDeviceInfo(const FString& Body);
	FString HandleAudioGetStats(const FString& Body);
	FString HandleAudioGetStatus(const FString& Body);
	FString HandleAudioListActiveSounds(const FString& Body);
	FString HandleAudioListSoundClasses(const FString& Body);
	FString HandleAudioPlaySound(const FString& Body);
	FString HandleAudioSetListener(const FString& Body);
	FString HandleAudioSetVolume(const FString& Body);
	FString HandleAudioStopSound(const FString& Body);

	// ---- Collision (auto-generated declarations) ----
	FString HandleCollisionTrace(const FString& Body);

	// ---- Console (auto-generated declarations) ----
	FString HandleExecuteConsole(const FString& Body);
	FString HandleGetCVar(const FString& Body);
	FString HandleListCVars(const FString& Body);
	FString HandleSetCVar(const FString& Body);

	// ---- DataTable (auto-generated declarations) ----
	FString HandleDataTableRead(const FString& Body);
	FString HandleDataTableWrite(const FString& Body);

	// ---- Input (auto-generated declarations) ----
	FString HandleSimulateInput(const FString& Body);

	// ---- MaterialGraphEdit (auto-generated declarations) ----
	FString HandleMaterialGetGraph(const FString& Body);
	FString HandleMaterialSetScalar(const FString& Body);
	FString HandleMaterialSetVector(const FString& Body);

	// ---- Materials (auto-generated declarations) ----
	FString HandleMaterialSetParam(const FString& Body);

	// ---- MetaXR (auto-generated declarations) ----
	FString HandleXRControllers(const FString& Body);
	FString HandleXRGuardian(const FString& Body);
	FString HandleXRHandTracking(const FString& Body);
	FString HandleXRPassthrough(const FString& Body);
	FString HandleXRRecenter(const FString& Body);
	FString HandleXRSetDisplayFrequency(const FString& Body);
	FString HandleXRSetGuardianVisibility(const FString& Body);
	FString HandleXRSetPassthrough(const FString& Body);
	FString HandleXRSetPerformanceLevels(const FString& Body);
	FString HandleXRStatus(const FString& Body);

	// ---- Niagara (auto-generated declarations) ----
	FString HandleNiagaraActivateSystem(const FString& Body);
	FString HandleNiagaraDebugHUD(const FString& Body);
	FString HandleNiagaraGetEmitters(const FString& Body);
	FString HandleNiagaraGetParameters(const FString& Body);
	FString HandleNiagaraGetStats(const FString& Body);
	FString HandleNiagaraGetStatus(const FString& Body);
	FString HandleNiagaraGetSystemInfo(const FString& Body);
	FString HandleNiagaraListSystems(const FString& Body);
	FString HandleNiagaraResetSystem(const FString& Body);
	FString HandleNiagaraSetEmitterEnable(const FString& Body);
	FString HandleNiagaraSetParameter(const FString& Body);

	// ---- PCG (auto-generated declarations) ----
	FString HandlePCGCleanup(const FString& Body);
	FString HandlePCGGenerate(const FString& Body);
	FString HandlePCGGetComponent(const FString& Body);
	FString HandlePCGSetSeed(const FString& Body);

	// ---- PIE (auto-generated declarations) ----
	FString HandleGetPIEState(const FString& Body);
	FString HandlePausePIE(const FString& Body);
	FString HandleStartPIE(const FString& Body);
	FString HandleStepPIE(const FString& Body);
	FString HandleStopPIE(const FString& Body);

	// ---- PixelStreaming (auto-generated declarations) ----
	FString HandlePixelStreamingGetCodec(const FString& Body);
	FString HandlePixelStreamingGetStatus(const FString& Body);
	FString HandlePixelStreamingListPlayers(const FString& Body);
	FString HandlePixelStreamingListStreamers(const FString& Body);
	FString HandlePixelStreamingSetCodec(const FString& Body);
	FString HandlePixelStreamingStart(const FString& Body);
	FString HandlePixelStreamingStop(const FString& Body);

	// ---- Read (auto-generated declarations) ----
	FString HandleFindReferences(const FString& Body);
	FString HandleGetBlueprint(const FString& Body);
	FString HandleGetGraph(const FString& Body);
	FString HandleList(const FString& Body);
	FString HandleSearch(const FString& Body);

	// ---- RenderDoc (auto-generated declarations) ----
	FString HandleRenderDocCapture(const FString& Body);

	// ---- Story (auto-generated declarations) ----
	FString HandleStoryAdvance(const FString& Body);
	FString HandleStoryGoto(const FString& Body);
	FString HandleStoryPlay(const FString& Body);
	FString HandleStoryState(const FString& Body);

};
