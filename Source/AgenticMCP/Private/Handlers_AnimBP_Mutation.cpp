// Handlers_AnimBP_Mutation.cpp
// Animation Blueprint mutation handlers for AgenticMCP.
// Target: UE 5.4 - 5.6
//
// Endpoints:
//   animBPAddState           - Add a state to a state machine
//   animBPRemoveState        - Remove a state from a state machine
//   animBPAddTransition      - Add a transition rule between states
//   animBPSetTransitionRule  - Set a transition rule condition
//   animBPAddBlendNode       - Add a blend space or blend node
//   animBPGetStateMachine    - Get full state machine graph (states, transitions)
//   animBPSetStateAnimation  - Assign an animation sequence to a state

#include "AgenticMCPServer.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateConduitNode.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"

// Helper: find an AnimBlueprint by name
static UAnimBlueprint* FindAnimBlueprintByName(const FString& Name)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> Assets;
	AR.GetAssetsByClass(UAnimBlueprint::StaticClass()->GetClassPathName(), Assets, true);

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			return Cast<UAnimBlueprint>(Asset.GetAsset());
		}
	}
	return nullptr;
}

// Helper: find the state machine graph in an AnimBP
static UAnimationStateMachineGraph* FindStateMachineGraph(UAnimBlueprint* AnimBP, const FString& StateMachineName)
{
	if (!AnimBP) return nullptr;

	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node);
			if (SMNode)
			{
				UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
				if (SMGraph)
				{
					if (StateMachineName.IsEmpty() || SMNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(StateMachineName))
					{
						return SMGraph;
					}
				}
			}
		}
	}
	return nullptr;
}

// Helper: find a state node by name in a state machine graph
static UAnimStateNode* FindStateNode(UAnimationStateMachineGraph* SMGraph, const FString& StateName)
{
	if (!SMGraph) return nullptr;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node);
		if (StateNode && StateNode->GetStateName() == StateName)
		{
			return StateNode;
		}
	}
	return nullptr;
}

// ============================================================
// animBPAddState - Add a state to a state machine
// Params: animBlueprintName, stateMachineName (opt, uses first found),
//         stateName, posX (opt), posY (opt)
// ============================================================
FString FAgenticMCPServer::HandleAnimBPAddState(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ABPName = Json->GetStringField(TEXT("animBlueprintName"));
	FString SMName = Json->GetStringField(TEXT("stateMachineName"));
	FString StateName = Json->GetStringField(TEXT("stateName"));

	if (ABPName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: animBlueprintName"));
	if (StateName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: stateName"));

	int32 PosX = Json->HasField(TEXT("posX")) ? (int32)Json->GetNumberField(TEXT("posX")) : 200;
	int32 PosY = Json->HasField(TEXT("posY")) ? (int32)Json->GetNumberField(TEXT("posY")) : 0;

	UAnimBlueprint* AnimBP = FindAnimBlueprintByName(ABPName);
	if (!AnimBP) return MakeErrorJson(FString::Printf(TEXT("AnimBlueprint not found: %s"), *ABPName));

	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, SMName);
	if (!SMGraph) return MakeErrorJson(TEXT("No state machine graph found. Ensure the AnimBP has a state machine."));

	// Check if state already exists
	if (FindStateNode(SMGraph, StateName))
		return MakeErrorJson(FString::Printf(TEXT("State '%s' already exists"), *StateName));

	// Create new state node
	FGraphNodeCreator<UAnimStateNode> NodeCreator(*SMGraph);
	UAnimStateNode* NewState = NodeCreator.CreateNode(true);
	if (!NewState)
		return MakeErrorJson(TEXT("Failed to create state node"));

	NewState->NodePosX = PosX;
	NewState->NodePosY = PosY;
	NodeCreator.Finalize();

	// Rename the state
	NewState->Rename(*StateName);

	FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBP);
	AnimBP->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("animBlueprintName"), ABPName);
	Result->SetStringField(TEXT("stateName"), StateName);
	Result->SetNumberField(TEXT("posX"), PosX);
	Result->SetNumberField(TEXT("posY"), PosY);
	return JsonToString(Result);
}

// ============================================================
// animBPRemoveState - Remove a state from a state machine
// Params: animBlueprintName, stateMachineName (opt), stateName
// ============================================================
FString FAgenticMCPServer::HandleAnimBPRemoveState(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ABPName = Json->GetStringField(TEXT("animBlueprintName"));
	FString SMName = Json->GetStringField(TEXT("stateMachineName"));
	FString StateName = Json->GetStringField(TEXT("stateName"));

	if (ABPName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: animBlueprintName"));
	if (StateName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: stateName"));

	UAnimBlueprint* AnimBP = FindAnimBlueprintByName(ABPName);
	if (!AnimBP) return MakeErrorJson(FString::Printf(TEXT("AnimBlueprint not found: %s"), *ABPName));

	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, SMName);
	if (!SMGraph) return MakeErrorJson(TEXT("No state machine graph found"));

	UAnimStateNode* StateNode = FindStateNode(SMGraph, StateName);
	if (!StateNode) return MakeErrorJson(FString::Printf(TEXT("State not found: %s"), *StateName));

	// Remove all connected transitions first
	TArray<UEdGraphPin*> Pins = StateNode->Pins;
	for (UEdGraphPin* Pin : Pins)
	{
		Pin->BreakAllPinLinks();
	}

	SMGraph->RemoveNode(StateNode);
	FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBP);
	AnimBP->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("animBlueprintName"), ABPName);
	Result->SetStringField(TEXT("removedState"), StateName);
	return JsonToString(Result);
}

// ============================================================
// animBPAddTransition - Add a transition between two states
// Params: animBlueprintName, stateMachineName (opt),
//         fromState, toState
// ============================================================
FString FAgenticMCPServer::HandleAnimBPAddTransition(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ABPName = Json->GetStringField(TEXT("animBlueprintName"));
	FString SMName = Json->GetStringField(TEXT("stateMachineName"));
	FString FromState = Json->GetStringField(TEXT("fromState"));
	FString ToState = Json->GetStringField(TEXT("toState"));

	if (ABPName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: animBlueprintName"));
	if (FromState.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: fromState"));
	if (ToState.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: toState"));

	UAnimBlueprint* AnimBP = FindAnimBlueprintByName(ABPName);
	if (!AnimBP) return MakeErrorJson(FString::Printf(TEXT("AnimBlueprint not found: %s"), *ABPName));

	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, SMName);
	if (!SMGraph) return MakeErrorJson(TEXT("No state machine graph found"));

	UAnimStateNode* FromNode = FindStateNode(SMGraph, FromState);
	UAnimStateNode* ToNode = FindStateNode(SMGraph, ToState);

	if (!FromNode) return MakeErrorJson(FString::Printf(TEXT("From state not found: %s"), *FromState));
	if (!ToNode) return MakeErrorJson(FString::Printf(TEXT("To state not found: %s"), *ToState));

	// Create transition node
	FGraphNodeCreator<UAnimStateTransitionNode> NodeCreator(*SMGraph);
	UAnimStateTransitionNode* TransNode = NodeCreator.CreateNode(true);
	if (!TransNode)
		return MakeErrorJson(TEXT("Failed to create transition node"));

	// Position between the two states
	TransNode->NodePosX = (FromNode->NodePosX + ToNode->NodePosX) / 2;
	TransNode->NodePosY = (FromNode->NodePosY + ToNode->NodePosY) / 2;
	NodeCreator.Finalize();

	// Connect: FromNode output -> TransNode input, TransNode output -> ToNode input
	UEdGraphPin* FromOutputPin = nullptr;
	UEdGraphPin* ToInputPin = nullptr;
	UEdGraphPin* TransInputPin = nullptr;
	UEdGraphPin* TransOutputPin = nullptr;

	for (UEdGraphPin* Pin : FromNode->Pins)
	{
		if (Pin->Direction == EGPD_Output) { FromOutputPin = Pin; break; }
	}
	for (UEdGraphPin* Pin : ToNode->Pins)
	{
		if (Pin->Direction == EGPD_Input) { ToInputPin = Pin; break; }
	}
	for (UEdGraphPin* Pin : TransNode->Pins)
	{
		if (Pin->Direction == EGPD_Input) TransInputPin = Pin;
		else if (Pin->Direction == EGPD_Output) TransOutputPin = Pin;
	}

	if (FromOutputPin && TransInputPin)
		FromOutputPin->MakeLinkTo(TransInputPin);
	if (TransOutputPin && ToInputPin)
		TransOutputPin->MakeLinkTo(ToInputPin);

	FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBP);
	AnimBP->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("animBlueprintName"), ABPName);
	Result->SetStringField(TEXT("fromState"), FromState);
	Result->SetStringField(TEXT("toState"), ToState);
	return JsonToString(Result);
}

// ============================================================
// animBPSetTransitionRule - Set a transition rule condition
// Params: animBlueprintName, stateMachineName (opt),
//         fromState, toState,
//         conditionType (bool, timeRemaining, automatic),
//         boolVariableName (for bool type),
//         timeRemaining (for timeRemaining type)
// ============================================================
FString FAgenticMCPServer::HandleAnimBPSetTransitionRule(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ABPName = Json->GetStringField(TEXT("animBlueprintName"));
	FString SMName = Json->GetStringField(TEXT("stateMachineName"));
	FString FromState = Json->GetStringField(TEXT("fromState"));
	FString ToState = Json->GetStringField(TEXT("toState"));
	FString CondType = Json->HasField(TEXT("conditionType")) ? Json->GetStringField(TEXT("conditionType")) : TEXT("automatic");

	if (ABPName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: animBlueprintName"));
	if (FromState.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: fromState"));
	if (ToState.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: toState"));

	UAnimBlueprint* AnimBP = FindAnimBlueprintByName(ABPName);
	if (!AnimBP) return MakeErrorJson(FString::Printf(TEXT("AnimBlueprint not found: %s"), *ABPName));

	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, SMName);
	if (!SMGraph) return MakeErrorJson(TEXT("No state machine graph found"));

	// Find the transition node between fromState and toState
	UAnimStateTransitionNode* TransNode = nullptr;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateTransitionNode* TN = Cast<UAnimStateTransitionNode>(Node);
		if (!TN) continue;

		UAnimStateNode* Prev = TN->GetPreviousState();
		UAnimStateNode* Next = TN->GetNextState();
		if (Prev && Next &&
			Prev->GetStateName() == FromState &&
			Next->GetStateName() == ToState)
		{
			TransNode = TN;
			break;
		}
	}

	if (!TransNode)
		return MakeErrorJson(FString::Printf(TEXT("No transition found from '%s' to '%s'"), *FromState, *ToState));

	// Set transition properties based on condition type
	if (CondType == TEXT("automatic"))
	{
		TransNode->bAutomaticRuleBasedOnSequencePlayerInState = true;
	}
	else if (CondType == TEXT("timeRemaining"))
	{
		TransNode->bAutomaticRuleBasedOnSequencePlayerInState = true;
		// The actual time remaining threshold is set in the transition graph
		// via a "Time Remaining" node. We note this for the caller.
	}
	else if (CondType == TEXT("bool"))
	{
		TransNode->bAutomaticRuleBasedOnSequencePlayerInState = false;
		// Bool-based transitions require a variable getter in the transition graph.
		// The caller should use addNode to add the bool variable getter.
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBP);
	AnimBP->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("animBlueprintName"), ABPName);
	Result->SetStringField(TEXT("fromState"), FromState);
	Result->SetStringField(TEXT("toState"), ToState);
	Result->SetStringField(TEXT("conditionType"), CondType);
	if (CondType == TEXT("bool"))
	{
		FString BoolVar = Json->GetStringField(TEXT("boolVariableName"));
		Result->SetStringField(TEXT("boolVariableName"), BoolVar);
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("Transition set to bool-based. Use 'addNode' in the transition graph to add a 'Get %s' node "
				 "and connect it to the transition result."), *BoolVar));
	}
	else if (CondType == TEXT("automatic"))
	{
		Result->SetStringField(TEXT("note"), TEXT("Transition set to automatic (fires when animation completes)."));
	}
	return JsonToString(Result);
}

// ============================================================
// animBPSetStateAnimation - Assign an animation sequence to a state
// Params: animBlueprintName, stateMachineName (opt),
//         stateName, animationAssetPath, looping (opt, default true)
// ============================================================
FString FAgenticMCPServer::HandleAnimBPSetStateAnimation(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ABPName = Json->GetStringField(TEXT("animBlueprintName"));
	FString SMName = Json->GetStringField(TEXT("stateMachineName"));
	FString StateName = Json->GetStringField(TEXT("stateName"));
	FString AnimPath = Json->GetStringField(TEXT("animationAssetPath"));

	if (ABPName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: animBlueprintName"));
	if (StateName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: stateName"));
	if (AnimPath.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: animationAssetPath"));

	bool bLooping = Json->HasField(TEXT("looping")) ? Json->GetBoolField(TEXT("looping")) : true;

	UAnimBlueprint* AnimBP = FindAnimBlueprintByName(ABPName);
	if (!AnimBP) return MakeErrorJson(FString::Printf(TEXT("AnimBlueprint not found: %s"), *ABPName));

	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, SMName);
	if (!SMGraph) return MakeErrorJson(TEXT("No state machine graph found"));

	UAnimStateNode* StateNode = FindStateNode(SMGraph, StateName);
	if (!StateNode) return MakeErrorJson(FString::Printf(TEXT("State not found: %s"), *StateName));

	// Load animation sequence
	UAnimSequence* AnimSeq = Cast<UAnimSequence>(StaticLoadObject(UAnimSequence::StaticClass(), nullptr, *AnimPath));
	if (!AnimSeq) return MakeErrorJson(FString::Printf(TEXT("Animation sequence not found: %s"), *AnimPath));

	// Get the state's bound graph (inner graph)
	UEdGraph* BoundGraph = StateNode->BoundGraph;
	if (!BoundGraph) return MakeErrorJson(TEXT("State has no bound graph"));

	// Look for existing sequence player node, or create one
	UAnimGraphNode_SequencePlayer* SeqPlayer = nullptr;
	for (UEdGraphNode* Node : BoundGraph->Nodes)
	{
		SeqPlayer = Cast<UAnimGraphNode_SequencePlayer>(Node);
		if (SeqPlayer) break;
	}

	if (!SeqPlayer)
	{
		// Create a new sequence player node
		FGraphNodeCreator<UAnimGraphNode_SequencePlayer> NodeCreator(*BoundGraph);
		SeqPlayer = NodeCreator.CreateNode(true);
		if (!SeqPlayer)
			return MakeErrorJson(TEXT("Failed to create sequence player node"));
		SeqPlayer->NodePosX = 0;
		SeqPlayer->NodePosY = 0;
		NodeCreator.Finalize();

		// Connect to the state result node
		UAnimGraphNode_StateResult* ResultNode = nullptr;
		for (UEdGraphNode* Node : BoundGraph->Nodes)
		{
			ResultNode = Cast<UAnimGraphNode_StateResult>(Node);
			if (ResultNode) break;
		}
		if (ResultNode)
		{
			// Find output pose pin on sequence player and input pose pin on result
			UEdGraphPin* OutputPin = nullptr;
			UEdGraphPin* InputPin = nullptr;
			for (UEdGraphPin* Pin : SeqPlayer->Pins)
			{
				if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
				{
					OutputPin = Pin;
					break;
				}
			}
			for (UEdGraphPin* Pin : ResultNode->Pins)
			{
				if (Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
				{
					InputPin = Pin;
					break;
				}
			}
			if (OutputPin && InputPin)
				OutputPin->MakeLinkTo(InputPin);
		}
	}

	// Set the animation on the sequence player
	SeqPlayer->Node.SetSequence(AnimSeq);
	SeqPlayer->Node.SetLoopAnimation(bLooping);

	FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBP);
	AnimBP->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("animBlueprintName"), ABPName);
	Result->SetStringField(TEXT("stateName"), StateName);
	Result->SetStringField(TEXT("animationAsset"), AnimSeq->GetName());
	Result->SetBoolField(TEXT("looping"), bLooping);
	return JsonToString(Result);
}

// ============================================================
// animBPGetStateMachine - Get full state machine graph
// Params: animBlueprintName, stateMachineName (opt)
// ============================================================
FString FAgenticMCPServer::HandleAnimBPGetStateMachine(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ABPName = Json->GetStringField(TEXT("animBlueprintName"));
	if (ABPName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: animBlueprintName"));

	FString SMName = Json->GetStringField(TEXT("stateMachineName"));

	UAnimBlueprint* AnimBP = FindAnimBlueprintByName(ABPName);
	if (!AnimBP) return MakeErrorJson(FString::Printf(TEXT("AnimBlueprint not found: %s"), *ABPName));

	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, SMName);
	if (!SMGraph) return MakeErrorJson(TEXT("No state machine graph found"));

	// Collect states
	TArray<TSharedPtr<FJsonValue>> StatesArr;
	TArray<TSharedPtr<FJsonValue>> TransitionsArr;

	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
		{
			TSharedRef<FJsonObject> StateJson = MakeShared<FJsonObject>();
			StateJson->SetStringField(TEXT("name"), StateNode->GetStateName());
			StateJson->SetNumberField(TEXT("posX"), StateNode->NodePosX);
			StateJson->SetNumberField(TEXT("posY"), StateNode->NodePosY);

			// Check for animation assignment
			if (StateNode->BoundGraph)
			{
				for (UEdGraphNode* InnerNode : StateNode->BoundGraph->Nodes)
				{
					if (UAnimGraphNode_SequencePlayer* SP = Cast<UAnimGraphNode_SequencePlayer>(InnerNode))
					{
						UAnimSequence* Seq = SP->Node.GetSequence();
						if (Seq)
						{
							StateJson->SetStringField(TEXT("animation"), Seq->GetName());
							StateJson->SetStringField(TEXT("animationPath"), Seq->GetPathName());
						}
						break;
					}
				}
			}

			StatesArr.Add(MakeShared<FJsonValueObject>(StateJson));
		}
		else if (UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(Node))
		{
			TSharedRef<FJsonObject> TransJson = MakeShared<FJsonObject>();
			UAnimStateNode* Prev = TransNode->GetPreviousState();
			UAnimStateNode* Next = TransNode->GetNextState();
			TransJson->SetStringField(TEXT("fromState"), Prev ? Prev->GetStateName() : TEXT("(none)"));
			TransJson->SetStringField(TEXT("toState"), Next ? Next->GetStateName() : TEXT("(none)"));
			TransJson->SetBoolField(TEXT("automaticRule"), TransNode->bAutomaticRuleBasedOnSequencePlayerInState);
			TransitionsArr.Add(MakeShared<FJsonValueObject>(TransJson));
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("animBlueprintName"), ABPName);
	Result->SetNumberField(TEXT("stateCount"), StatesArr.Num());
	Result->SetArrayField(TEXT("states"), StatesArr);
	Result->SetNumberField(TEXT("transitionCount"), TransitionsArr.Num());
	Result->SetArrayField(TEXT("transitions"), TransitionsArr);
	return JsonToString(Result);
}

// ============================================================
// animBPAddBlendNode - Add a blend space player to a state
// Params: animBlueprintName, stateMachineName (opt),
//         stateName, blendSpacePath
// ============================================================
FString FAgenticMCPServer::HandleAnimBPAddBlendNode(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ABPName = Json->GetStringField(TEXT("animBlueprintName"));
	FString SMName = Json->GetStringField(TEXT("stateMachineName"));
	FString StateName = Json->GetStringField(TEXT("stateName"));
	FString BSPath = Json->GetStringField(TEXT("blendSpacePath"));

	if (ABPName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: animBlueprintName"));
	if (StateName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: stateName"));
	if (BSPath.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: blendSpacePath"));

	UAnimBlueprint* AnimBP = FindAnimBlueprintByName(ABPName);
	if (!AnimBP) return MakeErrorJson(FString::Printf(TEXT("AnimBlueprint not found: %s"), *ABPName));

	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, SMName);
	if (!SMGraph) return MakeErrorJson(TEXT("No state machine graph found"));

	UAnimStateNode* StateNode = FindStateNode(SMGraph, StateName);
	if (!StateNode) return MakeErrorJson(FString::Printf(TEXT("State not found: %s"), *StateName));

	// Load blend space
	UBlendSpace* BlendSpace = Cast<UBlendSpace>(StaticLoadObject(UBlendSpace::StaticClass(), nullptr, *BSPath));
	if (!BlendSpace) return MakeErrorJson(FString::Printf(TEXT("BlendSpace not found: %s"), *BSPath));

	UEdGraph* BoundGraph = StateNode->BoundGraph;
	if (!BoundGraph) return MakeErrorJson(TEXT("State has no bound graph"));

	// Create blend space player node
	FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer> NodeCreator(*BoundGraph);
	UAnimGraphNode_BlendSpacePlayer* BSPlayer = NodeCreator.CreateNode(true);
	if (!BSPlayer) return MakeErrorJson(TEXT("Failed to create blend space player node"));

	BSPlayer->NodePosX = -200;
	BSPlayer->NodePosY = 0;
	NodeCreator.Finalize();

	BSPlayer->Node.SetBlendSpace(BlendSpace);

	// Connect to state result
	UAnimGraphNode_StateResult* ResultNode = nullptr;
	for (UEdGraphNode* Node : BoundGraph->Nodes)
	{
		ResultNode = Cast<UAnimGraphNode_StateResult>(Node);
		if (ResultNode) break;
	}
	if (ResultNode)
	{
		UEdGraphPin* OutputPin = nullptr;
		UEdGraphPin* InputPin = nullptr;
		for (UEdGraphPin* Pin : BSPlayer->Pins)
		{
			if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
			{
				OutputPin = Pin;
				break;
			}
		}
		for (UEdGraphPin* Pin : ResultNode->Pins)
		{
			if (Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
			{
				InputPin = Pin;
				break;
			}
		}
		if (OutputPin && InputPin)
			OutputPin->MakeLinkTo(InputPin);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBP);
	AnimBP->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("animBlueprintName"), ABPName);
	Result->SetStringField(TEXT("stateName"), StateName);
	Result->SetStringField(TEXT("blendSpace"), BlendSpace->GetName());
	return JsonToString(Result);
}
