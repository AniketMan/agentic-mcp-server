// Handlers_VCam.cpp
// Virtual Camera handlers for AgenticMCP.
// UE 5.6 target. VCam component, modifiers, output providers.
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
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPVCam, Log, All);

// ============================================================
// vcamList
// List all VCam-capable actors in the scene
// ============================================================
FString FAgenticMCPServer::HandleVCamList(const FString& Body)
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
    Writer->WriteArrayStart(TEXT("cameras"));

    for (TActorIterator<ACameraActor> It(World); It; ++It)
    {
        ACameraActor* Cam = *It;
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("name"), Cam->GetActorLabel());
        Writer->WriteValue(TEXT("class"), Cam->GetClass()->GetName());
        FVector Loc = Cam->GetActorLocation();
        FRotator Rot = Cam->GetActorRotation();
        Writer->WriteValue(TEXT("locationX"), Loc.X);
        Writer->WriteValue(TEXT("locationY"), Loc.Y);
        Writer->WriteValue(TEXT("locationZ"), Loc.Z);
        Writer->WriteValue(TEXT("rotationPitch"), Rot.Pitch);
        Writer->WriteValue(TEXT("rotationYaw"), Rot.Yaw);
        Writer->WriteValue(TEXT("rotationRoll"), Rot.Roll);

        // Check for VCam component
        UActorComponent* VCamComp = Cam->FindComponentByClass(
<<<<<<< HEAD
            FindObject<UClass>(nullptr, TEXT("VCamComponent")));
=======
            FindFirstObject<UClass>(TEXT("VCamComponent"), EFindFirstObjectOptions::NativeFirst));
>>>>>>> dff5884439a2782dee312ccab688904ae4de2c17
        Writer->WriteValue(TEXT("hasVCamComponent"), VCamComp != nullptr);
        Writer->WriteObjectEnd();
    }

    Writer->WriteArrayEnd();
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// vcamCreate
// Create a VCam actor with VCam component
// Params: name (string, optional), location (object: x,y,z), rotation (object: pitch,yaw,roll)
// ============================================================
FString FAgenticMCPServer::HandleVCamCreate(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return MakeErrorJson(TEXT("No editor world"));
    }

    FVector Location(0, 0, 100);
    if (Json->HasField(TEXT("location")))
    {
        auto Loc = Json->GetObjectField(TEXT("location"));
        Location.X = Loc->GetNumberField(TEXT("x"));
        Location.Y = Loc->GetNumberField(TEXT("y"));
        Location.Z = Loc->GetNumberField(TEXT("z"));
    }

    FRotator Rotation(0, 0, 0);
    if (Json->HasField(TEXT("rotation")))
    {
        auto Rot = Json->GetObjectField(TEXT("rotation"));
        Rotation.Pitch = Rot->HasField(TEXT("pitch")) ? Rot->GetNumberField(TEXT("pitch")) : 0;
        Rotation.Yaw = Rot->HasField(TEXT("yaw")) ? Rot->GetNumberField(TEXT("yaw")) : 0;
        Rotation.Roll = Rot->HasField(TEXT("roll")) ? Rot->GetNumberField(TEXT("roll")) : 0;
    }

    // Spawn a CameraActor first
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    FTransform SpawnTransform(Rotation, Location);
    ACameraActor* Cam = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), SpawnTransform, Params);
    if (!Cam)
    {
        return MakeErrorJson(TEXT("Failed to spawn camera actor"));
    }

    FString Name = Json->HasField(TEXT("name")) ? Json->GetStringField(TEXT("name")) : TEXT("VCam");
    Cam->SetActorLabel(*Name);

    // Try to add VCam component dynamically
<<<<<<< HEAD
    UClass* VCamClass = FindObject<UClass>(nullptr, TEXT("VCamComponent"));
=======
    UClass* VCamClass = FindFirstObject<UClass>(TEXT("VCamComponent"), EFindFirstObjectOptions::NativeFirst);
>>>>>>> dff5884439a2782dee312ccab688904ae4de2c17
    if (VCamClass)
    {
        UActorComponent* VCamComp = NewObject<UActorComponent>(Cam, VCamClass, TEXT("VCamComponent"));
        if (VCamComp)
        {
            Cam->AddInstanceComponent(VCamComp);
            VCamComp->RegisterComponent();
        }
    }

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteValue(TEXT("name"), Name);
    Writer->WriteValue(TEXT("hasVCamComponent"), VCamClass != nullptr);
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// vcamAddModifier
// Add a modifier to a VCam component
// Params: actorName (string), modifierClass (string)
//         connectionPoint (string, optional)
// ============================================================
FString FAgenticMCPServer::HandleVCamAddModifier(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString ActorName = Json->GetStringField(TEXT("actorName"));
    FString ModifierClass = Json->GetStringField(TEXT("modifierClass"));

    if (ActorName.IsEmpty() || ModifierClass.IsEmpty())
    {
        return MakeErrorJson(TEXT("actorName and modifierClass are required"));
    }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "actors = unreal.EditorLevelLibrary.get_all_level_actors(); "
             "cam = next((a for a in actors if a.get_actor_label() == '%s'), None); "
             "if cam: "
             "  vcam = cam.get_component_by_class(unreal.VCamComponent); "
             "  if vcam: "
             "    mod_class = unreal.find_class('%s'); "
             "    vcam.add_modifier(mod_class); "
             "    print('SUCCESS') "
             "  else: print('NO_VCAM') "
             "else: print('NO_ACTOR')"),
        *ActorName, *ModifierClass);

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
            Writer->WriteValue(TEXT("modifierClass"), ModifierClass);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}
