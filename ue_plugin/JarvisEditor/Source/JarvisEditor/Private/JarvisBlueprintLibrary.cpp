// Copyright Aniket Bhatt. All Rights Reserved.
// JarvisBlueprintLibrary.cpp - Implementation of all Python-callable graph manipulation functions
//
// LOGGING CONVENTION:
// Every public function logs:
//   [ENTRY]  - Function name and parameters
//   [EXIT]   - Return value or success/failure
//   [ERROR]  - Any error condition with diagnostic info
//   [WARN]   - Non-fatal issues (e.g., node already exists)
//
// Filter Output Log with "LogJarvis" to see only JARVIS operations.

#include "JarvisBlueprintLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_GetSubsystem.h"
#include "K2Node_MakeStruct.h"
#include "EdGraphSchema_K2.h"
#include "UObject/UObjectIterator.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/LevelScriptBlueprint.h"
#include "ScopedTransaction.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY(LogJarvis);

// Static member initialization
TSharedPtr<FScopedTransaction> UJarvisBlueprintLibrary::ActiveTransaction = nullptr;

// =============================================================================
// SECTION 1: GRAPH NODE OPERATIONS
// =============================================================================

UK2Node_CallFunction* UJarvisBlueprintLibrary::AddCallFunctionNode(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const FString& FunctionName,
	int32 NodePosX,
	int32 NodePosY)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] AddCallFunctionNode: BP=%s, Graph=%s, Func=%s, Pos=(%d,%d)"),
		Blueprint ? *Blueprint->GetName() : TEXT("null"),
		*GraphName, *FunctionName, NodePosX, NodePosY);

	if (!Blueprint)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddCallFunctionNode: Blueprint is null"));
		return nullptr;
	}

	UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddCallFunctionNode: Graph '%s' not found in BP '%s'"),
			*GraphName, *Blueprint->GetName());
		return nullptr;
	}

	// Parse the function name: "ClassName.FunctionName" or just "FunctionName"
	FString ClassName, FuncName;
	if (!FunctionName.Split(TEXT("."), &ClassName, &FuncName))
	{
		FuncName = FunctionName;
		ClassName = TEXT("");
	}

	// Find the UFunction
	UFunction* Function = nullptr;
	if (!ClassName.IsEmpty())
	{
		// Look up the class first
		UClass* TargetClass = FindObject<UClass>(ANY_PACKAGE, *ClassName);
		if (!TargetClass)
		{
			// Try with U prefix
			TargetClass = FindObject<UClass>(ANY_PACKAGE, *(TEXT("U") + ClassName));
		}
		if (TargetClass)
		{
			Function = TargetClass->FindFunctionByName(*FuncName);
		}
	}

	if (!Function)
	{
		// Search all classes for the function
		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			Function = ClassIt->FindFunctionByName(*FuncName);
			if (Function) break;
		}
	}

	if (!Function)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddCallFunctionNode: Function '%s' not found"),
			*FunctionName);
		return nullptr;
	}

	// Create the node within a transaction
	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("JARVIS: Add %s node"), *FunctionName)));

	Graph->Modify();

	// Spawn the K2Node_CallFunction
	UK2Node_CallFunction* NewNode = NewObject<UK2Node_CallFunction>(Graph);
	NewNode->SetFromFunction(Function);
	NewNode->NodePosX = NodePosX;
	NewNode->NodePosY = NodePosY;
	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	// Add to graph
	Graph->AddNode(NewNode, false, false);
	NewNode->SetFlags(RF_Transactional);

	// Notify the Blueprint
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] AddCallFunctionNode: Created node GUID=%s"),
		*NewNode->NodeGuid.ToString());

	return NewNode;
}

UK2Node_CustomEvent* UJarvisBlueprintLibrary::AddCustomEventNode(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const FString& EventName,
	int32 NodePosX,
	int32 NodePosY)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] AddCustomEventNode: BP=%s, Graph=%s, Event=%s"),
		Blueprint ? *Blueprint->GetName() : TEXT("null"),
		*GraphName, *EventName);

	if (!Blueprint)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddCustomEventNode: Blueprint is null"));
		return nullptr;
	}

	UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddCustomEventNode: Graph '%s' not found"), *GraphName);
		return nullptr;
	}

	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("JARVIS: Add Custom Event '%s'"), *EventName)));

	Graph->Modify();

	UK2Node_CustomEvent* NewNode = NewObject<UK2Node_CustomEvent>(Graph);
	NewNode->CustomFunctionName = *EventName;
	NewNode->NodePosX = NodePosX;
	NewNode->NodePosY = NodePosY;
	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	Graph->AddNode(NewNode, false, false);
	NewNode->SetFlags(RF_Transactional);

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] AddCustomEventNode: Created '%s' GUID=%s"),
		*EventName, *NewNode->NodeGuid.ToString());

	return NewNode;
}

UK2Node_IfThenElse* UJarvisBlueprintLibrary::AddBranchNode(
	UBlueprint* Blueprint,
	const FString& GraphName,
	int32 NodePosX,
	int32 NodePosY)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] AddBranchNode: BP=%s, Graph=%s"),
		Blueprint ? *Blueprint->GetName() : TEXT("null"), *GraphName);

	if (!Blueprint)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddBranchNode: Blueprint is null"));
		return nullptr;
	}

	UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddBranchNode: Graph '%s' not found"), *GraphName);
		return nullptr;
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("JARVIS: Add Branch node")));
	Graph->Modify();

	UK2Node_IfThenElse* NewNode = NewObject<UK2Node_IfThenElse>(Graph);
	NewNode->NodePosX = NodePosX;
	NewNode->NodePosY = NodePosY;
	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	Graph->AddNode(NewNode, false, false);
	NewNode->SetFlags(RF_Transactional);

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] AddBranchNode: Created GUID=%s"),
		*NewNode->NodeGuid.ToString());

	return NewNode;
}

UK2Node_VariableGet* UJarvisBlueprintLibrary::AddVariableGetNode(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const FString& VariableName,
	int32 NodePosX,
	int32 NodePosY)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] AddVariableGetNode: BP=%s, Var=%s"),
		Blueprint ? *Blueprint->GetName() : TEXT("null"), *VariableName);

	if (!Blueprint)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddVariableGetNode: Blueprint is null"));
		return nullptr;
	}

	UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddVariableGetNode: Graph '%s' not found"), *GraphName);
		return nullptr;
	}

	// Find the variable property
	FProperty* VarProp = FindFProperty<FProperty>(Blueprint->GeneratedClass, *VariableName);
	if (!VarProp)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddVariableGetNode: Variable '%s' not found in BP '%s'"),
			*VariableName, *Blueprint->GetName());
		return nullptr;
	}

	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("JARVIS: Add Get %s"), *VariableName)));
	Graph->Modify();

	UK2Node_VariableGet* NewNode = NewObject<UK2Node_VariableGet>(Graph);
	NewNode->VariableReference.SetSelfMember(*VariableName);
	NewNode->NodePosX = NodePosX;
	NewNode->NodePosY = NodePosY;
	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	Graph->AddNode(NewNode, false, false);
	NewNode->SetFlags(RF_Transactional);

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] AddVariableGetNode: Created Get '%s' GUID=%s"),
		*VariableName, *NewNode->NodeGuid.ToString());

	return NewNode;
}

UK2Node_VariableSet* UJarvisBlueprintLibrary::AddVariableSetNode(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const FString& VariableName,
	int32 NodePosX,
	int32 NodePosY)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] AddVariableSetNode: BP=%s, Var=%s"),
		Blueprint ? *Blueprint->GetName() : TEXT("null"), *VariableName);

	if (!Blueprint)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddVariableSetNode: Blueprint is null"));
		return nullptr;
	}

	UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddVariableSetNode: Graph '%s' not found"), *GraphName);
		return nullptr;
	}

	FProperty* VarProp = FindFProperty<FProperty>(Blueprint->GeneratedClass, *VariableName);
	if (!VarProp)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddVariableSetNode: Variable '%s' not found"), *VariableName);
		return nullptr;
	}

	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("JARVIS: Add Set %s"), *VariableName)));
	Graph->Modify();

	UK2Node_VariableSet* NewNode = NewObject<UK2Node_VariableSet>(Graph);
	NewNode->VariableReference.SetSelfMember(*VariableName);
	NewNode->NodePosX = NodePosX;
	NewNode->NodePosY = NodePosY;
	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	Graph->AddNode(NewNode, false, false);
	NewNode->SetFlags(RF_Transactional);

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] AddVariableSetNode: Created Set '%s' GUID=%s"),
		*VariableName, *NewNode->NodeGuid.ToString());

	return NewNode;
}

UK2Node_DynamicCast* UJarvisBlueprintLibrary::AddDynamicCastNode(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const FString& TargetClassName,
	int32 NodePosX,
	int32 NodePosY)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] AddDynamicCastNode: BP=%s, Target=%s"),
		Blueprint ? *Blueprint->GetName() : TEXT("null"), *TargetClassName);

	if (!Blueprint)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddDynamicCastNode: Blueprint is null"));
		return nullptr;
	}

	UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddDynamicCastNode: Graph '%s' not found"), *GraphName);
		return nullptr;
	}

	// Find the target class
	UClass* TargetClass = FindObject<UClass>(ANY_PACKAGE, *TargetClassName);
	if (!TargetClass)
	{
		// Try loading it as an asset path
		TargetClass = LoadObject<UClass>(nullptr, *TargetClassName);
	}
	if (!TargetClass)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddDynamicCastNode: Class '%s' not found"), *TargetClassName);
		return nullptr;
	}

	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("JARVIS: Add Cast To %s"), *TargetClassName)));
	Graph->Modify();

	UK2Node_DynamicCast* NewNode = NewObject<UK2Node_DynamicCast>(Graph);
	NewNode->TargetType = TargetClass;
	NewNode->NodePosX = NodePosX;
	NewNode->NodePosY = NodePosY;
	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	Graph->AddNode(NewNode, false, false);
	NewNode->SetFlags(RF_Transactional);

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] AddDynamicCastNode: Created Cast To '%s' GUID=%s"),
		*TargetClassName, *NewNode->NodeGuid.ToString());

	return NewNode;
}

bool UJarvisBlueprintLibrary::RemoveNodeByGuid(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const FString& NodeGuid)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] RemoveNodeByGuid: BP=%s, Graph=%s, GUID=%s"),
		Blueprint ? *Blueprint->GetName() : TEXT("null"), *GraphName, *NodeGuid);

	UEdGraphNode* Node = FindNodeByGuid(Blueprint, GraphName, NodeGuid);
	if (!Node)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] RemoveNodeByGuid: Node not found"));
		return false;
	}

	UEdGraph* Graph = Node->GetGraph();

	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("JARVIS: Remove node %s"), *NodeGuid)));

	Graph->Modify();

	// Break all pin connections first to avoid dangling references
	// THIS IS THE KEY SAFETY STEP - prevents the stale LinkedTo crash
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin)
		{
			Pin->BreakAllPinLinks();
		}
	}

	// Now safe to remove
	Graph->RemoveNode(Node);

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] RemoveNodeByGuid: Removed successfully"));
	return true;
}

int32 UJarvisBlueprintLibrary::RemoveAllNodes(
	UBlueprint* Blueprint,
	const FString& GraphName)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] RemoveAllNodes: BP=%s, Graph=%s"),
		Blueprint ? *Blueprint->GetName() : TEXT("null"), *GraphName);

	if (!Blueprint)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] RemoveAllNodes: Blueprint is null"));
		return 0;
	}

	UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] RemoveAllNodes: Graph '%s' not found"), *GraphName);
		return 0;
	}

	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("JARVIS: Clear all nodes in %s"), *GraphName)));

	Graph->Modify();

	// Collect nodes to remove (skip FunctionEntry and FunctionResult - those are structural)
	TArray<UEdGraphNode*> NodesToRemove;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && !Node->IsA<UK2Node_FunctionEntry>() && !Node->IsA<UK2Node_FunctionResult>())
		{
			NodesToRemove.Add(Node);
		}
	}

	// Break all connections first
	for (UEdGraphNode* Node : NodesToRemove)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin) Pin->BreakAllPinLinks();
		}
	}

	// Then remove
	for (UEdGraphNode* Node : NodesToRemove)
	{
		Graph->RemoveNode(Node);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] RemoveAllNodes: Removed %d nodes"), NodesToRemove.Num());
	return NodesToRemove.Num();
}

UEdGraphNode* UJarvisBlueprintLibrary::FindNodeByGuid(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const FString& NodeGuid)
{
	if (!Blueprint) return nullptr;

	UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
	if (!Graph) return nullptr;

	FGuid TargetGuid;
	if (!StringToGuid(NodeGuid, TargetGuid)) return nullptr;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->NodeGuid == TargetGuid)
		{
			return Node;
		}
	}

	return nullptr;
}

// =============================================================================
// SECTION 2: PIN OPERATIONS
// =============================================================================

bool UJarvisBlueprintLibrary::ConnectPins(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const FString& SourceNodeGuid,
	const FString& SourcePinName,
	const FString& TargetNodeGuid,
	const FString& TargetPinName)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] ConnectPins: %s.%s -> %s.%s"),
		*SourceNodeGuid, *SourcePinName, *TargetNodeGuid, *TargetPinName);

	if (!Blueprint)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] ConnectPins: Blueprint is null"));
		return false;
	}

	UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] ConnectPins: Graph '%s' not found"), *GraphName);
		return false;
	}

	// Find source node and pin
	UEdGraphNode* SourceNode = FindNodeByGuid(Blueprint, GraphName, SourceNodeGuid);
	if (!SourceNode)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] ConnectPins: Source node '%s' not found"), *SourceNodeGuid);
		return false;
	}

	UEdGraphPin* SourcePin = FindPinByName(SourceNode, SourcePinName, EGPD_Output);
	if (!SourcePin)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] ConnectPins: Source pin '%s' not found on node '%s'"),
			*SourcePinName, *SourceNodeGuid);
		// Log available pins for debugging
		for (UEdGraphPin* Pin : SourceNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output)
			{
				UE_LOG(LogJarvis, Log, TEXT("  Available output pin: '%s' (%s)"),
					*Pin->PinName.ToString(), *Pin->GetDisplayName().ToString());
			}
		}
		return false;
	}

	// Find target node and pin
	UEdGraphNode* TargetNode = FindNodeByGuid(Blueprint, GraphName, TargetNodeGuid);
	if (!TargetNode)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] ConnectPins: Target node '%s' not found"), *TargetNodeGuid);
		return false;
	}

	UEdGraphPin* TargetPin = FindPinByName(TargetNode, TargetPinName, EGPD_Input);
	if (!TargetPin)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] ConnectPins: Target pin '%s' not found on node '%s'"),
			*TargetPinName, *TargetNodeGuid);
		// Log available pins for debugging
		for (UEdGraphPin* Pin : TargetNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input)
			{
				UE_LOG(LogJarvis, Log, TEXT("  Available input pin: '%s' (%s)"),
					*Pin->PinName.ToString(), *Pin->GetDisplayName().ToString());
			}
		}
		return false;
	}

	// Validate the connection is allowed by the schema
	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (Schema)
	{
		FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
		if (Response.Response == CONNECT_RESPONSE_DISALLOW)
		{
			UE_LOG(LogJarvis, Error, TEXT("[ERROR] ConnectPins: Connection disallowed: %s"),
				*Response.Message.ToString());
			return false;
		}
	}

	// Make the connection within a transaction
	FScopedTransaction Transaction(FText::FromString(TEXT("JARVIS: Connect pins")));
	Graph->Modify();

	// MakeLinkTo handles bidirectional connection
	// This is the safe way - it updates both LinkedTo arrays atomically
	bool bSuccess = Schema->TryCreateConnection(SourcePin, TargetPin);

	if (bSuccess)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		UE_LOG(LogJarvis, Log, TEXT("[EXIT] ConnectPins: Connected successfully"));
	}
	else
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] ConnectPins: TryCreateConnection failed"));
	}

	return bSuccess;
}

bool UJarvisBlueprintLibrary::DisconnectPins(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const FString& SourceNodeGuid,
	const FString& SourcePinName,
	const FString& TargetNodeGuid,
	const FString& TargetPinName)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] DisconnectPins: %s.%s -X- %s.%s"),
		*SourceNodeGuid, *SourcePinName, *TargetNodeGuid, *TargetPinName);

	if (!Blueprint) return false;

	UEdGraphNode* SourceNode = FindNodeByGuid(Blueprint, GraphName, SourceNodeGuid);
	UEdGraphNode* TargetNode = FindNodeByGuid(Blueprint, GraphName, TargetNodeGuid);
	if (!SourceNode || !TargetNode) return false;

	UEdGraphPin* SourcePin = FindPinByName(SourceNode, SourcePinName, EGPD_Output);
	UEdGraphPin* TargetPin = FindPinByName(TargetNode, TargetPinName, EGPD_Input);
	if (!SourcePin || !TargetPin) return false;

	FScopedTransaction Transaction(FText::FromString(TEXT("JARVIS: Disconnect pins")));

	// BreakLinkTo handles bidirectional disconnection safely
	SourcePin->BreakLinkTo(TargetPin);

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] DisconnectPins: Disconnected successfully"));
	return true;
}

int32 UJarvisBlueprintLibrary::DisconnectAllPins(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const FString& NodeGuid)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] DisconnectAllPins: Node=%s"), *NodeGuid);

	UEdGraphNode* Node = FindNodeByGuid(Blueprint, GraphName, NodeGuid);
	if (!Node) return 0;

	FScopedTransaction Transaction(FText::FromString(TEXT("JARVIS: Disconnect all pins")));

	int32 Count = 0;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin)
		{
			Count += Pin->LinkedTo.Num();
			Pin->BreakAllPinLinks();
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] DisconnectAllPins: Broke %d connections"), Count);
	return Count;
}

FString UJarvisBlueprintLibrary::GetNodePins(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const FString& NodeGuid)
{
	UEdGraphNode* Node = FindNodeByGuid(Blueprint, GraphName, NodeGuid);
	if (!Node) return TEXT("[]");

	TArray<TSharedPtr<FJsonValue>> PinsArray;

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->bHidden) continue;

		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
		PinObj->SetStringField(TEXT("display_name"), Pin->GetDisplayName().ToString());
		PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
		PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
		PinObj->SetStringField(TEXT("sub_type"), Pin->PinType.PinSubCategory.ToString());
		PinObj->SetBoolField(TEXT("is_connected"), Pin->LinkedTo.Num() > 0);
		PinObj->SetNumberField(TEXT("connection_count"), Pin->LinkedTo.Num());

		// List connections
		TArray<TSharedPtr<FJsonValue>> Connections;
		for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (LinkedPin && LinkedPin->GetOwningNode())
			{
				TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
				ConnObj->SetStringField(TEXT("node_guid"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
				ConnObj->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
				Connections.Add(MakeShared<FJsonValueObject>(ConnObj));
			}
		}
		PinObj->SetArrayField(TEXT("connections"), Connections);

		PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(PinsArray, Writer);

	return OutputString;
}

// =============================================================================
// SECTION 3: BLUEPRINT COMPILATION
// =============================================================================

bool UJarvisBlueprintLibrary::CompileBlueprint(UBlueprint* Blueprint)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] CompileBlueprint: BP=%s"),
		Blueprint ? *Blueprint->GetName() : TEXT("null"));

	if (!Blueprint)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] CompileBlueprint: Blueprint is null"));
		return false;
	}

	// Compile using the Kismet compiler
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, nullptr);

	// Check result
	bool bSuccess = (Blueprint->Status != BS_Error);

	if (bSuccess)
	{
		UE_LOG(LogJarvis, Log, TEXT("[EXIT] CompileBlueprint: SUCCESS - %s compiled cleanly"),
			*Blueprint->GetName());
	}
	else
	{
		UE_LOG(LogJarvis, Error, TEXT("[EXIT] CompileBlueprint: FAILED - %s has compile errors"),
			*Blueprint->GetName());

		// Log each error for debugging
		for (const FCompilerResultsLog::TCompilerEvent& Event : Blueprint->CurrentMessageLog->Messages)
		{
			UE_LOG(LogJarvis, Error, TEXT("  Compile Error: %s"), *Event->Message.ToString());
		}
	}

	return bSuccess;
}

FString UJarvisBlueprintLibrary::GetCompileErrors(UBlueprint* Blueprint)
{
	if (!Blueprint) return TEXT("[]");

	TArray<TSharedPtr<FJsonValue>> ErrorsArray;

	// Check for compile notes/errors stored on the Blueprint
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			if (Node->bHasCompilerMessage)
			{
				TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
				ErrorObj->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());
				ErrorObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
				ErrorObj->SetBoolField(TEXT("is_error"), Node->ErrorType >= EMessageSeverity::Error);
				ErrorObj->SetStringField(TEXT("message"), Node->ErrorMsg);
				ErrorsArray.Add(MakeShared<FJsonValueObject>(ErrorObj));
			}
		}
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(ErrorsArray, Writer);

	return OutputString;
}

void UJarvisBlueprintLibrary::MarkBlueprintDirty(UBlueprint* Blueprint)
{
	if (Blueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		UE_LOG(LogJarvis, Log, TEXT("MarkBlueprintDirty: %s marked as modified"), *Blueprint->GetName());
	}
}

// =============================================================================
// SECTION 4: GRAPH INSPECTION
// =============================================================================

FString UJarvisBlueprintLibrary::ListGraphs(UBlueprint* Blueprint)
{
	if (!Blueprint) return TEXT("[]");

	TArray<TSharedPtr<FJsonValue>> GraphsArray;

	auto AddGraphs = [&](const TArray<UEdGraph*>& Graphs, const FString& Type)
	{
		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph) continue;
			TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
			GraphObj->SetStringField(TEXT("name"), Graph->GetName());
			GraphObj->SetStringField(TEXT("type"), Type);
			GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
			GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
		}
	};

	AddGraphs(Blueprint->UbergraphPages, TEXT("EventGraph"));
	AddGraphs(Blueprint->FunctionGraphs, TEXT("Function"));
	AddGraphs(Blueprint->MacroGraphs, TEXT("Macro"));

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(GraphsArray, Writer);

	return OutputString;
}

FString UJarvisBlueprintLibrary::DumpGraphState(
	UBlueprint* Blueprint,
	const FString& GraphName)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] DumpGraphState: BP=%s, Graph=%s"),
		Blueprint ? *Blueprint->GetName() : TEXT("null"), *GraphName);

	if (!Blueprint) return TEXT("{}");

	UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
	if (!Graph) return TEXT("{}");

	TSharedPtr<FJsonObject> RootObj = MakeShared<FJsonObject>();
	RootObj->SetStringField(TEXT("graph_name"), Graph->GetName());
	RootObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

	TArray<TSharedPtr<FJsonValue>> NodesArray;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
		NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeObj->SetStringField(TEXT("compact_title"), Node->GetNodeTitle(ENodeTitleType::MenuTitle).ToString());
		NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);
		NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);
		NodeObj->SetBoolField(TEXT("has_compile_error"), Node->bHasCompilerMessage);

		// Pins
		TArray<TSharedPtr<FJsonValue>> PinsArray;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->bHidden) continue;

			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
			PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			PinObj->SetNumberField(TEXT("connections"), Pin->LinkedTo.Num());

			TArray<TSharedPtr<FJsonValue>> LinkedArray;
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin && LinkedPin->GetOwningNode())
				{
					TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
					LinkObj->SetStringField(TEXT("node_guid"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
					LinkObj->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
					LinkedArray.Add(MakeShared<FJsonValueObject>(LinkObj));
				}
			}
			PinObj->SetArrayField(TEXT("linked_to"), LinkedArray);

			PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		NodeObj->SetArrayField(TEXT("pins"), PinsArray);

		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	RootObj->SetArrayField(TEXT("nodes"), NodesArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObj.ToSharedRef(), Writer);

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] DumpGraphState: Dumped %d nodes"), Graph->Nodes.Num());
	return OutputString;
}

FString UJarvisBlueprintLibrary::GetGraphTopology(
	UBlueprint* Blueprint,
	const FString& GraphName)
{
	if (!Blueprint) return TEXT("{}");

	UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
	if (!Graph) return TEXT("{}");

	TSharedPtr<FJsonObject> TopologyObj = MakeShared<FJsonObject>();

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		TArray<TSharedPtr<FJsonValue>> Connections;

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;

				TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
				ConnObj->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
				ConnObj->SetStringField(TEXT("to_node"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
				ConnObj->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());
				Connections.Add(MakeShared<FJsonValueObject>(ConnObj));
			}
		}

		TopologyObj->SetArrayField(Node->NodeGuid.ToString(), Connections);
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(TopologyObj.ToSharedRef(), Writer);

	return OutputString;
}

// =============================================================================
// SECTION 5: LEVEL SCRIPT ACCESS
// =============================================================================

UBlueprint* UJarvisBlueprintLibrary::GetLevelScriptBlueprint()
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] GetLevelScriptBlueprint"));

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] GetLevelScriptBlueprint: No editor world"));
		return nullptr;
	}

	ULevel* Level = World->GetCurrentLevel();
	if (!Level)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] GetLevelScriptBlueprint: No current level"));
		return nullptr;
	}

	ULevelScriptBlueprint* LSB = Level->GetLevelScriptBlueprint(true);
	if (!LSB)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] GetLevelScriptBlueprint: Level has no script blueprint"));
		return nullptr;
	}

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] GetLevelScriptBlueprint: Found '%s'"), *LSB->GetName());
	return LSB;
}

UBlueprint* UJarvisBlueprintLibrary::GetLevelScriptBlueprintByPath(const FString& LevelPath)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] GetLevelScriptBlueprintByPath: %s"), *LevelPath);

	// Load the level package
	UPackage* Package = LoadPackage(nullptr, *LevelPath, LOAD_None);
	if (!Package)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] GetLevelScriptBlueprintByPath: Could not load '%s'"), *LevelPath);
		return nullptr;
	}

	// Find the world in the package
	UWorld* World = UWorld::FindWorldInPackage(Package);
	if (!World)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] GetLevelScriptBlueprintByPath: No world in '%s'"), *LevelPath);
		return nullptr;
	}

	ULevel* Level = World->PersistentLevel;
	if (!Level)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] GetLevelScriptBlueprintByPath: No persistent level"));
		return nullptr;
	}

	ULevelScriptBlueprint* LSB = Level->GetLevelScriptBlueprint(true);
	UE_LOG(LogJarvis, Log, TEXT("[EXIT] GetLevelScriptBlueprintByPath: %s"),
		LSB ? *LSB->GetName() : TEXT("null"));

	return LSB;
}

// =============================================================================
// SECTION 6: TRANSACTION CONTROL
// =============================================================================

void UJarvisBlueprintLibrary::BeginTransaction(const FString& TransactionName)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] BeginTransaction: '%s'"), *TransactionName);

	if (ActiveTransaction.IsValid())
	{
		UE_LOG(LogJarvis, Warning, TEXT("[WARN] BeginTransaction: Previous transaction still active, ending it first"));
		ActiveTransaction.Reset();
	}

	ActiveTransaction = MakeShared<FScopedTransaction>(FText::FromString(TransactionName));

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] BeginTransaction: Transaction started"));
}

void UJarvisBlueprintLibrary::EndTransaction()
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] EndTransaction"));

	if (!ActiveTransaction.IsValid())
	{
		UE_LOG(LogJarvis, Warning, TEXT("[WARN] EndTransaction: No active transaction"));
		return;
	}

	// Resetting the shared pointer ends the FScopedTransaction scope
	ActiveTransaction.Reset();

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] EndTransaction: Transaction committed"));
}

// =============================================================================
// SECTION 7: GENERIC NODE OPERATIONS
// =============================================================================

UEdGraphNode* UJarvisBlueprintLibrary::AddNodeToGraph(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const FString& NodeClassName,
	int32 NodePosX,
	int32 NodePosY)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] AddNodeToGraph: BP=%s, Graph=%s, Class=%s, Pos=(%d,%d)"),
		Blueprint ? *Blueprint->GetName() : TEXT("null"),
		*GraphName, *NodeClassName, NodePosX, NodePosY);

	if (!Blueprint)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddNodeToGraph: Blueprint is null"));
		return nullptr;
	}

	UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddNodeToGraph: Graph '%s' not found"), *GraphName);
		return nullptr;
	}

	// Find the UClass for the node type
	UClass* NodeClass = FindObject<UClass>(ANY_PACKAGE, *NodeClassName);
	if (!NodeClass)
	{
		// Try with U prefix (e.g. "K2Node_GetSubsystem" -> "UK2Node_GetSubsystem")
		NodeClass = FindObject<UClass>(ANY_PACKAGE, *(TEXT("U") + NodeClassName));
	}
	if (!NodeClass)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddNodeToGraph: Class '%s' not found"), *NodeClassName);
		return nullptr;
	}

	// Verify it is a K2Node subclass
	if (!NodeClass->IsChildOf(UK2Node::StaticClass()))
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddNodeToGraph: '%s' is not a K2Node subclass"), *NodeClassName);
		return nullptr;
	}

	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("JARVIS: Add %s node"), *NodeClassName)));
	Graph->Modify();

	UEdGraphNode* NewNode = NewObject<UEdGraphNode>(Graph, NodeClass);
	NewNode->NodePosX = NodePosX;
	NewNode->NodePosY = NodePosY;
	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	Graph->AddNode(NewNode, false, false);
	NewNode->SetFlags(RF_Transactional);

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] AddNodeToGraph: Created '%s' GUID=%s"),
		*NodeClassName, *NewNode->NodeGuid.ToString());

	return NewNode;
}

UEdGraphNode* UJarvisBlueprintLibrary::AddGetSubsystemNode(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const FString& SubsystemClassName,
	int32 NodePosX,
	int32 NodePosY)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] AddGetSubsystemNode: BP=%s, Subsystem=%s"),
		Blueprint ? *Blueprint->GetName() : TEXT("null"), *SubsystemClassName);

	if (!Blueprint)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddGetSubsystemNode: Blueprint is null"));
		return nullptr;
	}

	UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddGetSubsystemNode: Graph '%s' not found"), *GraphName);
		return nullptr;
	}

	// Find the subsystem class
	UClass* SubsystemClass = FindObject<UClass>(ANY_PACKAGE, *SubsystemClassName);
	if (!SubsystemClass)
	{
		SubsystemClass = FindObject<UClass>(ANY_PACKAGE, *(TEXT("U") + SubsystemClassName));
	}
	if (!SubsystemClass)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddGetSubsystemNode: Subsystem class '%s' not found"),
			*SubsystemClassName);
		return nullptr;
	}

	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("JARVIS: Add Get %s"), *SubsystemClassName)));
	Graph->Modify();

	UK2Node_GetSubsystem* NewNode = NewObject<UK2Node_GetSubsystem>(Graph);
	// Set the subsystem class before allocating pins so the output pin type is correct
	NewNode->CustomClass = SubsystemClass;
	NewNode->NodePosX = NodePosX;
	NewNode->NodePosY = NodePosY;
	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	Graph->AddNode(NewNode, false, false);
	NewNode->SetFlags(RF_Transactional);

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] AddGetSubsystemNode: Created Get '%s' GUID=%s"),
		*SubsystemClassName, *NewNode->NodeGuid.ToString());

	return NewNode;
}

UEdGraphNode* UJarvisBlueprintLibrary::AddMakeStructNode(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const FString& StructPath,
	int32 NodePosX,
	int32 NodePosY)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] AddMakeStructNode: BP=%s, Struct=%s"),
		Blueprint ? *Blueprint->GetName() : TEXT("null"), *StructPath);

	if (!Blueprint)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddMakeStructNode: Blueprint is null"));
		return nullptr;
	}

	UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddMakeStructNode: Graph '%s' not found"), *GraphName);
		return nullptr;
	}

	// Find the struct -- try direct name first, then with F prefix, then as full path
	UScriptStruct* StructType = FindObject<UScriptStruct>(ANY_PACKAGE, *StructPath);
	if (!StructType)
	{
		StructType = FindObject<UScriptStruct>(ANY_PACKAGE, *(TEXT("F") + StructPath));
	}
	if (!StructType)
	{
		// Try loading as a full asset path
		StructType = LoadObject<UScriptStruct>(nullptr, *StructPath);
	}
	if (!StructType)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] AddMakeStructNode: Struct '%s' not found"), *StructPath);
		return nullptr;
	}

	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("JARVIS: Add Make %s"), *StructPath)));
	Graph->Modify();

	UK2Node_MakeStruct* NewNode = NewObject<UK2Node_MakeStruct>(Graph);
	NewNode->StructType = StructType;
	NewNode->NodePosX = NodePosX;
	NewNode->NodePosY = NodePosY;
	NewNode->CreateNewGuid();
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	Graph->AddNode(NewNode, false, false);
	NewNode->SetFlags(RF_Transactional);

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] AddMakeStructNode: Created Make '%s' GUID=%s"),
		*StructPath, *NewNode->NodeGuid.ToString());

	return NewNode;
}

bool UJarvisBlueprintLibrary::SetPinDefaultValue(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const FString& NodeGuid,
	const FString& PinName,
	const FString& DefaultValue)
{
	UE_LOG(LogJarvis, Log, TEXT("[ENTRY] SetPinDefaultValue: Node=%s, Pin=%s, Value=%s"),
		*NodeGuid, *PinName, *DefaultValue);

	if (!Blueprint)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] SetPinDefaultValue: Blueprint is null"));
		return false;
	}

	UEdGraphNode* Node = FindNodeByGuid(Blueprint, GraphName, NodeGuid);
	if (!Node)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] SetPinDefaultValue: Node '%s' not found"), *NodeGuid);
		return false;
	}

	// Find the pin -- try input first, then output
	UEdGraphPin* Pin = FindPinByName(Node, PinName, EGPD_Input);
	if (!Pin)
	{
		Pin = FindPinByName(Node, PinName, EGPD_Output);
	}
	if (!Pin)
	{
		UE_LOG(LogJarvis, Error, TEXT("[ERROR] SetPinDefaultValue: Pin '%s' not found on node '%s'"),
			*PinName, *NodeGuid);
		// Log available pins for debugging
		for (UEdGraphPin* AvailPin : Node->Pins)
		{
			if (AvailPin && !AvailPin->bHidden)
			{
				UE_LOG(LogJarvis, Log, TEXT("  Available pin: '%s' (%s, %s)"),
					*AvailPin->PinName.ToString(),
					*AvailPin->GetDisplayName().ToString(),
					AvailPin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
			}
		}
		return false;
	}

	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("JARVIS: Set %s.%s = %s"), *NodeGuid, *PinName, *DefaultValue)));

	// Use the schema to set the default value -- this validates type compatibility
	const UEdGraphSchema* Schema = Node->GetGraph()->GetSchema();
	if (Schema)
	{
		Schema->TrySetDefaultValue(*Pin, DefaultValue);
	}
	else
	{
		Pin->DefaultValue = DefaultValue;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	UE_LOG(LogJarvis, Log, TEXT("[EXIT] SetPinDefaultValue: Set '%s' = '%s'"),
		*PinName, *DefaultValue);

	return true;
}

// =============================================================================
// INTERNAL HELPERS
// =============================================================================

UEdGraph* UJarvisBlueprintLibrary::FindGraphByName(UBlueprint* Blueprint, const FString& GraphName)
{
	if (!Blueprint) return nullptr;

	// Search all graph collections
	auto SearchGraphs = [&](const TArray<UEdGraph*>& Graphs) -> UEdGraph*
	{
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetName() == GraphName)
			{
				return Graph;
			}
		}
		return nullptr;
	};

	if (UEdGraph* Found = SearchGraphs(Blueprint->UbergraphPages)) return Found;
	if (UEdGraph* Found = SearchGraphs(Blueprint->FunctionGraphs)) return Found;
	if (UEdGraph* Found = SearchGraphs(Blueprint->MacroGraphs)) return Found;

	// Also check sub-graphs
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph) continue;
		for (UEdGraph* SubGraph : Graph->SubGraphs)
		{
			if (SubGraph && SubGraph->GetName() == GraphName) return SubGraph;
		}
	}

	return nullptr;
}

UEdGraphPin* UJarvisBlueprintLibrary::FindPinByName(
	UEdGraphNode* Node,
	const FString& PinName,
	EEdGraphPinDirection Direction)
{
	if (!Node) return nullptr;

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->Direction != Direction) continue;

		// Match by internal name first
		if (Pin->PinName.ToString() == PinName) return Pin;

		// Then try display name
		if (Pin->GetDisplayName().ToString() == PinName) return Pin;
	}

	// Fuzzy match: case-insensitive
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->Direction != Direction) continue;

		if (Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase)) return Pin;
		if (Pin->GetDisplayName().ToString().Equals(PinName, ESearchCase::IgnoreCase)) return Pin;
	}

	return nullptr;
}

bool UJarvisBlueprintLibrary::StringToGuid(const FString& GuidString, FGuid& OutGuid)
{
	return FGuid::Parse(GuidString, OutGuid);
}
