// Handlers_AI_Mutation.cpp
// AI Behavior Tree mutation handlers for AgenticMCP.
// Target: UE 5.4 - 5.6
//
// Endpoints:
//   btAddTask            - Add a task node to a behavior tree
//   btRemoveNode         - Remove a node from a behavior tree
//   btAddDecorator       - Add a decorator to a node
//   btAddService         - Add a service to a composite node
//   btSetBlackboardValue - Set a blackboard key value
//   btWireNodes          - Connect parent composite to child node
//   btGetTree            - Get full behavior tree graph
//   btAddComposite       - Add a composite node (Selector, Sequence, SimpleParallel)

#include "AgenticMCPServer.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "BehaviorTree/Tasks/BTTask_RunBehavior.h"
#include "BehaviorTree/Tasks/BTTask_PlayAnimation.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "BehaviorTree/Decorators/BTDecorator_Cooldown.h"
#include "BehaviorTree/Decorators/BTDecorator_TimeLimit.h"
#include "BehaviorTree/Decorators/BTDecorator_Loop.h"
#include "BehaviorTree/Services/BTService_DefaultFocus.h"
#include "BehaviorTree/Services/BTService_RunEQS.h"
#include "BehaviorTree/BehaviorTreeManager.h"
#include "AIController.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "BehaviorTreeEditorModule.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "BehaviorTreeGraphNode_Decorator.h"
#include "BehaviorTreeGraphNode_Service.h"
#include "BehaviorTreeGraphNode_Root.h"

// Helper: find a behavior tree by name
static UBehaviorTree* FindBehaviorTreeByName(const FString& Name)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> Assets;
	AR.GetAssetsByClass(UBehaviorTree::StaticClass()->GetClassPathName(), Assets, true);

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			return Cast<UBehaviorTree>(Asset.GetAsset());
		}
	}
	return nullptr;
}

// Helper: find a blackboard asset by name
static UBlackboardData* FindBlackboardByName(const FString& Name)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> Assets;
	AR.GetAssetsByClass(UBlackboardData::StaticClass()->GetClassPathName(), Assets, true);

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			return Cast<UBlackboardData>(Asset.GetAsset());
		}
	}
	return nullptr;
}

// Helper: find a graph node by name/description in the BT graph
static UBehaviorTreeGraphNode* FindBTGraphNode(UEdGraph* Graph, const FString& NodeName)
{
	if (!Graph) return nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UBehaviorTreeGraphNode* BTNode = Cast<UBehaviorTreeGraphNode>(Node);
		if (BTNode)
		{
			FString Title = BTNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			if (Title.Contains(NodeName) || BTNode->GetName().Contains(NodeName))
			{
				return BTNode;
			}
		}
	}
	return nullptr;
}

// ============================================================
// btAddTask - Add a task node to a behavior tree
// Params: behaviorTreeName, taskType (Wait, MoveTo, PlayAnimation,
//         RunBehavior, or custom class path), parentNodeName (opt),
//         nodeName (opt), posX (opt), posY (opt)
// ============================================================
FString FAgenticMCPServer::HandleBTAddTask(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BTName = Json->GetStringField(TEXT("behaviorTreeName"));
	FString TaskType = Json->GetStringField(TEXT("taskType"));

	if (BTName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: behaviorTreeName"));
	if (TaskType.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: taskType"));

	UBehaviorTree* BT = FindBehaviorTreeByName(BTName);
	if (!BT) return MakeErrorJson(FString::Printf(TEXT("BehaviorTree not found: %s"), *BTName));

	UEdGraph* Graph = nullptr;
	if (BT->BTGraph)
		Graph = BT->BTGraph;
	else
		return MakeErrorJson(TEXT("BehaviorTree has no graph"));

	// Determine task class
	UClass* TaskClass = nullptr;
	if (TaskType == TEXT("Wait"))
		TaskClass = UBTTask_Wait::StaticClass();
	else if (TaskType == TEXT("MoveTo"))
		TaskClass = UBTTask_MoveTo::StaticClass();
	else if (TaskType == TEXT("PlayAnimation"))
		TaskClass = UBTTask_PlayAnimation::StaticClass();
	else if (TaskType == TEXT("RunBehavior"))
		TaskClass = UBTTask_RunBehavior::StaticClass();
	else
	{
		// Try loading as a class path
		TaskClass = LoadClass<UBTTaskNode>(nullptr, *TaskType);
		if (!TaskClass)
			return MakeErrorJson(FString::Printf(
				TEXT("Unknown taskType: %s. Built-in: Wait, MoveTo, PlayAnimation, RunBehavior. "
					 "Or provide a full class path."), *TaskType));
	}

	// Create the graph node
	FGraphNodeCreator<UBehaviorTreeGraphNode_Task> NodeCreator(*Graph);
	UBehaviorTreeGraphNode_Task* NewNode = NodeCreator.CreateNode(true);
	if (!NewNode) return MakeErrorJson(TEXT("Failed to create task graph node"));

	int32 PosX = Json->HasField(TEXT("posX")) ? (int32)Json->GetNumberField(TEXT("posX")) : 300;
	int32 PosY = Json->HasField(TEXT("posY")) ? (int32)Json->GetNumberField(TEXT("posY")) : 0;
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;

	// Create the runtime task node
	UBTTaskNode* TaskNode = NewObject<UBTTaskNode>(NewNode, TaskClass);
	NewNode->NodeInstance = TaskNode;
	NodeCreator.Finalize();

	BT->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("behaviorTreeName"), BTName);
	Result->SetStringField(TEXT("taskType"), TaskType);
	Result->SetStringField(TEXT("taskClass"), TaskClass->GetName());
	Result->SetStringField(TEXT("nodeName"), NewNode->GetName());
	Result->SetNumberField(TEXT("posX"), PosX);
	Result->SetNumberField(TEXT("posY"), PosY);
	return JsonToString(Result);
}

// ============================================================
// btAddComposite - Add a composite node (Selector, Sequence, SimpleParallel)
// Params: behaviorTreeName, compositeType, posX (opt), posY (opt)
// ============================================================
FString FAgenticMCPServer::HandleBTAddComposite(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BTName = Json->GetStringField(TEXT("behaviorTreeName"));
	FString CompType = Json->GetStringField(TEXT("compositeType"));

	if (BTName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: behaviorTreeName"));
	if (CompType.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: compositeType"));

	UBehaviorTree* BT = FindBehaviorTreeByName(BTName);
	if (!BT) return MakeErrorJson(FString::Printf(TEXT("BehaviorTree not found: %s"), *BTName));

	UEdGraph* Graph = BT->BTGraph;
	if (!Graph) return MakeErrorJson(TEXT("BehaviorTree has no graph"));

	UClass* CompClass = nullptr;
	if (CompType == TEXT("Selector"))
		CompClass = UBTComposite_Selector::StaticClass();
	else if (CompType == TEXT("Sequence"))
		CompClass = UBTComposite_Sequence::StaticClass();
	else if (CompType == TEXT("SimpleParallel"))
		CompClass = UBTComposite_SimpleParallel::StaticClass();
	else
	{
		CompClass = LoadClass<UBTCompositeNode>(nullptr, *CompType);
		if (!CompClass)
			return MakeErrorJson(FString::Printf(
				TEXT("Unknown compositeType: %s. Built-in: Selector, Sequence, SimpleParallel."), *CompType));
	}

	FGraphNodeCreator<UBehaviorTreeGraphNode_Composite> NodeCreator(*Graph);
	UBehaviorTreeGraphNode_Composite* NewNode = NodeCreator.CreateNode(true);
	if (!NewNode) return MakeErrorJson(TEXT("Failed to create composite graph node"));

	int32 PosX = Json->HasField(TEXT("posX")) ? (int32)Json->GetNumberField(TEXT("posX")) : 200;
	int32 PosY = Json->HasField(TEXT("posY")) ? (int32)Json->GetNumberField(TEXT("posY")) : 0;
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;

	UBTCompositeNode* CompNode = NewObject<UBTCompositeNode>(NewNode, CompClass);
	NewNode->NodeInstance = CompNode;
	NodeCreator.Finalize();

	BT->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("behaviorTreeName"), BTName);
	Result->SetStringField(TEXT("compositeType"), CompType);
	Result->SetStringField(TEXT("nodeName"), NewNode->GetName());
	Result->SetNumberField(TEXT("posX"), PosX);
	Result->SetNumberField(TEXT("posY"), PosY);
	return JsonToString(Result);
}

// ============================================================
// btRemoveNode - Remove a node from a behavior tree
// Params: behaviorTreeName, nodeName
// ============================================================
FString FAgenticMCPServer::HandleBTRemoveNode(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BTName = Json->GetStringField(TEXT("behaviorTreeName"));
	FString NodeName = Json->GetStringField(TEXT("nodeName"));

	if (BTName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: behaviorTreeName"));
	if (NodeName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: nodeName"));

	UBehaviorTree* BT = FindBehaviorTreeByName(BTName);
	if (!BT) return MakeErrorJson(FString::Printf(TEXT("BehaviorTree not found: %s"), *BTName));

	UEdGraph* Graph = BT->BTGraph;
	if (!Graph) return MakeErrorJson(TEXT("BehaviorTree has no graph"));

	UBehaviorTreeGraphNode* BTNode = FindBTGraphNode(Graph, NodeName);
	if (!BTNode) return MakeErrorJson(FString::Printf(TEXT("Node not found: %s"), *NodeName));

	// Don't allow removing root
	if (BTNode->IsA<UBehaviorTreeGraphNode_Root>())
		return MakeErrorJson(TEXT("Cannot remove root node"));

	// Break all pin connections
	for (UEdGraphPin* Pin : BTNode->Pins)
	{
		Pin->BreakAllPinLinks();
	}

	Graph->RemoveNode(BTNode);
	BT->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("behaviorTreeName"), BTName);
	Result->SetStringField(TEXT("removedNode"), NodeName);
	return JsonToString(Result);
}

// ============================================================
// btAddDecorator - Add a decorator to a node
// Params: behaviorTreeName, targetNodeName,
//         decoratorType (Blackboard, Cooldown, TimeLimit, Loop,
//                        or custom class path)
// ============================================================
FString FAgenticMCPServer::HandleBTAddDecorator(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BTName = Json->GetStringField(TEXT("behaviorTreeName"));
	FString TargetName = Json->GetStringField(TEXT("targetNodeName"));
	FString DecType = Json->GetStringField(TEXT("decoratorType"));

	if (BTName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: behaviorTreeName"));
	if (TargetName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: targetNodeName"));
	if (DecType.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: decoratorType"));

	UBehaviorTree* BT = FindBehaviorTreeByName(BTName);
	if (!BT) return MakeErrorJson(FString::Printf(TEXT("BehaviorTree not found: %s"), *BTName));

	UEdGraph* Graph = BT->BTGraph;
	if (!Graph) return MakeErrorJson(TEXT("BehaviorTree has no graph"));

	UBehaviorTreeGraphNode* TargetNode = FindBTGraphNode(Graph, TargetName);
	if (!TargetNode) return MakeErrorJson(FString::Printf(TEXT("Target node not found: %s"), *TargetName));

	UClass* DecClass = nullptr;
	if (DecType == TEXT("Blackboard"))
		DecClass = UBTDecorator_Blackboard::StaticClass();
	else if (DecType == TEXT("Cooldown"))
		DecClass = UBTDecorator_Cooldown::StaticClass();
	else if (DecType == TEXT("TimeLimit"))
		DecClass = UBTDecorator_TimeLimit::StaticClass();
	else if (DecType == TEXT("Loop"))
		DecClass = UBTDecorator_Loop::StaticClass();
	else
	{
		DecClass = LoadClass<UBTDecorator>(nullptr, *DecType);
		if (!DecClass)
			return MakeErrorJson(FString::Printf(
				TEXT("Unknown decoratorType: %s. Built-in: Blackboard, Cooldown, TimeLimit, Loop."), *DecType));
	}

	// Create decorator graph node as a sub-node of the target
	UBehaviorTreeGraphNode_Decorator* DecGraphNode = NewObject<UBehaviorTreeGraphNode_Decorator>(TargetNode);
	if (!DecGraphNode) return MakeErrorJson(TEXT("Failed to create decorator graph node"));

	UBTDecorator* DecNode = NewObject<UBTDecorator>(DecGraphNode, DecClass);
	DecGraphNode->NodeInstance = DecNode;

	TargetNode->Decorators.Add(DecGraphNode);

	BT->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("behaviorTreeName"), BTName);
	Result->SetStringField(TEXT("targetNodeName"), TargetName);
	Result->SetStringField(TEXT("decoratorType"), DecType);
	Result->SetStringField(TEXT("decoratorClass"), DecClass->GetName());
	return JsonToString(Result);
}

// ============================================================
// btAddService - Add a service to a composite node
// Params: behaviorTreeName, targetNodeName,
//         serviceType (DefaultFocus, RunEQS, or custom class path)
// ============================================================
FString FAgenticMCPServer::HandleBTAddService(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BTName = Json->GetStringField(TEXT("behaviorTreeName"));
	FString TargetName = Json->GetStringField(TEXT("targetNodeName"));
	FString SvcType = Json->GetStringField(TEXT("serviceType"));

	if (BTName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: behaviorTreeName"));
	if (TargetName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: targetNodeName"));
	if (SvcType.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: serviceType"));

	UBehaviorTree* BT = FindBehaviorTreeByName(BTName);
	if (!BT) return MakeErrorJson(FString::Printf(TEXT("BehaviorTree not found: %s"), *BTName));

	UEdGraph* Graph = BT->BTGraph;
	if (!Graph) return MakeErrorJson(TEXT("BehaviorTree has no graph"));

	UBehaviorTreeGraphNode* TargetNode = FindBTGraphNode(Graph, TargetName);
	if (!TargetNode) return MakeErrorJson(FString::Printf(TEXT("Target node not found: %s"), *TargetName));

	UClass* SvcClass = nullptr;
	if (SvcType == TEXT("DefaultFocus"))
		SvcClass = UBTService_DefaultFocus::StaticClass();
	else if (SvcType == TEXT("RunEQS"))
		SvcClass = UBTService_RunEQS::StaticClass();
	else
	{
		SvcClass = LoadClass<UBTService>(nullptr, *SvcType);
		if (!SvcClass)
			return MakeErrorJson(FString::Printf(
				TEXT("Unknown serviceType: %s. Built-in: DefaultFocus, RunEQS."), *SvcType));
	}

	UBehaviorTreeGraphNode_Service* SvcGraphNode = NewObject<UBehaviorTreeGraphNode_Service>(TargetNode);
	if (!SvcGraphNode) return MakeErrorJson(TEXT("Failed to create service graph node"));

	UBTService* SvcNode = NewObject<UBTService>(SvcGraphNode, SvcClass);
	SvcGraphNode->NodeInstance = SvcNode;

	TargetNode->Services.Add(SvcGraphNode);

	BT->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("behaviorTreeName"), BTName);
	Result->SetStringField(TEXT("targetNodeName"), TargetName);
	Result->SetStringField(TEXT("serviceType"), SvcType);
	Result->SetStringField(TEXT("serviceClass"), SvcClass->GetName());
	return JsonToString(Result);
}

// ============================================================
// btSetBlackboardValue - Set a blackboard key definition
// Params: blackboardName, keyName, keyType (Bool, Float, Int,
//         String, Object, Vector, Rotator, Enum, Class)
// ============================================================
FString FAgenticMCPServer::HandleBTSetBlackboardValue(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BBName = Json->GetStringField(TEXT("blackboardName"));
	FString KeyName = Json->GetStringField(TEXT("keyName"));
	FString KeyType = Json->GetStringField(TEXT("keyType"));

	if (BBName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: blackboardName"));
	if (KeyName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: keyName"));
	if (KeyType.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: keyType"));

	UBlackboardData* BB = FindBlackboardByName(BBName);
	if (!BB) return MakeErrorJson(FString::Printf(TEXT("Blackboard not found: %s"), *BBName));

	// Check if key already exists
	for (const FBlackboardEntry& Entry : BB->Keys)
	{
		if (Entry.EntryName == FName(*KeyName))
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), true);
			Result->SetBoolField(TEXT("alreadyExists"), true);
			Result->SetStringField(TEXT("blackboardName"), BBName);
			Result->SetStringField(TEXT("keyName"), KeyName);
			Result->SetStringField(TEXT("existingType"), Entry.KeyType ? Entry.KeyType->GetClass()->GetName() : TEXT("unknown"));
			return JsonToString(Result);
		}
	}

	// Create new key
	FBlackboardEntry NewEntry;
	NewEntry.EntryName = FName(*KeyName);

	if (KeyType == TEXT("Bool"))
		NewEntry.KeyType = NewObject<UBlackboardKeyType_Bool>(BB);
	else if (KeyType == TEXT("Float"))
		NewEntry.KeyType = NewObject<UBlackboardKeyType_Float>(BB);
	else if (KeyType == TEXT("Int"))
		NewEntry.KeyType = NewObject<UBlackboardKeyType_Int>(BB);
	else if (KeyType == TEXT("String"))
		NewEntry.KeyType = NewObject<UBlackboardKeyType_String>(BB);
	else if (KeyType == TEXT("Object"))
		NewEntry.KeyType = NewObject<UBlackboardKeyType_Object>(BB);
	else if (KeyType == TEXT("Vector"))
		NewEntry.KeyType = NewObject<UBlackboardKeyType_Vector>(BB);
	else if (KeyType == TEXT("Rotator"))
		NewEntry.KeyType = NewObject<UBlackboardKeyType_Rotator>(BB);
	else if (KeyType == TEXT("Enum"))
		NewEntry.KeyType = NewObject<UBlackboardKeyType_Enum>(BB);
	else if (KeyType == TEXT("Class"))
		NewEntry.KeyType = NewObject<UBlackboardKeyType_Class>(BB);
	else
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Unknown keyType: %s. Supported: Bool, Float, Int, String, Object, Vector, Rotator, Enum, Class"),
			*KeyType));
	}

	BB->Keys.Add(NewEntry);
	BB->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("alreadyExists"), false);
	Result->SetStringField(TEXT("blackboardName"), BBName);
	Result->SetStringField(TEXT("keyName"), KeyName);
	Result->SetStringField(TEXT("keyType"), KeyType);
	Result->SetNumberField(TEXT("totalKeys"), BB->Keys.Num());
	return JsonToString(Result);
}

// ============================================================
// btWireNodes - Connect parent composite output to child node input
// Params: behaviorTreeName, parentNodeName, childNodeName
// ============================================================
FString FAgenticMCPServer::HandleBTWireNodes(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BTName = Json->GetStringField(TEXT("behaviorTreeName"));
	FString ParentName = Json->GetStringField(TEXT("parentNodeName"));
	FString ChildName = Json->GetStringField(TEXT("childNodeName"));

	if (BTName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: behaviorTreeName"));
	if (ParentName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: parentNodeName"));
	if (ChildName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: childNodeName"));

	UBehaviorTree* BT = FindBehaviorTreeByName(BTName);
	if (!BT) return MakeErrorJson(FString::Printf(TEXT("BehaviorTree not found: %s"), *BTName));

	UEdGraph* Graph = BT->BTGraph;
	if (!Graph) return MakeErrorJson(TEXT("BehaviorTree has no graph"));

	UBehaviorTreeGraphNode* ParentNode = FindBTGraphNode(Graph, ParentName);
	UBehaviorTreeGraphNode* ChildNode = FindBTGraphNode(Graph, ChildName);

	if (!ParentNode) return MakeErrorJson(FString::Printf(TEXT("Parent node not found: %s"), *ParentName));
	if (!ChildNode) return MakeErrorJson(FString::Printf(TEXT("Child node not found: %s"), *ChildName));

	// Find output pin on parent and input pin on child
	UEdGraphPin* OutputPin = nullptr;
	UEdGraphPin* InputPin = nullptr;

	for (UEdGraphPin* Pin : ParentNode->Pins)
	{
		if (Pin->Direction == EGPD_Output)
		{
			OutputPin = Pin;
			break;
		}
	}
	for (UEdGraphPin* Pin : ChildNode->Pins)
	{
		if (Pin->Direction == EGPD_Input)
		{
			InputPin = Pin;
			break;
		}
	}

	if (!OutputPin) return MakeErrorJson(TEXT("Parent node has no output pin"));
	if (!InputPin) return MakeErrorJson(TEXT("Child node has no input pin"));

	OutputPin->MakeLinkTo(InputPin);
	BT->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("behaviorTreeName"), BTName);
	Result->SetStringField(TEXT("parentNodeName"), ParentName);
	Result->SetStringField(TEXT("childNodeName"), ChildName);
	return JsonToString(Result);
}

// ============================================================
// btGetTree - Get full behavior tree graph
// Params: behaviorTreeName
// ============================================================
FString FAgenticMCPServer::HandleBTGetTree(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BTName = Json->GetStringField(TEXT("behaviorTreeName"));
	if (BTName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: behaviorTreeName"));

	UBehaviorTree* BT = FindBehaviorTreeByName(BTName);
	if (!BT) return MakeErrorJson(FString::Printf(TEXT("BehaviorTree not found: %s"), *BTName));

	UEdGraph* Graph = BT->BTGraph;
	if (!Graph) return MakeErrorJson(TEXT("BehaviorTree has no graph"));

	// Report blackboard
	FString BBName = TEXT("(none)");
	if (BT->BlackboardAsset)
		BBName = BT->BlackboardAsset->GetName();

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UBehaviorTreeGraphNode* BTNode = Cast<UBehaviorTreeGraphNode>(Node);
		if (!BTNode) continue;

		TSharedRef<FJsonObject> NodeJson = MakeShared<FJsonObject>();
		NodeJson->SetStringField(TEXT("name"), BTNode->GetName());
		NodeJson->SetStringField(TEXT("title"), BTNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeJson->SetStringField(TEXT("class"), BTNode->GetClass()->GetName());
		NodeJson->SetNumberField(TEXT("posX"), BTNode->NodePosX);
		NodeJson->SetNumberField(TEXT("posY"), BTNode->NodePosY);

		if (BTNode->NodeInstance)
		{
			NodeJson->SetStringField(TEXT("instanceClass"), BTNode->NodeInstance->GetClass()->GetName());
		}

		// Report connections
		TArray<TSharedPtr<FJsonValue>> ConnectionsArr;
		for (UEdGraphPin* Pin : BTNode->Pins)
		{
			if (Pin->Direction == EGPD_Output)
			{
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (LinkedPin && LinkedPin->GetOwningNode())
					{
						TSharedRef<FJsonObject> ConnJson = MakeShared<FJsonObject>();
						ConnJson->SetStringField(TEXT("connectedTo"), LinkedPin->GetOwningNode()->GetName());
						ConnectionsArr.Add(MakeShared<FJsonValueObject>(ConnJson));
					}
				}
			}
		}
		NodeJson->SetArrayField(TEXT("children"), ConnectionsArr);

		// Report decorators
		TArray<TSharedPtr<FJsonValue>> DecsArr;
		for (UBehaviorTreeGraphNode* Dec : BTNode->Decorators)
		{
			TSharedRef<FJsonObject> DecJson = MakeShared<FJsonObject>();
			DecJson->SetStringField(TEXT("class"), Dec->NodeInstance ? Dec->NodeInstance->GetClass()->GetName() : TEXT("unknown"));
			DecsArr.Add(MakeShared<FJsonValueObject>(DecJson));
		}
		NodeJson->SetArrayField(TEXT("decorators"), DecsArr);

		// Report services
		TArray<TSharedPtr<FJsonValue>> SvcsArr;
		for (UBehaviorTreeGraphNode* Svc : BTNode->Services)
		{
			TSharedRef<FJsonObject> SvcJson = MakeShared<FJsonObject>();
			SvcJson->SetStringField(TEXT("class"), Svc->NodeInstance ? Svc->NodeInstance->GetClass()->GetName() : TEXT("unknown"));
			SvcsArr.Add(MakeShared<FJsonValueObject>(SvcJson));
		}
		NodeJson->SetArrayField(TEXT("services"), SvcsArr);

		NodesArr.Add(MakeShared<FJsonValueObject>(NodeJson));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("behaviorTreeName"), BTName);
	Result->SetStringField(TEXT("blackboardAsset"), BBName);
	Result->SetNumberField(TEXT("nodeCount"), NodesArr.Num());
	Result->SetArrayField(TEXT("nodes"), NodesArr);
	return JsonToString(Result);
}
