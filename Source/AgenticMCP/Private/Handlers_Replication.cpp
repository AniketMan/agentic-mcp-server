// Handlers_Replication.cpp
// Multiplayer/Replication handlers for AgenticMCP.
// UE 5.6 target. Replication settings, net relevancy, RPCs.
// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPReplication, Log, All);

// ============================================================
// replicationGetSettings
// Get replication settings for an actor
// Params: actorName (string)
// ============================================================
FString FAgenticMCPServer::HandleReplicationGetSettings(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString ActorName = Json->GetStringField(TEXT("actorName"));
    if (ActorName.IsEmpty())
    {
        return MakeErrorJson(TEXT("actorName is required"));
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return MakeErrorJson(TEXT("No editor world"));
    }

    AActor* FoundActor = nullptr;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if ((*It)->GetActorLabel() == ActorName)
        {
            FoundActor = *It;
            break;
        }
    }

    if (!FoundActor)
    {
        return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteValue(TEXT("actorName"), ActorName);
    Writer->WriteValue(TEXT("replicates"), FoundActor->GetIsReplicated());
    Writer->WriteValue(TEXT("alwaysRelevant"), FoundActor->bAlwaysRelevant);
    Writer->WriteValue(TEXT("replicateMovement"), FoundActor->IsReplicatingMovement());
    Writer->WriteValue(TEXT("netCullDistanceSquared"), FoundActor->NetCullDistanceSquared);
    Writer->WriteValue(TEXT("netUpdateFrequency"), FoundActor->NetUpdateFrequency);
    Writer->WriteValue(TEXT("minNetUpdateFrequency"), FoundActor->MinNetUpdateFrequency);
    Writer->WriteValue(TEXT("netPriority"), FoundActor->NetPriority);
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// replicationSetSettings
// Set replication settings for an actor
// Params: actorName (string), replicates (bool), alwaysRelevant (bool)
//         replicateMovement (bool), netUpdateFrequency (float)
//         netCullDistance (float), netPriority (float)
// ============================================================
FString FAgenticMCPServer::HandleReplicationSetSettings(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString ActorName = Json->GetStringField(TEXT("actorName"));
    if (ActorName.IsEmpty())
    {
        return MakeErrorJson(TEXT("actorName is required"));
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return MakeErrorJson(TEXT("No editor world"));
    }

    AActor* FoundActor = nullptr;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if ((*It)->GetActorLabel() == ActorName)
        {
            FoundActor = *It;
            break;
        }
    }

    if (!FoundActor)
    {
        return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    int32 PropsSet = 0;

    if (Json->HasField(TEXT("replicates")))
    {
        FoundActor->SetReplicates(Json->GetBoolField(TEXT("replicates")));
        PropsSet++;
    }
    if (Json->HasField(TEXT("alwaysRelevant")))
    {
        FoundActor->bAlwaysRelevant = Json->GetBoolField(TEXT("alwaysRelevant"));
        PropsSet++;
    }
    if (Json->HasField(TEXT("replicateMovement")))
    {
        FoundActor->SetReplicatingMovement(Json->GetBoolField(TEXT("replicateMovement")));
        PropsSet++;
    }
    if (Json->HasField(TEXT("netUpdateFrequency")))
    {
        FoundActor->NetUpdateFrequency = (float)Json->GetNumberField(TEXT("netUpdateFrequency"));
        PropsSet++;
    }
    if (Json->HasField(TEXT("minNetUpdateFrequency")))
    {
        FoundActor->MinNetUpdateFrequency = (float)Json->GetNumberField(TEXT("minNetUpdateFrequency"));
        PropsSet++;
    }
    if (Json->HasField(TEXT("netCullDistance")))
    {
        FoundActor->NetCullDistanceSquared = FMath::Square((float)Json->GetNumberField(TEXT("netCullDistance")));
        PropsSet++;
    }
    if (Json->HasField(TEXT("netPriority")))
    {
        FoundActor->NetPriority = (float)Json->GetNumberField(TEXT("netPriority"));
        PropsSet++;
    }

    FoundActor->MarkPackageDirty();

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteValue(TEXT("actorName"), ActorName);
    Writer->WriteValue(TEXT("propertiesSet"), PropsSet);
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// replicationList
// List all replicated actors in the scene
// ============================================================
FString FAgenticMCPServer::HandleReplicationList(const FString& Body)
{
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return MakeErrorJson(TEXT("No editor world"));
    }

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteArrayStart(TEXT("replicatedActors"));

    int32 Count = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if ((*It)->GetIsReplicated())
        {
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("name"), (*It)->GetActorLabel());
            Writer->WriteValue(TEXT("class"), (*It)->GetClass()->GetName());
            Writer->WriteValue(TEXT("alwaysRelevant"), (*It)->bAlwaysRelevant);
            Writer->WriteValue(TEXT("netUpdateFrequency"), (*It)->NetUpdateFrequency);
            Writer->WriteValue(TEXT("netPriority"), (*It)->NetPriority);
            Writer->WriteObjectEnd();
            Count++;
        }
    }

    Writer->WriteArrayEnd();
    Writer->WriteValue(TEXT("count"), Count);
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}
