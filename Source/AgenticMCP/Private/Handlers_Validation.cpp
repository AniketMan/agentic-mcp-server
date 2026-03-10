// Handlers_Validation.cpp
// Validation and safety handlers for AgenticMCP.
// These handlers provide Blueprint validation, graph snapshots, and rollback.
//
// Endpoints implemented:
//   /api/validate-blueprint  - Compile and report errors/warnings
//   /api/snapshot-graph      - Take a snapshot of a graph for rollback
//   /api/restore-graph       - Restore a graph from a snapshot (limited)

#include "AgenticMCPServer.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ============================================================
// HandleValidateBlueprint
// POST /api/validate-blueprint { "blueprint": "..." }
// ============================================================

FString FAgenticMCPServer::HandleValidateBlueprint(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	if (BlueprintName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: blueprint"));

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

	// Compile the Blueprint
	EBlueprintCompileOptions CompileOpts = EBlueprintCompileOptions::None;
	FKismetEditorUtilities::CompileBlueprint(BP, CompileOpts, nullptr);

	// Collect validation results
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("blueprint"), BlueprintName);

	// Status
	FString StatusStr;
	switch (BP->Status)
	{
		case BS_Error:      StatusStr = TEXT("error"); break;
		case BS_UpToDate:   StatusStr = TEXT("up_to_date"); break;
		case BS_Dirty:      StatusStr = TEXT("dirty"); break;
		default:            StatusStr = TEXT("unknown"); break;
	}
	Result->SetStringField(TEXT("status"), StatusStr);

	// Check for unconnected pins and orphan nodes
	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);

	TArray<TSharedPtr<FJsonValue>> Warnings;
	int32 TotalNodes = 0;
	int32 TotalConnections = 0;

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			TotalNodes++;

			bool bHasAnyConnection = false;
			int32 UnconnectedInputs = 0;

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin) continue;
				if (Pin->LinkedTo.Num() > 0)
				{
					bHasAnyConnection = true;
					TotalConnections += Pin->LinkedTo.Num();
				}
				else if (Pin->Direction == EGPD_Input &&
						 Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					UnconnectedInputs++;
				}
			}

			// Warn about completely disconnected nodes (except events and comments)
			if (!bHasAnyConnection &&
				!Node->IsA<UK2Node_Event>() &&
				!Node->IsA<UK2Node_CustomEvent>() &&
				!Node->IsA<UEdGraphNode_Comment>())
			{
				TSharedRef<FJsonObject> Warning = MakeShared<FJsonObject>();
				Warning->SetStringField(TEXT("type"), TEXT("disconnected_node"));
				Warning->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
				Warning->SetStringField(TEXT("nodeTitle"),
					Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
				Warning->SetStringField(TEXT("graph"), Graph->GetName());
				Warnings.Add(MakeShared<FJsonValueObject>(Warning));
			}
		}
	}

	// Halve connections since we counted both directions
	TotalConnections /= 2;

	Result->SetNumberField(TEXT("totalNodes"), TotalNodes);
	Result->SetNumberField(TEXT("totalConnections"), TotalConnections);
	Result->SetNumberField(TEXT("graphCount"), AllGraphs.Num());
	Result->SetArrayField(TEXT("warnings"), Warnings);
	Result->SetBoolField(TEXT("isValid"), StatusStr != TEXT("error"));

	return JsonToString(Result);
}

// ============================================================
// HandleSnapshotGraph
// POST /api/snapshot-graph { "blueprint": "...", "description": "Before refactoring event graph" }
// ============================================================

FString FAgenticMCPServer::HandleSnapshotGraph(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	FString Description = Json->GetStringField(TEXT("description"));

	if (BlueprintName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: blueprint"));

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

	// Create snapshot
	FAgenticGraphSnapshot Snapshot;
	Snapshot.SnapshotId = GenerateSnapshotId(BlueprintName);
	Snapshot.BlueprintName = BlueprintName;
	Snapshot.BlueprintPath = BP->GetPathName();
	Snapshot.CreatedAt = FDateTime::Now();

	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph)
		{
			Snapshot.Graphs.Add(Graph->GetName(), CaptureGraphSnapshot(Graph));
		}
	}

	// Store snapshot
	Snapshots.Add(Snapshot.SnapshotId, Snapshot);
	PruneOldSnapshots();

	// Build response
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("snapshotId"), Snapshot.SnapshotId);
	Result->SetStringField(TEXT("blueprint"), BlueprintName);
	Result->SetStringField(TEXT("description"), Description);
	Result->SetNumberField(TEXT("graphCount"), Snapshot.Graphs.Num());

	int32 TotalNodes = 0;
	int32 TotalConnections = 0;
	for (const auto& Pair : Snapshot.Graphs)
	{
		TotalNodes += Pair.Value.Nodes.Num();
		TotalConnections += Pair.Value.Connections.Num();
	}
	Result->SetNumberField(TEXT("totalNodes"), TotalNodes);
	Result->SetNumberField(TEXT("totalConnections"), TotalConnections);
	Result->SetNumberField(TEXT("activeSnapshots"), Snapshots.Num());

	UE_LOG(LogTemp, Display, TEXT("AgenticMCP: Snapshot '%s' created for '%s' (%d nodes, %d connections)"),
		*Snapshot.SnapshotId, *BlueprintName, TotalNodes, TotalConnections);

	return JsonToString(Result);
}

// ============================================================
// HandleRestoreGraph
// POST /api/restore-graph { "snapshotId": "..." }
// ============================================================

FString FAgenticMCPServer::HandleRestoreGraph(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SnapshotId = Json->GetStringField(TEXT("snapshotId"));
	if (SnapshotId.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: snapshotId"));

	FAgenticGraphSnapshot* Snapshot = Snapshots.Find(SnapshotId);
	if (!Snapshot)
	{
		// List available snapshots
		TArray<TSharedPtr<FJsonValue>> Available;
		for (const auto& Pair : Snapshots)
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("snapshotId"), Pair.Key);
			Entry->SetStringField(TEXT("blueprint"), Pair.Value.BlueprintName);
			Entry->SetStringField(TEXT("createdAt"), Pair.Value.CreatedAt.ToString());
			Available.Add(MakeShared<FJsonValueObject>(Entry));
		}
		TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Snapshot '%s' not found"), *SnapshotId));
		E->SetArrayField(TEXT("availableSnapshots"), Available);
		return JsonToString(E);
	}

	// Load the Blueprint
	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(Snapshot->BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

	// Restore strategy: Clear all graphs and recreate nodes
	// NOTE: This is a destructive operation. Full restore with connection rewiring
	// requires matching node GUIDs, which may not be possible after graph changes.
	// For now, we clear the graph and report what was in the snapshot.

	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);

	int32 ClearedNodes = 0;
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;

		FAgenticGraphSnapshotData* SnapshotData = Snapshot->Graphs.Find(Graph->GetName());
		if (!SnapshotData) continue;

		// Clear existing nodes (except function entry nodes which are required)
		TArray<UEdGraphNode*> NodesToRemove;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && !Node->IsA<UK2Node_FunctionEntry>())
			{
				NodesToRemove.Add(Node);
			}
		}
		for (UEdGraphNode* Node : NodesToRemove)
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin) Pin->BreakAllPinLinks();
			}
			Graph->RemoveNode(Node);
			ClearedNodes++;
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("snapshotId"), SnapshotId);
	Result->SetStringField(TEXT("blueprint"), Snapshot->BlueprintName);
	Result->SetNumberField(TEXT("clearedNodes"), ClearedNodes);
	Result->SetStringField(TEXT("note"),
		TEXT("Graph cleared. Snapshot node data is available but automatic "
			 "node recreation requires using add-node and connect-pins for each node. "
			 "Use the snapshot data as a reference for manual reconstruction."));

	// Include snapshot data for reference
	TArray<TSharedPtr<FJsonValue>> SnapshotGraphs;
	for (const auto& Pair : Snapshot->Graphs)
	{
		TSharedRef<FJsonObject> GraphJson = MakeShared<FJsonObject>();
		GraphJson->SetStringField(TEXT("graphName"), Pair.Key);
		GraphJson->SetNumberField(TEXT("nodeCount"), Pair.Value.Nodes.Num());
		GraphJson->SetNumberField(TEXT("connectionCount"), Pair.Value.Connections.Num());

		TArray<TSharedPtr<FJsonValue>> NodeList;
		for (const FAgenticNodeRecord& NodeRec : Pair.Value.Nodes)
		{
			TSharedRef<FJsonObject> NodeJson = MakeShared<FJsonObject>();
			NodeJson->SetStringField(TEXT("nodeGuid"), NodeRec.NodeGuid);
			NodeJson->SetStringField(TEXT("nodeClass"), NodeRec.NodeClass);
			NodeJson->SetStringField(TEXT("nodeTitle"), NodeRec.NodeTitle);
			NodeList.Add(MakeShared<FJsonValueObject>(NodeJson));
		}
		GraphJson->SetArrayField(TEXT("nodes"), NodeList);

		SnapshotGraphs.Add(MakeShared<FJsonValueObject>(GraphJson));
	}
	Result->SetArrayField(TEXT("snapshotGraphs"), SnapshotGraphs);

	return JsonToString(Result);
}
