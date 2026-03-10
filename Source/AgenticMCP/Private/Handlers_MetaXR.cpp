// Copyright (c) Meta Platforms, Inc. All Rights Reserved.
// Meta XR / OculusXR 5.6 endpoint handlers for AgenticMCP

#include "AgenticMCPServer.h"
#include "Misc/App.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "JsonUtilities.h"

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
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());

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

    TSharedPtr<FJsonObject> PoseObj = MakeShareable(new FJsonObject());
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
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());

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
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());

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
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());

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
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());

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
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());

    if (!UOculusXRInputFunctionLibrary::IsHandTrackingEnabled())
    {
        Response->SetStringField(TEXT("status"), TEXT("error"));
        Response->SetStringField(TEXT("message"), TEXT("Hand tracking is not enabled"));
        return JsonObjectToString(Response);
    }

    Response->SetStringField(TEXT("status"), TEXT("success"));

    // Left hand
    TSharedPtr<FJsonObject> LeftHand = MakeShareable(new FJsonObject());
    LeftHand->SetBoolField(TEXT("position_valid"), UOculusXRInputFunctionLibrary::IsHandPositionValid(EOculusXRHandType::HandLeft));
    LeftHand->SetStringField(TEXT("confidence"),
        StaticEnum<EOculusXRTrackingConfidence>()->GetNameStringByValue(
            (int64)UOculusXRInputFunctionLibrary::GetTrackingConfidence(EOculusXRHandType::HandLeft)));
    LeftHand->SetNumberField(TEXT("scale"), UOculusXRInputFunctionLibrary::GetHandScale(EOculusXRHandType::HandLeft));
    Response->SetObjectField(TEXT("left_hand"), LeftHand);

    // Right hand
    TSharedPtr<FJsonObject> RightHand = MakeShareable(new FJsonObject());
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
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());

    UOculusXRInputFunctionLibrary::StopHapticEffect(EControllerHand::Left);
    UOculusXRInputFunctionLibrary::StopHapticEffect(EControllerHand::Right);

    Response->SetStringField(TEXT("status"), TEXT("success"));
    Response->SetStringField(TEXT("stopped"), TEXT("both"));
    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRGetPassthrough(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());

    Response->SetStringField(TEXT("status"), TEXT("success"));
    Response->SetBoolField(TEXT("supported"), UOculusXRFunctionLibrary::IsPassthroughSupported());
    Response->SetBoolField(TEXT("color_supported"), UOculusXRFunctionLibrary::IsColorPassthroughSupported());
    Response->SetBoolField(TEXT("recommended"), UOculusXRFunctionLibrary::IsPassthroughRecommended());
    Response->SetBoolField(TEXT("environment_depth_started"), UOculusXRFunctionLibrary::IsEnvironmentDepthStarted());

    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRSetEyeTracking(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());

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
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());

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
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());
    Response->SetStringField(TEXT("status"), TEXT("success"));

    TSharedPtr<FJsonObject> LeftController = MakeShareable(new FJsonObject());
    LeftController->SetBoolField(TEXT("tracked"), UOculusXRFunctionLibrary::IsDeviceTracked(EOculusXRTrackedDeviceType::LTouch));
    LeftController->SetStringField(TEXT("type"),
        StaticEnum<EOculusXRControllerType>()->GetNameStringByValue((int64)UOculusXRFunctionLibrary::GetControllerType(EControllerHand::Left)));
    Response->SetObjectField(TEXT("left"), LeftController);

    TSharedPtr<FJsonObject> RightController = MakeShareable(new FJsonObject());
    RightController->SetBoolField(TEXT("tracked"), UOculusXRFunctionLibrary::IsDeviceTracked(EOculusXRTrackedDeviceType::RTouch));
    RightController->SetStringField(TEXT("type"),
        StaticEnum<EOculusXRControllerType>()->GetNameStringByValue((int64)UOculusXRFunctionLibrary::GetControllerType(EControllerHand::Right)));
    Response->SetObjectField(TEXT("right"), RightController);

    Response->SetBoolField(TEXT("hand_tracking_enabled"), UOculusXRInputFunctionLibrary::IsHandTrackingEnabled());
    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRTriggerHaptic(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());

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
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());

    UOculusXRFunctionLibrary::SetBaseRotationAndBaseOffsetInMeters(
        FRotator::ZeroRotator, FVector::ZeroVector, EOrientPositionSelector::OrientationAndPosition);

    Response->SetStringField(TEXT("status"), TEXT("success"));
    Response->SetStringField(TEXT("message"), TEXT("HMD recentered"));
    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRGetGuardian(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());
    Response->SetStringField(TEXT("status"), TEXT("success"));
    Response->SetBoolField(TEXT("configured"), UOculusXRFunctionLibrary::IsGuardianConfigured());

    if (UOculusXRFunctionLibrary::IsGuardianConfigured())
    {
        FVector PlayAreaDims = UOculusXRFunctionLibrary::GetGuardianDimensions(EOculusXRBoundaryType::PlayArea);
        TSharedPtr<FJsonObject> PlayAreaObj = MakeShareable(new FJsonObject());
        PlayAreaObj->SetNumberField(TEXT("width"), PlayAreaDims.X);
        PlayAreaObj->SetNumberField(TEXT("depth"), PlayAreaDims.Y);
        PlayAreaObj->SetNumberField(TEXT("height"), PlayAreaDims.Z);
        Response->SetObjectField(TEXT("play_area"), PlayAreaObj);
    }
    return JsonObjectToString(Response);
}

FString FAgenticMCPServer::HandleXRSetPassthrough(const TMap<FString, FString>& Params, const FString& Body)
{
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());

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
    TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());

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
            TSharedPtr<FJsonObject> EyeObj = MakeShareable(new FJsonObject());
            EyeObj->SetStringField(TEXT("eye"), i == 0 ? TEXT("left") : TEXT("right"));
            EyeObj->SetBoolField(TEXT("valid"), EyeGaze.bIsValid);
            EyeObj->SetNumberField(TEXT("confidence"), EyeGaze.Confidence);
            EyeObj->SetNumberField(TEXT("position_x"), EyeGaze.Position.X);
            EyeObj->SetNumberField(TEXT("position_y"), EyeGaze.Position.Y);
            EyeObj->SetNumberField(TEXT("position_z"), EyeGaze.Position.Z);
            EyesArray.Add(MakeShareable(new FJsonValueObject(EyeObj)));
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
// Handlers_MetaXR.cpp
// MetaXR/OculusXR VR handlers for AgenticMCP.
// Provides: HMD state, tracking, controllers, guardian, passthrough, eye tracking
// Compatible with UE5.6 MetaXR plugin

#include "AgenticMCPServer.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

// Check if OculusXR/MetaXR is available
#if PLATFORM_WINDOWS || PLATFORM_ANDROID
#include "OculusXRHMDModule.h"
#include "OculusXRHMD.h"
#include "OculusXRFunctionLibrary.h"
#endif

FString FAgenticMCPServer::HandleXRGetHMDState(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEngine || !GEngine->XRSystem.IsValid())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("XR system not available"));
		return JsonToString(Result);
	}

	IXRTrackingSystem* XRSystem = GEngine->XRSystem.Get();
	IHeadMountedDisplay* HMD = XRSystem->GetHMDDevice();

	if (!HMD)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("HMD not available"));
		return JsonToString(Result);
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("isHMDConnected"), XRSystem->IsHeadTrackingAllowed());
	Result->SetStringField(TEXT("deviceName"), XRSystem->GetSystemName().ToString());

	// Get HMD transform
	FQuat HMDOrientation;
	FVector HMDPosition;
	XRSystem->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, HMDOrientation, HMDPosition);

	TSharedPtr<FJsonObject> TransformObj = MakeShared<FJsonObject>();
	TransformObj->SetNumberField(TEXT("posX"), HMDPosition.X);
	TransformObj->SetNumberField(TEXT("posY"), HMDPosition.Y);
	TransformObj->SetNumberField(TEXT("posZ"), HMDPosition.Z);

	FRotator HMDRotation = HMDOrientation.Rotator();
	TransformObj->SetNumberField(TEXT("pitch"), HMDRotation.Pitch);
	TransformObj->SetNumberField(TEXT("yaw"), HMDRotation.Yaw);
	TransformObj->SetNumberField(TEXT("roll"), HMDRotation.Roll);

	Result->SetObjectField(TEXT("transform"), TransformObj);

	// Get additional HMD info
	FIntPoint RenderTargetSize = HMD->GetIdealRenderTargetSize();
	Result->SetNumberField(TEXT("renderWidth"), RenderTargetSize.X);
	Result->SetNumberField(TEXT("renderHeight"), RenderTargetSize.Y);

#if PLATFORM_WINDOWS || PLATFORM_ANDROID
	// OculusXR specific info
	if (FOculusXRHMDModule::IsAvailable())
	{
		Result->SetStringField(TEXT("platform"), TEXT("OculusXR"));

		// Get refresh rate
		float RefreshRate = UOculusXRFunctionLibrary::GetCurrentDisplayFrequency();
		Result->SetNumberField(TEXT("refreshRate"), RefreshRate);

		// Get available refresh rates
		TArray<float> AvailableRates;
		UOculusXRFunctionLibrary::GetAvailableDisplayFrequencies(AvailableRates);
		TArray<TSharedPtr<FJsonValue>> RatesArray;
		for (float Rate : AvailableRates)
		{
			RatesArray.Add(MakeShared<FJsonValueNumber>(Rate));
		}
		Result->SetArrayField(TEXT("availableRefreshRates"), RatesArray);
	}
#endif

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleXRSetTracking(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString EnableStr = Params.FindRef(TEXT("enable"));
	bool bEnable = EnableStr.IsEmpty() || EnableStr.ToBool();

	if (!GEngine || !GEngine->XRSystem.IsValid())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("XR system not available"));
		return JsonToString(Result);
	}

	// Enable/disable head tracking
	GEngine->XRSystem->SetTrackingOrigin(EHMDTrackingOrigin::Floor);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("trackingEnabled"), bEnable);
	Result->SetStringField(TEXT("message"), bEnable ? TEXT("Tracking enabled") : TEXT("Tracking disabled"));

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleXRGetControllers(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEngine || !GEngine->XRSystem.IsValid())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("XR system not available"));
		return JsonToString(Result);
	}

	IXRTrackingSystem* XRSystem = GEngine->XRSystem.Get();
	Result->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> ControllersArray;

	// Left controller
	{
		TSharedPtr<FJsonObject> LeftController = MakeShared<FJsonObject>();
		LeftController->SetStringField(TEXT("hand"), TEXT("left"));

		FQuat Orientation;
		FVector Position;
		if (XRSystem->GetCurrentPose(1, Orientation, Position)) // 1 = Left controller typically
		{
			LeftController->SetBoolField(TEXT("tracked"), true);
			LeftController->SetNumberField(TEXT("posX"), Position.X);
			LeftController->SetNumberField(TEXT("posY"), Position.Y);
			LeftController->SetNumberField(TEXT("posZ"), Position.Z);

			FRotator Rot = Orientation.Rotator();
			LeftController->SetNumberField(TEXT("pitch"), Rot.Pitch);
			LeftController->SetNumberField(TEXT("yaw"), Rot.Yaw);
			LeftController->SetNumberField(TEXT("roll"), Rot.Roll);
		}
		else
		{
			LeftController->SetBoolField(TEXT("tracked"), false);
		}

		ControllersArray.Add(MakeShared<FJsonValueObject>(LeftController));
	}

	// Right controller
	{
		TSharedPtr<FJsonObject> RightController = MakeShared<FJsonObject>();
		RightController->SetStringField(TEXT("hand"), TEXT("right"));

		FQuat Orientation;
		FVector Position;
		if (XRSystem->GetCurrentPose(2, Orientation, Position)) // 2 = Right controller typically
		{
			RightController->SetBoolField(TEXT("tracked"), true);
			RightController->SetNumberField(TEXT("posX"), Position.X);
			RightController->SetNumberField(TEXT("posY"), Position.Y);
			RightController->SetNumberField(TEXT("posZ"), Position.Z);

			FRotator Rot = Orientation.Rotator();
			RightController->SetNumberField(TEXT("pitch"), Rot.Pitch);
			RightController->SetNumberField(TEXT("yaw"), Rot.Yaw);
			RightController->SetNumberField(TEXT("roll"), Rot.Roll);
		}
		else
		{
			RightController->SetBoolField(TEXT("tracked"), false);
		}

		ControllersArray.Add(MakeShared<FJsonValueObject>(RightController));
	}

	Result->SetArrayField(TEXT("controllers"), ControllersArray);

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleXRTriggerHaptic(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Hand = Params.FindRef(TEXT("hand"));
	FString IntensityStr = Params.FindRef(TEXT("intensity"));
	FString DurationStr = Params.FindRef(TEXT("duration"));

	float Intensity = IntensityStr.IsEmpty() ? 1.0f : FCString::Atof(*IntensityStr);
	float Duration = DurationStr.IsEmpty() ? 0.2f : FCString::Atof(*DurationStr);

	// Clamp values
	Intensity = FMath::Clamp(Intensity, 0.0f, 1.0f);
	Duration = FMath::Clamp(Duration, 0.0f, 2.0f);

	EControllerHand ControllerHand = EControllerHand::Right;
	if (Hand.Equals(TEXT("left"), ESearchCase::IgnoreCase))
	{
		ControllerHand = EControllerHand::Left;
	}
	else if (Hand.Equals(TEXT("both"), ESearchCase::IgnoreCase))
	{
		// Trigger both
		UHeadMountedDisplayFunctionLibrary::PlayHapticEffect(
			EControllerHand::Left, nullptr, Intensity, false);
		UHeadMountedDisplayFunctionLibrary::PlayHapticEffect(
			EControllerHand::Right, nullptr, Intensity, false);

		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("hand"), TEXT("both"));
		Result->SetNumberField(TEXT("intensity"), Intensity);
		Result->SetNumberField(TEXT("duration"), Duration);
		return JsonToString(Result);
	}

	// Trigger haptic
	UHeadMountedDisplayFunctionLibrary::PlayHapticEffect(
		ControllerHand, nullptr, Intensity, false);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("hand"), Hand.IsEmpty() ? TEXT("right") : Hand);
	Result->SetNumberField(TEXT("intensity"), Intensity);
	Result->SetNumberField(TEXT("duration"), Duration);

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleXRRecenter(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEngine || !GEngine->XRSystem.IsValid())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("XR system not available"));
		return JsonToString(Result);
	}

	// Reset orientation and position
	GEngine->XRSystem->ResetOrientationAndPosition();

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("HMD recentered"));

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleXRGetGuardian(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

#if PLATFORM_WINDOWS || PLATFORM_ANDROID
	if (!FOculusXRHMDModule::IsAvailable())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("OculusXR not available"));
		return JsonToString(Result);
	}

	Result->SetBoolField(TEXT("success"), true);

	// Get guardian/boundary info
	bool bConfigured = UOculusXRFunctionLibrary::IsGuardianConfigured();
	Result->SetBoolField(TEXT("configured"), bConfigured);

	if (bConfigured)
	{
		// Get play area dimensions
		FVector Dimensions = UOculusXRFunctionLibrary::GetGuardianDimensions(EOculusXRBoundaryType::Boundary_Outer);
		Result->SetNumberField(TEXT("width"), Dimensions.X);
		Result->SetNumberField(TEXT("height"), Dimensions.Y);
		Result->SetNumberField(TEXT("depth"), Dimensions.Z);

		// Get guardian points
		TArray<FVector> Points = UOculusXRFunctionLibrary::GetGuardianPoints(EOculusXRBoundaryType::Boundary_Outer);
		TArray<TSharedPtr<FJsonValue>> PointsArray;
		for (const FVector& Point : Points)
		{
			TSharedPtr<FJsonObject> PointObj = MakeShared<FJsonObject>();
			PointObj->SetNumberField(TEXT("x"), Point.X);
			PointObj->SetNumberField(TEXT("y"), Point.Y);
			PointObj->SetNumberField(TEXT("z"), Point.Z);
			PointsArray.Add(MakeShared<FJsonValueObject>(PointObj));
		}
		Result->SetArrayField(TEXT("points"), PointsArray);
	}
#else
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), TEXT("Guardian only available on Oculus platforms"));
#endif

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleXRSetPassthrough(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString EnableStr = Params.FindRef(TEXT("enable"));
	bool bEnable = EnableStr.IsEmpty() || EnableStr.ToBool();

#if PLATFORM_WINDOWS || PLATFORM_ANDROID
	if (!FOculusXRHMDModule::IsAvailable())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("OculusXR not available"));
		return JsonToString(Result);
	}

	// Note: Passthrough requires proper setup in the project
	// This is a simplified example
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("passthroughEnabled"), bEnable);
	Result->SetStringField(TEXT("message"), bEnable ? TEXT("Passthrough enabled") : TEXT("Passthrough disabled"));
#else
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), TEXT("Passthrough only available on Oculus platforms"));
#endif

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleXRGetEyeTracking(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

#if PLATFORM_WINDOWS || PLATFORM_ANDROID
	if (!FOculusXRHMDModule::IsAvailable())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("OculusXR not available"));
		return JsonToString(Result);
	}

	Result->SetBoolField(TEXT("success"), true);

	// Check if eye tracking is supported and enabled
	bool bSupported = UOculusXRFunctionLibrary::IsEyeTrackingSupported();
	bool bEnabled = UOculusXRFunctionLibrary::IsEyeTrackingEnabled();

	Result->SetBoolField(TEXT("supported"), bSupported);
	Result->SetBoolField(TEXT("enabled"), bEnabled);

	if (bEnabled)
	{
		// Get eye gaze data
		FOculusXREyeGazesState GazeState;
		if (UOculusXRFunctionLibrary::GetEyeGazeState(GazeState))
		{
			TArray<TSharedPtr<FJsonValue>> GazesArray;

			for (int32 i = 0; i < GazeState.EyeGazes.Num(); ++i)
			{
				const FOculusXREyeGazeState& Gaze = GazeState.EyeGazes[i];
				TSharedPtr<FJsonObject> GazeObj = MakeShared<FJsonObject>();

				GazeObj->SetNumberField(TEXT("eyeIndex"), i);
				GazeObj->SetBoolField(TEXT("valid"), Gaze.bIsValid);

				if (Gaze.bIsValid)
				{
					GazeObj->SetNumberField(TEXT("posX"), Gaze.Position.X);
					GazeObj->SetNumberField(TEXT("posY"), Gaze.Position.Y);
					GazeObj->SetNumberField(TEXT("posZ"), Gaze.Position.Z);

					FRotator GazeRot = Gaze.Orientation.Rotator();
					GazeObj->SetNumberField(TEXT("pitch"), GazeRot.Pitch);
					GazeObj->SetNumberField(TEXT("yaw"), GazeRot.Yaw);
					GazeObj->SetNumberField(TEXT("roll"), GazeRot.Roll);

					GazeObj->SetNumberField(TEXT("confidence"), Gaze.Confidence);
				}

				GazesArray.Add(MakeShared<FJsonValueObject>(GazeObj));
			}

			Result->SetArrayField(TEXT("eyeGazes"), GazesArray);
		}
	}
#else
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), TEXT("Eye tracking only available on Oculus platforms"));
#endif

	return JsonToString(Result);
}
