// Handlers_PCG.cpp
// Procedural Content Generation (PCG) handlers for AgenticMCP.
// Provides inspection and control of PCG graphs, components, and generation.
//
// Endpoints:
//   pcgListGraphs        - List all PCG graph assets in the project
//   pcgGetGraph           - Get details of a specific PCG graph (nodes, edges, settings)
//   pcgListComponents     - List all actors with PCG components in the current level
//   pcgGetComponent       - Get PCG component details (graph ref, seed, generation status)
//   pcgGenerate           - Trigger PCG generation on a specific actor's PCG component
//   pcgCleanup            - Clean up (remove) generated PCG output from a component
//   pcgSetSeed            - Set the seed on a PCG component
//   pcgSetGraphParameter  - Set an exposed parameter on a PCG graph instance

#include "AgenticMCPServer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"

// PCG headers - guarded for projects that don't have the PCG plugin
#if __has_include("PCGComponent.h")
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGEdge.h"
#include "PCGSettings.h"
#include "PCGSubsystem.h"
#define HAS_PCG_PLUGIN 1
#else
#define HAS_PCG_PLUGIN 0
#endif

// ============================================================
// Helper: Get the current editor world
// ============================================================
static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

// ============================================================
// pcgListGraphs - List all PCG graph assets
// ============================================================
FString FAgenticMCPServer::HandlePCGListGraphs(const FString& Body)
{
#if !HAS_PCG_PLUGIN
	return MakeErrorJson(TEXT("PCG plugin is not available in this build"));
#else
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> PCGAssets;
	AssetRegistry.GetAssetsByClass(UPCGGraphInterface::StaticClass()->GetClassPathName(), PCGAssets, true);

	TArray<TSharedPtr<FJsonValue>> GraphArray;
	for (const FAssetData& Asset : PCGAssets)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		Entry->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
		GraphArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), GraphArray.Num());
	Result->SetArrayField(TEXT("graphs"), GraphArray);
	return JsonToString(Result);
#endif
}

// ============================================================
// pcgGetGraph - Get details of a specific PCG graph
// ============================================================
FString FAgenticMCPServer::HandlePCGGetGraphInfo(const FString& Body)
{
#if !HAS_PCG_PLUGIN
	return MakeErrorJson(TEXT("PCG plugin is not available in this build"));
#else
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString GraphName = Json->GetStringField(TEXT("name"));
	if (GraphName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: name"));

	// Find the PCG graph asset
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> PCGAssets;
	AssetRegistry.GetAssetsByClass(UPCGGraphInterface::StaticClass()->GetClassPathName(), PCGAssets, true);

	UPCGGraph* FoundGraph = nullptr;
	for (const FAssetData& Asset : PCGAssets)
	{
		if (Asset.AssetName.ToString() == GraphName || Asset.GetObjectPathString().Contains(GraphName))
		{
			FoundGraph = Cast<UPCGGraph>(Asset.GetAsset());
			break;
		}
	}

	if (!FoundGraph)
		return MakeErrorJson(FString::Printf(TEXT("PCG graph not found: %s"), *GraphName));

	// Serialize graph info
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), FoundGraph->GetName());
	Result->SetStringField(TEXT("path"), FoundGraph->GetPathName());

	// Nodes
	TArray<TSharedPtr<FJsonValue>> NodeArray;
	for (UPCGNode* Node : FoundGraph->GetNodes())
	{
		if (!Node) continue;
		TSharedRef<FJsonObject> NodeJson = MakeShared<FJsonObject>();
		NodeJson->SetStringField(TEXT("name"), Node->GetName());
		NodeJson->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle().ToString());

		if (const UPCGSettings* Settings = Node->GetSettings())
		{
			NodeJson->SetStringField(TEXT("settingsClass"), Settings->GetClass()->GetName());
		}

		// Node position
		NodeJson->SetNumberField(TEXT("posX"), Node->PositionX);
		NodeJson->SetNumberField(TEXT("posY"), Node->PositionY);

		NodeArray.Add(MakeShared<FJsonValueObject>(NodeJson));
	}
	Result->SetArrayField(TEXT("nodes"), NodeArray);
	Result->SetNumberField(TEXT("nodeCount"), NodeArray.Num());

	return JsonToString(Result);
#endif
}

// ============================================================
// pcgListComponents - List actors with PCG components in the level
// ============================================================
FString FAgenticMCPServer::HandlePCGListComponents(const FString& Body)
{
#if !HAS_PCG_PLUGIN
	return MakeErrorJson(TEXT("PCG plugin is not available in this build"));
#else
	UWorld* World = GetEditorWorld();
	if (!World)
		return MakeErrorJson(TEXT("No editor world available"));

	TArray<TSharedPtr<FJsonValue>> CompArray;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		TArray<UPCGComponent*> PCGComps;
		Actor->GetComponents<UPCGComponent>(PCGComps);

		for (UPCGComponent* Comp : PCGComps)
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
			Entry->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());
			Entry->SetStringField(TEXT("componentName"), Comp->GetName());
			Entry->SetBoolField(TEXT("isGenerated"), Comp->bGenerated);
			Entry->SetNumberField(TEXT("seed"), Comp->Seed);

			if (UPCGGraphInterface* GraphInterface = Comp->GetGraph())
			{
				Entry->SetStringField(TEXT("graphName"), GraphInterface->GetName());
			}
			else
			{
				Entry->SetStringField(TEXT("graphName"), TEXT("(none)"));
			}

			// Location
			FVector Loc = Actor->GetActorLocation();
			Entry->SetNumberField(TEXT("locationX"), Loc.X);
			Entry->SetNumberField(TEXT("locationY"), Loc.Y);
			Entry->SetNumberField(TEXT("locationZ"), Loc.Z);

			CompArray.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), CompArray.Num());
	Result->SetArrayField(TEXT("components"), CompArray);
	return JsonToString(Result);
#endif
}

// ============================================================
// pcgGetComponent - Get detailed PCG component info
// ============================================================
FString FAgenticMCPServer::HandlePCGGetComponent(const FString& Body)
{
#if !HAS_PCG_PLUGIN
	return MakeErrorJson(TEXT("PCG plugin is not available in this build"));
#else
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	if (ActorName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: actorName"));

	UWorld* World = GetEditorWorld();
	if (!World)
		return MakeErrorJson(TEXT("No editor world available"));

	// Find the actor
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorName || (*It)->GetName() == ActorName)
		{
			FoundActor = *It;
			break;
		}
	}
	if (!FoundActor)
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	UPCGComponent* PCGComp = FoundActor->FindComponentByClass<UPCGComponent>();
	if (!PCGComp)
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' has no PCG component"), *ActorName));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("actorName"), FoundActor->GetActorLabel());
	Result->SetStringField(TEXT("componentName"), PCGComp->GetName());
	Result->SetBoolField(TEXT("isGenerated"), PCGComp->bGenerated);
	Result->SetNumberField(TEXT("seed"), PCGComp->Seed);
	Result->SetBoolField(TEXT("generateOnLoad"), PCGComp->bGenerateOnLoad);

	if (UPCGGraphInterface* GraphInterface = PCGComp->GetGraph())
	{
		Result->SetStringField(TEXT("graphName"), GraphInterface->GetName());
		Result->SetStringField(TEXT("graphPath"), GraphInterface->GetPathName());
	}

	return JsonToString(Result);
#endif
}

// ============================================================
// pcgGenerate - Trigger PCG generation on an actor
// ============================================================
FString FAgenticMCPServer::HandlePCGGenerate(const FString& Body)
{
#if !HAS_PCG_PLUGIN
	return MakeErrorJson(TEXT("PCG plugin is not available in this build"));
#else
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	if (ActorName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: actorName"));

	UWorld* World = GetEditorWorld();
	if (!World)
		return MakeErrorJson(TEXT("No editor world available"));

	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorName || (*It)->GetName() == ActorName)
		{
			FoundActor = *It;
			break;
		}
	}
	if (!FoundActor)
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	UPCGComponent* PCGComp = FoundActor->FindComponentByClass<UPCGComponent>();
	if (!PCGComp)
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' has no PCG component"), *ActorName));

	// Trigger generation
	PCGComp->Generate();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), FoundActor->GetActorLabel());
	Result->SetStringField(TEXT("action"), TEXT("generate"));
	Result->SetBoolField(TEXT("isGenerated"), PCGComp->bGenerated);
	return JsonToString(Result);
#endif
}

// ============================================================
// pcgCleanup - Remove generated PCG output
// ============================================================
FString FAgenticMCPServer::HandlePCGCleanup(const FString& Body)
{
#if !HAS_PCG_PLUGIN
	return MakeErrorJson(TEXT("PCG plugin is not available in this build"));
#else
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	if (ActorName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: actorName"));

	UWorld* World = GetEditorWorld();
	if (!World)
		return MakeErrorJson(TEXT("No editor world available"));

	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorName || (*It)->GetName() == ActorName)
		{
			FoundActor = *It;
			break;
		}
	}
	if (!FoundActor)
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	UPCGComponent* PCGComp = FoundActor->FindComponentByClass<UPCGComponent>();
	if (!PCGComp)
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' has no PCG component"), *ActorName));

	PCGComp->Cleanup();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), FoundActor->GetActorLabel());
	Result->SetStringField(TEXT("action"), TEXT("cleanup"));
	Result->SetBoolField(TEXT("isGenerated"), PCGComp->bGenerated);
	return JsonToString(Result);
#endif
}

// ============================================================
// pcgSetSeed - Set the seed on a PCG component
// ============================================================
FString FAgenticMCPServer::HandlePCGSetSeed(const FString& Body)
{
#if !HAS_PCG_PLUGIN
	return MakeErrorJson(TEXT("PCG plugin is not available in this build"));
#else
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	if (ActorName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: actorName"));

	if (!Json->HasField(TEXT("seed")))
		return MakeErrorJson(TEXT("Missing required field: seed"));
	int32 NewSeed = (int32)Json->GetNumberField(TEXT("seed"));

	UWorld* World = GetEditorWorld();
	if (!World)
		return MakeErrorJson(TEXT("No editor world available"));

	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorName || (*It)->GetName() == ActorName)
		{
			FoundActor = *It;
			break;
		}
	}
	if (!FoundActor)
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	UPCGComponent* PCGComp = FoundActor->FindComponentByClass<UPCGComponent>();
	if (!PCGComp)
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' has no PCG component"), *ActorName));

	int32 OldSeed = PCGComp->Seed;
	PCGComp->Seed = NewSeed;

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), FoundActor->GetActorLabel());
	Result->SetNumberField(TEXT("oldSeed"), OldSeed);
	Result->SetNumberField(TEXT("newSeed"), NewSeed);
	return JsonToString(Result);
#endif
}

// ============================================================
// pcgExecuteGraph - Execute a PCG graph by name (standalone, not on a component)
// ============================================================
FString FAgenticMCPServer::HandlePCGExecuteGraph(const FString& Body)
{
#if !HAS_PCG_PLUGIN
	return MakeErrorJson(TEXT("PCG plugin is not available in this build"));
#else
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	if (ActorName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: actorName"));

	FString GraphName = Json->GetStringField(TEXT("graphName"));

	UWorld* World = nullptr;
	if (GEditor)
		World = GEditor->GetEditorWorldContext().World();
	if (!World)
		return MakeErrorJson(TEXT("No editor world available"));

	// Find actor with PCG component
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorName || (*It)->GetName() == ActorName)
		{
			FoundActor = *It;
			break;
		}
	}
	if (!FoundActor)
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	// If a graph name is specified, find and assign it first
	if (!GraphName.IsEmpty())
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> PCGAssets;
		ARM.Get().GetAssetsByClass(UPCGGraphInterface::StaticClass()->GetClassPathName(), PCGAssets, true);

		UPCGGraph* NewGraph = nullptr;
		for (const FAssetData& Asset : PCGAssets)
		{
			if (Asset.AssetName.ToString() == GraphName || Asset.GetObjectPathString().Contains(GraphName))
			{
				NewGraph = Cast<UPCGGraph>(Asset.GetAsset());
				break;
			}
		}

		if (NewGraph)
		{
			UPCGComponent* PCGComp = FoundActor->FindComponentByClass<UPCGComponent>();
			if (PCGComp)
			{
				PCGComp->SetGraph(NewGraph);
			}
		}
	}

	UPCGComponent* PCGComp = FoundActor->FindComponentByClass<UPCGComponent>();
	if (!PCGComp)
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' has no PCG component"), *ActorName));

	PCGComp->Generate();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), FoundActor->GetActorLabel());
	Result->SetStringField(TEXT("action"), TEXT("executeGraph"));
	if (PCGComp->GetGraph())
		Result->SetStringField(TEXT("graphName"), PCGComp->GetGraph()->GetName());
	return JsonToString(Result);
#endif
}

// ============================================================
// pcgGetNodeSettings - Get settings/properties of a specific PCG node
// ============================================================
FString FAgenticMCPServer::HandlePCGGetNodeSettings(const FString& Body)
{
#if !HAS_PCG_PLUGIN
	return MakeErrorJson(TEXT("PCG plugin is not available in this build"));
#else
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString GraphName = Json->GetStringField(TEXT("graphName"));
	if (GraphName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: graphName"));

	FString NodeName = Json->GetStringField(TEXT("nodeName"));
	int32 NodeIndex = Json->HasField(TEXT("nodeIndex")) ? Json->GetIntegerField(TEXT("nodeIndex")) : -1;

	if (NodeName.IsEmpty() && NodeIndex < 0)
		return MakeErrorJson(TEXT("Must provide either nodeName or nodeIndex"));

	// Find graph
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> PCGAssets;
	ARM.Get().GetAssetsByClass(UPCGGraphInterface::StaticClass()->GetClassPathName(), PCGAssets, true);

	UPCGGraph* FoundGraph = nullptr;
	for (const FAssetData& Asset : PCGAssets)
	{
		if (Asset.AssetName.ToString() == GraphName || Asset.GetObjectPathString().Contains(GraphName))
		{
			FoundGraph = Cast<UPCGGraph>(Asset.GetAsset());
			break;
		}
	}
	if (!FoundGraph)
		return MakeErrorJson(FString::Printf(TEXT("PCG graph not found: %s"), *GraphName));

	// Find node
	const TArray<UPCGNode*>& Nodes = FoundGraph->GetNodes();
	UPCGNode* TargetNode = nullptr;

	for (int32 i = 0; i < Nodes.Num(); i++)
	{
		if (!Nodes[i]) continue;
		if (!NodeName.IsEmpty() && (Nodes[i]->GetName() == NodeName || Nodes[i]->GetNodeTitle().ToString() == NodeName))
		{
			TargetNode = Nodes[i];
			break;
		}
		else if (i == NodeIndex)
		{
			TargetNode = Nodes[i];
			break;
		}
	}

	if (!TargetNode)
		return MakeErrorJson(TEXT("PCG node not found"));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("nodeName"), TargetNode->GetName());
	Result->SetStringField(TEXT("nodeTitle"), TargetNode->GetNodeTitle().ToString());
	Result->SetNumberField(TEXT("posX"), TargetNode->PositionX);
	Result->SetNumberField(TEXT("posY"), TargetNode->PositionY);

	const UPCGSettings* Settings = TargetNode->GetSettings();
	if (Settings)
	{
		Result->SetStringField(TEXT("settingsClass"), Settings->GetClass()->GetName());
		Result->SetNumberField(TEXT("seed"), Settings->GetSeed());

		// Serialize all UPROPERTY fields via reflection
		TArray<TSharedPtr<FJsonValue>> PropsArray;
		for (TFieldIterator<FProperty> PropIt(Settings->GetClass()); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit)) continue;

			TSharedRef<FJsonObject> PropJson = MakeShared<FJsonObject>();
			PropJson->SetStringField(TEXT("name"), Prop->GetName());
			PropJson->SetStringField(TEXT("type"), Prop->GetCPPType());

			FString ValueStr;
			const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Settings);
			Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
			PropJson->SetStringField(TEXT("value"), ValueStr);

			PropsArray.Add(MakeShared<FJsonValueObject>(PropJson));
		}
		Result->SetArrayField(TEXT("properties"), PropsArray);
	}

	return JsonToString(Result);
#endif
}

// ============================================================
// pcgSetNodeSettings - Set a property on a PCG node's settings
// ============================================================
FString FAgenticMCPServer::HandlePCGSetNodeSettings(const FString& Body)
{
#if !HAS_PCG_PLUGIN
	return MakeErrorJson(TEXT("PCG plugin is not available in this build"));
#else
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString GraphName = Json->GetStringField(TEXT("graphName"));
	if (GraphName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: graphName"));

	FString NodeName = Json->GetStringField(TEXT("nodeName"));
	int32 NodeIndex = Json->HasField(TEXT("nodeIndex")) ? Json->GetIntegerField(TEXT("nodeIndex")) : -1;

	FString PropertyName = Json->GetStringField(TEXT("propertyName"));
	if (PropertyName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: propertyName"));

	FString PropertyValue = Json->GetStringField(TEXT("propertyValue"));

	// Find graph
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> PCGAssets;
	ARM.Get().GetAssetsByClass(UPCGGraphInterface::StaticClass()->GetClassPathName(), PCGAssets, true);

	UPCGGraph* FoundGraph = nullptr;
	for (const FAssetData& Asset : PCGAssets)
	{
		if (Asset.AssetName.ToString() == GraphName || Asset.GetObjectPathString().Contains(GraphName))
		{
			FoundGraph = Cast<UPCGGraph>(Asset.GetAsset());
			break;
		}
	}
	if (!FoundGraph)
		return MakeErrorJson(FString::Printf(TEXT("PCG graph not found: %s"), *GraphName));

	// Find node
	const TArray<UPCGNode*>& Nodes = FoundGraph->GetNodes();
	UPCGNode* TargetNode = nullptr;

	for (int32 i = 0; i < Nodes.Num(); i++)
	{
		if (!Nodes[i]) continue;
		if (!NodeName.IsEmpty() && (Nodes[i]->GetName() == NodeName || Nodes[i]->GetNodeTitle().ToString() == NodeName))
		{
			TargetNode = Nodes[i];
			break;
		}
		else if (i == NodeIndex)
		{
			TargetNode = Nodes[i];
			break;
		}
	}

	if (!TargetNode)
		return MakeErrorJson(TEXT("PCG node not found"));

	UPCGSettings* Settings = TargetNode->GetSettings();
	if (!Settings)
		return MakeErrorJson(TEXT("Node has no settings object"));

	// Find and set the property
	FProperty* TargetProp = nullptr;
	for (TFieldIterator<FProperty> PropIt(Settings->GetClass()); PropIt; ++PropIt)
	{
		if ((*PropIt)->GetName() == PropertyName)
		{
			TargetProp = *PropIt;
			break;
		}
	}

	if (!TargetProp)
		return MakeErrorJson(FString::Printf(TEXT("Property not found: %s"), *PropertyName));

	void* ValuePtr = TargetProp->ContainerPtrToValuePtr<void>(Settings);
	if (!TargetProp->ImportText_Direct(*PropertyValue, ValuePtr, Settings, PPF_None))
		return MakeErrorJson(FString::Printf(TEXT("Failed to set property '%s' to '%s'"), *PropertyName, *PropertyValue));

	Settings->PostEditChange();
	FoundGraph->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("graphName"), FoundGraph->GetName());
	Result->SetStringField(TEXT("nodeName"), TargetNode->GetName());
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("propertyValue"), PropertyValue);
	return JsonToString(Result);
#endif
}

// ============================================================================
// PCG MUTATION HANDLERS
// ============================================================================

// --- pcgAddNode ---
FString FAgenticMCPServer::HandlePCGAddNode(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString GraphPath = Json->GetStringField(TEXT("graphPath"));
	FString NodeClass = Json->GetStringField(TEXT("nodeClass"));

	if (GraphPath.IsEmpty() || NodeClass.IsEmpty())
		return MakeErrorJson(TEXT("Missing 'graphPath' or 'nodeClass'"));

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!Graph)
		return MakeErrorJson(FString::Printf(TEXT("PCG Graph not found: %s"), *GraphPath));

	UClass* SettingsClass = FindObject<UClass>(ANY_PACKAGE, *NodeClass);
	if (!SettingsClass)
		return MakeErrorJson(FString::Printf(TEXT("Node class not found: %s"), *NodeClass));

	UPCGSettings* Settings = NewObject<UPCGSettings>(Graph, SettingsClass);
	if (!Settings)
		return MakeErrorJson(TEXT("Failed to create node settings"));

	UPCGNode* NewNode = Graph->AddNode(Settings);
	if (!NewNode)
		return MakeErrorJson(TEXT("Failed to add node to graph"));

	double PosX = 0, PosY = 0;
	Json->TryGetNumberField(TEXT("posX"), PosX);
	Json->TryGetNumberField(TEXT("posY"), PosY);
	NewNode->PositionX = (int32)PosX;
	NewNode->PositionY = (int32)PosY;

	Graph->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("nodeId"), NewNode->GetName());
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- pcgRemoveNode ---
FString FAgenticMCPServer::HandlePCGRemoveNode(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString GraphPath = Json->GetStringField(TEXT("graphPath"));
	FString NodeName = Json->GetStringField(TEXT("nodeName"));

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!Graph)
		return MakeErrorJson(FString::Printf(TEXT("PCG Graph not found: %s"), *GraphPath));

	for (UPCGNode* Node : Graph->GetNodes())
	{
		if (Node && Node->GetName() == NodeName)
		{
			Graph->RemoveNode(Node);
			Graph->MarkPackageDirty();

			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("status"), TEXT("ok"));
			Result->SetStringField(TEXT("removed"), NodeName);
			FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Result, W); return Out;
		}
	}

	return MakeErrorJson(FString::Printf(TEXT("Node not found: %s"), *NodeName));
}

// --- pcgConnectNodes ---
FString FAgenticMCPServer::HandlePCGConnectNodes(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString GraphPath = Json->GetStringField(TEXT("graphPath"));
	FString SourceNode = Json->GetStringField(TEXT("sourceNode"));
	FString TargetNode = Json->GetStringField(TEXT("targetNode"));
	FString SourcePin = Json->GetStringField(TEXT("sourcePin"));
	FString TargetPin = Json->GetStringField(TEXT("targetPin"));

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
	if (!Graph)
		return MakeErrorJson(FString::Printf(TEXT("PCG Graph not found: %s"), *GraphPath));

	UPCGNode* SrcNode = nullptr;
	UPCGNode* TgtNode = nullptr;
	for (UPCGNode* Node : Graph->GetNodes())
	{
		if (Node->GetName() == SourceNode) SrcNode = Node;
		if (Node->GetName() == TargetNode) TgtNode = Node;
	}

	if (!SrcNode)
		return MakeErrorJson(FString::Printf(TEXT("Source node not found: %s"), *SourceNode));
	if (!TgtNode)
		return MakeErrorJson(FString::Printf(TEXT("Target node not found: %s"), *TargetNode));

	bool bConnected = Graph->AddEdge(SrcNode, FName(*SourcePin), TgtNode, FName(*TargetPin));
	if (!bConnected)
		return MakeErrorJson(TEXT("Failed to connect nodes"));

	Graph->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- pcgCreateGraph ---
FString FAgenticMCPServer::HandlePCGCreateGraph(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Path = Json->GetStringField(TEXT("path"));
	if (Path.IsEmpty())
		return MakeErrorJson(TEXT("Missing 'path'"));

	FString AssetName = FPaths::GetBaseFilename(Path);
	UPackage* Package = CreatePackage(*Path);
	if (!Package)
		return MakeErrorJson(TEXT("Failed to create package"));

	UPCGGraph* Graph = NewObject<UPCGGraph>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!Graph)
		return MakeErrorJson(TEXT("Failed to create PCG graph"));

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Graph);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("path"), Path);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}
