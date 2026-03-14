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
FString FAgenticMCPServer::HandlePCGGetGraph(const FString& Body)
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
