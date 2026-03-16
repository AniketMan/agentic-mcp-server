// Handlers_WaterSystem.cpp
// Water system handlers for AgenticMCP.
// UE 5.6 target. Water bodies, ocean, rivers, lakes.
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

DEFINE_LOG_CATEGORY_STATIC(LogMCPWater, Log, All);

// ============================================================
// waterList
// List all water bodies in the scene
// ============================================================
FString FAgenticMCPServer::HandleWaterList(const FString& Body)
{
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return MakeErrorJson(TEXT("No editor world"));
    }

<<<<<<< HEAD
    UClass* WaterBodyClass = FindObject<UClass>(nullptr, TEXT("WaterBody"));
=======
    UClass* WaterBodyClass = FindFirstObject<UClass>(TEXT("WaterBody"), EFindFirstObjectOptions::NativeFirst);
>>>>>>> dff5884439a2782dee312ccab688904ae4de2c17

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteValue(TEXT("waterPluginAvailable"), WaterBodyClass != nullptr);

    Writer->WriteArrayStart(TEXT("waterBodies"));
    if (WaterBodyClass)
    {
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if ((*It)->IsA(WaterBodyClass))
            {
                Writer->WriteObjectStart();
                Writer->WriteValue(TEXT("name"), (*It)->GetActorLabel());
                Writer->WriteValue(TEXT("class"), (*It)->GetClass()->GetName());
                FVector Loc = (*It)->GetActorLocation();
                Writer->WriteValue(TEXT("locationX"), Loc.X);
                Writer->WriteValue(TEXT("locationY"), Loc.Y);
                Writer->WriteValue(TEXT("locationZ"), Loc.Z);
                Writer->WriteObjectEnd();
            }
        }
    }
    Writer->WriteArrayEnd();

    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// waterCreate
// Create a water body actor
// Params: waterType (string: Ocean/River/Lake/Custom, default Ocean)
//         name (string, optional), location (object: x,y,z)
// ============================================================
FString FAgenticMCPServer::HandleWaterCreate(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString WaterType = Json->HasField(TEXT("waterType")) ? Json->GetStringField(TEXT("waterType")) : TEXT("Ocean");
    FString Name = Json->HasField(TEXT("name")) ? Json->GetStringField(TEXT("name")) : WaterType;

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return MakeErrorJson(TEXT("No editor world"));
    }

    // Map water type to class name
    FString ClassName;
    if (WaterType == TEXT("Ocean")) ClassName = TEXT("WaterBodyOcean");
    else if (WaterType == TEXT("River")) ClassName = TEXT("WaterBodyRiver");
    else if (WaterType == TEXT("Lake")) ClassName = TEXT("WaterBodyLake");
    else ClassName = TEXT("WaterBodyCustom");

<<<<<<< HEAD
    UClass* WaterClass = FindObject<UClass>(nullptr, *ClassName);
=======
    UClass* WaterClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
>>>>>>> dff5884439a2782dee312ccab688904ae4de2c17
    if (!WaterClass)
    {
        return MakeErrorJson(FString::Printf(TEXT("Water class not found: %s. Enable the Water plugin."), *ClassName));
    }

    FVector Location(0, 0, 0);
    if (Json->HasField(TEXT("location")))
    {
        auto Loc = Json->GetObjectField(TEXT("location"));
        Location.X = Loc->GetNumberField(TEXT("x"));
        Location.Y = Loc->GetNumberField(TEXT("y"));
        Location.Z = Loc->GetNumberField(TEXT("z"));
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    FTransform SpawnTransform(FRotator::ZeroRotator, Location);
    AActor* NewActor = World->SpawnActor<AActor>(WaterClass, SpawnTransform, Params);
    if (!NewActor)
    {
        return MakeErrorJson(TEXT("Failed to spawn water body"));
    }

    NewActor->SetActorLabel(*Name);

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteValue(TEXT("name"), Name);
    Writer->WriteValue(TEXT("waterType"), WaterType);
    Writer->WriteValue(TEXT("class"), NewActor->GetClass()->GetName());
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// waterSetProperties
// Set water body properties
// Params: actorName (string), waveAmplitude (float), waveFrequency (float)
//         waterColor (object: r,g,b), opacity (float), maxWaveHeight (float)
// ============================================================
FString FAgenticMCPServer::HandleWaterSetProperties(const FString& Body)
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

    // Build property commands
    TArray<FString> PropCmds;
    if (Json->HasField(TEXT("waveAmplitude")))
        PropCmds.Add(FString::Printf(TEXT("wb.set_editor_property('wave_amplitude', %f)"), Json->GetNumberField(TEXT("waveAmplitude"))));
    if (Json->HasField(TEXT("waveFrequency")))
        PropCmds.Add(FString::Printf(TEXT("wb.set_editor_property('wave_frequency', %f)"), Json->GetNumberField(TEXT("waveFrequency"))));
    if (Json->HasField(TEXT("opacity")))
        PropCmds.Add(FString::Printf(TEXT("wb.set_editor_property('opacity', %f)"), Json->GetNumberField(TEXT("opacity"))));
    if (Json->HasField(TEXT("maxWaveHeight")))
        PropCmds.Add(FString::Printf(TEXT("wb.set_editor_property('max_wave_height', %f)"), Json->GetNumberField(TEXT("maxWaveHeight"))));

    FString AllProps;
    for (const FString& Cmd : PropCmds) { AllProps += Cmd + TEXT("; "); }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "actors = unreal.EditorLevelLibrary.get_all_level_actors(); "
             "wb = next((a for a in actors if a.get_actor_label() == '%s'), None); "
             "if wb: %s unreal.EditorAssetLibrary.save_loaded_asset(wb); print('SUCCESS') "
             "else: print('NOT_FOUND')"),
        *ActorName, *AllProps);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        {
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("actorName"), ActorName);
            Writer->WriteValue(TEXT("propertiesSet"), PropCmds.Num());
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}
