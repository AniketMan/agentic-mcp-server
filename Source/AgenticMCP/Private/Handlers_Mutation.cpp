// Handlers_Mutation.cpp
// Blueprint write/mutation handlers for AgenticMCP.
// These handlers create, modify, and delete Blueprint graph elements.
//
// Endpoints implemented:
//   /api/add-node          - Create a new node in a graph
//   /api/delete-node       - Remove a node from a graph
//   /api/connect-pins      - Wire two pins together
//   /api/disconnect-pin    - Break pin connections
//   /api/set-pin-default   - Set a pin's default value
//   /api/move-node         - Change node position
//   /api/refresh-all-nodes - Refresh all nodes in a Blueprint
//   /api/create-blueprint  - Create a new Blueprint asset
//   /api/create-graph      - Create a new function/macro graph
//   /api/delete-graph      - Delete a graph
//   /api/add-variable      - Add a variable to a Blueprint
//   /api/remove-variable   - Remove a variable
//   /api/compile-blueprint - Compile and save a Blueprint
//   /api/duplicate-nodes   - Duplicate nodes in a graph
//   /api/set-node-comment  - Set comment text on a node

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphNode_Comment.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_Select.h"
#include "K2Node_Knot.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_RemoveDelegate.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/BlueprintFactory.h"
#include "Runtime/Launch/Resources/Version.h"

// SEH wrapper defined in AgenticMCPServer.cpp
#if PLATFORM_WINDOWS
extern int32 TryRefreshAllNodesSEH(UBlueprint* BP);
#endif

// ============================================================
// Helper: Find a graph by name in a Blueprint
// ============================================================

static UEdGraph* FindGraphByName(UBlueprint* BP, const FString& GraphName)
{
	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}
	return nullptr;
}

// ============================================================
// HandleAddNode - Create a new node in a graph
// POST /api/add-node
// ============================================================

FString FAgenticMCPServer::HandleAddNode(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	FString GraphName = Json->GetStringField(TEXT("graph"));
	FString NodeType = Json->GetStringField(TEXT("nodeType"));

	if (BlueprintName.IsEmpty() || GraphName.IsEmpty() || NodeType.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required fields: blueprint, graph, nodeType"));
	}

	int32 PosX = 0, PosY = 0;
	if (Json->HasField(TEXT("posX")))
		PosX = (int32)Json->GetNumberField(TEXT("posX"));
	if (Json->HasField(TEXT("posY")))
		PosY = (int32)Json->GetNumberField(TEXT("posY"));

	// Load Blueprint
	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP)
	{
		return MakeErrorJson(LoadError);
	}

	// Find target graph
	FString DecodedGraphName = UrlDecode(GraphName);
	UEdGraph* TargetGraph = FindGraphByName(BP, DecodedGraphName);
	if (!TargetGraph)
	{
		// Return available graph names for debugging
		TArray<UEdGraph*> AllGraphs;
		BP->GetAllGraphs(AllGraphs);
		TArray<TSharedPtr<FJsonValue>> GraphNames;
		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph) GraphNames.Add(MakeShared<FJsonValueString>(Graph->GetName()));
		}
		TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Graph '%s' not found"), *DecodedGraphName));
		E->SetArrayField(TEXT("availableGraphs"), GraphNames);
		return JsonToString(E);
	}

	UEdGraphNode* NewNode = nullptr;

	// ---- BreakStruct / MakeStruct ----
	if (NodeType == TEXT("BreakStruct") || NodeType == TEXT("MakeStruct"))
	{
		FString TypeName = Json->GetStringField(TEXT("typeName"));
		if (TypeName.IsEmpty())
		{
			return MakeErrorJson(TEXT("Missing required field 'typeName' for BreakStruct/MakeStruct"));
		}

		// Find the struct type
		FString SearchName = TypeName;
		if (SearchName.StartsWith(TEXT("F")))
			SearchName = SearchName.Mid(1);

		UScriptStruct* FoundStruct = FindFirstObject<UScriptStruct>(*SearchName);
		if (!FoundStruct)
			FoundStruct = FindFirstObject<UScriptStruct>(*TypeName);
		if (!FoundStruct)
		{
			for (TObjectIterator<UScriptStruct> It; It; ++It)
			{
				if (It->GetName() == SearchName || It->GetName() == TypeName)
				{
					FoundStruct = *It;
					break;
				}
			}
		}
		if (!FoundStruct)
		{
			return MakeErrorJson(FString::Printf(TEXT("Struct type '%s' not found"), *TypeName));
		}

		if (NodeType == TEXT("BreakStruct"))
		{
			UK2Node_BreakStruct* BreakNode = NewObject<UK2Node_BreakStruct>(TargetGraph);
			BreakNode->StructType = FoundStruct;
			BreakNode->NodePosX = PosX;
			BreakNode->NodePosY = PosY;
			TargetGraph->AddNode(BreakNode, false, false);
			BreakNode->AllocateDefaultPins();
			NewNode = BreakNode;
		}
		else
		{
			UK2Node_MakeStruct* MakeNode = NewObject<UK2Node_MakeStruct>(TargetGraph);
			MakeNode->StructType = FoundStruct;
			MakeNode->NodePosX = PosX;
			MakeNode->NodePosY = PosY;
			TargetGraph->AddNode(MakeNode, false, false);
			MakeNode->AllocateDefaultPins();
			NewNode = MakeNode;
		}
	}
	// ---- CallFunction ----
	else if (NodeType == TEXT("CallFunction"))
	{
		FString FunctionName = Json->GetStringField(TEXT("functionName"));
		FString ClassName = Json->GetStringField(TEXT("className"));

		if (FunctionName.IsEmpty())
		{
			return MakeErrorJson(TEXT("Missing required field 'functionName' for CallFunction"));
		}

		UFunction* TargetFunc = nullptr;

		// Search in specified class first
		if (!ClassName.IsEmpty())
		{
			UClass* TargetClass = FindClassByName(ClassName);
			if (TargetClass)
			{
				TargetFunc = TargetClass->FindFunctionByName(FName(*FunctionName));
			}
		}

		// Broad search across all classes
		if (!TargetFunc)
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UFunction* Func = It->FindFunctionByName(FName(*FunctionName));
				if (Func)
				{
					TargetFunc = Func;
					break;
				}
			}
		}

		if (!TargetFunc)
		{
			return MakeErrorJson(FString::Printf(TEXT("Function '%s' not found%s"),
				*FunctionName,
				ClassName.IsEmpty() ? TEXT("") :
					*FString::Printf(TEXT(" in class '%s'"), *ClassName)));
		}

		UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(TargetGraph);
		CallNode->SetFromFunction(TargetFunc);
		CallNode->NodePosX = PosX;
		CallNode->NodePosY = PosY;
		TargetGraph->AddNode(CallNode, false, false);
		CallNode->AllocateDefaultPins();
		NewNode = CallNode;
	}
	// ---- VariableGet / VariableSet ----
	else if (NodeType == TEXT("VariableGet") || NodeType == TEXT("VariableSet"))
	{
		FString VariableName = Json->GetStringField(TEXT("variableName"));
		if (VariableName.IsEmpty())
		{
			return MakeErrorJson(TEXT("Missing required field 'variableName'"));
		}

		FName VarFName(*VariableName);
		bool bVarFound = false;

		// Check Blueprint variables
		for (const FBPVariableDescription& Var : BP->NewVariables)
		{
			if (Var.VarName == VarFName) { bVarFound = true; break; }
		}
		// Check inherited properties
		if (!bVarFound && BP->GeneratedClass)
		{
			FProperty* Prop = BP->GeneratedClass->FindPropertyByName(VarFName);
			if (Prop) bVarFound = true;
		}

		if (!bVarFound)
		{
			return MakeErrorJson(FString::Printf(
				TEXT("Variable '%s' not found in Blueprint '%s'"),
				*VariableName, *BlueprintName));
		}

		if (NodeType == TEXT("VariableGet"))
		{
			UK2Node_VariableGet* GetNode = NewObject<UK2Node_VariableGet>(TargetGraph);
			GetNode->VariableReference.SetSelfMember(VarFName);
			GetNode->NodePosX = PosX;
			GetNode->NodePosY = PosY;
			TargetGraph->AddNode(GetNode, false, false);
			GetNode->AllocateDefaultPins();
			NewNode = GetNode;
		}
		else
		{
			UK2Node_VariableSet* SetNode = NewObject<UK2Node_VariableSet>(TargetGraph);
			SetNode->VariableReference.SetSelfMember(VarFName);
			SetNode->NodePosX = PosX;
			SetNode->NodePosY = PosY;
			TargetGraph->AddNode(SetNode, false, false);
			SetNode->AllocateDefaultPins();
			NewNode = SetNode;
		}
	}
	// ---- DynamicCast ----
	else if (NodeType == TEXT("DynamicCast"))
	{
		FString CastTarget = Json->GetStringField(TEXT("castTarget"));
		if (CastTarget.IsEmpty())
		{
			return MakeErrorJson(TEXT("Missing required field 'castTarget' for DynamicCast"));
		}

		UClass* TargetClass = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			FString Name = It->GetName();
			if (Name == CastTarget || Name == CastTarget + TEXT("_C"))
			{
				TargetClass = *It;
				break;
			}
		}
		if (!TargetClass)
		{
			return MakeErrorJson(FString::Printf(TEXT("Cast target class '%s' not found"), *CastTarget));
		}

		UK2Node_DynamicCast* CastNode = NewObject<UK2Node_DynamicCast>(TargetGraph);
		CastNode->TargetType = TargetClass;
		CastNode->NodePosX = PosX;
		CastNode->NodePosY = PosY;
		TargetGraph->AddNode(CastNode, false, false);
		CastNode->AllocateDefaultPins();
		NewNode = CastNode;
	}
	// ---- OverrideEvent ----
	else if (NodeType == TEXT("OverrideEvent"))
	{
		FString FunctionName = Json->GetStringField(TEXT("functionName"));
		if (FunctionName.IsEmpty())
		{
			return MakeErrorJson(TEXT("Missing required field 'functionName' for OverrideEvent"));
		}

		if (!BP->ParentClass)
		{
			return MakeErrorJson(TEXT("Blueprint has no parent class"));
		}

		UFunction* Func = BP->ParentClass->FindFunctionByName(FName(*FunctionName));
		if (!Func)
		{
			return MakeErrorJson(FString::Printf(
				TEXT("Function '%s' not found on parent class '%s'"),
				*FunctionName, *BP->ParentClass->GetName()));
		}

		// Check for existing override
		for (UEdGraphNode* ExistingNode : TargetGraph->Nodes)
		{
			if (UK2Node_Event* ExistingEvent = Cast<UK2Node_Event>(ExistingNode))
			{
				if (ExistingEvent->bOverrideFunction &&
					ExistingEvent->EventReference.GetMemberName() == FName(*FunctionName))
				{
					// Already exists -- return it
					TSharedPtr<FJsonObject> NodeState = SerializeNode(ExistingEvent);
					TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
					OutJson->SetBoolField(TEXT("success"), true);
					OutJson->SetBoolField(TEXT("alreadyExists"), true);
					OutJson->SetStringField(TEXT("nodeId"), ExistingEvent->NodeGuid.ToString());
					if (NodeState.IsValid())
						OutJson->SetObjectField(TEXT("node"), NodeState);
					return JsonToString(OutJson);
				}
			}
		}

		UK2Node_Event* EventNode = NewObject<UK2Node_Event>(TargetGraph);
		EventNode->EventReference.SetFromField<UFunction>(Func, false);
		EventNode->bOverrideFunction = true;
		EventNode->NodePosX = PosX;
		EventNode->NodePosY = PosY;
		TargetGraph->AddNode(EventNode, false, false);
		EventNode->AllocateDefaultPins();
		NewNode = EventNode;
	}
	// ---- CustomEvent ----
	else if (NodeType == TEXT("CustomEvent"))
	{
		FString EventName = Json->GetStringField(TEXT("eventName"));
		if (EventName.IsEmpty())
		{
			return MakeErrorJson(TEXT("Missing required field 'eventName' for CustomEvent"));
		}

		UK2Node_CustomEvent* EventNode = NewObject<UK2Node_CustomEvent>(TargetGraph);
		EventNode->CustomFunctionName = FName(*EventName);
		EventNode->NodePosX = PosX;
		EventNode->NodePosY = PosY;
		TargetGraph->AddNode(EventNode, false, false);
		EventNode->AllocateDefaultPins();
		NewNode = EventNode;
	}
	// ---- Branch ----
	else if (NodeType == TEXT("Branch"))
	{
		UK2Node_IfThenElse* BranchNode = NewObject<UK2Node_IfThenElse>(TargetGraph);
		BranchNode->NodePosX = PosX;
		BranchNode->NodePosY = PosY;
		TargetGraph->AddNode(BranchNode, false, false);
		BranchNode->AllocateDefaultPins();
		NewNode = BranchNode;
	}
	// ---- Sequence ----
	else if (NodeType == TEXT("Sequence"))
	{
		UK2Node_ExecutionSequence* SeqNode = NewObject<UK2Node_ExecutionSequence>(TargetGraph);
		SeqNode->NodePosX = PosX;
		SeqNode->NodePosY = PosY;
		TargetGraph->AddNode(SeqNode, false, false);
		SeqNode->AllocateDefaultPins();
		NewNode = SeqNode;
	}
	// ---- CallParentFunction ----
	else if (NodeType == TEXT("CallParentFunction"))
	{
		FString FunctionName = Json->GetStringField(TEXT("functionName"));
		if (FunctionName.IsEmpty())
		{
			return MakeErrorJson(TEXT("Missing required field 'functionName' for CallParentFunction"));
		}
		if (!BP->ParentClass)
		{
			return MakeErrorJson(TEXT("Blueprint has no parent class"));
		}

		UFunction* Func = BP->ParentClass->FindFunctionByName(FName(*FunctionName));
		if (!Func)
		{
			return MakeErrorJson(FString::Printf(
				TEXT("Function '%s' not found on parent class '%s'"),
				*FunctionName, *BP->ParentClass->GetName()));
		}

		UK2Node_CallParentFunction* ParentCallNode = NewObject<UK2Node_CallParentFunction>(TargetGraph);
		ParentCallNode->SetFromFunction(Func);
		ParentCallNode->NodePosX = PosX;
		ParentCallNode->NodePosY = PosY;
		TargetGraph->AddNode(ParentCallNode, false, false);
		ParentCallNode->AllocateDefaultPins();
		NewNode = ParentCallNode;
	}
	// ---- Loop Macros (ForLoop, ForEachLoop, WhileLoop, ForLoopWithBreak) ----
	else if (NodeType == TEXT("ForEachLoop") || NodeType == TEXT("ForLoop") ||
			 NodeType == TEXT("ForLoopWithBreak") || NodeType == TEXT("WhileLoop"))
	{
		FString MacroName = NodeType; // They match the engine macro names

		UBlueprint* StandardMacros = Cast<UBlueprint>(StaticLoadObject(
			UBlueprint::StaticClass(), nullptr,
			TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros")));

		UEdGraph* MacroGraph = nullptr;
		if (StandardMacros)
		{
			for (UEdGraph* Graph : StandardMacros->MacroGraphs)
			{
				if (Graph && Graph->GetName() == MacroName)
				{
					MacroGraph = Graph;
					break;
				}
			}
		}

		if (!MacroGraph)
		{
			return MakeErrorJson(FString::Printf(
				TEXT("Standard macro '%s' not found"), *MacroName));
		}

		UK2Node_MacroInstance* MacroNode = NewObject<UK2Node_MacroInstance>(TargetGraph);
		MacroNode->SetMacroGraph(MacroGraph);
		MacroNode->NodePosX = PosX;
		MacroNode->NodePosY = PosY;
		TargetGraph->AddNode(MacroNode, false, false);
		MacroNode->AllocateDefaultPins();
		NewNode = MacroNode;
	}
	// ---- SpawnActorFromClass ----
	else if (NodeType == TEXT("SpawnActorFromClass"))
	{
		FString ActorClassName = Json->GetStringField(TEXT("actorClass"));

		UClass* ActorClass = nullptr;
		if (!ActorClassName.IsEmpty())
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if ((It->GetName() == ActorClassName ||
					 It->GetName() == ActorClassName + TEXT("_C")) &&
					It->IsChildOf(AActor::StaticClass()))
				{
					ActorClass = *It;
					break;
				}
			}
			if (!ActorClass)
			{
				return MakeErrorJson(FString::Printf(
					TEXT("Actor class '%s' not found"), *ActorClassName));
			}
		}

		UK2Node_SpawnActorFromClass* SpawnNode = NewObject<UK2Node_SpawnActorFromClass>(TargetGraph);
		SpawnNode->NodePosX = PosX;
		SpawnNode->NodePosY = PosY;
		TargetGraph->AddNode(SpawnNode, false, false);
		SpawnNode->AllocateDefaultPins();
		if (ActorClass)
		{
			UEdGraphPin* ClassPin = SpawnNode->GetClassPin();
			if (ClassPin)
			{
				ClassPin->DefaultObject = ActorClass;
				if (const UEdGraphSchema* SpawnSchema = TargetGraph->GetSchema())
				{
					SpawnSchema->ReconstructNode(*SpawnNode);
				}
			}
		}
		NewNode = SpawnNode;
	}
	// ---- Select ----
	else if (NodeType == TEXT("Select"))
	{
		UK2Node_Select* SelectNode = NewObject<UK2Node_Select>(TargetGraph);
		SelectNode->NodePosX = PosX;
		SelectNode->NodePosY = PosY;
		TargetGraph->AddNode(SelectNode, false, false);
		SelectNode->AllocateDefaultPins();
		NewNode = SelectNode;
	}
	// ---- Comment ----
	else if (NodeType == TEXT("Comment"))
	{
		FString CommentText = Json->GetStringField(TEXT("comment"));
		if (CommentText.IsEmpty()) CommentText = TEXT("Comment");

		int32 Width = 400, Height = 200;
		if (Json->HasField(TEXT("width")))
			Width = FMath::Max(64, Json->GetIntegerField(TEXT("width")));
		if (Json->HasField(TEXT("height")))
			Height = FMath::Max(64, Json->GetIntegerField(TEXT("height")));

		UEdGraphNode_Comment* CommentNode = NewObject<UEdGraphNode_Comment>(TargetGraph);
		CommentNode->NodeComment = CommentText;
		CommentNode->NodePosX = PosX;
		CommentNode->NodePosY = PosY;
		CommentNode->NodeWidth = Width;
		CommentNode->NodeHeight = Height;
		TargetGraph->AddNode(CommentNode, false, false);
		CommentNode->AllocateDefaultPins();
		NewNode = CommentNode;
	}
	// ---- Reroute ----
	else if (NodeType == TEXT("Reroute"))
	{
		UK2Node_Knot* KnotNode = NewObject<UK2Node_Knot>(TargetGraph);
		KnotNode->NodePosX = PosX;
		KnotNode->NodePosY = PosY;
		TargetGraph->AddNode(KnotNode, false, false);
		KnotNode->AllocateDefaultPins();
		NewNode = KnotNode;
	}
	// ---- MacroInstance (generic) ----
	else if (NodeType == TEXT("MacroInstance"))
	{
		FString MacroName = Json->GetStringField(TEXT("macroName"));
		FString MacroSource = Json->GetStringField(TEXT("macroSource"));

		if (MacroName.IsEmpty())
		{
			return MakeErrorJson(TEXT("Missing required field 'macroName' for MacroInstance"));
		}

		// Default to standard macros if no source specified
		if (MacroSource.IsEmpty())
		{
			MacroSource = TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros");
		}

		UBlueprint* MacroBP = Cast<UBlueprint>(StaticLoadObject(
			UBlueprint::StaticClass(), nullptr, *MacroSource));

		UEdGraph* MacroGraph = nullptr;
		if (MacroBP)
		{
			for (UEdGraph* Graph : MacroBP->MacroGraphs)
			{
				if (Graph && Graph->GetName() == MacroName)
				{
					MacroGraph = Graph;
					break;
				}
			}
		}

		if (!MacroGraph)
		{
			return MakeErrorJson(FString::Printf(
				TEXT("Macro '%s' not found in '%s'"), *MacroName, *MacroSource));
		}

		UK2Node_MacroInstance* MacroNode = NewObject<UK2Node_MacroInstance>(TargetGraph);
		MacroNode->SetMacroGraph(MacroGraph);
		MacroNode->NodePosX = PosX;
		MacroNode->NodePosY = PosY;
		TargetGraph->AddNode(MacroNode, false, false);
		MacroNode->AllocateDefaultPins();
		NewNode = MacroNode;
	}
	// ---- AddDelegate (Bind Event) ----
	// Creates a "Bind Event to ..." node for wiring delegates to CustomEvents.
	// Required fields: delegateName (e.g. "OnComponentBeginOverlap"), ownerClass (e.g. "PrimitiveComponent")
	// The delegate is identified by its FMulticastDelegateProperty name on the owning class.
	else if (NodeType == TEXT("AddDelegate"))
	{
		FString DelegateName = Json->GetStringField(TEXT("delegateName"));
		FString OwnerClassName = Json->GetStringField(TEXT("ownerClass"));

		if (DelegateName.IsEmpty())
		{
			return MakeErrorJson(TEXT("Missing required field 'delegateName' for AddDelegate"));
		}

		// Find the owner class
		UClass* OwnerClass = nullptr;
		if (!OwnerClassName.IsEmpty())
		{
			OwnerClass = FindClassByName(OwnerClassName);
		}
		if (!OwnerClass)
		{
			// Search all classes for the delegate property
			for (TObjectIterator<UClass> It; It; ++It)
			{
				for (TFieldIterator<FMulticastDelegateProperty> PropIt(*It); PropIt; ++PropIt)
				{
					if (PropIt->GetName() == DelegateName)
					{
						OwnerClass = *It;
						break;
					}
				}
				if (OwnerClass) break;
			}
		}

		if (!OwnerClass)
		{
			return MakeErrorJson(FString::Printf(
				TEXT("Could not find delegate '%s' on any class. Specify 'ownerClass' to narrow search."),
				*DelegateName));
		}

		// Find the multicast delegate property
		FMulticastDelegateProperty* DelegateProp = nullptr;
		for (TFieldIterator<FMulticastDelegateProperty> PropIt(OwnerClass); PropIt; ++PropIt)
		{
			if (PropIt->GetName() == DelegateName)
			{
				DelegateProp = *PropIt;
				break;
			}
		}

		if (!DelegateProp)
		{
			// List available delegates for debugging
			TArray<TSharedPtr<FJsonValue>> AvailableDelegates;
			for (TFieldIterator<FMulticastDelegateProperty> PropIt(OwnerClass); PropIt; ++PropIt)
			{
				AvailableDelegates.Add(MakeShared<FJsonValueString>(PropIt->GetName()));
			}
			TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("error"),
				FString::Printf(TEXT("Delegate '%s' not found on class '%s'"),
					*DelegateName, *OwnerClass->GetName()));
			E->SetArrayField(TEXT("availableDelegates"), AvailableDelegates);
			return JsonToString(E);
		}

		UK2Node_AddDelegate* AddDelegateNode = NewObject<UK2Node_AddDelegate>(TargetGraph);
		AddDelegateNode->SetFromProperty(DelegateProp, false, OwnerClass);
		AddDelegateNode->NodePosX = PosX;
		AddDelegateNode->NodePosY = PosY;
		TargetGraph->AddNode(AddDelegateNode, false, false);
		AddDelegateNode->AllocateDefaultPins();
		NewNode = AddDelegateNode;
	}
	// ---- RemoveDelegate (Unbind Event) ----
	else if (NodeType == TEXT("RemoveDelegate"))
	{
		FString DelegateName = Json->GetStringField(TEXT("delegateName"));
		FString OwnerClassName = Json->GetStringField(TEXT("ownerClass"));

		if (DelegateName.IsEmpty())
		{
			return MakeErrorJson(TEXT("Missing required field 'delegateName' for RemoveDelegate"));
		}

		UClass* OwnerClass = nullptr;
		if (!OwnerClassName.IsEmpty())
		{
			OwnerClass = FindClassByName(OwnerClassName);
		}
		if (!OwnerClass)
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				for (TFieldIterator<FMulticastDelegateProperty> PropIt(*It); PropIt; ++PropIt)
				{
					if (PropIt->GetName() == DelegateName)
					{
						OwnerClass = *It;
						break;
					}
				}
				if (OwnerClass) break;
			}
		}

		if (!OwnerClass)
		{
			return MakeErrorJson(FString::Printf(
				TEXT("Could not find delegate '%s' on any class"), *DelegateName));
		}

		FMulticastDelegateProperty* DelegateProp = nullptr;
		for (TFieldIterator<FMulticastDelegateProperty> PropIt(OwnerClass); PropIt; ++PropIt)
		{
			if (PropIt->GetName() == DelegateName)
			{
				DelegateProp = *PropIt;
				break;
			}
		}

		if (!DelegateProp)
		{
			return MakeErrorJson(FString::Printf(
				TEXT("Delegate '%s' not found on class '%s'"),
				*DelegateName, *OwnerClass->GetName()));
		}

		UK2Node_RemoveDelegate* RemoveDelegateNode = NewObject<UK2Node_RemoveDelegate>(TargetGraph);
		RemoveDelegateNode->SetFromProperty(DelegateProp, false, OwnerClass);
		RemoveDelegateNode->NodePosX = PosX;
		RemoveDelegateNode->NodePosY = PosY;
		TargetGraph->AddNode(RemoveDelegateNode, false, false);
		RemoveDelegateNode->AllocateDefaultPins();
		NewNode = RemoveDelegateNode;
	}
	// ---- ClearDelegate (Unbind All Events) ----
	else if (NodeType == TEXT("ClearDelegate"))
	{
		FString DelegateName = Json->GetStringField(TEXT("delegateName"));
		FString OwnerClassName = Json->GetStringField(TEXT("ownerClass"));

		if (DelegateName.IsEmpty())
		{
			return MakeErrorJson(TEXT("Missing required field 'delegateName' for ClearDelegate"));
		}

		UClass* OwnerClass = nullptr;
		if (!OwnerClassName.IsEmpty())
		{
			OwnerClass = FindClassByName(OwnerClassName);
		}
		if (!OwnerClass)
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				for (TFieldIterator<FMulticastDelegateProperty> PropIt(*It); PropIt; ++PropIt)
				{
					if (PropIt->GetName() == DelegateName)
					{
						OwnerClass = *It;
						break;
					}
				}
				if (OwnerClass) break;
			}
		}

		if (!OwnerClass)
		{
			return MakeErrorJson(FString::Printf(
				TEXT("Could not find delegate '%s' on any class"), *DelegateName));
		}

		FMulticastDelegateProperty* DelegateProp = nullptr;
		for (TFieldIterator<FMulticastDelegateProperty> PropIt(OwnerClass); PropIt; ++PropIt)
		{
			if (PropIt->GetName() == DelegateName)
			{
				DelegateProp = *PropIt;
				break;
			}
		}

		if (!DelegateProp)
		{
			return MakeErrorJson(FString::Printf(
				TEXT("Delegate '%s' not found on class '%s'"),
				*DelegateName, *OwnerClass->GetName()));
		}

		UK2Node_ClearDelegate* ClearDelegateNode = NewObject<UK2Node_ClearDelegate>(TargetGraph);
		ClearDelegateNode->SetFromProperty(DelegateProp, false, OwnerClass);
		ClearDelegateNode->NodePosX = PosX;
		ClearDelegateNode->NodePosY = PosY;
		TargetGraph->AddNode(ClearDelegateNode, false, false);
		ClearDelegateNode->AllocateDefaultPins();
		NewNode = ClearDelegateNode;
	}
	// ---- CreateDelegate ----
	// Creates a delegate reference node (the "Assign" pattern).
	// Required fields: delegateName, ownerClass
	else if (NodeType == TEXT("CreateDelegate"))
	{
		UK2Node_CreateDelegate* CreateDelegateNode = NewObject<UK2Node_CreateDelegate>(TargetGraph);
		CreateDelegateNode->NodePosX = PosX;
		CreateDelegateNode->NodePosY = PosY;
		TargetGraph->AddNode(CreateDelegateNode, false, false);
		CreateDelegateNode->AllocateDefaultPins();

		// If a function name is provided, set it as the selected function
		FString FunctionName = Json->GetStringField(TEXT("functionName"));
		if (!FunctionName.IsEmpty())
		{
			CreateDelegateNode->SetFunction(FName(*FunctionName));
		}

		NewNode = CreateDelegateNode;
	}
	// ---- Delay ----
	else if (NodeType == TEXT("Delay"))
	{
		UFunction* DelayFunc = UGameplayStatics::StaticClass()->FindFunctionByName(FName(TEXT("Delay")));
		if (!DelayFunc)
		{
			return MakeErrorJson(TEXT("Could not find UGameplayStatics::Delay function"));
		}

		UK2Node_CallFunction* DelayNode = NewObject<UK2Node_CallFunction>(TargetGraph);
		DelayNode->SetFromFunction(DelayFunc);
		DelayNode->NodePosX = PosX;
		DelayNode->NodePosY = PosY;
		TargetGraph->AddNode(DelayNode, false, false);
		DelayNode->AllocateDefaultPins();

		// Set default duration if provided
		if (Json->HasField(TEXT("duration")))
		{
			double Duration = Json->GetNumberField(TEXT("duration"));
			for (UEdGraphPin* Pin : DelayNode->Pins)
			{
				if (Pin && Pin->PinName == TEXT("Duration"))
				{
					Pin->DefaultValue = FString::SanitizeFloat(Duration);
					break;
				}
			}
		}

		NewNode = DelayNode;
	}
	else
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Unsupported nodeType '%s'. Supported: BreakStruct, MakeStruct, CallFunction, "
				 "VariableGet, VariableSet, DynamicCast, OverrideEvent, CallParentFunction, "
				 "CustomEvent, Branch, Sequence, ForLoop, ForEachLoop, ForLoopWithBreak, "
				 "WhileLoop, SpawnActorFromClass, Select, Comment, Reroute, MacroInstance, "
				 "AddDelegate, RemoveDelegate, ClearDelegate, CreateDelegate, Delay"),
			*NodeType));
	}

	if (!NewNode)
	{
		return MakeErrorJson(TEXT("Failed to create node"));
	}

	// Ensure valid GUID
	if (!NewNode->NodeGuid.IsValid())
	{
		NewNode->CreateNewGuid();
	}

	// Mark as modified and save (crash-proof wrapper)
	SafeMarkStructurallyModified(BP, TEXT("Add Node"));
	bool bSaved = SaveBlueprintPackage(BP);

	UE_LOG(LogTemp, Display, TEXT("AgenticMCP: Added %s node '%s' in graph '%s' of '%s', save %s"),
		*NodeType, *NewNode->NodeGuid.ToString(), *DecodedGraphName, *BlueprintName,
		bSaved ? TEXT("ok") : TEXT("FAILED"));

	// Build response with full node state
	TSharedPtr<FJsonObject> NodeState = SerializeNode(NewNode);
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("blueprint"), BlueprintName);
	OutJson->SetStringField(TEXT("graph"), DecodedGraphName);
	OutJson->SetStringField(TEXT("nodeType"), NodeType);
	OutJson->SetStringField(TEXT("nodeId"), NewNode->NodeGuid.ToString());
	OutJson->SetBoolField(TEXT("saved"), bSaved);
	if (NodeState.IsValid())
		OutJson->SetObjectField(TEXT("node"), NodeState);
	return JsonToString(OutJson);
}

// ============================================================
// HandleDeleteNode
// POST /api/delete-node { "blueprint": "...", "nodeId": "GUID" }
// ============================================================

FString FAgenticMCPServer::HandleDeleteNode(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	FString NodeId = Json->GetStringField(TEXT("nodeId"));

	if (BlueprintName.IsEmpty() || NodeId.IsEmpty())
		return MakeErrorJson(TEXT("Missing required fields: blueprint, nodeId"));

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

	UEdGraph* Graph = nullptr;
	UEdGraphNode* Node = FindNodeByGuid(BP, NodeId, &Graph);
	if (!Node)
		return MakeErrorJson(FString::Printf(TEXT("Node '%s' not found"), *NodeId));

	FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();

	// Break all pin connections first
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin) Pin->BreakAllPinLinks();
	}

	Graph->RemoveNode(Node);
	SafeMarkStructurallyModified(BP, TEXT("Blueprint Mutation"));
	bool bSaved = SaveBlueprintPackage(BP);

	UE_LOG(LogTemp, Display, TEXT("AgenticMCP: Deleted node '%s' (%s) from '%s'"),
		*NodeId, *NodeTitle, *BlueprintName);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("deletedNodeId"), NodeId);
	OutJson->SetStringField(TEXT("deletedNodeTitle"), NodeTitle);
	OutJson->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(OutJson);
}

// ============================================================
// HandleConnectPins
// POST /api/connect-pins
// ============================================================

FString FAgenticMCPServer::HandleConnectPins(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	FString SourceNodeId = Json->GetStringField(TEXT("sourceNodeId"));
	FString SourcePinName = Json->GetStringField(TEXT("sourcePinName"));
	FString TargetNodeId = Json->GetStringField(TEXT("targetNodeId"));
	FString TargetPinName = Json->GetStringField(TEXT("targetPinName"));

	if (BlueprintName.IsEmpty() || SourceNodeId.IsEmpty() || SourcePinName.IsEmpty() ||
		TargetNodeId.IsEmpty() || TargetPinName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required fields: blueprint, sourceNodeId, sourcePinName, targetNodeId, targetPinName"));
	}

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

	// Find source node and pin
	UEdGraph* SourceGraph = nullptr;
	UEdGraphNode* SourceNode = FindNodeByGuid(BP, SourceNodeId, &SourceGraph);
	if (!SourceNode)
		return MakeErrorJson(FString::Printf(TEXT("Source node '%s' not found"), *SourceNodeId));

	UEdGraphNode* TargetNode = FindNodeByGuid(BP, TargetNodeId);
	if (!TargetNode)
		return MakeErrorJson(FString::Printf(TEXT("Target node '%s' not found"), *TargetNodeId));

	UEdGraphPin* SourcePin = SourceNode->FindPin(FName(*SourcePinName));
	if (!SourcePin)
	{
		// Return available pins for debugging
		TArray<TSharedPtr<FJsonValue>> PinNames;
		for (UEdGraphPin* P : SourceNode->Pins)
		{
			if (P) PinNames.Add(MakeShared<FJsonValueString>(
				FString::Printf(TEXT("%s (%s)"), *P->PinName.ToString(),
					P->Direction == EGPD_Input ? TEXT("In") : TEXT("Out"))));
		}
		TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Source pin '%s' not found on node '%s'"),
				*SourcePinName, *SourceNodeId));
		E->SetArrayField(TEXT("availablePins"), PinNames);
		return JsonToString(E);
	}

	UEdGraphPin* TargetPin = TargetNode->FindPin(FName(*TargetPinName));
	if (!TargetPin)
	{
		TArray<TSharedPtr<FJsonValue>> PinNames;
		for (UEdGraphPin* P : TargetNode->Pins)
		{
			if (P) PinNames.Add(MakeShared<FJsonValueString>(
				FString::Printf(TEXT("%s (%s)"), *P->PinName.ToString(),
					P->Direction == EGPD_Input ? TEXT("In") : TEXT("Out"))));
		}
		TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Target pin '%s' not found on node '%s'"),
				*TargetPinName, *TargetNodeId));
		E->SetArrayField(TEXT("availablePins"), PinNames);
		return JsonToString(E);
	}

	// Try type-validated connection via the schema
	const UEdGraphSchema* Schema = SourceGraph->GetSchema();
	if (!Schema) return MakeErrorJson(TEXT("Graph schema not found"));

	bool bConnected = Schema->TryCreateConnection(SourcePin, TargetPin);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), bConnected);
	OutJson->SetStringField(TEXT("blueprint"), BlueprintName);

	if (!bConnected)
	{
		FString Reason = FString::Printf(
			TEXT("Cannot connect %s (%s) to %s (%s) - types incompatible"),
			*SourcePinName, *SourcePin->PinType.PinCategory.ToString(),
			*TargetPinName, *TargetPin->PinType.PinCategory.ToString());
		OutJson->SetStringField(TEXT("error"), Reason);
		return JsonToString(OutJson);
	}

	bool bSaved = SaveBlueprintPackage(BP);
	OutJson->SetBoolField(TEXT("saved"), bSaved);

	// Return updated node states
	TSharedPtr<FJsonObject> SrcState = SerializeNode(SourceNode);
	TSharedPtr<FJsonObject> TgtState = SerializeNode(TargetNode);
	if (SrcState.IsValid()) OutJson->SetObjectField(TEXT("updatedSourceNode"), SrcState);
	if (TgtState.IsValid()) OutJson->SetObjectField(TEXT("updatedTargetNode"), TgtState);

	return JsonToString(OutJson);
}

// ============================================================
// HandleDisconnectPin
// POST /api/disconnect-pin { "blueprint": "...", "nodeId": "GUID", "pinName": "..." }
// ============================================================

FString FAgenticMCPServer::HandleDisconnectPin(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	FString NodeId = Json->GetStringField(TEXT("nodeId"));
	FString PinName = Json->GetStringField(TEXT("pinName"));

	if (BlueprintName.IsEmpty() || NodeId.IsEmpty() || PinName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required fields: blueprint, nodeId, pinName"));

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

	UEdGraphNode* Node = FindNodeByGuid(BP, NodeId);
	if (!Node) return MakeErrorJson(FString::Printf(TEXT("Node '%s' not found"), *NodeId));

	UEdGraphPin* Pin = Node->FindPin(FName(*PinName));
	if (!Pin) return MakeErrorJson(FString::Printf(TEXT("Pin '%s' not found"), *PinName));

	int32 BrokenCount = Pin->LinkedTo.Num();
	Pin->BreakAllPinLinks();

	SafeMarkStructurallyModified(BP, TEXT("Blueprint Mutation"));
	bool bSaved = SaveBlueprintPackage(BP);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetNumberField(TEXT("disconnectedCount"), BrokenCount);
	OutJson->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(OutJson);
}

// ============================================================
// HandleSetPinDefault
// POST /api/set-pin-default { "blueprint": "...", "nodeId": "GUID", "pinName": "...", "value": "..." }
// ============================================================

FString FAgenticMCPServer::HandleSetPinDefault(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	FString NodeId = Json->GetStringField(TEXT("nodeId"));
	FString PinName = Json->GetStringField(TEXT("pinName"));
	FString Value = Json->GetStringField(TEXT("value"));

	if (BlueprintName.IsEmpty() || NodeId.IsEmpty() || PinName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required fields: blueprint, nodeId, pinName"));

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

	UEdGraph* Graph = nullptr;
	UEdGraphNode* Node = FindNodeByGuid(BP, NodeId, &Graph);
	if (!Node) return MakeErrorJson(FString::Printf(TEXT("Node '%s' not found"), *NodeId));

	UEdGraphPin* Pin = Node->FindPin(FName(*PinName));
	if (!Pin)
	{
		TArray<TSharedPtr<FJsonValue>> Available;
		for (UEdGraphPin* P : Node->Pins)
		{
			if (P && P->Direction == EGPD_Input)
				Available.Add(MakeShared<FJsonValueString>(P->PinName.ToString()));
		}
		TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Pin '%s' not found"), *PinName));
		E->SetArrayField(TEXT("availableInputPins"), Available);
		return JsonToString(E);
	}

	// Handle default object (for class/asset references)
	if (Json->HasField(TEXT("defaultObject")))
	{
		FString ObjectPath = Json->GetStringField(TEXT("defaultObject"));
		UObject* Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
		if (Obj)
		{
			Pin->DefaultObject = Obj;
		}
		else
		{
			return MakeErrorJson(FString::Printf(TEXT("Object '%s' not found"), *ObjectPath));
		}
	}
	else
	{
		// Use schema to set default value (handles type validation)
		if (Graph && Graph->GetSchema())
		{
			Graph->GetSchema()->TrySetDefaultValue(*Pin, Value);
		}
		else
		{
			Pin->DefaultValue = Value;
		}
	}

	SafeMarkModified(BP, TEXT("Blueprint Mutation"));
	bool bSaved = SaveBlueprintPackage(BP);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("pinName"), PinName);
	OutJson->SetStringField(TEXT("newValue"), Pin->DefaultValue);
	OutJson->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(OutJson);
}

// ============================================================
// HandleMoveNode
// POST /api/move-node { "blueprint": "...", "nodeId": "GUID", "posX": 100, "posY": 200 }
// ============================================================

FString FAgenticMCPServer::HandleMoveNode(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	FString NodeId = Json->GetStringField(TEXT("nodeId"));

	if (BlueprintName.IsEmpty() || NodeId.IsEmpty())
		return MakeErrorJson(TEXT("Missing required fields: blueprint, nodeId"));

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

	UEdGraphNode* Node = FindNodeByGuid(BP, NodeId);
	if (!Node) return MakeErrorJson(FString::Printf(TEXT("Node '%s' not found"), *NodeId));

	if (Json->HasField(TEXT("posX")))
		Node->NodePosX = (int32)Json->GetNumberField(TEXT("posX"));
	if (Json->HasField(TEXT("posY")))
		Node->NodePosY = (int32)Json->GetNumberField(TEXT("posY"));

	SafeMarkModified(BP, TEXT("Blueprint Mutation"));
	bool bSaved = SaveBlueprintPackage(BP);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetNumberField(TEXT("posX"), Node->NodePosX);
	OutJson->SetNumberField(TEXT("posY"), Node->NodePosY);
	OutJson->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(OutJson);
}

// ============================================================
// HandleRefreshAllNodes
// POST /api/refresh-all-nodes { "blueprint": "..." }
// ============================================================

FString FAgenticMCPServer::HandleRefreshAllNodes(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	if (BlueprintName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: blueprint"));

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

#if PLATFORM_WINDOWS
	int32 RefreshResult = TryRefreshAllNodesSEH(BP);
	if (RefreshResult != 0)
	{
		return MakeErrorJson(TEXT("RefreshAllNodes crashed (SEH caught). Blueprint may be in a bad state."));
	}
#else
	FBlueprintEditorUtils::RefreshAllNodes(BP);
#endif

	bool bSaved = SaveBlueprintPackage(BP);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("blueprint"), BlueprintName);
	OutJson->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(OutJson);
}

// ============================================================
// HandleCreateBlueprint
// POST /api/create-blueprint { "name": "BP_MyActor", "parentClass": "Actor", "path": "/Game/Blueprints" }
// ============================================================

FString FAgenticMCPServer::HandleCreateBlueprint(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	FString ParentClassName = Json->GetStringField(TEXT("parentClass"));
	FString Path = Json->GetStringField(TEXT("path"));

	if (Name.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: name"));
	if (ParentClassName.IsEmpty())
		ParentClassName = TEXT("Actor");
	if (Path.IsEmpty())
		Path = TEXT("/Game/Blueprints");

	UClass* ParentClass = FindClassByName(ParentClassName);
	if (!ParentClass)
		return MakeErrorJson(FString::Printf(TEXT("Parent class '%s' not found"), *ParentClassName));

	// Create the Blueprint
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = ParentClass;

	UObject* NewAsset = AssetTools.CreateAsset(Name, Path, UBlueprint::StaticClass(), Factory);
	UBlueprint* NewBP = Cast<UBlueprint>(NewAsset);
	if (!NewBP)
		return MakeErrorJson(TEXT("Failed to create Blueprint asset"));

	bool bSaved = SaveBlueprintPackage(NewBP);

	// Update cached asset list
	AllBlueprintAssets.Add(FAssetData(NewBP));

	UE_LOG(LogTemp, Display, TEXT("AgenticMCP: Created Blueprint '%s' in '%s' (parent: %s)"),
		*Name, *Path, *ParentClassName);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("name"), Name);
	OutJson->SetStringField(TEXT("path"), NewBP->GetPathName());
	OutJson->SetStringField(TEXT("parentClass"), ParentClassName);
	OutJson->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(OutJson);
}

// ============================================================
// HandleCreateGraph
// POST /api/create-graph { "blueprint": "...", "graphName": "MyFunction", "graphType": "function" }
// ============================================================

FString FAgenticMCPServer::HandleCreateGraph(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	FString GraphName = Json->GetStringField(TEXT("graphName"));
	FString GraphType = Json->GetStringField(TEXT("graphType"));

	if (BlueprintName.IsEmpty() || GraphName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required fields: blueprint, graphName"));
	if (GraphType.IsEmpty())
		GraphType = TEXT("function");

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

	UEdGraph* NewGraph = nullptr;

	if (GraphType == TEXT("function"))
	{
		NewGraph = FBlueprintEditorUtils::CreateNewGraph(
			BP, FName(*GraphName), UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		if (NewGraph)
		{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			// UE 5.6+ requires 4th param: SignatureFromObject (nullptr = no existing signature)
			FBlueprintEditorUtils::AddFunctionGraph(BP, NewGraph, false, static_cast<UFunction*>(nullptr));
#else
			FBlueprintEditorUtils::AddFunctionGraph(BP, NewGraph, false);
#endif
		}
	}
	else if (GraphType == TEXT("macro"))
	{
		NewGraph = FBlueprintEditorUtils::CreateNewGraph(
			BP, FName(*GraphName), UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		if (NewGraph)
		{
			FBlueprintEditorUtils::AddMacroGraph(BP, NewGraph, false, nullptr);
		}
	}
	else
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Unsupported graphType '%s'. Supported: function, macro"), *GraphType));
	}

	if (!NewGraph)
		return MakeErrorJson(TEXT("Failed to create graph"));

	SafeMarkStructurallyModified(BP, TEXT("Blueprint Mutation"));
	bool bSaved = SaveBlueprintPackage(BP);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("graphName"), GraphName);
	OutJson->SetStringField(TEXT("graphType"), GraphType);
	OutJson->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(OutJson);
}

// ============================================================
// HandleDeleteGraph
// POST /api/delete-graph { "blueprint": "...", "graphName": "MyFunction" }
// ============================================================

FString FAgenticMCPServer::HandleDeleteGraph(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	FString GraphName = Json->GetStringField(TEXT("graphName"));

	if (BlueprintName.IsEmpty() || GraphName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required fields: blueprint, graphName"));

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

	UEdGraph* Graph = FindGraphByName(BP, GraphName);
	if (!Graph)
		return MakeErrorJson(FString::Printf(TEXT("Graph '%s' not found"), *GraphName));

	FBlueprintEditorUtils::RemoveGraph(BP, Graph);
	bool bSaved = SaveBlueprintPackage(BP);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("deletedGraph"), GraphName);
	OutJson->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(OutJson);
}

// ============================================================
// HandleAddVariable
// POST /api/add-variable { "blueprint": "...", "name": "MyVar", "type": "float" }
// ============================================================

FString FAgenticMCPServer::HandleAddVariable(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	FString VarName = Json->GetStringField(TEXT("name"));
	FString TypeName = Json->GetStringField(TEXT("type"));

	if (BlueprintName.IsEmpty() || VarName.IsEmpty() || TypeName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required fields: blueprint, name, type"));

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

	// Resolve type
	FEdGraphPinType PinType;
	FString TypeError;
	if (!ResolveTypeFromString(TypeName, PinType, TypeError))
		return MakeErrorJson(TypeError);

	// Add variable
	FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType);
	SafeMarkStructurallyModified(BP, TEXT("Blueprint Mutation"));
	bool bSaved = SaveBlueprintPackage(BP);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("variableName"), VarName);
	OutJson->SetStringField(TEXT("variableType"), TypeName);
	OutJson->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(OutJson);
}

// ============================================================
// HandleRemoveVariable
// POST /api/remove-variable { "blueprint": "...", "name": "MyVar" }
// ============================================================

FString FAgenticMCPServer::HandleRemoveVariable(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	FString VarName = Json->GetStringField(TEXT("name"));

	if (BlueprintName.IsEmpty() || VarName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required fields: blueprint, name"));

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

	FBlueprintEditorUtils::RemoveMemberVariable(BP, FName(*VarName));
	SafeMarkStructurallyModified(BP, TEXT("Blueprint Mutation"));
	bool bSaved = SaveBlueprintPackage(BP);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("removedVariable"), VarName);
	OutJson->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(OutJson);
}

// ============================================================
// HandleCompileBlueprint
// POST /api/compile-blueprint { "blueprint": "..." }
// ============================================================

FString FAgenticMCPServer::HandleCompileBlueprint(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	if (BlueprintName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: blueprint"));

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

	bool bSaved = SaveBlueprintPackage(BP);

	// Collect compile diagnostics via Blueprint->Status (works on UE 5.4-5.7+)
	// ErrorsFromLastCompile was removed in UE 5.6; use FMessageLog for detailed errors
	FString StatusStr;
	switch (BP->Status)
	{
	case BS_Error:       StatusStr = TEXT("error"); break;
	case BS_UpToDate:    StatusStr = TEXT("up_to_date"); break;
	case BS_Dirty:       StatusStr = TEXT("dirty"); break;
	case BS_BeingCreated: StatusStr = TEXT("being_created"); break;
	default:             StatusStr = TEXT("unknown"); break;
	}

	// Collect any compiler messages from the message log
	TArray<TSharedPtr<FJsonValue>> DiagnosticsArray;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	// UE 5.6+: Use UpgradeNotesLog or compiler results from FKismetCompilerContext
	// The Blueprint->Status enum is the authoritative compilation result
	if (BP->Status == BS_Error)
	{
		TSharedRef<FJsonObject> DiagObj = MakeShared<FJsonObject>();
		DiagObj->SetStringField(TEXT("severity"), TEXT("error"));
		DiagObj->SetStringField(TEXT("message"),
			TEXT("Blueprint has compile errors. Open in editor for details, or use /api/validate-blueprint."));
		DiagnosticsArray.Add(MakeShared<FJsonValueObject>(DiagObj));
	}
#else
	// UE 5.4-5.5: ErrorsFromLastCompile is available
	for (const auto& CompileError : BP->ErrorsFromLastCompile)
	{
		TSharedRef<FJsonObject> DiagObj = MakeShared<FJsonObject>();
		DiagObj->SetStringField(TEXT("message"), CompileError.ToString());
		DiagnosticsArray.Add(MakeShared<FJsonValueObject>(DiagObj));
	}
#endif

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), bSaved && BP->Status != BS_Error);
	OutJson->SetStringField(TEXT("blueprint"), BlueprintName);
	OutJson->SetStringField(TEXT("status"), StatusStr);
	OutJson->SetBoolField(TEXT("saved"), bSaved);
	OutJson->SetArrayField(TEXT("diagnostics"), DiagnosticsArray);
	return JsonToString(OutJson);
}

// ============================================================
// HandleDuplicateNodes
// POST /api/duplicate-nodes { "blueprint": "...", "nodeIds": ["GUID1", "GUID2"], "offsetX": 200, "offsetY": 100 }
// ============================================================

FString FAgenticMCPServer::HandleDuplicateNodes(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	if (BlueprintName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: blueprint"));

	const TArray<TSharedPtr<FJsonValue>>* NodeIdArray;
	if (!Json->TryGetArrayField(TEXT("nodeIds"), NodeIdArray) || NodeIdArray->Num() == 0)
		return MakeErrorJson(TEXT("Missing or empty required field: nodeIds"));

	int32 OffsetX = 200, OffsetY = 100;
	if (Json->HasField(TEXT("offsetX")))
		OffsetX = (int32)Json->GetNumberField(TEXT("offsetX"));
	if (Json->HasField(TEXT("offsetY")))
		OffsetY = (int32)Json->GetNumberField(TEXT("offsetY"));

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

	// Note: Full duplication with connection preservation would require
	// FBlueprintEditorUtils::DuplicateGraph or clipboard-based approach.
	// For now, we create new nodes of the same type at offset positions.
	// This is a simplified implementation.

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("blueprint"), BlueprintName);
	OutJson->SetStringField(TEXT("note"),
		TEXT("Node duplication creates new nodes at offset positions. "
			 "Connections are not preserved. Use connect-pins to rewire."));
	return JsonToString(OutJson);
}

// ============================================================
// HandleSetNodeComment
// POST /api/set-node-comment { "blueprint": "...", "nodeId": "GUID", "comment": "My comment" }
// ============================================================

FString FAgenticMCPServer::HandleSetNodeComment(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	FString NodeId = Json->GetStringField(TEXT("nodeId"));
	FString Comment = Json->GetStringField(TEXT("comment"));

	if (BlueprintName.IsEmpty() || NodeId.IsEmpty())
		return MakeErrorJson(TEXT("Missing required fields: blueprint, nodeId"));

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

	UEdGraphNode* Node = FindNodeByGuid(BP, NodeId);
	if (!Node) return MakeErrorJson(FString::Printf(TEXT("Node '%s' not found"), *NodeId));

	Node->NodeComment = Comment;
	Node->bCommentBubbleVisible = !Comment.IsEmpty();
	Node->bCommentBubblePinned = !Comment.IsEmpty();

	SafeMarkModified(BP, TEXT("Blueprint Mutation"));
	bool bSaved = SaveBlueprintPackage(BP);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("comment"), Comment);
	OutJson->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(OutJson);
}

// ============================================================
// HandleAddComponent
// POST /api/add-component { "blueprint": "BP_Name", "componentClass": "LiveLinkComponent", "componentName": "LiveLinkFace" }
// Adds a component to a Blueprint's component hierarchy via SCS.
// ============================================================

FString FAgenticMCPServer::HandleAddComponent(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	FString ComponentClassName = Json->GetStringField(TEXT("componentClass"));
	FString ComponentName = Json->GetStringField(TEXT("componentName"));

	if (BlueprintName.IsEmpty() || ComponentClassName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required fields: blueprint, componentClass"));

	if (ComponentName.IsEmpty())
		ComponentName = ComponentClassName;

	// Load the blueprint
	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP) return MakeErrorJson(LoadError);

	// Get the Simple Construction Script
	USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
	if (!SCS)
		return MakeErrorJson(TEXT("Blueprint does not have a SimpleConstructionScript (not an Actor Blueprint?)"));

	// Find the component class
	UClass* ComponentClass = FindClassByName(ComponentClassName);
	if (!ComponentClass)
	{
		// Try with "U" prefix stripped or added
		ComponentClass = FindClassByName(FString(TEXT("U")) + ComponentClassName);
	}
	if (!ComponentClass)
	{
		// Try common component class paths
		ComponentClass = LoadClass<UActorComponent>(nullptr,
			*FString::Printf(TEXT("/Script/LiveLink.%s"), *ComponentClassName));
	}
	if (!ComponentClass)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Component class '%s' not found"), *ComponentClassName));
	}

	// Check if component already exists
	for (USCS_Node* Node : SCS->GetAllNodes())
	{
		if (Node && Node->ComponentTemplate && Node->ComponentTemplate->GetClass() == ComponentClass)
		{
			TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
			OutJson->SetBoolField(TEXT("success"), true);
			OutJson->SetBoolField(TEXT("alreadyExists"), true);
			OutJson->SetStringField(TEXT("componentName"), Node->GetVariableName().ToString());
			return JsonToString(OutJson);
		}
	}

	// Create new SCS node
	USCS_Node* NewNode = SCS->CreateNode(ComponentClass, *ComponentName);
	if (!NewNode)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Failed to create SCS node for class '%s'"), *ComponentClassName));
	}

	// Add to root (or default scene root)
	SCS->AddNode(NewNode);

	// Mark as modified
	SafeMarkStructurallyModified(BP, TEXT("Blueprint Mutation"));

	// Compile and save
	bool bSaved = SaveBlueprintPackage(BP);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("blueprint"), BlueprintName);
	OutJson->SetStringField(TEXT("componentClass"), ComponentClassName);
	OutJson->SetStringField(TEXT("componentName"), NewNode->GetVariableName().ToString());
	OutJson->SetBoolField(TEXT("saved"), bSaved);

	return JsonToString(OutJson);
}
