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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), BPArray.Num());
	Result->SetArrayField(TEXT("animBlueprints"), BPArray);
	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), AnimBP->GetName());
	Result->SetStringField(TEXT("path"), AnimBP->GetPathName());

	if (AnimBP->TargetSkeleton)
	{
		Result->SetStringField(TEXT("skeleton"), AnimBP->TargetSkeleton->GetName());
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
				if (UAnimationStateMachineGraph* SMGraph = SMNode->GetStateMachineGraph())
				{
					NodeJson->SetNumberField(TEXT("stateCount"), SMGraph->Nodes.Num());
				}
			}

			NodeArr.Add(MakeShared<FJsonValueObject>(NodeJson));
		}
		GraphJson->SetArrayField(TEXT("nodes"), NodeArr);

		GraphArray.Add(MakeShared<FJsonValueObject>(GraphJson));
	}
	Result->SetArrayField(TEXT("graphs"), GraphArray);

	return JsonToString(Result);
}

// ============================================================
// animBPListMontages - List all AnimMontage assets
// ============================================================
FString FAgenticMCPServer::HandleAnimBPListMontages(const FString& Body)
{
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), MontageArray.Num());
	Result->SetArrayField(TEXT("montages"), MontageArray);
	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), Montage->GetName());
	Result->SetStringField(TEXT("path"), Montage->GetPathName());
	Result->SetNumberField(TEXT("sequenceLength"), Montage->GetPlayLength());
	Result->SetNumberField(TEXT("numSections"), Montage->CompositeSections.Num());

	// Sections
	TArray<TSharedPtr<FJsonValue>> SectionArray;
	for (const FCompositeSection& Section : Montage->CompositeSections)
	{
		TSharedRef<FJsonObject> SecJson = MakeShared<FJsonObject>();
		SecJson->SetStringField(TEXT("name"), Section.SectionName.ToString());
		SecJson->SetNumberField(TEXT("startTime"), Section.GetTime());
		SectionArray.Add(MakeShared<FJsonValueObject>(SecJson));
	}
	Result->SetArrayField(TEXT("sections"), SectionArray);

	// Slot groups
	TArray<TSharedPtr<FJsonValue>> SlotArray;
	for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
	{
		TSharedRef<FJsonObject> SlotJson = MakeShared<FJsonObject>();
		SlotJson->SetStringField(TEXT("slotName"), Slot.SlotName.ToString());
		SlotArray.Add(MakeShared<FJsonValueObject>(SlotJson));
	}
	Result->SetArrayField(TEXT("slots"), SlotArray);

	return JsonToString(Result);
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

			UAnimationStateMachineGraph* SMGraph = SMNode->GetStateMachineGraph();
			if (!SMGraph) continue;

			for (UEdGraphNode* SMChild : SMGraph->Nodes)
			{
				if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(SMChild))
				{
					TSharedRef<FJsonObject> StateJson = MakeShared<FJsonObject>();
					StateJson->SetStringField(TEXT("name"), StateNode->GetStateName());
					StateJson->SetStringField(TEXT("guid"), StateNode->NodeGuid.ToString());
					StateJson->SetNumberField(TEXT("posX"), StateNode->NodePosX);
					StateJson->SetNumberField(TEXT("posY"), StateNode->NodePosY);
					StateArray.Add(MakeShared<FJsonValueObject>(StateJson));
				}
				else if (UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(SMChild))
				{
					TSharedRef<FJsonObject> TransJson = MakeShared<FJsonObject>();
					TransJson->SetStringField(TEXT("guid"), TransNode->NodeGuid.ToString());

					if (UAnimStateNodeBase* Prev = TransNode->GetPreviousState())
						TransJson->SetStringField(TEXT("fromState"), Prev->GetStateName());
					if (UAnimStateNodeBase* Next = TransNode->GetNextState())
						TransJson->SetStringField(TEXT("toState"), Next->GetStateName());

					TransitionArray.Add(MakeShared<FJsonValueObject>(TransJson));
				}
			}
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("animBlueprint"), BPName);
	Result->SetNumberField(TEXT("stateCount"), StateArray.Num());
	Result->SetArrayField(TEXT("states"), StateArray);
	Result->SetNumberField(TEXT("transitionCount"), TransitionArray.Num());
	Result->SetArrayField(TEXT("transitions"), TransitionArray);
	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), BlendSpace->GetName());
	Result->SetStringField(TEXT("path"), BlendSpace->GetPathName());
	Result->SetStringField(TEXT("axisX"), BlendSpace->GetBlendParameter(0).DisplayName);
	Result->SetStringField(TEXT("axisY"), BlendSpace->GetBlendParameter(1).DisplayName);
	Result->SetNumberField(TEXT("numSamples"), BlendSpace->GetBlendSamples().Num());

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
	Result->SetArrayField(TEXT("samples"), SampleArray);

	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("animBlueprint"), BPName);
	Result->SetStringField(TEXT("skeleton"), Skeleton->GetName());
	Result->SetNumberField(TEXT("groupCount"), GroupArray.Num());
	Result->SetArrayField(TEXT("slotGroups"), GroupArray);
	return JsonToString(Result);
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

			UAnimationStateMachineGraph* SMGraph = SMNode->GetStateMachineGraph();
			if (!SMGraph) continue;

			for (UEdGraphNode* SMChild : SMGraph->Nodes)
			{
				UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(SMChild);
				if (!TransNode) continue;

				TSharedRef<FJsonObject> TransJson = MakeShared<FJsonObject>();
				TransJson->SetStringField(TEXT("guid"), TransNode->NodeGuid.ToString());

				if (UAnimStateNodeBase* Prev = TransNode->GetPreviousState())
					TransJson->SetStringField(TEXT("fromState"), Prev->GetStateName());
				if (UAnimStateNodeBase* Next = TransNode->GetNextState())
					TransJson->SetStringField(TEXT("toState"), Next->GetStateName());

				// Transition rule info
				TransJson->SetBoolField(TEXT("bidirectional"), TransNode->Bidirectional);

				// Get the transition graph for rule details
				if (UEdGraph* TransGraph = TransNode->GetBoundGraph())
				{
					TransJson->SetStringField(TEXT("ruleGraph"), TransGraph->GetName());
					TransJson->SetNumberField(TEXT("ruleNodeCount"), TransGraph->Nodes.Num());
				}

				TransitionArray.Add(MakeShared<FJsonValueObject>(TransJson));
			}
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("animBlueprint"), BPName);
	Result->SetNumberField(TEXT("transitionCount"), TransitionArray.Num());
	Result->SetArrayField(TEXT("transitions"), TransitionArray);
	return JsonToString(Result);
}
