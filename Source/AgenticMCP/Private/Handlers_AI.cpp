// Handlers_AI.cpp
// AI system inspection handlers for AgenticMCP.
// Provides visibility into Behavior Trees, Blackboards, EQS, and AI Controllers.
//
// Endpoints:
//   aiListBehaviorTrees   - List all Behavior Tree assets
//   aiGetBehaviorTree     - Get BT structure (nodes, decorators, services)
//   aiListBlackboards     - List all Blackboard Data assets
//   aiGetBlackboard       - Get blackboard keys and types
//   aiListControllers     - List all AI controllers in the level with their BT/BB assignments
//   aiGetEQSQueries       - List all Environment Query assets

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"

// AI headers - guarded for projects without AI module
#if __has_include("BehaviorTree/BehaviorTree.h")
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#define HAS_AI_MODULE 1
#else
#define HAS_AI_MODULE 0
#endif

#if __has_include("EnvironmentQuery/EnvQuery.h")
#include "EnvironmentQuery/EnvQuery.h"
#define HAS_EQS 1
#else
#define HAS_EQS 0
#endif

// ============================================================
// aiListBehaviorTrees - List all BT assets
// ============================================================
FString FAgenticMCPServer::HandleAIListBehaviorTrees(const FString& Body)
{
#if !HAS_AI_MODULE
	return MakeErrorJson(TEXT("AI module is not available in this build"));
#else
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> BTAssets;
	AR.GetAssetsByClass(UBehaviorTree::StaticClass()->GetClassPathName(), BTAssets, true);

	TSharedPtr<FJsonObject> BodyJson = ParseBodyJson(Body);
	FString Filter;
	if (BodyJson.IsValid() && BodyJson->HasField(TEXT("filter")))
		Filter = BodyJson->GetStringField(TEXT("filter"));

	TArray<TSharedPtr<FJsonValue>> BTArray;
	for (const FAssetData& Asset : BTAssets)
	{
		FString Name = Asset.AssetName.ToString();
		if (!Filter.IsEmpty() && !Name.Contains(Filter))
			continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Name);
		Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		BTArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetNumberField(TEXT("count"), BTArray.Num());
	OutJson->SetArrayField(TEXT("behaviorTrees"), BTArray);
	return JsonToString(OutJson);
#endif
}

// ============================================================
// Helper: Recursively serialize a BT composite node
// ============================================================
#if HAS_AI_MODULE
static TSharedRef<FJsonObject> SerializeBTNode(UBTCompositeNode* CompositeNode, int32 Depth = 0)
{
	TSharedRef<FJsonObject> NodeJson = MakeShared<FJsonObject>();
	if (!CompositeNode) return NodeJson;

	NodeJson->SetStringField(TEXT("name"), CompositeNode->GetNodeName());
	NodeJson->SetStringField(TEXT("class"), CompositeNode->GetClass()->GetName());
	NodeJson->SetNumberField(TEXT("depth"), Depth);

	// Services
	TArray<TSharedPtr<FJsonValue>> ServiceArr;
	for (UBTService* Service : CompositeNode->Services)
	{
		if (!Service) continue;
		TSharedRef<FJsonObject> SvcJson = MakeShared<FJsonObject>();
		SvcJson->SetStringField(TEXT("name"), Service->GetNodeName());
		SvcJson->SetStringField(TEXT("class"), Service->GetClass()->GetName());
		ServiceArr.Add(MakeShared<FJsonValueObject>(SvcJson));
	}
	NodeJson->SetArrayField(TEXT("services"), ServiceArr);

	// Children
	TArray<TSharedPtr<FJsonValue>> ChildArr;
	for (int32 i = 0; i < CompositeNode->Children.Num(); ++i)
	{
		const FBTCompositeChild& Child = CompositeNode->Children[i];

		TSharedRef<FJsonObject> ChildJson = MakeShared<FJsonObject>();

		// Decorators
		TArray<TSharedPtr<FJsonValue>> DecArr;
		for (UBTDecorator* Dec : Child.Decorators)
		{
			if (!Dec) continue;
			TSharedRef<FJsonObject> DecJson = MakeShared<FJsonObject>();
			DecJson->SetStringField(TEXT("name"), Dec->GetNodeName());
			DecJson->SetStringField(TEXT("class"), Dec->GetClass()->GetName());
			DecArr.Add(MakeShared<FJsonValueObject>(DecJson));
		}
		ChildJson->SetArrayField(TEXT("decorators"), DecArr);

		if (UBTCompositeNode* ChildComposite = Child.ChildComposite)
		{
			ChildJson->SetStringField(TEXT("type"), TEXT("composite"));
			ChildJson->SetObjectField(TEXT("node"), SerializeBTNode(ChildComposite, Depth + 1));
		}
		else if (UBTTaskNode* ChildTask = Child.ChildTask)
		{
			ChildJson->SetStringField(TEXT("type"), TEXT("task"));
			TSharedRef<FJsonObject> TaskJson = MakeShared<FJsonObject>();
			TaskJson->SetStringField(TEXT("name"), ChildTask->GetNodeName());
			TaskJson->SetStringField(TEXT("class"), ChildTask->GetClass()->GetName());

			// Task services
			TArray<TSharedPtr<FJsonValue>> TaskSvcArr;
			for (UBTService* Svc : ChildTask->Services)
			{
				if (!Svc) continue;
				TSharedRef<FJsonObject> SvcJson = MakeShared<FJsonObject>();
				SvcJson->SetStringField(TEXT("name"), Svc->GetNodeName());
				SvcJson->SetStringField(TEXT("class"), Svc->GetClass()->GetName());
				TaskSvcArr.Add(MakeShared<FJsonValueObject>(SvcJson));
			}
			TaskJson->SetArrayField(TEXT("services"), TaskSvcArr);

			ChildJson->SetObjectField(TEXT("node"), TaskJson);
		}

		ChildArr.Add(MakeShared<FJsonValueObject>(ChildJson));
	}
	NodeJson->SetArrayField(TEXT("children"), ChildArr);

	return NodeJson;
}
#endif

// ============================================================
// aiGetBehaviorTree - Get BT structure
// ============================================================
FString FAgenticMCPServer::HandleAIGetBehaviorTree(const FString& Body)
{
#if !HAS_AI_MODULE
	return MakeErrorJson(TEXT("AI module is not available in this build"));
#else
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	if (Name.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: name"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> BTAssets;
	AR.GetAssetsByClass(UBehaviorTree::StaticClass()->GetClassPathName(), BTAssets, true);

	UBehaviorTree* BT = nullptr;
	for (const FAssetData& Asset : BTAssets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			BT = Cast<UBehaviorTree>(Asset.GetAsset());
			break;
		}
	}
	if (!BT) return MakeErrorJson(FString::Printf(TEXT("Behavior Tree not found: %s"), *Name));

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("name"), BT->GetName());
	OutJson->SetStringField(TEXT("path"), BT->GetPathName());

	if (BT->BlackboardAsset)
	{
		OutJson->SetStringField(TEXT("blackboard"), BT->BlackboardAsset->GetName());
	}

	if (BT->RootNode)
	{
		OutJson->SetObjectField(TEXT("rootNode"), SerializeBTNode(BT->RootNode));
	}

	return JsonToString(OutJson);
#endif
}

// ============================================================
// aiListBlackboards - List all Blackboard Data assets
// ============================================================
FString FAgenticMCPServer::HandleAIListBlackboards(const FString& Body)
{
#if !HAS_AI_MODULE
	return MakeErrorJson(TEXT("AI module is not available in this build"));
#else
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> BBAssets;
	AR.GetAssetsByClass(UBlackboardData::StaticClass()->GetClassPathName(), BBAssets, true);

	TArray<TSharedPtr<FJsonValue>> BBArray;
	for (const FAssetData& Asset : BBAssets)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		BBArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetNumberField(TEXT("count"), BBArray.Num());
	OutJson->SetArrayField(TEXT("blackboards"), BBArray);
	return JsonToString(OutJson);
#endif
}

// ============================================================
// aiGetBlackboard - Get blackboard keys and types
// ============================================================
FString FAgenticMCPServer::HandleAIGetBlackboard(const FString& Body)
{
#if !HAS_AI_MODULE
	return MakeErrorJson(TEXT("AI module is not available in this build"));
#else
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	if (Name.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: name"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> BBAssets;
	AR.GetAssetsByClass(UBlackboardData::StaticClass()->GetClassPathName(), BBAssets, true);

	UBlackboardData* BB = nullptr;
	for (const FAssetData& Asset : BBAssets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			BB = Cast<UBlackboardData>(Asset.GetAsset());
			break;
		}
	}
	if (!BB) return MakeErrorJson(FString::Printf(TEXT("Blackboard not found: %s"), *Name));

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("name"), BB->GetName());
	OutJson->SetStringField(TEXT("path"), BB->GetPathName());

	if (BB->Parent)
	{
		OutJson->SetStringField(TEXT("parent"), BB->Parent->GetName());
	}

	TArray<TSharedPtr<FJsonValue>> KeyArray;
	for (const FBlackboardEntry& Key : BB->Keys)
	{
		TSharedRef<FJsonObject> KeyJson = MakeShared<FJsonObject>();
		KeyJson->SetStringField(TEXT("name"), Key.EntryName.ToString());
		if (Key.KeyType)
		{
			KeyJson->SetStringField(TEXT("type"), Key.KeyType->GetClass()->GetName());
		}
		KeyJson->SetBoolField(TEXT("instanceSynced"), Key.bInstanceSynced);
		KeyArray.Add(MakeShared<FJsonValueObject>(KeyJson));
	}
	OutJson->SetNumberField(TEXT("keyCount"), KeyArray.Num());
	OutJson->SetArrayField(TEXT("keys"), KeyArray);

	return JsonToString(OutJson);
#endif
}

// ============================================================
// aiListControllers - List AI controllers in the level
// ============================================================
FString FAgenticMCPServer::HandleAIListControllers(const FString& Body)
{
#if !HAS_AI_MODULE
	return MakeErrorJson(TEXT("AI module is not available in this build"));
#else
	UWorld* World = nullptr;
	if (GEditor) World = GEditor->GetEditorWorldContext().World();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	TArray<TSharedPtr<FJsonValue>> ControllerArr;
	for (TActorIterator<AAIController> It(World); It; ++It)
	{
		AAIController* AIC = *It;
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), AIC->GetActorLabel());
		Entry->SetStringField(TEXT("class"), AIC->GetClass()->GetName());

		APawn* Pawn = AIC->GetPawn();
		if (Pawn)
		{
			Entry->SetStringField(TEXT("pawn"), Pawn->GetActorLabel());
			Entry->SetStringField(TEXT("pawnClass"), Pawn->GetClass()->GetName());
		}

		// Check for BT component
		UBehaviorTreeComponent* BTComp = Cast<UBehaviorTreeComponent>(AIC->GetBrainComponent());
		if (BTComp)
		{
			Entry->SetBoolField(TEXT("hasBehaviorTree"), true);
			if (BTComp->GetCurrentTree())
			{
				Entry->SetStringField(TEXT("currentTree"), BTComp->GetCurrentTree()->GetName());
			}
		}
		else
		{
			Entry->SetBoolField(TEXT("hasBehaviorTree"), false);
		}

		ControllerArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetNumberField(TEXT("count"), ControllerArr.Num());
	OutJson->SetArrayField(TEXT("controllers"), ControllerArr);
	return JsonToString(OutJson);
#endif
}

// ============================================================
// aiGetEQSQueries - List all EQS query assets
// ============================================================
FString FAgenticMCPServer::HandleAIGetEQSQueries(const FString& Body)
{
#if !HAS_EQS
	return MakeErrorJson(TEXT("EQS module is not available in this build"));
#else
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> EQSAssets;
	AR.GetAssetsByClass(UEnvQuery::StaticClass()->GetClassPathName(), EQSAssets, true);

	TArray<TSharedPtr<FJsonValue>> EQSArray;
	for (const FAssetData& Asset : EQSAssets)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		EQSArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetNumberField(TEXT("count"), EQSArray.Num());
	OutJson->SetArrayField(TEXT("queries"), EQSArray);
	return JsonToString(OutJson);
#endif
}
