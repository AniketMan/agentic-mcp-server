// Handlers_Audio.cpp
// Audio debugging and control endpoints for AgenticMCP

#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPAudio, Log, All);

FString FAgenticMCPServer::HandleAudioGetStatus(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEngine)
	{
		return MakeErrorJson(TEXT("Engine not available"));
	}
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> StatusObj = MakeShared<FJsonObject>();

	if (FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
	{
		StatusObj->SetBoolField(TEXT("audioDeviceManagerActive"), true);
		StatusObj->SetNumberField(TEXT("numActiveAudioDevices"), DeviceManager->GetNumActiveAudioDevices());
	}
	else
	{
		StatusObj->SetBoolField(TEXT("audioDeviceManagerActive"), false);
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("status"), StatusObj);
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleAudioListActiveSounds(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return MakeErrorJson(TEXT("No editor world available"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	TArray<TSharedPtr<FJsonValue>> SoundsArray;

	for (TObjectIterator<UAudioComponent> It; It; ++It)
	{
		UAudioComponent* AudioComp = *It;
		if (AudioComp && AudioComp->GetWorld() == World && AudioComp->IsPlaying())
		{
			TSharedRef<FJsonObject> SoundObj = MakeShared<FJsonObject>();
			SoundObj->SetStringField(TEXT("componentName"), AudioComp->GetName());
			if (AActor* Owner = AudioComp->GetOwner())
			{
				SoundObj->SetStringField(TEXT("ownerActor"), Owner->GetName());
			}
			SoundObj->SetBoolField(TEXT("isPlaying"), AudioComp->IsPlaying());
			SoundsArray.Add(MakeShared<FJsonValueObject>(SoundObj));
		}
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("activeSounds"), SoundsArray);
	Result->SetNumberField(TEXT("count"), SoundsArray.Num());
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleAudioGetDeviceInfo(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEngine)
	{
		return MakeErrorJson(TEXT("Engine not available"));
	}
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> DevicesArray;

	if (FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
	{
		TSharedRef<FJsonObject> MainDeviceObj = MakeShared<FJsonObject>();
		MainDeviceObj->SetNumberField(TEXT("deviceId"), DeviceManager->GetMainAudioDeviceID());
		DevicesArray.Add(MakeShared<FJsonValueObject>(MainDeviceObj));
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("devices"), DevicesArray);
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleAudioListSoundClasses(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEngine)
	{
		return MakeErrorJson(TEXT("Engine not available"));
	}
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ClassesArray;

	for (TObjectIterator<USoundClass> It; It; ++It)
	{
		USoundClass* SoundClass = *It;
		if (SoundClass)
		{
			TSharedRef<FJsonObject> ClassObj = MakeShared<FJsonObject>();
			ClassObj->SetStringField(TEXT("name"), SoundClass->GetName());
			ClassObj->SetStringField(TEXT("path"), SoundClass->GetPathName());
			ClassesArray.Add(MakeShared<FJsonValueObject>(ClassObj));
		}
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("soundClasses"), ClassesArray);
	Result->SetNumberField(TEXT("count"), ClassesArray.Num());
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleAudioSetVolume(const TMap<FString, FString>& Params, const FString& Body)
{
	UE_LOG(LogMCPAudio, Log, TEXT("HandleAudioSetVolume called with body: %s"), *Body);

	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		UE_LOG(LogMCPAudio, Error, TEXT("HandleAudioSetVolume: Invalid JSON body"));
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString SoundClassName;
	JsonBody->TryGetStringField(TEXT("soundClass"), SoundClassName);

	double Volume = 1.0;
	JsonBody->TryGetNumberField(TEXT("volume"), Volume);

	UE_LOG(LogMCPAudio, Log, TEXT("HandleAudioSetVolume: SoundClass='%s', Volume=%.2f"), *SoundClassName, Volume);

	// Clamp volume to valid range
	Volume = FMath::Clamp(Volume, 0.0, 2.0);

	bool bFoundClass = false;
	FString ActualClassName;

	// Find and modify the sound class
	for (TObjectIterator<USoundClass> It; It; ++It)
	{
		USoundClass* SoundClass = *It;
		if (SoundClass && (SoundClassName.IsEmpty() || SoundClass->GetName().Contains(SoundClassName)))
		{
			// Modify the sound class properties
			SoundClass->Properties.Volume = static_cast<float>(Volume);
			bFoundClass = true;
			ActualClassName = SoundClass->GetName();

			// If specific class was requested and found, break
			if (!SoundClassName.IsEmpty() && SoundClass->GetName() == SoundClassName)
			{
				break;
			}
		}
	}

	// Also set volume on active audio components if targeting by component name
	if (!bFoundClass && !SoundClassName.IsEmpty())
	{
		if (GEditor && GEditor->GetEditorWorldContext().World())
		{
			UWorld* World = GEditor->GetEditorWorldContext().World();
			for (TObjectIterator<UAudioComponent> It; It; ++It)
			{
				UAudioComponent* AudioComp = *It;
				if (AudioComp && AudioComp->GetWorld() == World)
				{
					if (AudioComp->GetName().Contains(SoundClassName) ||
						(AudioComp->GetOwner() && AudioComp->GetOwner()->GetName().Contains(SoundClassName)))
					{
						AudioComp->SetVolumeMultiplier(static_cast<float>(Volume));
						bFoundClass = true;
						ActualClassName = AudioComp->GetName();
					}
				}
			}
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bFoundClass);
	Result->SetStringField(TEXT("soundClass"), bFoundClass ? ActualClassName : SoundClassName);
	Result->SetNumberField(TEXT("volume"), Volume);
	if (!bFoundClass)
	{
		UE_LOG(LogMCPAudio, Warning, TEXT("HandleAudioSetVolume: Sound class '%s' not found"), *SoundClassName);
		Result->SetStringField(TEXT("warning"), FString::Printf(TEXT("Sound class '%s' not found. Volume not changed."), *SoundClassName));
	}
	else
	{
		UE_LOG(LogMCPAudio, Log, TEXT("HandleAudioSetVolume: SUCCESS - Set '%s' volume to %.2f"), *ActualClassName, Volume);
	}
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleAudioGetStats

FString FAgenticMCPServer::HandleAudioGetStats(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> StatsObj = MakeShared<FJsonObject>();

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return MakeErrorJson(TEXT("No editor world available"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	int32 ActiveComponents = 0;
	int32 PlayingComponents = 0;

	for (TObjectIterator<UAudioComponent> It; It; ++It)
	{
		UAudioComponent* AudioComp = *It;
		if (AudioComp && AudioComp->GetWorld() == World)
		{
			ActiveComponents++;
			if (AudioComp->IsPlaying())
			{
				PlayingComponents++;
			}
		}
	}

	StatsObj->SetNumberField(TEXT("activeAudioComponents"), ActiveComponents);
	StatsObj->SetNumberField(TEXT("playingComponents"), PlayingComponents);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("stats"), StatsObj);
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleAudioPlaySound(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString SoundPath;
	if (!JsonBody->TryGetStringField(TEXT("sound"), SoundPath))
	{
		return MakeErrorJson(TEXT("Missing 'sound' field"));
	}

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return MakeErrorJson(TEXT("No editor world available"));
	}

	USoundBase* Sound = LoadObject<USoundBase>(nullptr, *SoundPath);
	if (!Sound)
	{
		return MakeErrorJson(FString::Printf(TEXT("Sound asset not found: %s"), *SoundPath));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	UGameplayStatics::PlaySoundAtLocation(World, Sound, FVector::ZeroVector, 1.0f);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("sound"), SoundPath);
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleAudioStopSound(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return MakeErrorJson(TEXT("No editor world available"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	bool bStopAll = false;
	JsonBody->TryGetBoolField(TEXT("all"), bStopAll);
	int32 StoppedCount = 0;

	if (bStopAll)
	{
		for (TObjectIterator<UAudioComponent> It; It; ++It)
		{
			UAudioComponent* AudioComp = *It;
			if (AudioComp && AudioComp->GetWorld() == World && AudioComp->IsPlaying())
			{
				AudioComp->Stop();
				StoppedCount++;
			}
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("stoppedCount"), StoppedCount);
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleAudioSetListener(const TMap<FString, FString>& Params, const FString& Body)
{
	UE_LOG(LogMCPAudio, Log, TEXT("HandleAudioSetListener called with body: %s"), *Body);

	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		UE_LOG(LogMCPAudio, Error, TEXT("HandleAudioSetListener: Invalid JSON body"));
		return MakeErrorJson(TEXT("Invalid JSON body. Expected: {\"location\": {\"x\": 0, \"y\": 0, \"z\": 0}, \"rotation\": {\"pitch\": 0, \"yaw\": 0, \"roll\": 0}}"));
	}

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return MakeErrorJson(TEXT("No editor world available"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();

	// Parse location
	FVector ListenerLocation = FVector::ZeroVector;
	const TSharedPtr<FJsonObject>* LocationObj;
	if (JsonBody->TryGetObjectField(TEXT("location"), LocationObj))
	{
		(*LocationObj)->TryGetNumberField(TEXT("x"), ListenerLocation.X);
		(*LocationObj)->TryGetNumberField(TEXT("y"), ListenerLocation.Y);
		(*LocationObj)->TryGetNumberField(TEXT("z"), ListenerLocation.Z);
	}

	// Parse rotation
	FRotator ListenerRotation = FRotator::ZeroRotator;
	const TSharedPtr<FJsonObject>* RotationObj;
	if (JsonBody->TryGetObjectField(TEXT("rotation"), RotationObj))
	{
		double Pitch = 0, Yaw = 0, Roll = 0;
		(*RotationObj)->TryGetNumberField(TEXT("pitch"), Pitch);
		(*RotationObj)->TryGetNumberField(TEXT("yaw"), Yaw);
		(*RotationObj)->TryGetNumberField(TEXT("roll"), Roll);
		ListenerRotation = FRotator(Pitch, Yaw, Roll);
	}

	// Get the audio device and set listener override
	bool bSuccess = false;
	UE_LOG(LogMCPAudio, Log, TEXT("HandleAudioSetListener: Setting listener at (%.1f, %.1f, %.1f) rot (%.1f, %.1f, %.1f)"),
		ListenerLocation.X, ListenerLocation.Y, ListenerLocation.Z,
		ListenerRotation.Pitch, ListenerRotation.Yaw, ListenerRotation.Roll);

	if (FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
	{
		if (FAudioDevice* AudioDevice = DeviceManager->GetActiveAudioDevice().GetAudioDevice())
		{
			// Set listener position for the main viewport/listener index 0
			FTransform ListenerTransform(ListenerRotation, ListenerLocation);
			AudioDevice->SetListener(World, 0, ListenerTransform, 0.0f);
			bSuccess = true;
			UE_LOG(LogMCPAudio, Log, TEXT("HandleAudioSetListener: SUCCESS - Listener position updated"));
		}
		else
		{
			UE_LOG(LogMCPAudio, Error, TEXT("HandleAudioSetListener: Failed to get active audio device"));
		}
	}
	else
	{
		UE_LOG(LogMCPAudio, Error, TEXT("HandleAudioSetListener: FAudioDeviceManager not available"));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);

	TSharedRef<FJsonObject> LocJson = MakeShared<FJsonObject>();
	LocJson->SetNumberField(TEXT("x"), ListenerLocation.X);
	LocJson->SetNumberField(TEXT("y"), ListenerLocation.Y);
	LocJson->SetNumberField(TEXT("z"), ListenerLocation.Z);
	Result->SetObjectField(TEXT("location"), LocJson);

	TSharedRef<FJsonObject> RotJson = MakeShared<FJsonObject>();
	RotJson->SetNumberField(TEXT("pitch"), ListenerRotation.Pitch);
	RotJson->SetNumberField(TEXT("yaw"), ListenerRotation.Yaw);
	RotJson->SetNumberField(TEXT("roll"), ListenerRotation.Roll);
	Result->SetObjectField(TEXT("rotation"), RotJson);

	if (!bSuccess)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to get audio device"));
	}

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleAudioDebugVisualize(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		return MakeErrorJson(TEXT("No editor world available"));
	}
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	bool bEnable = true;
	if (JsonBody.IsValid())
	{
		JsonBody->TryGetBoolField(TEXT("enable"), bEnable);
	}

	if (GEngine && GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (bEnable)
		{
			GEngine->Exec(World, TEXT("au.Debug.Sounds 1"));
		}
		else
		{
			GEngine->Exec(World, TEXT("au.Debug.Sounds 0"));
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("enabled"), bEnable);
	return JsonToString(Result);
}
