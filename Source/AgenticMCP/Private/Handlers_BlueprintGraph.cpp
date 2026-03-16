// Handlers_BlueprintGraph.cpp
// Blueprint Event Graph editing handlers for AgenticMCP.
// Allows creating/editing Blueprint function graphs, adding nodes, wiring pins.
// UE 5.6 target.
#include "AgenticMCPServer.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// --- bpCreateBlueprint ---
FString FAgenticMCPServer::HandleBPCreateBlueprint(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Path = Json->GetStringField(TEXT("path"));
	FString ParentClass = Json->GetStringField(TEXT("parentClass"));
	if (Path.IsEmpty())
		return MakeErrorJson(TEXT("Missing 'path'"));

	if (ParentClass.IsEmpty()) ParentClass = TEXT("Actor");

	UClass* Parent = FindObject<UClass>(ANY_PACKAGE, *ParentClass);
	if (!Parent) Parent = AActor::StaticClass();

	FString AssetName = FPaths::GetBaseFilename(Path);
	UPackage* Package = CreatePackage(*Path);
	if (!Package)
		return MakeErrorJson(TEXT("Failed to create package"));

	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(Parent, Package, FName(*AssetName), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!BP)
		return MakeErrorJson(TEXT("Failed to create Blueprint"));

	FAssetRegistryModule::AssetCreated(BP);
	Package->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("path"), Path);
	Result->SetStringField(TEXT("parentClass"), Parent->GetName());
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- bpAddVariable ---
FString FAgenticMCPServer::HandleBPAddVariable(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BPPath = Json->GetStringField(TEXT("blueprintPath"));
	FString VarName = Json->GetStringField(TEXT("variableName"));
	FString VarType = Json->GetStringField(TEXT("variableType"));

	if (BPPath.IsEmpty() || VarName.IsEmpty() || VarType.IsEmpty())
		return MakeErrorJson(TEXT("Missing 'blueprintPath', 'variableName', or 'variableType'"));

	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BPPath);
	if (!BP)
		return MakeErrorJson(FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));

	FEdGraphPinType PinType;
	if (VarType == TEXT("bool")) PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	else if (VarType == TEXT("int")) PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	else if (VarType == TEXT("float")) PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
	else if (VarType == TEXT("string")) PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	else if (VarType == TEXT("vector")) PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	else if (VarType == TEXT("rotator")) PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	else if (VarType == TEXT("transform")) PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	else if (VarType == TEXT("object")) PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	else PinType.PinCategory = UEdGraphSchema_K2::PC_Real;

	bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType);
	if (!bSuccess)
		return MakeErrorJson(TEXT("Failed to add variable"));

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("variable"), VarName);
	Result->SetStringField(TEXT("type"), VarType);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- bpAddFunction ---
FString FAgenticMCPServer::HandleBPAddFunction(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BPPath = Json->GetStringField(TEXT("blueprintPath"));
	FString FuncName = Json->GetStringField(TEXT("functionName"));

	if (BPPath.IsEmpty() || FuncName.IsEmpty())
		return MakeErrorJson(TEXT("Missing 'blueprintPath' or 'functionName'"));

	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BPPath);
	if (!BP)
		return MakeErrorJson(FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(BP, FName(*FuncName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	if (!NewGraph)
		return MakeErrorJson(TEXT("Failed to create function graph"));

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	FBlueprintEditorUtils::AddFunctionGraph(BP, NewGraph, false, static_cast<UFunction*>(nullptr));
#else
	FBlueprintEditorUtils::AddFunctionGraph(BP, NewGraph, false);
#endif
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("function"), FuncName);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- bpAddNode ---
FString FAgenticMCPServer::HandleBPAddNode(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BPPath = Json->GetStringField(TEXT("blueprintPath"));
	FString GraphName = Json->GetStringField(TEXT("graphName"));
	FString NodeType = Json->GetStringField(TEXT("nodeType"));

	if (BPPath.IsEmpty() || NodeType.IsEmpty())
		return MakeErrorJson(TEXT("Missing 'blueprintPath' or 'nodeType'"));

	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BPPath);
	if (!BP)
		return MakeErrorJson(FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));

	// Find graph
	UEdGraph* Graph = nullptr;
	if (GraphName.IsEmpty())
	{
		// Use the event graph
		for (UEdGraph* G : BP->UbergraphPages)
		{
			Graph = G;
			break;
		}
	}
	else
	{
		for (UEdGraph* G : BP->UbergraphPages)
		{
			if (G->GetName() == GraphName) { Graph = G; break; }
		}
		if (!Graph)
		{
			for (UEdGraph* G : BP->FunctionGraphs)
			{
				if (G->GetName() == GraphName) { Graph = G; break; }
			}
		}
	}
	if (!Graph)
		return MakeErrorJson(TEXT("Graph not found"));

	double PosX = 0, PosY = 0;
	Json->TryGetNumberField(TEXT("posX"), PosX);
	Json->TryGetNumberField(TEXT("posY"), PosY);

	UEdGraphNode* NewNode = nullptr;

	if (NodeType == TEXT("CallFunction"))
	{
		FString FunctionName = Json->GetStringField(TEXT("functionName"));
		UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(Graph);
		UFunction* Func = FindObject<UFunction>(ANY_PACKAGE, *FunctionName);
		if (Func)
			CallNode->SetFromFunction(Func);
		NewNode = CallNode;
	}
	else if (NodeType == TEXT("CustomEvent"))
	{
		FString EventName = Json->GetStringField(TEXT("eventName"));
		UK2Node_CustomEvent* EventNode = NewObject<UK2Node_CustomEvent>(Graph);
		EventNode->CustomFunctionName = FName(*EventName);
		NewNode = EventNode;
	}
	else if (NodeType == TEXT("Branch"))
	{
		NewNode = NewObject<UK2Node_IfThenElse>(Graph);
	}
	else if (NodeType == TEXT("VariableGet"))
	{
		FString VarName = Json->GetStringField(TEXT("variableName"));
		UK2Node_VariableGet* GetNode = NewObject<UK2Node_VariableGet>(Graph);
		GetNode->VariableReference.SetSelfMember(FName(*VarName));
		NewNode = GetNode;
	}
	else if (NodeType == TEXT("VariableSet"))
	{
		FString VarName = Json->GetStringField(TEXT("variableName"));
		UK2Node_VariableSet* SetNode = NewObject<UK2Node_VariableSet>(Graph);
		SetNode->VariableReference.SetSelfMember(FName(*VarName));
		NewNode = SetNode;
	}
	else
	{
		return MakeErrorJson(TEXT("Unknown nodeType. Use: CallFunction, CustomEvent, Branch, VariableGet, VariableSet"));
	}

	if (!NewNode)
		return MakeErrorJson(TEXT("Failed to create node"));

	NewNode->NodePosX = (int32)PosX;
	NewNode->NodePosY = (int32)PosY;
	Graph->AddNode(NewNode, true, false);
	NewNode->AllocateDefaultPins();

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("nodeType"), NodeType);
	Result->SetStringField(TEXT("nodeId"), NewNode->NodeGuid.ToString());
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- bpConnectPins ---
FString FAgenticMCPServer::HandleBPConnectPins(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BPPath = Json->GetStringField(TEXT("blueprintPath"));
	FString SourceNodeId = Json->GetStringField(TEXT("sourceNodeId"));
	FString SourcePinName = Json->GetStringField(TEXT("sourcePinName"));
	FString TargetNodeId = Json->GetStringField(TEXT("targetNodeId"));
	FString TargetPinName = Json->GetStringField(TEXT("targetPinName"));

	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BPPath);
	if (!BP)
		return MakeErrorJson(FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));

	FGuid SourceGuid, TargetGuid;
	FGuid::Parse(SourceNodeId, SourceGuid);
	FGuid::Parse(TargetNodeId, TargetGuid);

	UEdGraphNode* SourceNode = nullptr;
	UEdGraphNode* TargetNode = nullptr;

	// Search all graphs
	TArray<UEdGraph*> AllGraphs;
	AllGraphs.Append(BP->UbergraphPages);
	AllGraphs.Append(BP->FunctionGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node->NodeGuid == SourceGuid) SourceNode = Node;
			if (Node->NodeGuid == TargetGuid) TargetNode = Node;
		}
	}

	if (!SourceNode)
		return MakeErrorJson(TEXT("Source node not found"));
	if (!TargetNode)
		return MakeErrorJson(TEXT("Target node not found"));

	UEdGraphPin* SourcePin = SourceNode->FindPin(FName(*SourcePinName));
	UEdGraphPin* TargetPin = TargetNode->FindPin(FName(*TargetPinName));

	if (!SourcePin)
		return MakeErrorJson(FString::Printf(TEXT("Source pin not found: %s"), *SourcePinName));
	if (!TargetPin)
		return MakeErrorJson(FString::Printf(TEXT("Target pin not found: %s"), *TargetPinName));

	bool bConnected = SourcePin->GetSchema()->TryCreateConnection(SourcePin, TargetPin);
	if (!bConnected)
		return MakeErrorJson(TEXT("Failed to connect pins - incompatible types"));

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- bpCompile ---
FString FAgenticMCPServer::HandleBPCompile(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BPPath = Json->GetStringField(TEXT("blueprintPath"));
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BPPath);
	if (!BP)
		return MakeErrorJson(FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));

	FKismetEditorUtilities::CompileBlueprint(BP);

	bool bHasErrors = BP->Status == BS_Error;

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), bHasErrors ? TEXT("error") : TEXT("ok"));
	Result->SetBoolField(TEXT("hasErrors"), bHasErrors);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- bpGetGraph ---
FString FAgenticMCPServer::HandleBPGetGraph(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BPPath = Json->GetStringField(TEXT("blueprintPath"));
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BPPath);
	if (!BP)
		return MakeErrorJson(FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));

	FString GraphName;
	Json->TryGetStringField(TEXT("graphName"), GraphName);

	TArray<TSharedPtr<FJsonValue>> GraphsArr;
	TArray<UEdGraph*> AllGraphs;
	AllGraphs.Append(BP->UbergraphPages);
	AllGraphs.Append(BP->FunctionGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!GraphName.IsEmpty() && Graph->GetName() != GraphName) continue;

		TSharedRef<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		GraphObj->SetStringField(TEXT("name"), Graph->GetName());
		GraphObj->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());

		TArray<TSharedPtr<FJsonValue>> NodesArr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			TSharedRef<FJsonObject> NodeObj = MakeShared<FJsonObject>();
			NodeObj->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
			NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
			NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			NodeObj->SetNumberField(TEXT("posX"), Node->NodePosX);
			NodeObj->SetNumberField(TEXT("posY"), Node->NodePosY);

			TArray<TSharedPtr<FJsonValue>> PinsArr;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				TSharedRef<FJsonObject> PinObj = MakeShared<FJsonObject>();
				PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
				PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
				PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
				PinObj->SetBoolField(TEXT("connected"), Pin->LinkedTo.Num() > 0);
				PinsArr.Add(MakeShared<FJsonValueObject>(PinObj));
			}
			NodeObj->SetArrayField(TEXT("pins"), PinsArr);
			NodesArr.Add(MakeShared<FJsonValueObject>(NodeObj));
		}
		GraphObj->SetArrayField(TEXT("nodes"), NodesArr);
		GraphsArr.Add(MakeShared<FJsonValueObject>(GraphObj));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("graphs"), GraphsArr);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- bpDeleteNode ---
FString FAgenticMCPServer::HandleBPDeleteNode(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BPPath = Json->GetStringField(TEXT("blueprintPath"));
	FString NodeId = Json->GetStringField(TEXT("nodeId"));

	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BPPath);
	if (!BP)
		return MakeErrorJson(FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));

	FGuid NodeGuid;
	FGuid::Parse(NodeId, NodeGuid);

	TArray<UEdGraph*> AllGraphs;
	AllGraphs.Append(BP->UbergraphPages);
	AllGraphs.Append(BP->FunctionGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node->NodeGuid == NodeGuid)
			{
				Graph->RemoveNode(Node);
				FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("status"), TEXT("ok"));
				Result->SetStringField(TEXT("removedNode"), NodeId);
				FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
				FJsonSerializer::Serialize(Result, W); return Out;
			}
		}
	}

	return MakeErrorJson(FString::Printf(TEXT("Node not found: %s"), *NodeId));
}
