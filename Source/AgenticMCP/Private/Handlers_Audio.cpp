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

FString FAgenticMCPServer::HandleAudioGetStatus(const TMap<FString, FString>& Params, const FString& Body)
{
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
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString SoundClassName;
	JsonBody->TryGetStringField(TEXT("soundClass"), SoundClassName);

	double Volume = 1.0;
	JsonBody->TryGetNumberField(TEXT("volume"), Volume);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("soundClass"), SoundClassName);
	Result->SetNumberField(TEXT("volume"), Volume);
	return JsonToString(Result);
}

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
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleAudioDebugVisualize(const TMap<FString, FString>& Params, const FString& Body)
{
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
