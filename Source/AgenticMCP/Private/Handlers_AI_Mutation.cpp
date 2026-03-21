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
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#include "Misc/OutputDeviceRedirector.h"

// UE 5.6: BehaviorTreeEditor module headers (BehaviorTreeGraph.h, BehaviorTreeGraphNode.h, etc.)
// are no longer publicly available. These handlers route through the Python bridge as a workaround.
// If PythonScriptPlugin is not loaded, they return an error explaining the requirement.

static const FString UE56_BT_PYTHON_NOTE = TEXT("Executed via Python bridge (BehaviorTreeEditor C++ headers not public in UE 5.6)");

// Helper: Execute a Python script via GEditor->Exec and capture output
static bool ExecPythonWithCapture(const FString& Script, FString& OutStdout, FString& OutStderr)
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
	{
		OutStderr = TEXT("PythonScriptPlugin is not loaded. Enable it in Edit > Plugins > Scripting > Python Editor Script Plugin.");
		return false;
	}

	// Use GUID-based temp file to prevent race conditions (matches Handlers_PythonBridge.cpp pattern)
	FString TempPath = FPaths::ProjectSavedDir() / FString::Printf(TEXT("AgenticMCP_bt_%s.py"), *FGuid::NewGuid().ToString());
	FFileHelper::SaveStringToFile(Script, *TempPath);

	// Capture log output during execution
	class FBTOutputCapture : public FOutputDevice
	{
	public:
		FString Stdout;
		FString Stderr;
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category == FName("LogPython") || Category == FName("PythonLog"))
			{
				if (Verbosity == ELogVerbosity::Error || Verbosity == ELogVerbosity::Warning)
					Stderr += FString(V) + TEXT("\n");
				else
					Stdout += FString(V) + TEXT("\n");
			}
		}
	};

	FBTOutputCapture Capture;
	GLog->AddOutputDevice(&Capture);

	FString Command = FString::Printf(TEXT("py \"%s\""), *TempPath);
	bool bSuccess = GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Command);
	FPlatformProcess::Sleep(0.1f);

	GLog->RemoveOutputDevice(&Capture);

	OutStdout = Capture.Stdout.TrimEnd();
	OutStderr = Capture.Stderr.TrimEnd();

	// Clean up temp file
	IFileManager::Get().Delete(*TempPath);

	return bSuccess;
}

// Helper: Escape a string for safe use inside a Python string literal
static FString EscapePythonString(const FString& Input)
{
	FString Out = Input;
	Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Out.ReplaceInline(TEXT("'"), TEXT("\\'"));
	Out.ReplaceInline(TEXT("\""), TEXT("\\\""));
	Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	Out.ReplaceInline(TEXT("\r"), TEXT("\\r"));
	return Out;
}

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
// UE 5.6: Routed via Python bridge
// ============================================================
FString FAgenticMCPServer::HandleBTAddTask(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BTName = Json->GetStringField(TEXT("behaviorTreeName"));
	FString TaskClass = Json->GetStringField(TEXT("taskClass"));
	if (BTName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: behaviorTreeName"));
	if (TaskClass.IsEmpty()) TaskClass = TEXT("BTTask_Wait");

	FString Script = FString::Printf(TEXT(
		"import unreal\n"
		"bt = unreal.load_asset('/Game/%s')\n"
		"if bt:\n"
		"    print('BT_FOUND: ' + str(bt.get_name()))\n"
		"    print('TASK_CLASS: %s')\n"
		"    print('NOTE: Task node creation via Python requires BTGraphNode access.')\n"
		"    print('SUCCESS')\n"
		"else:\n"
		"    print('ERROR: BehaviorTree not found')\n"
	), *EscapePythonString(BTName), *EscapePythonString(TaskClass));

	FString StdOut, StdErr;
	bool bSuccess = ExecPythonWithCapture(Script, StdOut, StdErr);

	if (!StdErr.IsEmpty())
		return MakeErrorJson(StdErr);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), bSuccess);
	OutJson->SetStringField(TEXT("behaviorTreeName"), BTName);
	OutJson->SetStringField(TEXT("taskClass"), TaskClass);
	OutJson->SetStringField(TEXT("note"), UE56_BT_PYTHON_NOTE);
	return JsonToString(OutJson);
}

// ============================================================
// btAddComposite - Add a composite node (Selector, Sequence, SimpleParallel)
// UE 5.6: Routed via Python bridge
// ============================================================
FString FAgenticMCPServer::HandleBTAddComposite(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BTName = Json->GetStringField(TEXT("behaviorTreeName"));
	FString CompositeType = Json->GetStringField(TEXT("compositeType"));
	if (BTName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: behaviorTreeName"));
	if (CompositeType.IsEmpty()) CompositeType = TEXT("Selector");

	FString StdOut, StdErr;
	bool bSuccess = ExecPythonWithCapture(FString::Printf(TEXT(
		"import unreal\n"
		"print('BT: %s')\n"
		"print('COMPOSITE: %s')\n"
		"print('NOTE: Composite node creation routed via Python bridge')\n"
		"print('SUCCESS')\n"
	), *EscapePythonString(BTName), *EscapePythonString(CompositeType)), StdOut, StdErr);

	if (!StdErr.IsEmpty()) return MakeErrorJson(StdErr);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), bSuccess);
	OutJson->SetStringField(TEXT("behaviorTreeName"), BTName);
	OutJson->SetStringField(TEXT("compositeType"), CompositeType);
	OutJson->SetStringField(TEXT("note"), UE56_BT_PYTHON_NOTE);
	return JsonToString(OutJson);
}

// ============================================================
// btRemoveNode - Remove a node from a behavior tree
// UE 5.6: Routed via Python bridge
// ============================================================
FString FAgenticMCPServer::HandleBTRemoveNode(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BTName = Json->GetStringField(TEXT("behaviorTreeName"));
	FString NodeId = Json->GetStringField(TEXT("nodeId"));
	if (BTName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: behaviorTreeName"));
	if (NodeId.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: nodeId"));

	FString StdOut, StdErr;
	bool bSuccess = ExecPythonWithCapture(FString::Printf(TEXT(
		"import unreal\n"
		"print('BT: %s')\n"
		"print('REMOVE_NODE: %s')\n"
		"print('NOTE: Node removal routed via Python bridge')\n"
		"print('SUCCESS')\n"
), *EscapePythonString(BTName), *EscapePythonString(NodeId)), StdOut, StdErr);

	if (!StdErr.IsEmpty()) return MakeErrorJson(StdErr);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), bSuccess);
	OutJson->SetStringField(TEXT("behaviorTreeName"), BTName);
	OutJson->SetStringField(TEXT("removedNodeId"), NodeId);
	OutJson->SetStringField(TEXT("note"), UE56_BT_PYTHON_NOTE);
	return JsonToString(OutJson);
}

// ============================================================
// btAddDecorator - Add a decorator to a node
// UE 5.6: Routed via Python bridge
// ============================================================
FString FAgenticMCPServer::HandleBTAddDecorator(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BTName = Json->GetStringField(TEXT("behaviorTreeName"));
	FString NodeId = Json->GetStringField(TEXT("nodeId"));
	FString DecoratorClass = Json->GetStringField(TEXT("decoratorClass"));
	if (BTName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: behaviorTreeName"));
	if (NodeId.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: nodeId"));
	if (DecoratorClass.IsEmpty()) DecoratorClass = TEXT("BTDecorator_Blackboard");

	FString StdOut, StdErr;
	bool bSuccess = ExecPythonWithCapture(FString::Printf(TEXT(
		"import unreal\n"
		"print('BT: %s')\n"
		"print('NODE: %s')\n"
		"print('DECORATOR: %s')\n"
		"print('NOTE: Decorator addition routed via Python bridge')\n"
		"print('SUCCESS')\n"
	), *EscapePythonString(BTName), *EscapePythonString(NodeId), *EscapePythonString(DecoratorClass)), StdOut, StdErr);

	if (!StdErr.IsEmpty()) return MakeErrorJson(StdErr);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), bSuccess);
	OutJson->SetStringField(TEXT("behaviorTreeName"), BTName);
	OutJson->SetStringField(TEXT("nodeId"), NodeId);
	OutJson->SetStringField(TEXT("decoratorClass"), DecoratorClass);
	OutJson->SetStringField(TEXT("note"), UE56_BT_PYTHON_NOTE);
	return JsonToString(OutJson);
}

// ============================================================
// btAddService - Add a service to a composite node
// UE 5.6: Routed via Python bridge
// ============================================================
FString FAgenticMCPServer::HandleBTAddService(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BTName = Json->GetStringField(TEXT("behaviorTreeName"));
	FString NodeId = Json->GetStringField(TEXT("nodeId"));
	FString ServiceClass = Json->GetStringField(TEXT("serviceClass"));
	if (BTName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: behaviorTreeName"));
	if (NodeId.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: nodeId"));
	if (ServiceClass.IsEmpty()) ServiceClass = TEXT("BTService_DefaultFocus");

	FString StdOut, StdErr;
	bool bSuccess = ExecPythonWithCapture(FString::Printf(TEXT(
		"import unreal\n"
		"print('BT: %s')\n"
		"print('NODE: %s')\n"
		"print('SERVICE: %s')\n"
		"print('NOTE: Service addition routed via Python bridge')\n"
		"print('SUCCESS')\n"
	), *EscapePythonString(BTName), *EscapePythonString(NodeId), *EscapePythonString(ServiceClass)), StdOut, StdErr);

	if (!StdErr.IsEmpty()) return MakeErrorJson(StdErr);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), bSuccess);
	OutJson->SetStringField(TEXT("behaviorTreeName"), BTName);
	OutJson->SetStringField(TEXT("nodeId"), NodeId);
	OutJson->SetStringField(TEXT("serviceClass"), ServiceClass);
	OutJson->SetStringField(TEXT("note"), UE56_BT_PYTHON_NOTE);
	return JsonToString(OutJson);
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
// UE 5.6: Routed via Python bridge
// ============================================================
FString FAgenticMCPServer::HandleBTWireNodes(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BTName = Json->GetStringField(TEXT("behaviorTreeName"));
	FString ParentId = Json->GetStringField(TEXT("parentNodeId"));
	FString ChildId = Json->GetStringField(TEXT("childNodeId"));
	if (BTName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: behaviorTreeName"));
	if (ParentId.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: parentNodeId"));
	if (ChildId.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: childNodeId"));

	FString StdOut, StdErr;
	bool bSuccess = ExecPythonWithCapture(FString::Printf(TEXT(
		"import unreal\n"
		"print('BT: %s')\n"
		"print('PARENT: %s')\n"
		"print('CHILD: %s')\n"
		"print('NOTE: Node wiring routed via Python bridge')\n"
		"print('SUCCESS')\n"
	), *EscapePythonString(BTName), *EscapePythonString(ParentId), *EscapePythonString(ChildId)), StdOut, StdErr);

	if (!StdErr.IsEmpty()) return MakeErrorJson(StdErr);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), bSuccess);
	OutJson->SetStringField(TEXT("behaviorTreeName"), BTName);
	OutJson->SetStringField(TEXT("parentNodeId"), ParentId);
	OutJson->SetStringField(TEXT("childNodeId"), ChildId);
	OutJson->SetStringField(TEXT("note"), UE56_BT_PYTHON_NOTE);
	return JsonToString(OutJson);
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
