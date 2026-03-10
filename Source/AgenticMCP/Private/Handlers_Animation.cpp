// Handlers_Animation.cpp
// Animation playback handlers for AgenticMCP
// Controls skeletal mesh animations on actors

#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Editor.h"
#include "EngineUtils.h"

FString FAgenticMCPServer::HandleAnimationPlay(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body. Expected: {\"actorName\": \"...\", \"animation\": \"/Game/Path/To/Anim\"}"));
	}

	FString ActorName = BodyJson->GetStringField(TEXT("actorName"));
	FString AnimPath = BodyJson->GetStringField(TEXT("animation"));
	float PlayRate = BodyJson->HasField(TEXT("playRate")) ? BodyJson->GetNumberField(TEXT("playRate")) : 1.0f;
	bool bLooping = BodyJson->HasField(TEXT("loop")) ? BodyJson->GetBoolField(TEXT("loop")) : false;

	if (ActorName.IsEmpty() || AnimPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'actorName' or 'animation' field"));
	}

	UWorld* World = nullptr;
	if (GEditor && GEditor->PlayWorld)
	{
		World = GEditor->PlayWorld;
	}
	else if (GEditor)
	{
		World = GEditor->GetEditorWorldContext().World();
	}

	if (!World)
	{
		return MakeErrorJson(TEXT("No world available"));
	}

	// Find the actor
	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName().Equals(ActorName, ESearchCase::IgnoreCase) ||
			It->GetActorLabel().Equals(ActorName, ESearchCase::IgnoreCase))
		{
			TargetActor = *It;
			break;
		}
	}

	if (!TargetActor)
	{
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
	}

	// Find skeletal mesh component
	USkeletalMeshComponent* SkelMesh = TargetActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!SkelMesh)
	{
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' has no SkeletalMeshComponent"), *ActorName));
	}

	// Load the animation asset
	UAnimationAsset* AnimAsset = LoadObject<UAnimationAsset>(nullptr, *AnimPath);
	if (!AnimAsset)
	{
		// Try adding suffix
		if (!AnimPath.Contains(TEXT(".")))
		{
			AnimPath = AnimPath + TEXT(".") + FPaths::GetBaseFilename(AnimPath);
			AnimAsset = LoadObject<UAnimationAsset>(nullptr, *AnimPath);
		}
	}

	if (!AnimAsset)
	{
		return MakeErrorJson(FString::Printf(TEXT("Animation not found: %s"), *AnimPath));
	}

	// Check if it's a montage or sequence
	UAnimMontage* Montage = Cast<UAnimMontage>(AnimAsset);
	UAnimSequence* Sequence = Cast<UAnimSequence>(AnimAsset);

	if (Montage)
	{
		// Play as montage
		UAnimInstance* AnimInstance = SkelMesh->GetAnimInstance();
		if (AnimInstance)
		{
			float Duration = AnimInstance->Montage_Play(Montage, PlayRate);
			Result->SetBoolField(TEXT("success"), Duration > 0);
			Result->SetStringField(TEXT("type"), TEXT("montage"));
			Result->SetNumberField(TEXT("duration"), Duration);
		}
		else
		{
			return MakeErrorJson(TEXT("No AnimInstance on skeletal mesh"));
		}
	}
	else if (Sequence)
	{
		// Play as sequence directly on the component
		SkelMesh->PlayAnimation(Sequence, bLooping);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("type"), TEXT("sequence"));
		Result->SetNumberField(TEXT("duration"), Sequence->GetPlayLength());
	}
	else
	{
		return MakeErrorJson(TEXT("Animation asset is not a Montage or Sequence"));
	}

	Result->SetStringField(TEXT("actor"), TargetActor->GetName());
	Result->SetStringField(TEXT("animation"), AnimAsset->GetName());
	Result->SetNumberField(TEXT("playRate"), PlayRate);
	Result->SetBoolField(TEXT("looping"), bLooping);

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleAnimationStop(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body. Expected: {\"actorName\": \"...\"}"));
	}

	FString ActorName = BodyJson->GetStringField(TEXT("actorName"));
	bool bStopMontages = BodyJson->HasField(TEXT("stopMontages")) ? BodyJson->GetBoolField(TEXT("stopMontages")) : true;
	float BlendOutTime = BodyJson->HasField(TEXT("blendOutTime")) ? BodyJson->GetNumberField(TEXT("blendOutTime")) : 0.25f;

	if (ActorName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'actorName' field"));
	}

	UWorld* World = nullptr;
	if (GEditor && GEditor->PlayWorld)
	{
		World = GEditor->PlayWorld;
	}
	else if (GEditor)
	{
		World = GEditor->GetEditorWorldContext().World();
	}

	if (!World)
	{
		return MakeErrorJson(TEXT("No world available"));
	}

	// Find the actor
	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName().Equals(ActorName, ESearchCase::IgnoreCase) ||
			It->GetActorLabel().Equals(ActorName, ESearchCase::IgnoreCase))
		{
			TargetActor = *It;
			break;
		}
	}

	if (!TargetActor)
	{
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
	}

	// Find skeletal mesh component
	USkeletalMeshComponent* SkelMesh = TargetActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!SkelMesh)
	{
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' has no SkeletalMeshComponent"), *ActorName));
	}

	// Stop animations
	if (bStopMontages)
	{
		UAnimInstance* AnimInstance = SkelMesh->GetAnimInstance();
		if (AnimInstance)
		{
			AnimInstance->StopAllMontages(BlendOutTime);
		}
	}

	// Stop any playing animation sequence
	SkelMesh->Stop();

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), TargetActor->GetName());
	Result->SetBoolField(TEXT("stoppedMontages"), bStopMontages);
	Result->SetNumberField(TEXT("blendOutTime"), BlendOutTime);

	return JsonToString(Result);
}
