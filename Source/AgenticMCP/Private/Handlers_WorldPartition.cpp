// Handlers_WorldPartition.cpp
// World Partition and Data Layer handlers for AgenticMCP.
// UE 5.6 target.
#include "AgenticMCPServer.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// --- wpGetInfo ---
FString FAgenticMCPServer::HandleWPGetInfo(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
		return MakeErrorJson(TEXT("No editor world"));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("worldPartitionEnabled"), World->GetWorldPartition() != nullptr);

	if (UWorldPartition* WP = World->GetWorldPartition())
	{
		Result->SetBoolField(TEXT("isStreamingEnabled"), WP->IsStreamingEnabled());
	}

	// Data layers
	UDataLayerManager* DLM = UDataLayerManager::GetDataLayerManager(World);
	TArray<TSharedPtr<FJsonValue>> LayersArr;
	if (DLM)
	{
		DLM->ForEachDataLayerInstance([&](UDataLayerInstance* Instance)
		{
			if (Instance)
			{
				TSharedRef<FJsonObject> LayerObj = MakeShared<FJsonObject>();
				LayerObj->SetStringField(TEXT("name"), Instance->GetDataLayerShortName());
				LayerObj->SetStringField(TEXT("label"), Instance->GetDataLayerFullName());
				LayersArr.Add(MakeShared<FJsonValueObject>(LayerObj));
			}
			return true;
		});
	}
	Result->SetArrayField(TEXT("dataLayers"), LayersArr);

	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- wpSetActorDataLayer ---
FString FAgenticMCPServer::HandleWPSetActorDataLayer(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString LayerName = Json->GetStringField(TEXT("dataLayer"));

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
		return MakeErrorJson(TEXT("No editor world"));

	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorName || It->GetName() == ActorName)
		{ FoundActor = *It; break; }
	}
	if (!FoundActor)
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	UDataLayerManager* DLM = UDataLayerManager::GetDataLayerManager(World);
	if (!DLM)
		return MakeErrorJson(TEXT("No DataLayerManager"));

	UDataLayerInstance* Layer = nullptr;
	DLM->ForEachDataLayerInstance([&](UDataLayerInstance* Instance)
	{
		if (Instance && Instance->GetDataLayerShortName() == LayerName)
		{
			Layer = Instance;
			return false;
		}
		return true;
	});

	if (!Layer)
		return MakeErrorJson(FString::Printf(TEXT("Data layer not found: %s"), *LayerName));

	FoundActor->AddDataLayer(Layer);
	FoundActor->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("actor"), ActorName);
	Result->SetStringField(TEXT("dataLayer"), LayerName);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- wpSetActorRuntimeGrid ---
FString FAgenticMCPServer::HandleWPSetActorRuntimeGrid(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString GridName = Json->GetStringField(TEXT("runtimeGrid"));

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
		return MakeErrorJson(TEXT("No editor world"));

	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorName || It->GetName() == ActorName)
		{ FoundActor = *It; break; }
	}
	if (!FoundActor)
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	FoundActor->SetRuntimeGrid(FName(*GridName));
	FoundActor->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("runtimeGrid"), GridName);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}
