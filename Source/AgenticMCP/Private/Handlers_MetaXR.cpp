// Copyright (c) Meta Platforms, Inc. All Rights Reserved.
// Meta XR / OculusXR 5.6 endpoint handlers for AgenticMCP

#include "AgenticMCPServer.h"
#include "Misc/App.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// OculusXR 5.6 Headers
#include "OculusXRFunctionLibrary.h"
#include "OculusXRHMDTypes.h"
#include "OculusXRInputFunctionLibrary.h"
#include "OculusXRMovementFunctionLibrary.h"
#include "OculusXRMovementTypes.h"
#include "IOculusXRHMDModule.h"

// ============================================================================
// HMD STATE & DEVICE INFO
// ============================================================================

FString FAgenticMCPServer::HandleXRGetHMDState(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();

    if (!IOculusXRHMDModule::IsAvailable())
    {
        Response->SetStringField(TEXT("status"), TEXT("error"));
        Response->SetStringField(TEXT("message"), TEXT("OculusXR HMD module not available"));
        return JsonObjectToString(Response);
    }

    Response->SetStringField(TEXT("status"), TEXT("success"));

    EOculusXRDeviceType DeviceType = UOculusXRFunctionLibrary::GetDeviceType();
    Response->SetStringField(TEXT("device_type"), StaticEnum<EOculusXRDeviceType>()->GetNameStringByValue((int64)DeviceType));

    FRotator DeviceRotation;
    FVector DevicePosition;
    FVector NeckPosition;
    UOculusXRFunctionLibrary::GetPose(DeviceRotation, DevicePosition, NeckPosition);

    TSharedRef<FJsonObject> PoseObj = MakeShared<FJsonObject>();
    PoseObj->SetNumberField(TEXT("rotation_pitch"), DeviceRotation.Pitch);
    PoseObj->SetNumberField(TEXT("rotation_yaw"), DeviceRotation.Yaw);
    PoseObj->SetNumberField(TEXT("rotation_roll"), DeviceRotation.Roll);
    PoseObj->SetNumberField(TEXT("position_x"), DevicePosition.X);
    PoseObj->SetNumberField(TEXT("position_y"), DevicePosition.Y);
    PoseObj->SetNumberField(TEXT("position_z"), DevicePosition.Z);
    Response->SetObjectField(TEXT("pose"), PoseObj);

    Response->SetBoolField(TEXT("hmd_tracked"), UOculusXRFunctionLibrary::IsDeviceTracked(EOculusXRTrackedDeviceType::HMD));
    Response->SetBoolField(TEXT("has_input_focus"), UOculusXRFunctionLibrary::HasInputFocus());
    Response->SetNumberField(TEXT("display_frequency"), UOculusXRFunctionLibrary::GetCurrentDisplayFrequency());

    bool bGPUAvailable;
    float GPUUtilization;
    UOculusXRFunctionLibrary::GetGPUUtilization(bGPUAvailable, GPUUtilization);
    Response->SetNumberField(TEXT("gpu_utilization"), GPUUtilization);
    Response->SetNumberField(TEXT("gpu_frame_time"), UOculusXRFunctionLibrary::GetGPUFrameTime());

    return JsonObjectToString(Response);
}

// ============================================================================
// FACE TRACKING (OculusXRMovement 5.6)
// ============================================================================

FString FAgenticMCPServer::HandleXRGetFaceTracking(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();

    Response->SetBoolField(TEXT("supported"), UOculusXRMovementFunctionLibrary::IsFaceTrackingSupported());
    Response->SetBoolField(TEXT("enabled"), UOculusXRMovementFunctionLibrary::IsFaceTrackingEnabled());
    Response->SetBoolField(TEXT("visemes_supported"), UOculusXRMovementFunctionLibrary::IsFaceTrackingVisemesSupported());

    if (!UOculusXRMovementFunctionLibrary::IsFaceTrackingEnabled())
    {
        Response->SetStringField(TEXT("status"), TEXT("success"));
        return JsonObjectToString(Response);
    }

    FOculusXRFaceState FaceState;
    if (UOculusXRMovementFunctionLibrary::TryGetFaceState(FaceState))
    {
        Response->SetStringField(TEXT("status"), TEXT("success"));
        Response->SetBoolField(TEXT("valid"), FaceState.bIsValid);
        Response->SetNumberField(TEXT("time"), FaceState.Time);
        Response->SetNumberField(TEXT("expression_count"), FaceState.ExpressionWeights.Num());
    }
    else
    {
        Response->SetStringField(TEXT("status"), TEXT("error"));
        Response->SetStringField(TEXT("message"), TEXT("Failed to get face state"));
    }
    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRSetFaceTracking(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();

    TSharedPtr<FJsonObject> BodyJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
    {
        Response->SetStringField(TEXT("status"), TEXT("error"));
        Response->SetStringField(TEXT("message"), TEXT("Invalid JSON body"));
        return JsonObjectToString(Response);
    }

    bool bSuccess = true;
    if (BodyJson->HasField(TEXT("enabled")))
    {
        if (BodyJson->GetBoolField(TEXT("enabled")))
            bSuccess = UOculusXRMovementFunctionLibrary::StartFaceTracking();
        else
            bSuccess = UOculusXRMovementFunctionLibrary::StopFaceTracking();
    }

    Response->SetStringField(TEXT("status"), bSuccess ? TEXT("success") : TEXT("error"));
    Response->SetBoolField(TEXT("enabled"), UOculusXRMovementFunctionLibrary::IsFaceTrackingEnabled());
    return JsonObjectToString(Response);
}

// ============================================================================
// BODY TRACKING (OculusXRMovement 5.6)
// ============================================================================

FString FAgenticMCPServer::HandleXRGetBodyTracking(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();

    Response->SetBoolField(TEXT("supported"), UOculusXRMovementFunctionLibrary::IsBodyTrackingSupported());
    Response->SetBoolField(TEXT("enabled"), UOculusXRMovementFunctionLibrary::IsBodyTrackingEnabled());

    if (!UOculusXRMovementFunctionLibrary::IsBodyTrackingEnabled())
    {
        Response->SetStringField(TEXT("status"), TEXT("success"));
        return JsonObjectToString(Response);
    }

    FOculusXRBodyState BodyState;
    if (UOculusXRMovementFunctionLibrary::TryGetBodyState(BodyState))
    {
        Response->SetStringField(TEXT("status"), TEXT("success"));
        Response->SetBoolField(TEXT("active"), BodyState.IsActive);
        Response->SetNumberField(TEXT("confidence"), BodyState.Confidence);
        Response->SetNumberField(TEXT("time"), BodyState.Time);
        Response->SetNumberField(TEXT("joint_count"), BodyState.Joints.Num());
    }
    else
    {
        Response->SetStringField(TEXT("status"), TEXT("error"));
        Response->SetStringField(TEXT("message"), TEXT("Failed to get body state"));
    }
    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRSetBodyTracking(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();

    TSharedPtr<FJsonObject> BodyJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
    {
        Response->SetStringField(TEXT("status"), TEXT("error"));
        Response->SetStringField(TEXT("message"), TEXT("Invalid JSON body"));
        return JsonObjectToString(Response);
    }

    bool bSuccess = true;
    if (BodyJson->HasField(TEXT("enabled")))
    {
        if (BodyJson->GetBoolField(TEXT("enabled")))
        {
            EOculusXRBodyJointSet JointSet = EOculusXRBodyJointSet::UpperBody;
            if (BodyJson->HasField(TEXT("joint_set")))
            {
                FString JointSetStr = BodyJson->GetStringField(TEXT("joint_set"));
                if (JointSetStr.Equals(TEXT("FullBody"), ESearchCase::IgnoreCase))
                    JointSet = EOculusXRBodyJointSet::FullBody;
            }
            bSuccess = UOculusXRMovementFunctionLibrary::StartBodyTrackingByJointSet(JointSet);
        }
        else
        {
            bSuccess = UOculusXRMovementFunctionLibrary::StopBodyTracking();
        }
    }

    Response->SetStringField(TEXT("status"), bSuccess ? TEXT("success") : TEXT("error"));
    Response->SetBoolField(TEXT("enabled"), UOculusXRMovementFunctionLibrary::IsBodyTrackingEnabled());
    return JsonObjectToString(Response);
}

// ============================================================================
// HAND TRACKING DETAIL (OculusXRInput 5.6)
// ============================================================================

FString FAgenticMCPServer::HandleXRGetHandTracking(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();

    if (!UOculusXRInputFunctionLibrary::IsHandTrackingEnabled())
    {
        Response->SetStringField(TEXT("status"), TEXT("error"));
        Response->SetStringField(TEXT("message"), TEXT("Hand tracking is not enabled"));
        return JsonObjectToString(Response);
    }

    Response->SetStringField(TEXT("status"), TEXT("success"));

    // Left hand
    TSharedRef<FJsonObject> LeftHand = MakeShared<FJsonObject>();
    LeftHand->SetBoolField(TEXT("position_valid"), UOculusXRInputFunctionLibrary::IsHandPositionValid(EOculusXRHandType::HandLeft));
    LeftHand->SetStringField(TEXT("confidence"),
        StaticEnum<EOculusXRTrackingConfidence>()->GetNameStringByValue(
            (int64)UOculusXRInputFunctionLibrary::GetTrackingConfidence(EOculusXRHandType::HandLeft)));
    LeftHand->SetNumberField(TEXT("scale"), UOculusXRInputFunctionLibrary::GetHandScale(EOculusXRHandType::HandLeft));
    Response->SetObjectField(TEXT("left_hand"), LeftHand);

    // Right hand
    TSharedRef<FJsonObject> RightHand = MakeShared<FJsonObject>();
    RightHand->SetBoolField(TEXT("position_valid"), UOculusXRInputFunctionLibrary::IsHandPositionValid(EOculusXRHandType::HandRight));
    RightHand->SetStringField(TEXT("confidence"),
        StaticEnum<EOculusXRTrackingConfidence>()->GetNameStringByValue(
            (int64)UOculusXRInputFunctionLibrary::GetTrackingConfidence(EOculusXRHandType::HandRight)));
    RightHand->SetNumberField(TEXT("scale"), UOculusXRInputFunctionLibrary::GetHandScale(EOculusXRHandType::HandRight));
    Response->SetObjectField(TEXT("right_hand"), RightHand);

    Response->SetStringField(TEXT("dominant_hand"),
        StaticEnum<EOculusXRHandType>()->GetNameStringByValue(
            (int64)UOculusXRInputFunctionLibrary::GetDominantHand()));

    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRStopHaptic(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();

    UOculusXRInputFunctionLibrary::StopHapticEffect(EControllerHand::Left);
    UOculusXRInputFunctionLibrary::StopHapticEffect(EControllerHand::Right);

    Response->SetStringField(TEXT("status"), TEXT("success"));
    Response->SetStringField(TEXT("stopped"), TEXT("both"));
    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRGetPassthrough(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();

    Response->SetStringField(TEXT("status"), TEXT("success"));
    Response->SetBoolField(TEXT("supported"), UOculusXRFunctionLibrary::IsPassthroughSupported());
    Response->SetBoolField(TEXT("color_supported"), UOculusXRFunctionLibrary::IsColorPassthroughSupported());
    Response->SetBoolField(TEXT("recommended"), UOculusXRFunctionLibrary::IsPassthroughRecommended());
    Response->SetBoolField(TEXT("environment_depth_started"), UOculusXRFunctionLibrary::IsEnvironmentDepthStarted());

    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRSetEyeTracking(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();

    TSharedPtr<FJsonObject> BodyJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
    {
        Response->SetStringField(TEXT("status"), TEXT("error"));
        Response->SetStringField(TEXT("message"), TEXT("Invalid JSON body"));
        return JsonObjectToString(Response);
    }

    bool bEnable = BodyJson->HasField(TEXT("enabled")) ? BodyJson->GetBoolField(TEXT("enabled")) : true;
    bool bSuccess = bEnable ? UOculusXRMovementFunctionLibrary::StartEyeTracking()
                            : UOculusXRMovementFunctionLibrary::StopEyeTracking();

    Response->SetStringField(TEXT("status"), bSuccess ? TEXT("success") : TEXT("error"));
    Response->SetBoolField(TEXT("enabled"), UOculusXRMovementFunctionLibrary::IsEyeTrackingEnabled());
    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRSetTracking(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();

    TSharedPtr<FJsonObject> BodyJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
    {
        Response->SetStringField(TEXT("status"), TEXT("error"));
        Response->SetStringField(TEXT("message"), TEXT("Invalid JSON body"));
        return JsonObjectToString(Response);
    }

    if (BodyJson->HasField(TEXT("position_tracking")))
    {
        UOculusXRFunctionLibrary::EnablePositionTracking(BodyJson->GetBoolField(TEXT("position_tracking")));
    }
    if (BodyJson->HasField(TEXT("orientation_tracking")))
    {
        UOculusXRFunctionLibrary::EnableOrientationTracking(BodyJson->GetBoolField(TEXT("orientation_tracking")));
    }

    Response->SetStringField(TEXT("status"), TEXT("success"));
    Response->SetBoolField(TEXT("hmd_tracked"), UOculusXRFunctionLibrary::IsDeviceTracked(EOculusXRTrackedDeviceType::HMD));
    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRGetControllers(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("status"), TEXT("success"));

    TSharedRef<FJsonObject> LeftController = MakeShared<FJsonObject>();
    LeftController->SetBoolField(TEXT("tracked"), UOculusXRFunctionLibrary::IsDeviceTracked(EOculusXRTrackedDeviceType::LTouch));
    LeftController->SetStringField(TEXT("type"),
        StaticEnum<EOculusXRControllerType>()->GetNameStringByValue((int64)UOculusXRFunctionLibrary::GetControllerType(EControllerHand::Left)));
    Response->SetObjectField(TEXT("left"), LeftController);

    TSharedRef<FJsonObject> RightController = MakeShared<FJsonObject>();
    RightController->SetBoolField(TEXT("tracked"), UOculusXRFunctionLibrary::IsDeviceTracked(EOculusXRTrackedDeviceType::RTouch));
    RightController->SetStringField(TEXT("type"),
        StaticEnum<EOculusXRControllerType>()->GetNameStringByValue((int64)UOculusXRFunctionLibrary::GetControllerType(EControllerHand::Right)));
    Response->SetObjectField(TEXT("right"), RightController);

    Response->SetBoolField(TEXT("hand_tracking_enabled"), UOculusXRInputFunctionLibrary::IsHandTrackingEnabled());
    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRTriggerHaptic(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();

    TSharedPtr<FJsonObject> BodyJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
    {
        Response->SetStringField(TEXT("status"), TEXT("error"));
        Response->SetStringField(TEXT("message"), TEXT("Invalid JSON body"));
        return JsonObjectToString(Response);
    }

    EControllerHand Hand = EControllerHand::Right;
    if (BodyJson->HasField(TEXT("hand")))
    {
        FString HandStr = BodyJson->GetStringField(TEXT("hand"));
        if (HandStr.Equals(TEXT("left"), ESearchCase::IgnoreCase)) Hand = EControllerHand::Left;
    }

    EOculusXRHandHapticsLocation Location = EOculusXRHandHapticsLocation::Hand;
    if (BodyJson->HasField(TEXT("location")))
    {
        FString LocStr = BodyJson->GetStringField(TEXT("location"));
        if (LocStr.Equals(TEXT("thumb"), ESearchCase::IgnoreCase)) Location = EOculusXRHandHapticsLocation::Thumb;
        else if (LocStr.Equals(TEXT("index"), ESearchCase::IgnoreCase)) Location = EOculusXRHandHapticsLocation::Index;
    }

    float Frequency = BodyJson->HasField(TEXT("frequency")) ? BodyJson->GetNumberField(TEXT("frequency")) : 160.0f;
    float Amplitude = BodyJson->HasField(TEXT("amplitude")) ? BodyJson->GetNumberField(TEXT("amplitude")) : 0.5f;

    UOculusXRInputFunctionLibrary::SetHapticsByValue(Frequency, Amplitude, Hand, Location);

    Response->SetStringField(TEXT("status"), TEXT("success"));
    Response->SetStringField(TEXT("hand"), Hand == EControllerHand::Left ? TEXT("left") : TEXT("right"));
    Response->SetNumberField(TEXT("frequency"), Frequency);
    Response->SetNumberField(TEXT("amplitude"), Amplitude);
    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRRecenter(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();

    UOculusXRFunctionLibrary::SetBaseRotationAndBaseOffsetInMeters(
        FRotator::ZeroRotator, FVector::ZeroVector, EOrientPositionSelector::OrientationAndPosition);

    Response->SetStringField(TEXT("status"), TEXT("success"));
    Response->SetStringField(TEXT("message"), TEXT("HMD recentered"));
    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRGetGuardian(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("status"), TEXT("success"));
    Response->SetBoolField(TEXT("configured"), UOculusXRFunctionLibrary::IsGuardianConfigured());

    if (UOculusXRFunctionLibrary::IsGuardianConfigured())
    {
        // Get guardian play area dimensions
        FVector PlayAreaDims = UOculusXRFunctionLibrary::GetGuardianDimensions(EOculusXRBoundaryType::Boundary_PlayArea);

        TSharedRef<FJsonObject> PlayAreaObj = MakeShared<FJsonObject>();
        PlayAreaObj->SetNumberField(TEXT("width"), PlayAreaDims.X);
        PlayAreaObj->SetNumberField(TEXT("depth"), PlayAreaDims.Y);
        PlayAreaObj->SetNumberField(TEXT("height"), PlayAreaDims.Z);
        Response->SetObjectField(TEXT("play_area"), PlayAreaObj);
    }
    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRSetPassthrough(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();

    TSharedPtr<FJsonObject> BodyJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
    {
        Response->SetStringField(TEXT("status"), TEXT("error"));
        Response->SetStringField(TEXT("message"), TEXT("Invalid JSON body"));
        return JsonObjectToString(Response);
    }

    if (BodyJson->HasField(TEXT("environment_depth")))
    {
        if (BodyJson->GetBoolField(TEXT("environment_depth")))
            UOculusXRFunctionLibrary::StartEnvironmentDepth();
        else
            UOculusXRFunctionLibrary::StopEnvironmentDepth();
    }
    if (BodyJson->HasField(TEXT("hand_removal")))
    {
        UOculusXRFunctionLibrary::SetEnvironmentDepthHandRemoval(BodyJson->GetBoolField(TEXT("hand_removal")));
    }
    if (BodyJson->HasField(TEXT("local_dimming")))
    {
        UOculusXRFunctionLibrary::SetLocalDimmingOn(BodyJson->GetBoolField(TEXT("local_dimming")));
    }

    Response->SetStringField(TEXT("status"), TEXT("success"));
    Response->SetBoolField(TEXT("passthrough_supported"), UOculusXRFunctionLibrary::IsPassthroughSupported());
    Response->SetBoolField(TEXT("environment_depth_started"), UOculusXRFunctionLibrary::IsEnvironmentDepthStarted());
    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRGetEyeTracking(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();

    Response->SetBoolField(TEXT("supported"), UOculusXRMovementFunctionLibrary::IsEyeTrackingSupported());
    Response->SetBoolField(TEXT("enabled"), UOculusXRMovementFunctionLibrary::IsEyeTrackingEnabled());

    if (!UOculusXRMovementFunctionLibrary::IsEyeTrackingEnabled())
    {
        Response->SetStringField(TEXT("status"), TEXT("success"));
        return JsonObjectToString(Response);
    }

    FOculusXREyeGazesState EyeGazesState;
    if (UOculusXRMovementFunctionLibrary::TryGetEyeGazesState(EyeGazesState))
    {
        Response->SetStringField(TEXT("status"), TEXT("success"));
        Response->SetNumberField(TEXT("time"), EyeGazesState.Time);

        TArray<TSharedPtr<FJsonValue>> EyesArray;
        for (int32 i = 0; i < EyeGazesState.EyeGazes.Num(); ++i)
        {
            const FOculusXREyeGazeState& EyeGaze = EyeGazesState.EyeGazes[i];
            TSharedRef<FJsonObject> EyeObj = MakeShared<FJsonObject>();
            EyeObj->SetStringField(TEXT("eye"), i == 0 ? TEXT("left") : TEXT("right"));
            EyeObj->SetBoolField(TEXT("valid"), EyeGaze.bIsValid);
            EyeObj->SetNumberField(TEXT("confidence"), EyeGaze.Confidence);
            EyeObj->SetNumberField(TEXT("position_x"), EyeGaze.Position.X);
            EyeObj->SetNumberField(TEXT("position_y"), EyeGaze.Position.Y);
            EyeObj->SetNumberField(TEXT("position_z"), EyeGaze.Position.Z);
            EyesArray.Add(MakeShared<FJsonValueObject>(EyeObj));
        }
        Response->SetArrayField(TEXT("eye_gazes"), EyesArray);
    }
    else
    {
        Response->SetStringField(TEXT("status"), TEXT("error"));
        Response->SetStringField(TEXT("message"), TEXT("Failed to get eye gaze state"));
    }
    return JsonObjectToString(Response);
}
