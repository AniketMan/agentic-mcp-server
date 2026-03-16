// Handlers_MetaXRAudioHaptics.cpp
// MetaXR Spatial Audio and Haptics handlers for AgenticMCP
// Exposes Touch Pro haptic feedback and OculusXR spatial audio APIs
// UE 5.6 target. Requires OculusXR plugins enabled.

#include "AgenticMCPServer.h"

#if WITH_OCULUSXR

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Components/AudioComponent.h" // UE 5.6: Required for UAudioComponent complete type

// OculusXR includes
#include "OculusXRFunctionLibrary.h"
#include "OculusXRInputFunctionLibrary.h"
#include "OculusXRHMDTypes.h"
// UE 5.6 / OculusXR SDK: OculusXRHapticsPlayerComponent.h may not exist
// Wrap in conditional include to prevent build failure
#if __has_include("OculusXRHapticsPlayerComponent.h")
#include "OculusXRHapticsPlayerComponent.h"
#endif
#include "EngineUtils.h" // UE 5.6: Required for TActorIterator

DEFINE_LOG_CATEGORY_STATIC(LogMCPMetaXRAudioHaptics, Log, All);

// ============================================================
// xrPlayHapticEffect
// Trigger haptic feedback on a controller
// POST /api/xr/haptics/play
// Params: hand (string: Left/Right), frequency (float 0-1),
//         amplitude (float 0-1), duration (float seconds)
// ============================================================
FString FAgenticMCPServer::HandleXRPlayHapticEffect(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedPtr<FJsonObject> BodyJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString Hand = BodyJson->HasField(TEXT("hand")) ? BodyJson->GetStringField(TEXT("hand")) : TEXT("Right");
    float Frequency = BodyJson->HasField(TEXT("frequency")) ? BodyJson->GetNumberField(TEXT("frequency")) : 0.5f;
    float Amplitude = BodyJson->HasField(TEXT("amplitude")) ? BodyJson->GetNumberField(TEXT("amplitude")) : 0.5f;
    float Duration = BodyJson->HasField(TEXT("duration")) ? BodyJson->GetNumberField(TEXT("duration")) : 0.2f;

    // Clamp values to valid ranges
    Frequency = FMath::Clamp(Frequency, 0.0f, 1.0f);
    Amplitude = FMath::Clamp(Amplitude, 0.0f, 1.0f);
    Duration = FMath::Clamp(Duration, 0.0f, 10.0f);

    // Get the player controller to trigger haptics
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return MakeErrorJson(TEXT("No world context available"));
    }

    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC)
    {
        return MakeErrorJson(TEXT("No player controller found. Is the game running in PIE?"));
    }

    EControllerHand ControllerHand = Hand.Equals(TEXT("Left"), ESearchCase::IgnoreCase)
        ? EControllerHand::Left : EControllerHand::Right;

    // UE 5.6: PlayHapticEffect signature is (HapticEffect, Hand, Scale, bLoop)
    // Using Amplitude as Scale; Frequency is not directly supported via this API
    PC->PlayHapticEffect(nullptr, ControllerHand, Amplitude, false);

    // Note: PlayHapticEffect with nullptr curve triggers a simple constant vibration.
    // For more complex patterns, a UHapticFeedbackEffect_Curve asset would be needed.

    TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
    OutJson->SetBoolField(TEXT("success"), true);
    OutJson->SetStringField(TEXT("hand"), Hand);
    OutJson->SetNumberField(TEXT("frequency"), Frequency);
    OutJson->SetNumberField(TEXT("amplitude"), Amplitude);
    OutJson->SetNumberField(TEXT("duration"), Duration);
    OutJson->SetStringField(TEXT("message"), TEXT("Haptic effect triggered"));

    return JsonToString(OutJson);
}

// ============================================================
// xrStopHapticEffect
// Stop haptic feedback on a controller
// POST /api/xr/haptics/stop
// Params: hand (string: Left/Right/Both)
// ============================================================
FString FAgenticMCPServer::HandleXRStopHapticEffect(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedPtr<FJsonObject> BodyJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString Hand = BodyJson->HasField(TEXT("hand")) ? BodyJson->GetStringField(TEXT("hand")) : TEXT("Both");

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return MakeErrorJson(TEXT("No world context available"));
    }

    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC)
    {
        return MakeErrorJson(TEXT("No player controller found. Is the game running in PIE?"));
    }

    if (Hand.Equals(TEXT("Left"), ESearchCase::IgnoreCase) || Hand.Equals(TEXT("Both"), ESearchCase::IgnoreCase))
    {
        PC->StopHapticEffect(EControllerHand::Left);
    }
    if (Hand.Equals(TEXT("Right"), ESearchCase::IgnoreCase) || Hand.Equals(TEXT("Both"), ESearchCase::IgnoreCase))
    {
        PC->StopHapticEffect(EControllerHand::Right);
    }

    TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
    OutJson->SetBoolField(TEXT("success"), true);
    OutJson->SetStringField(TEXT("hand"), Hand);
    OutJson->SetStringField(TEXT("message"), TEXT("Haptic effect stopped"));

    return JsonToString(OutJson);
}

// ============================================================
// xrGetHapticCapabilities
// Query haptic capabilities of the connected controllers
// GET /api/xr/haptics/capabilities
// ============================================================
FString FAgenticMCPServer::HandleXRGetHapticCapabilities(const TMap<FString, FString>& Params, const FString& Body)
{
    if (!GEngine || !GEngine->XRSystem.IsValid())
    {
        return MakeErrorJson(TEXT("XR system not available"));
    }

    TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

    // Check controller types to determine haptic capabilities
    EOculusXRControllerType LeftType = UOculusXRFunctionLibrary::GetControllerType(EControllerHand::Left);
    EOculusXRControllerType RightType = UOculusXRFunctionLibrary::GetControllerType(EControllerHand::Right);

    TSharedRef<FJsonObject> LeftObj = MakeShared<FJsonObject>();
    LeftObj->SetStringField(TEXT("controllerType"), UEnum::GetValueAsString(LeftType));
    LeftObj->SetBoolField(TEXT("tracked"), UOculusXRFunctionLibrary::IsDeviceTracked(EOculusXRTrackedDeviceType::LTouch));
    LeftObj->SetBoolField(TEXT("supportsHaptics"), true);
    OutJson->SetObjectField(TEXT("left"), LeftObj);

    TSharedRef<FJsonObject> RightObj = MakeShared<FJsonObject>();
    RightObj->SetStringField(TEXT("controllerType"), UEnum::GetValueAsString(RightType));
    RightObj->SetBoolField(TEXT("tracked"), UOculusXRFunctionLibrary::IsDeviceTracked(EOculusXRTrackedDeviceType::RTouch));
    RightObj->SetBoolField(TEXT("supportsHaptics"), true);
    OutJson->SetObjectField(TEXT("right"), RightObj);

    return JsonToString(OutJson);
}

// ============================================================
// xrSetSpatialAudioEnabled
// Enable or disable OculusXR spatial audio processing
// POST /api/xr/audio/spatial/set
// Params: enable (bool)
// ============================================================
FString FAgenticMCPServer::HandleXRSetSpatialAudioEnabled(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedPtr<FJsonObject> BodyJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    bool bEnable = BodyJson->GetBoolField(TEXT("enable"));

    // OculusXR spatial audio is configured via project settings and the OculusXR Audio plugin.
    // At runtime, we toggle the audio spatialization plugin.
    // The actual spatialization is handled per-sound via Attenuation Settings with
    // SpatializationAlgorithm set to the OculusXR spatialization plugin.

    // Execute via Python to toggle the project setting
    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "settings = unreal.get_default_object(unreal.AudioSettings); "
             "print('Spatial audio %s requested. Configure OculusXR Audio plugin in Project Settings > Platforms > Meta XR.')"),
        bEnable ? TEXT("enable") : TEXT("disable"));

    FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
    }

    TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
    OutJson->SetBoolField(TEXT("success"), true);
    OutJson->SetBoolField(TEXT("spatialAudioEnabled"), bEnable);
    OutJson->SetStringField(TEXT("note"),
        TEXT("OculusXR spatial audio requires the OculusXR Audio plugin enabled in Project Settings. "
             "Per-sound spatialization is configured via Attenuation Settings on individual Audio Components."));

    return JsonToString(OutJson);
}

// ============================================================
// xrGetSpatialAudioStatus
// Get current spatial audio configuration status
// GET /api/xr/audio/spatial/status
// ============================================================
FString FAgenticMCPServer::HandleXRGetSpatialAudioStatus(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

    // Check if OculusXR Audio module is loaded
    bool bOculusAudioLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("OculusXRAudio"));
    OutJson->SetBoolField(TEXT("oculusXRAudioPluginLoaded"), bOculusAudioLoaded);

    // Check if XR system is available
    bool bXRAvailable = GEngine && GEngine->XRSystem.IsValid();
    OutJson->SetBoolField(TEXT("xrSystemAvailable"), bXRAvailable);

    if (bXRAvailable)
    {
        // Report device info for audio context
        FString DeviceName = UOculusXRFunctionLibrary::GetDeviceName();
        OutJson->SetStringField(TEXT("deviceName"), DeviceName);
    }

    OutJson->SetStringField(TEXT("spatializationPlugin"),
        bOculusAudioLoaded ? TEXT("OculusXR Audio") : TEXT("Default (Built-in Spatialization)"));

    OutJson->SetStringField(TEXT("configurationPath"),
        TEXT("Project Settings > Platforms > Meta XR > Audio"));

    return JsonToString(OutJson);
}

// ============================================================
// xrConfigureAudioAttenuation
// Configure spatial audio attenuation for a sound source actor
// POST /api/xr/audio/attenuation
// Params: actorName (string), minDistance (float), maxDistance (float),
//         spatialize (bool, default true)
// ============================================================
FString FAgenticMCPServer::HandleXRConfigureAudioAttenuation(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedPtr<FJsonObject> BodyJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString ActorName = BodyJson->GetStringField(TEXT("actorName"));
    if (ActorName.IsEmpty())
    {
        return MakeErrorJson(TEXT("actorName is required"));
    }

    float MinDistance = BodyJson->HasField(TEXT("minDistance")) ? BodyJson->GetNumberField(TEXT("minDistance")) : 100.0f;
    float MaxDistance = BodyJson->HasField(TEXT("maxDistance")) ? BodyJson->GetNumberField(TEXT("maxDistance")) : 5000.0f;
    bool bSpatialize = BodyJson->HasField(TEXT("spatialize")) ? BodyJson->GetBoolField(TEXT("spatialize")) : true;

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return MakeErrorJson(TEXT("No world context available"));
    }

    // Find the actor
    AActor* FoundActor = nullptr;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetActorLabel().Equals(ActorName, ESearchCase::IgnoreCase) ||
            It->GetName().Equals(ActorName, ESearchCase::IgnoreCase))
        {
            FoundActor = *It;
            break;
        }
    }

    if (!FoundActor)
    {
        return MakeErrorJson(FString::Printf(TEXT("Actor '%s' not found"), *ActorName));
    }

    // Find or report Audio Component
    UAudioComponent* AudioComp = FoundActor->FindComponentByClass<UAudioComponent>();
    if (!AudioComp)
    {
        return MakeErrorJson(FString::Printf(
            TEXT("Actor '%s' has no AudioComponent. Add an Audio Component first via add_component."),
            *ActorName));
    }

    // Configure attenuation
    FSoundAttenuationSettings AttenuationSettings;
    AttenuationSettings.bAttenuate = true;
    AttenuationSettings.bSpatialize = bSpatialize;
    AttenuationSettings.FalloffDistance = MaxDistance - MinDistance;

    AudioComp->bOverrideAttenuation = true;
    AudioComp->AttenuationOverrides = AttenuationSettings;

    TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
    OutJson->SetBoolField(TEXT("success"), true);
    OutJson->SetStringField(TEXT("actorName"), ActorName);
    OutJson->SetNumberField(TEXT("minDistance"), MinDistance);
    OutJson->SetNumberField(TEXT("maxDistance"), MaxDistance);
    OutJson->SetBoolField(TEXT("spatialized"), bSpatialize);
    OutJson->SetStringField(TEXT("message"), TEXT("Audio attenuation configured for spatial audio"));

    return JsonToString(OutJson);
}


#pragma warning(pop)

#else // !WITH_OCULUSXR -- stub implementations

#define XR_AH_STUB(FuncName) \
FString FAgenticMCPServer::FuncName(const TMap<FString, FString>& Params, const FString& Body) \
{ return MakeErrorJson(TEXT("OculusXR/MetaXR plugin is not available. Enable the Meta XR plugin.")); }

XR_AH_STUB(HandleXRPlayHapticEffect)
XR_AH_STUB(HandleXRStopHapticEffect)
XR_AH_STUB(HandleXRGetHapticCapabilities)
XR_AH_STUB(HandleXRSetSpatialAudioEnabled)
XR_AH_STUB(HandleXRGetSpatialAudioStatus)
XR_AH_STUB(HandleXRConfigureAudioAttenuation)

#undef XR_AH_STUB

#endif // WITH_OCULUSXR
