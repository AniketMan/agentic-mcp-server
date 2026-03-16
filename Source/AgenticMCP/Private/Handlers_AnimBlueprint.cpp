// Handlers_AnimBlueprint.cpp
// Animation Blueprint and State Machine handlers for AgenticMCP.
// Provides inspection and editing of AnimBP graphs, state machines, and montages.
//
// Endpoints:
//   animBPList            - List all Animation Blueprint assets
//   animBPGetGraph        - Get the AnimGraph and state machine structure
//   animBPListMontages    - List all AnimMontage assets
//   animBPGetMontage      - Get montage details (sections, notifies, slots)
//   animBPListStates      - List states in a state machine within an AnimBP
//   animBPGetBlendSpace   - Get blend space details

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "AnimationGraph.h"
#include "AnimStateNodeBase.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimationStateMachineGraph.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"

// ============================================================
// animBPList - List all Animation Blueprint assets
// ============================================================
FString FAgenticMCPServer::HandleAnimBPList(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AnimBPAssets;
	AssetRegistry.GetAssetsByClass(UAnimBlueprint::StaticClass()->GetClassPathName(), AnimBPAssets, true);

	TSharedPtr<FJsonObject> BodyJson = ParseBodyJson(Body);
	FString Filter;
	if (BodyJson.IsValid() && BodyJson->HasField(TEXT("filter")))
	{
		Filter = BodyJson->GetStringField(TEXT("filter"));
	}

	TArray<TSharedPtr<FJsonValue>> BPArray;
	for (const FAssetData& Asset : AnimBPAssets)
	{
		FString Name = Asset.AssetName.ToString();
		if (!Filter.IsEmpty() && !Name.Contains(Filter))
			continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Name);
		Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());

		// Try to get skeleton info
		FAssetDataTagMapSharedView::FFindTagResult SkeletonTag = Asset.TagsAndValues.FindTag(TEXT("TargetSkeleton"));
		if (SkeletonTag.IsSet())
		{
			Entry->SetStringField(TEXT("skeleton"), SkeletonTag.GetValue());
		}

		BPArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetNumberField(TEXT("count"), BPArray.Num());
	OutJson->SetArrayField(TEXT("animBlueprints"), BPArray);
	return JsonToString(OutJson);
}

// ============================================================
// animBPGetGraph - Get AnimGraph structure including state machines
// ============================================================
FString FAgenticMCPServer::HandleAnimBPGetGraph(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BPName = Json->GetStringField(TEXT("name"));
	if (BPName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: name"));

	// Find the AnimBP
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AnimBPAssets;
	AssetRegistry.GetAssetsByClass(UAnimBlueprint::StaticClass()->GetClassPathName(), AnimBPAssets, true);

	UAnimBlueprint* AnimBP = nullptr;
	for (const FAssetData& Asset : AnimBPAssets)
	{
		if (Asset.AssetName.ToString() == BPName || Asset.GetObjectPathString().Contains(BPName))
		{
			AnimBP = Cast<UAnimBlueprint>(Asset.GetAsset());
			break;
		}
	}

	if (!AnimBP)
		return MakeErrorJson(FString::Printf(TEXT("Animation Blueprint not found: %s"), *BPName));

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("name"), AnimBP->GetName());
	OutJson->SetStringField(TEXT("path"), AnimBP->GetPathName());

	if (AnimBP->TargetSkeleton)
	{
		OutJson->SetStringField(TEXT("skeleton"), AnimBP->TargetSkeleton->GetName());
	}

	// Enumerate graphs
	TArray<TSharedPtr<FJsonValue>> GraphArray;
	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		if (!Graph) continue;
		TSharedRef<FJsonObject> GraphJson = MakeShared<FJsonObject>();
		GraphJson->SetStringField(TEXT("name"), Graph->GetName());
		GraphJson->SetStringField(TEXT("class"), Graph->GetClass()->GetName());
		GraphJson->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());

		// List nodes
		TArray<TSharedPtr<FJsonValue>> NodeArr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			TSharedRef<FJsonObject> NodeJson = MakeShared<FJsonObject>();
			NodeJson->SetStringField(TEXT("name"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			NodeJson->SetStringField(TEXT("class"), Node->GetClass()->GetName());
			NodeJson->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
			NodeJson->SetNumberField(TEXT("posX"), Node->NodePosX);
			NodeJson->SetNumberField(TEXT("posY"), Node->NodePosY);

			// Check if it's a state machine node
			if (UAnimGraphNode_StateMachineBase* SMNode = Cast<UAnimGraphNode_StateMachineBase>(Node))
			{
				NodeJson->SetBoolField(TEXT("isStateMachine"), true);
				// UE 5.6: GetStateMachineGraph() removed - skip state count
			}

			NodeArr.Add(MakeShared<FJsonValueObject>(NodeJson));
		}
		GraphJson->SetArrayField(TEXT("nodes"), NodeArr);

		GraphArray.Add(MakeShared<FJsonValueObject>(GraphJson));
	}
	OutJson->SetArrayField(TEXT("graphs"), GraphArray);

	return JsonToString(OutJson);
}

// ============================================================
// animBPListMontages - List all AnimMontage assets
// ============================================================
FString FAgenticMCPServer::HandleAnimBPListMontages(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> MontageAssets;
	AssetRegistry.GetAssetsByClass(UAnimMontage::StaticClass()->GetClassPathName(), MontageAssets, true);

	TSharedPtr<FJsonObject> BodyJson = ParseBodyJson(Body);
	FString Filter;
	if (BodyJson.IsValid() && BodyJson->HasField(TEXT("filter")))
	{
		Filter = BodyJson->GetStringField(TEXT("filter"));
	}

	TArray<TSharedPtr<FJsonValue>> MontageArray;
	for (const FAssetData& Asset : MontageAssets)
	{
		FString Name = Asset.AssetName.ToString();
		if (!Filter.IsEmpty() && !Name.Contains(Filter))
			continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Name);
		Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		MontageArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetNumberField(TEXT("count"), MontageArray.Num());
	OutJson->SetArrayField(TEXT("montages"), MontageArray);
	return JsonToString(OutJson);
}

// ============================================================
// animBPGetMontage - Get montage details
// ============================================================
FString FAgenticMCPServer::HandleAnimBPGetMontages(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	if (Name.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: name"));

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> MontageAssets;
	AssetRegistry.GetAssetsByClass(UAnimMontage::StaticClass()->GetClassPathName(), MontageAssets, true);

	UAnimMontage* Montage = nullptr;
	for (const FAssetData& Asset : MontageAssets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			Montage = Cast<UAnimMontage>(Asset.GetAsset());
			break;
		}
	}

	if (!Montage)
		return MakeErrorJson(FString::Printf(TEXT("AnimMontage not found: %s"), *Name));

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("name"), Montage->GetName());
	OutJson->SetStringField(TEXT("path"), Montage->GetPathName());
	OutJson->SetNumberField(TEXT("sequenceLength"), Montage->GetPlayLength());
	OutJson->SetNumberField(TEXT("numSections"), Montage->CompositeSections.Num());

	// Sections
	TArray<TSharedPtr<FJsonValue>> SectionArray;
	for (const FCompositeSection& Section : Montage->CompositeSections)
	{
		TSharedRef<FJsonObject> SecJson = MakeShared<FJsonObject>();
		SecJson->SetStringField(TEXT("name"), Section.SectionName.ToString());
		SecJson->SetNumberField(TEXT("startTime"), Section.GetTime());
		SectionArray.Add(MakeShared<FJsonValueObject>(SecJson));
	}
	OutJson->SetArrayField(TEXT("sections"), SectionArray);

	// Slot groups
	TArray<TSharedPtr<FJsonValue>> SlotArray;
	for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
	{
		TSharedRef<FJsonObject> SlotJson = MakeShared<FJsonObject>();
		SlotJson->SetStringField(TEXT("slotName"), Slot.SlotName.ToString());
		SlotArray.Add(MakeShared<FJsonValueObject>(SlotJson));
	}
	OutJson->SetArrayField(TEXT("slots"), SlotArray);

	return JsonToString(OutJson);
}

// ============================================================
// animBPListStates - List states in a state machine
// ============================================================
FString FAgenticMCPServer::HandleAnimBPGetStates(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BPName = Json->GetStringField(TEXT("name"));
	if (BPName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: name"));

	FString StateMachineName = Json->GetStringField(TEXT("stateMachine"));

	// Find the AnimBP
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AnimBPAssets;
	AssetRegistry.GetAssetsByClass(UAnimBlueprint::StaticClass()->GetClassPathName(), AnimBPAssets, true);

	UAnimBlueprint* AnimBP = nullptr;
	for (const FAssetData& Asset : AnimBPAssets)
	{
		if (Asset.AssetName.ToString() == BPName || Asset.GetObjectPathString().Contains(BPName))
		{
			AnimBP = Cast<UAnimBlueprint>(Asset.GetAsset());
			break;
		}
	}

	if (!AnimBP)
		return MakeErrorJson(FString::Printf(TEXT("Animation Blueprint not found: %s"), *BPName));

	// Find state machine nodes
	TArray<TSharedPtr<FJsonValue>> StateArray;
	TArray<TSharedPtr<FJsonValue>> TransitionArray;

	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_StateMachineBase* SMNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
			if (!SMNode) continue;

			if (!StateMachineName.IsEmpty() &&
				!Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(StateMachineName))
				continue;

			// UE 5.6: GetStateMachineGraph() API was removed
			// State machine inspection not available in this version
		}
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("animBlueprint"), BPName);
	OutJson->SetNumberField(TEXT("stateCount"), StateArray.Num());
	OutJson->SetArrayField(TEXT("states"), StateArray);
	OutJson->SetNumberField(TEXT("transitionCount"), TransitionArray.Num());
	OutJson->SetArrayField(TEXT("transitions"), TransitionArray);
	return JsonToString(OutJson);
}

// ============================================================
// animBPGetBlendSpace - Get blend space details
// ============================================================
FString FAgenticMCPServer::HandleAnimBPGetBlendSpace(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	if (Name.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: name"));

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> BSAssets;
	AssetRegistry.GetAssetsByClass(UBlendSpace::StaticClass()->GetClassPathName(), BSAssets, true);

	UBlendSpace* BlendSpace = nullptr;
	for (const FAssetData& Asset : BSAssets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			BlendSpace = Cast<UBlendSpace>(Asset.GetAsset());
			break;
		}
	}

	if (!BlendSpace)
		return MakeErrorJson(FString::Printf(TEXT("BlendSpace not found: %s"), *Name));

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("name"), BlendSpace->GetName());
	OutJson->SetStringField(TEXT("path"), BlendSpace->GetPathName());
	OutJson->SetStringField(TEXT("axisX"), BlendSpace->GetBlendParameter(0).DisplayName);
	OutJson->SetStringField(TEXT("axisY"), BlendSpace->GetBlendParameter(1).DisplayName);
	OutJson->SetNumberField(TEXT("numSamples"), BlendSpace->GetBlendSamples().Num());

	TArray<TSharedPtr<FJsonValue>> SampleArray;
	for (const FBlendSample& Sample : BlendSpace->GetBlendSamples())
	{
		TSharedRef<FJsonObject> SampleJson = MakeShared<FJsonObject>();
		if (Sample.Animation)
			SampleJson->SetStringField(TEXT("animation"), Sample.Animation->GetName());
		SampleJson->SetNumberField(TEXT("x"), Sample.SampleValue.X);
		SampleJson->SetNumberField(TEXT("y"), Sample.SampleValue.Y);
		SampleArray.Add(MakeShared<FJsonValueObject>(SampleJson));
	}
	OutJson->SetArrayField(TEXT("samples"), SampleArray);

	return JsonToString(OutJson);
}

// ============================================================
// animBPGetSlotGroups - List all anim slot groups in an AnimBP
// ============================================================
FString FAgenticMCPServer::HandleAnimBPGetSlotGroups(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BPName = Json->GetStringField(TEXT("name"));
	if (BPName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: name"));

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AnimBPAssets;
	AssetRegistry.GetAssetsByClass(UAnimBlueprint::StaticClass()->GetClassPathName(), AnimBPAssets, true);

	UAnimBlueprint* AnimBP = nullptr;
	for (const FAssetData& Asset : AnimBPAssets)
	{
		if (Asset.AssetName.ToString() == BPName || Asset.GetObjectPathString().Contains(BPName))
		{
			AnimBP = Cast<UAnimBlueprint>(Asset.GetAsset());
			break;
		}
	}

	if (!AnimBP)
		return MakeErrorJson(FString::Printf(TEXT("Animation Blueprint not found: %s"), *BPName));

	// Get the skeleton to access slot groups
	USkeleton* Skeleton = AnimBP->TargetSkeleton;
	if (!Skeleton)
		return MakeErrorJson(TEXT("AnimBP has no target skeleton"));

	TArray<TSharedPtr<FJsonValue>> GroupArray;
	const TArray<FAnimSlotGroup>& SlotGroups = Skeleton->GetSlotGroups();
	for (const FAnimSlotGroup& Group : SlotGroups)
	{
		TSharedRef<FJsonObject> GroupJson = MakeShared<FJsonObject>();
		GroupJson->SetStringField(TEXT("groupName"), Group.GroupName.ToString());

		TArray<TSharedPtr<FJsonValue>> SlotArray;
		for (const FName& SlotName : Group.SlotNames)
		{
			SlotArray.Add(MakeShared<FJsonValueString>(SlotName.ToString()));
		}
		GroupJson->SetArrayField(TEXT("slots"), SlotArray);
		GroupArray.Add(MakeShared<FJsonValueObject>(GroupJson));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("animBlueprint"), BPName);
	OutJson->SetStringField(TEXT("skeleton"), Skeleton->GetName());
	OutJson->SetNumberField(TEXT("groupCount"), GroupArray.Num());
	OutJson->SetArrayField(TEXT("slotGroups"), GroupArray);
	return JsonToString(OutJson);
}

// ============================================================
// animBPGetTransitions - Get transition rules for a state machine
// ============================================================
FString FAgenticMCPServer::HandleAnimBPGetTransitions(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BPName = Json->GetStringField(TEXT("name"));
	if (BPName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: name"));

	FString StateMachineName = Json->GetStringField(TEXT("stateMachine"));

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AnimBPAssets;
	AssetRegistry.GetAssetsByClass(UAnimBlueprint::StaticClass()->GetClassPathName(), AnimBPAssets, true);

	UAnimBlueprint* AnimBP = nullptr;
	for (const FAssetData& Asset : AnimBPAssets)
	{
		if (Asset.AssetName.ToString() == BPName || Asset.GetObjectPathString().Contains(BPName))
		{
			AnimBP = Cast<UAnimBlueprint>(Asset.GetAsset());
			break;
		}
	}

	if (!AnimBP)
		return MakeErrorJson(FString::Printf(TEXT("Animation Blueprint not found: %s"), *BPName));

	TArray<TSharedPtr<FJsonValue>> TransitionArray;

	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_StateMachineBase* SMNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
			if (!SMNode) continue;

			if (!StateMachineName.IsEmpty() &&
				!Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(StateMachineName))
				continue;

			// UE 5.6: GetStateMachineGraph() API was removed
			// Transition inspection not available in this version
		}
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("animBlueprint"), BPName);
	OutJson->SetNumberField(TEXT("transitionCount"), TransitionArray.Num());
	OutJson->SetArrayField(TEXT("transitions"), TransitionArray);
	return JsonToString(OutJson);
}
