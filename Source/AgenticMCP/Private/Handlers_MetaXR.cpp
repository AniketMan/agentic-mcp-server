// Handlers_MetaXR.cpp
// Meta XR / OculusXR handlers for AgenticMCP
// Controls passthrough, guardian, hand tracking, HMD info

#include "AgenticMCPServer.h"

#if WITH_OCULUSXR

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// OculusXR includes
#include "OculusXRFunctionLibrary.h"
#include "OculusXRInputFunctionLibrary.h"
#include "OculusXRHMDTypes.h"

FString FAgenticMCPServer::HandleXRStatus(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEngine || !GEngine->XRSystem.IsValid())
	{
		return MakeErrorJson(TEXT("XR system not available"));
	}
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	// Device info
	FString DeviceName = UOculusXRFunctionLibrary::GetDeviceName();
	EOculusXRDeviceType DeviceType = UOculusXRFunctionLibrary::GetDeviceType();

	OutJson->SetStringField(TEXT("deviceName"), DeviceName);
	OutJson->SetStringField(TEXT("deviceType"), UEnum::GetValueAsString(DeviceType));

	// HMD Pose
	FRotator DeviceRotation;
	FVector DevicePosition;
	FVector NeckPosition;
	UOculusXRFunctionLibrary::GetPose(DeviceRotation, DevicePosition, NeckPosition);

	TSharedRef<FJsonObject> PoseObj = MakeShared<FJsonObject>();
	PoseObj->SetNumberField(TEXT("rotationPitch"), DeviceRotation.Pitch);
	PoseObj->SetNumberField(TEXT("rotationYaw"), DeviceRotation.Yaw);
	PoseObj->SetNumberField(TEXT("rotationRoll"), DeviceRotation.Roll);
	PoseObj->SetNumberField(TEXT("positionX"), DevicePosition.X);
	PoseObj->SetNumberField(TEXT("positionY"), DevicePosition.Y);
	PoseObj->SetNumberField(TEXT("positionZ"), DevicePosition.Z);
	OutJson->SetObjectField(TEXT("pose"), PoseObj);

	// Tracking status
	bool bHMDTracked = UOculusXRFunctionLibrary::IsDeviceTracked(EOculusXRTrackedDeviceType::HMD);
	bool bLeftControllerTracked = UOculusXRFunctionLibrary::IsDeviceTracked(EOculusXRTrackedDeviceType::LTouch);
	bool bRightControllerTracked = UOculusXRFunctionLibrary::IsDeviceTracked(EOculusXRTrackedDeviceType::RTouch);

	TSharedRef<FJsonObject> TrackingObj = MakeShared<FJsonObject>();
	TrackingObj->SetBoolField(TEXT("hmd"), bHMDTracked);
	TrackingObj->SetBoolField(TEXT("leftController"), bLeftControllerTracked);
	TrackingObj->SetBoolField(TEXT("rightController"), bRightControllerTracked);
	OutJson->SetObjectField(TEXT("tracking"), TrackingObj);

	// Passthrough support
	bool bPassthroughSupported = UOculusXRFunctionLibrary::IsPassthroughSupported();
	bool bColorPassthroughSupported = UOculusXRFunctionLibrary::IsColorPassthroughSupported();
	bool bPassthroughRecommended = UOculusXRFunctionLibrary::IsPassthroughRecommended();

	TSharedRef<FJsonObject> PassthroughObj = MakeShared<FJsonObject>();
	PassthroughObj->SetBoolField(TEXT("supported"), bPassthroughSupported);
	PassthroughObj->SetBoolField(TEXT("colorSupported"), bColorPassthroughSupported);
	PassthroughObj->SetBoolField(TEXT("recommended"), bPassthroughRecommended);
	OutJson->SetObjectField(TEXT("passthrough"), PassthroughObj);

	// Display info
	float CurrentFreq = UOculusXRFunctionLibrary::GetCurrentDisplayFrequency();
	TArray<float> AvailableFreqs = UOculusXRFunctionLibrary::GetAvailableDisplayFrequencies();

	TSharedRef<FJsonObject> DisplayObj = MakeShared<FJsonObject>();
	DisplayObj->SetNumberField(TEXT("currentFrequency"), CurrentFreq);
	TArray<TSharedPtr<FJsonValue>> FreqArray;
	for (float Freq : AvailableFreqs)
	{
		FreqArray.Add(MakeShared<FJsonValueNumber>(Freq));
	}
	DisplayObj->SetArrayField(TEXT("availableFrequencies"), FreqArray);
	OutJson->SetObjectField(TEXT("display"), DisplayObj);

	// Performance
	EOculusXRProcessorPerformanceLevel CpuLevel, GpuLevel;
	UOculusXRFunctionLibrary::GetSuggestedCpuAndGpuPerformanceLevels(CpuLevel, GpuLevel);

	TSharedRef<FJsonObject> PerfObj = MakeShared<FJsonObject>();
	PerfObj->SetStringField(TEXT("cpuLevel"), UEnum::GetValueAsString(CpuLevel));
	PerfObj->SetStringField(TEXT("gpuLevel"), UEnum::GetValueAsString(GpuLevel));

	bool bGpuAvailable;
	float GpuUtil;
	UOculusXRFunctionLibrary::GetGPUUtilization(bGpuAvailable, GpuUtil);
	PerfObj->SetBoolField(TEXT("gpuAvailable"), bGpuAvailable);
	PerfObj->SetNumberField(TEXT("gpuUtilization"), GpuUtil);
	OutJson->SetObjectField(TEXT("performance"), PerfObj);

	// Input focus
	bool bHasInputFocus = UOculusXRFunctionLibrary::HasInputFocus();
	bool bHasSystemOverlay = UOculusXRFunctionLibrary::HasSystemOverlayPresent();
	OutJson->SetBoolField(TEXT("hasInputFocus"), bHasInputFocus);
	OutJson->SetBoolField(TEXT("hasSystemOverlay"), bHasSystemOverlay);

	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandleXRGuardian(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEngine || !GEngine->XRSystem.IsValid())
	{
		return MakeErrorJson(TEXT("XR system not available"));
	}
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	// Guardian status
	bool bConfigured = UOculusXRFunctionLibrary::IsGuardianConfigured();
	bool bDisplayed = UOculusXRFunctionLibrary::IsGuardianDisplayed();

	OutJson->SetBoolField(TEXT("configured"), bConfigured);
	OutJson->SetBoolField(TEXT("displayed"), bDisplayed);

	if (bConfigured)
	{
		// Play area dimensions
		FVector OuterDimensions = UOculusXRFunctionLibrary::GetGuardianDimensions(EOculusXRBoundaryType::Boundary_Outer);
		FVector PlayAreaDimensions = UOculusXRFunctionLibrary::GetGuardianDimensions(EOculusXRBoundaryType::Boundary_PlayArea);

		TSharedRef<FJsonObject> OuterObj = MakeShared<FJsonObject>();
		OuterObj->SetNumberField(TEXT("x"), OuterDimensions.X);
		OuterObj->SetNumberField(TEXT("y"), OuterDimensions.Y);
		OuterObj->SetNumberField(TEXT("z"), OuterDimensions.Z);
		OutJson->SetObjectField(TEXT("outerBoundary"), OuterObj);

		TSharedRef<FJsonObject> PlayAreaObj = MakeShared<FJsonObject>();
		PlayAreaObj->SetNumberField(TEXT("x"), PlayAreaDimensions.X);
		PlayAreaObj->SetNumberField(TEXT("y"), PlayAreaDimensions.Y);
		PlayAreaObj->SetNumberField(TEXT("z"), PlayAreaDimensions.Z);
		OutJson->SetObjectField(TEXT("playArea"), PlayAreaObj);

		// Play area transform
		FTransform PlayAreaTransform = UOculusXRFunctionLibrary::GetPlayAreaTransform();
		TSharedRef<FJsonObject> TransformObj = MakeShared<FJsonObject>();
		TransformObj->SetNumberField(TEXT("locationX"), PlayAreaTransform.GetLocation().X);
		TransformObj->SetNumberField(TEXT("locationY"), PlayAreaTransform.GetLocation().Y);
		TransformObj->SetNumberField(TEXT("locationZ"), PlayAreaTransform.GetLocation().Z);
		TransformObj->SetNumberField(TEXT("rotationPitch"), PlayAreaTransform.GetRotation().Rotator().Pitch);
		TransformObj->SetNumberField(TEXT("rotationYaw"), PlayAreaTransform.GetRotation().Rotator().Yaw);
		TransformObj->SetNumberField(TEXT("rotationRoll"), PlayAreaTransform.GetRotation().Rotator().Roll);
		OutJson->SetObjectField(TEXT("playAreaTransform"), TransformObj);

		// Guardian boundary points (outer)
		TArray<FVector> BoundaryPoints = UOculusXRFunctionLibrary::GetGuardianPoints(EOculusXRBoundaryType::Boundary_Outer, false);
		TArray<TSharedPtr<FJsonValue>> PointsArray;
		for (const FVector& Point : BoundaryPoints)
		{
			TSharedRef<FJsonObject> PointObj = MakeShared<FJsonObject>();
			PointObj->SetNumberField(TEXT("x"), Point.X);
			PointObj->SetNumberField(TEXT("y"), Point.Y);
			PointObj->SetNumberField(TEXT("z"), Point.Z);
			PointsArray.Add(MakeShared<FJsonValueObject>(PointObj));
		}
		OutJson->SetArrayField(TEXT("boundaryPoints"), PointsArray);
	}

	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandleXRSetGuardianVisibility(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	bool bVisible = BodyJson->GetBoolField(TEXT("visible"));

	UOculusXRFunctionLibrary::SetGuardianVisibility(bVisible);

	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetBoolField(TEXT("guardianVisible"), bVisible);

	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandleXRHandTracking(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEngine || !GEngine->XRSystem.IsValid())
	{
		return MakeErrorJson(TEXT("XR system not available"));
	}
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	// Check if hand tracking is enabled
	bool bHandTrackingEnabled = UOculusXRInputFunctionLibrary::IsHandTrackingEnabled();
	OutJson->SetBoolField(TEXT("handTrackingEnabled"), bHandTrackingEnabled);

	if (bHandTrackingEnabled)
	{
		// Left hand
		TSharedRef<FJsonObject> LeftHandObj = MakeShared<FJsonObject>();
		EOculusXRTrackingConfidence LeftConfidence = UOculusXRInputFunctionLibrary::GetTrackingConfidence(EOculusXRHandType::HandLeft);
		LeftHandObj->SetStringField(TEXT("confidence"), LeftConfidence == EOculusXRTrackingConfidence::High ? TEXT("High") : TEXT("Low"));

		bool bLeftValid = UOculusXRInputFunctionLibrary::IsPointerPoseValid(EOculusXRHandType::HandLeft);
		LeftHandObj->SetBoolField(TEXT("pointerPoseValid"), bLeftValid);
		if (bLeftValid)
		{
			FTransform LeftPointerPose = UOculusXRInputFunctionLibrary::GetPointerPose(EOculusXRHandType::HandLeft);
			FVector LeftPosition = LeftPointerPose.GetLocation();
			FRotator LeftRotation = LeftPointerPose.GetRotation().Rotator();
			LeftHandObj->SetNumberField(TEXT("positionX"), LeftPosition.X);
			LeftHandObj->SetNumberField(TEXT("positionY"), LeftPosition.Y);
			LeftHandObj->SetNumberField(TEXT("positionZ"), LeftPosition.Z);
			LeftHandObj->SetNumberField(TEXT("rotationPitch"), LeftRotation.Pitch);
			LeftHandObj->SetNumberField(TEXT("rotationYaw"), LeftRotation.Yaw);
			LeftHandObj->SetNumberField(TEXT("rotationRoll"), LeftRotation.Roll);
		}

		// Hand scale
		float LeftScale = UOculusXRInputFunctionLibrary::GetHandScale(EOculusXRHandType::HandLeft);
		LeftHandObj->SetNumberField(TEXT("handScale"), LeftScale);

		OutJson->SetObjectField(TEXT("leftHand"), LeftHandObj);

		// Right hand
		TSharedRef<FJsonObject> RightHandObj = MakeShared<FJsonObject>();
		EOculusXRTrackingConfidence RightConfidence = UOculusXRInputFunctionLibrary::GetTrackingConfidence(EOculusXRHandType::HandRight);
		RightHandObj->SetStringField(TEXT("confidence"), RightConfidence == EOculusXRTrackingConfidence::High ? TEXT("High") : TEXT("Low"));

		bool bRightValid = UOculusXRInputFunctionLibrary::IsPointerPoseValid(EOculusXRHandType::HandRight);
		RightHandObj->SetBoolField(TEXT("pointerPoseValid"), bRightValid);
		if (bRightValid)
		{
			FTransform RightPointerPose = UOculusXRInputFunctionLibrary::GetPointerPose(EOculusXRHandType::HandRight);
			FVector RightPosition = RightPointerPose.GetLocation();
			FRotator RightRotation = RightPointerPose.GetRotation().Rotator();
			RightHandObj->SetNumberField(TEXT("positionX"), RightPosition.X);
			RightHandObj->SetNumberField(TEXT("positionY"), RightPosition.Y);
			RightHandObj->SetNumberField(TEXT("positionZ"), RightPosition.Z);
			RightHandObj->SetNumberField(TEXT("rotationPitch"), RightRotation.Pitch);
			RightHandObj->SetNumberField(TEXT("rotationYaw"), RightRotation.Yaw);
			RightHandObj->SetNumberField(TEXT("rotationRoll"), RightRotation.Roll);
		}

		// Hand scale
		float RightScale = UOculusXRInputFunctionLibrary::GetHandScale(EOculusXRHandType::HandRight);
		RightHandObj->SetNumberField(TEXT("handScale"), RightScale);

		// Dominant hand
		EOculusXRHandType DominantHand = UOculusXRInputFunctionLibrary::GetDominantHand();
		OutJson->SetStringField(TEXT("dominantHand"), DominantHand == EOculusXRHandType::HandLeft ? TEXT("Left") : TEXT("Right"));

		OutJson->SetObjectField(TEXT("rightHand"), RightHandObj);
	}

	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandleXRControllers(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEngine || !GEngine->XRSystem.IsValid())
	{
		return MakeErrorJson(TEXT("XR system not available"));
	}
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	// Left controller
	bool bLeftTracked = UOculusXRFunctionLibrary::IsDeviceTracked(EOculusXRTrackedDeviceType::LTouch);
	EOculusXRControllerType LeftType = UOculusXRFunctionLibrary::GetControllerType(EControllerHand::Left);

	TSharedRef<FJsonObject> LeftObj = MakeShared<FJsonObject>();
	LeftObj->SetBoolField(TEXT("tracked"), bLeftTracked);
	LeftObj->SetStringField(TEXT("type"), UEnum::GetValueAsString(LeftType));
	OutJson->SetObjectField(TEXT("left"), LeftObj);

	// Right controller
	bool bRightTracked = UOculusXRFunctionLibrary::IsDeviceTracked(EOculusXRTrackedDeviceType::RTouch);
	EOculusXRControllerType RightType = UOculusXRFunctionLibrary::GetControllerType(EControllerHand::Right);

	TSharedRef<FJsonObject> RightObj = MakeShared<FJsonObject>();
	RightObj->SetBoolField(TEXT("tracked"), bRightTracked);
	RightObj->SetStringField(TEXT("type"), UEnum::GetValueAsString(RightType));
	OutJson->SetObjectField(TEXT("right"), RightObj);

	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandleXRPassthrough(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEngine || !GEngine->XRSystem.IsValid())
	{
		return MakeErrorJson(TEXT("XR system not available"));
	}
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	bool bSupported = UOculusXRFunctionLibrary::IsPassthroughSupported();
	bool bColorSupported = UOculusXRFunctionLibrary::IsColorPassthroughSupported();
	bool bRecommended = UOculusXRFunctionLibrary::IsPassthroughRecommended();
	bool bEnvironmentDepthStarted = UOculusXRFunctionLibrary::IsEnvironmentDepthStarted();

	OutJson->SetBoolField(TEXT("supported"), bSupported);
	OutJson->SetBoolField(TEXT("colorSupported"), bColorSupported);
	OutJson->SetBoolField(TEXT("recommended"), bRecommended);
	OutJson->SetBoolField(TEXT("environmentDepthStarted"), bEnvironmentDepthStarted);

	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandleXRSetPassthrough(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	bool bEnable = BodyJson->GetBoolField(TEXT("enable"));

	if (bEnable)
	{
		UOculusXRFunctionLibrary::StartEnvironmentDepth();
	}
	else
	{
		UOculusXRFunctionLibrary::StopEnvironmentDepth();
	}

	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetBoolField(TEXT("environmentDepthEnabled"), bEnable);

	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandleXRSetDisplayFrequency(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	float Frequency = BodyJson->GetNumberField(TEXT("frequency"));

	UOculusXRFunctionLibrary::SetDisplayFrequency(Frequency);

	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetNumberField(TEXT("frequency"), Frequency);

	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandleXRSetPerformanceLevels(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString CpuLevelStr = BodyJson->GetStringField(TEXT("cpuLevel"));
	FString GpuLevelStr = BodyJson->GetStringField(TEXT("gpuLevel"));

	EOculusXRProcessorPerformanceLevel CpuLevel = EOculusXRProcessorPerformanceLevel::SustainedLow;
	EOculusXRProcessorPerformanceLevel GpuLevel = EOculusXRProcessorPerformanceLevel::SustainedLow;

	// Parse CPU level
	if (CpuLevelStr.Equals(TEXT("PowerSavings"), ESearchCase::IgnoreCase))
		CpuLevel = EOculusXRProcessorPerformanceLevel::PowerSavings;
	else if (CpuLevelStr.Equals(TEXT("SustainedLow"), ESearchCase::IgnoreCase))
		CpuLevel = EOculusXRProcessorPerformanceLevel::SustainedLow;
	else if (CpuLevelStr.Equals(TEXT("SustainedHigh"), ESearchCase::IgnoreCase))
		CpuLevel = EOculusXRProcessorPerformanceLevel::SustainedHigh;
	else if (CpuLevelStr.Equals(TEXT("Boost"), ESearchCase::IgnoreCase))
		CpuLevel = EOculusXRProcessorPerformanceLevel::Boost;

	// Parse GPU level
	if (GpuLevelStr.Equals(TEXT("PowerSavings"), ESearchCase::IgnoreCase))
		GpuLevel = EOculusXRProcessorPerformanceLevel::PowerSavings;
	else if (GpuLevelStr.Equals(TEXT("SustainedLow"), ESearchCase::IgnoreCase))
		GpuLevel = EOculusXRProcessorPerformanceLevel::SustainedLow;
	else if (GpuLevelStr.Equals(TEXT("SustainedHigh"), ESearchCase::IgnoreCase))
		GpuLevel = EOculusXRProcessorPerformanceLevel::SustainedHigh;
	else if (GpuLevelStr.Equals(TEXT("Boost"), ESearchCase::IgnoreCase))
		GpuLevel = EOculusXRProcessorPerformanceLevel::Boost;

	UOculusXRFunctionLibrary::SetSuggestedCpuAndGpuPerformanceLevels(CpuLevel, GpuLevel);

	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("cpuLevel"), UEnum::GetValueAsString(CpuLevel));
	OutJson->SetStringField(TEXT("gpuLevel"), UEnum::GetValueAsString(GpuLevel));

	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandleXRRecenter(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEngine || !GEngine->XRSystem.IsValid())
	{
		return MakeErrorJson(TEXT("XR system not available"));
	}
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	// Reset position and rotation to current HMD pose
	FRotator CurrentRotation;
	FVector CurrentPosition;
	FVector NeckPosition;
	UOculusXRFunctionLibrary::GetPose(CurrentRotation, CurrentPosition, NeckPosition);

	// Set the base to current position (recenters the view)
	UOculusXRFunctionLibrary::SetBaseRotationAndPositionOffset(FRotator::ZeroRotator, FVector::ZeroVector, EOrientPositionSelector::OrientationAndPosition);

	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("message"), TEXT("View recentered"));

	return JsonToString(OutJson);
}


#pragma warning(pop)

#else // !WITH_OCULUSXR -- stub implementations

#define XR_STUB(FuncName) \
FString FAgenticMCPServer::FuncName(const TMap<FString, FString>& Params, const FString& Body) \
{ return MakeErrorJson(TEXT("OculusXR/MetaXR plugin is not available. Enable the Meta XR plugin.")); }

XR_STUB(HandleXRStatus)
XR_STUB(HandleXRGuardian)
XR_STUB(HandleXRSetGuardianVisibility)
XR_STUB(HandleXRHandTracking)
XR_STUB(HandleXRControllers)
XR_STUB(HandleXRPassthrough)
XR_STUB(HandleXRSetPassthrough)
XR_STUB(HandleXRSetDisplayFrequency)
XR_STUB(HandleXRSetPerformanceLevels)
XR_STUB(HandleXRRecenter)

#undef XR_STUB

#endif // WITH_OCULUSXR
