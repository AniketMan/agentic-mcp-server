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
	FString HandleGetLevelBlueprint(const FString& Body);

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

	// ---- PIE Control handlers (Handlers_PIE.cpp) ----
	FString HandleStartPIE(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleStopPIE(const TMap<FString, FString>& Params, const FString& Body);
	FString HandlePausePIE(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleStepPIE(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleGetPIEState(const TMap<FString, FString>& Params, const FString& Body);

	// ---- Console Command handlers (Handlers_Console.cpp) ----
	FString HandleExecuteConsole(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleGetCVar(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleSetCVar(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleListCVars(const TMap<FString, FString>& Params, const FString& Body);

	// ---- MetaXR/OculusXR 5.6 handlers (Handlers_MetaXR.cpp) ----
	FString HandleXRGetHMDState(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRSetTracking(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRGetControllers(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRTriggerHaptic(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRStopHaptic(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRRecenter(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRGetGuardian(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRGetPassthrough(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRSetPassthrough(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRGetEyeTracking(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRSetEyeTracking(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRGetFaceTracking(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRSetFaceTracking(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRGetBodyTracking(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRSetBodyTracking(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleXRGetHandTracking(const TMap<FString, FString>& Params, const FString& Body);

	// ---- RenderDoc GPU Debugging handlers (Handlers_RenderDoc.cpp) ----
	FString HandleRenderDocGetStatus(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleRenderDocCaptureFrame(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleRenderDocCaptureMulti(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleRenderDocCapturePIE(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleRenderDocListCaptures(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleRenderDocOpenCapture(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleRenderDocDeleteCapture(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleRenderDocGetSettings(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleRenderDocSetSettings(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleRenderDocSetOverlay(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleRenderDocLaunchUI(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleRenderDocIsCapturing(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleRenderDocSetCapturePath(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleRenderDocGetGPUInfo(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleRenderDocTriggerCapture(const TMap<FString, FString>& Params, const FString& Body);
	FString HandleRenderDocCleanCaptures(const TMap<FString, FString>& Params, const FString& Body);

	// ============================================================
	// JSON Helper
	// ============================================================
	FString JsonObjectToString(TSharedPtr<FJsonObject> JsonObj);

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

	// ---- Snapshot storage ----
	TMap<FString, FAgenticGraphSnapshot> Snapshots;
	static const int32 MaxSnapshots = 50;
	FString GenerateSnapshotId(const FString& BlueprintName);
	FAgenticGraphSnapshotData CaptureGraphSnapshot(UEdGraph* Graph);
	void PruneOldSnapshots();
};
