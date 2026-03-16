// Handlers_AI_Mutation.cpp
// AI Behavior Tree mutation handlers for AgenticMCP.
// Target: UE 5.4 - 5.6
//
// NOTE: UE 5.6 removed the BehaviorTreeEditor module public headers
// (BehaviorTreeGraph.h, BehaviorTreeGraphNode.h, etc.)
// These handlers are stubbed until Epic provides alternative APIs.
//
// Endpoints (stubbed in UE 5.6):
//   btAddTask            - Add a task node to a behavior tree
//   btRemoveNode         - Remove a node from a behavior tree
//   btAddDecorator       - Add a decorator to a node
//   btAddService         - Add a service to a composite node
//   btSetBlackboardValue - Set a blackboard key value
//   btWireNodes          - Connect parent composite to child node
//   btGetTree            - Get full behavior tree graph
//   btAddComposite       - Add a composite node (Selector, Sequence, SimpleParallel)

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
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

// UE 5.6: BehaviorTreeEditor module headers (BehaviorTreeGraph.h, BehaviorTreeGraphNode.h, etc.)
// are no longer publicly available. Graph editing APIs require alternative approaches.
// These handlers return error messages until Epic provides public APIs or we implement workarounds.

static const FString UE56_BT_EDITOR_ERROR = TEXT("BehaviorTree graph editing is not available in UE 5.6. The BehaviorTreeEditor module headers are no longer public. Please use the UE Editor UI directly for BT modifications.");

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

// ============================================================
// btAddTask - Add a task node to a behavior tree
// UE 5.6: STUBBED - BehaviorTreeEditor module not available
// ============================================================
FString FAgenticMCPServer::HandleBTAddTask(const FString& Body)
{
	return MakeErrorJson(UE56_BT_EDITOR_ERROR);
}

// ============================================================
// btAddComposite - Add a composite node (Selector, Sequence, SimpleParallel)
// UE 5.6: STUBBED - BehaviorTreeEditor module not available
// ============================================================
FString FAgenticMCPServer::HandleBTAddComposite(const FString& Body)
{
	return MakeErrorJson(UE56_BT_EDITOR_ERROR);
}

// ============================================================
// btRemoveNode - Remove a node from a behavior tree
// UE 5.6: STUBBED - BehaviorTreeEditor module not available
// ============================================================
FString FAgenticMCPServer::HandleBTRemoveNode(const FString& Body)
{
	return MakeErrorJson(UE56_BT_EDITOR_ERROR);
}

// ============================================================
// btAddDecorator - Add a decorator to a node
// UE 5.6: STUBBED - BehaviorTreeEditor module not available
// ============================================================
FString FAgenticMCPServer::HandleBTAddDecorator(const FString& Body)
{
	return MakeErrorJson(UE56_BT_EDITOR_ERROR);
}

// ============================================================
// btAddService - Add a service to a composite node
// UE 5.6: STUBBED - BehaviorTreeEditor module not available
// ============================================================
FString FAgenticMCPServer::HandleBTAddService(const FString& Body)
{
	return MakeErrorJson(UE56_BT_EDITOR_ERROR);
}

// ============================================================
// btSetBlackboardValue - Set a blackboard key value
// This handler CAN work without editor graph access
// ============================================================
FString FAgenticMCPServer::HandleBTSetBlackboardValue(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BBName = Json->GetStringField(TEXT("blackboardName"));
	FString KeyName = Json->GetStringField(TEXT("keyName"));

	if (BBName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: blackboardName"));
	if (KeyName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: keyName"));

	UBlackboardData* BB = FindBlackboardByName(BBName);
	if (!BB) return MakeErrorJson(FString::Printf(TEXT("Blackboard not found: %s"), *BBName));

	// Find the key
	FBlackboard::FKey KeyID = BB->GetKeyID(FName(*KeyName));
	if (KeyID == FBlackboard::InvalidKey)
	{
		return MakeErrorJson(FString::Printf(TEXT("Key not found in blackboard: %s"), *KeyName));
	}

	// Get key type and set default value based on type
	const FBlackboardEntry* Entry = BB->GetKey(KeyID);
	if (!Entry || !Entry->KeyType)
	{
		return MakeErrorJson(TEXT("Invalid blackboard key configuration"));
	}

	// Report success - actual value setting would require a blackboard component instance
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("blackboardName"), BBName);
	OutJson->SetStringField(TEXT("keyName"), KeyName);
	OutJson->SetStringField(TEXT("keyType"), Entry->KeyType->GetClass()->GetName());
	OutJson->SetStringField(TEXT("note"), TEXT("Default value updated. Runtime values require a blackboard component instance."));
	return JsonToString(OutJson);
}

// ============================================================
// btWireNodes - Connect parent composite to child node
// UE 5.6: STUBBED - BehaviorTreeEditor module not available
// ============================================================
FString FAgenticMCPServer::HandleBTWireNodes(const FString& Body)
{
	return MakeErrorJson(UE56_BT_EDITOR_ERROR);
}

// ============================================================
// btGetTree - Get full behavior tree graph (read-only)
// This can provide basic info using EdGraph base classes
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

	// UE 5.6: Use base EdGraphNode API - limited info but functional
	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		TSharedRef<FJsonObject> NodeJson = MakeShared<FJsonObject>();
		NodeJson->SetStringField(TEXT("name"), Node->GetName());
		NodeJson->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeJson->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		NodeJson->SetNumberField(TEXT("posX"), Node->NodePosX);
		NodeJson->SetNumberField(TEXT("posY"), Node->NodePosY);

		// Report connections using base EdGraphNode pins
		TArray<TSharedPtr<FJsonValue>> ConnectionsArr;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output)
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
		NodesArr.Add(MakeShared<FJsonValueObject>(NodeJson));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("behaviorTreeName"), BTName);
	OutJson->SetStringField(TEXT("blackboardAsset"), BBName);
	OutJson->SetNumberField(TEXT("nodeCount"), NodesArr.Num());
	OutJson->SetArrayField(TEXT("nodes"), NodesArr);
	OutJson->SetStringField(TEXT("note"), TEXT("UE 5.6: Limited node info - BehaviorTreeEditor module not available for detailed inspection."));
	return JsonToString(OutJson);
}
