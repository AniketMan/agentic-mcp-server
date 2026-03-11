// Handlers_Materials.cpp
// Material parameter handlers for AgenticMCP
// Controls material instance parameters on actors

#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Editor.h"
#include "EngineUtils.h"

FString FAgenticMCPServer::HandleMaterialSetParam(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body. Expected: {\"actorName\": \"...\", \"paramName\": \"...\", \"value\": ...}"));
	}

	FString ActorName = BodyJson->GetStringField(TEXT("actorName"));
	FString ParamName = BodyJson->GetStringField(TEXT("paramName"));
	int32 MaterialIndex = BodyJson->HasField(TEXT("materialIndex")) ? static_cast<int32>(BodyJson->GetNumberField(TEXT("materialIndex"))) : 0;

	if (ActorName.IsEmpty() || ParamName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'actorName' or 'paramName' field"));
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

	// Find mesh component
	UMeshComponent* MeshComp = TargetActor->FindComponentByClass<UStaticMeshComponent>();
	if (!MeshComp)
	{
		MeshComp = TargetActor->FindComponentByClass<USkeletalMeshComponent>();
	}

	if (!MeshComp)
	{
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' has no MeshComponent"), *ActorName));
	}

	// Get or create dynamic material instance
	UMaterialInstanceDynamic* DynMat = Cast<UMaterialInstanceDynamic>(MeshComp->GetMaterial(MaterialIndex));
	if (!DynMat)
	{
		UMaterialInterface* BaseMat = MeshComp->GetMaterial(MaterialIndex);
		if (!BaseMat)
		{
			return MakeErrorJson(FString::Printf(TEXT("No material at index %d"), MaterialIndex));
		}
		DynMat = UMaterialInstanceDynamic::Create(BaseMat, MeshComp);
		MeshComp->SetMaterial(MaterialIndex, DynMat);
	}

	bool bSuccess = false;
	FString ParamType;

	// Determine parameter type and set value
	if (BodyJson->HasField(TEXT("scalar")) || BodyJson->HasField(TEXT("float")))
	{
		float Value = BodyJson->HasField(TEXT("scalar")) ?
			BodyJson->GetNumberField(TEXT("scalar")) :
			BodyJson->GetNumberField(TEXT("float"));
		DynMat->SetScalarParameterValue(FName(*ParamName), Value);
		bSuccess = true;
		ParamType = TEXT("scalar");
		Result->SetNumberField(TEXT("value"), Value);
	}
	else if (BodyJson->HasField(TEXT("vector")) || BodyJson->HasField(TEXT("color")))
	{
		const TArray<TSharedPtr<FJsonValue>>* VecArray = nullptr;
		if (BodyJson->HasField(TEXT("vector")))
		{
			BodyJson->TryGetArrayField(TEXT("vector"), VecArray);
		}
		else
		{
			BodyJson->TryGetArrayField(TEXT("color"), VecArray);
		}

		if (VecArray && VecArray->Num() >= 3)
		{
			FLinearColor Color(
				(*VecArray)[0]->AsNumber(),
				(*VecArray)[1]->AsNumber(),
				(*VecArray)[2]->AsNumber(),
				VecArray->Num() > 3 ? (*VecArray)[3]->AsNumber() : 1.0f
			);
			DynMat->SetVectorParameterValue(FName(*ParamName), Color);
			bSuccess = true;
			ParamType = TEXT("vector");

			TArray<TSharedPtr<FJsonValue>> ColorArray;
			ColorArray.Add(MakeShared<FJsonValueNumber>(Color.R));
			ColorArray.Add(MakeShared<FJsonValueNumber>(Color.G));
			ColorArray.Add(MakeShared<FJsonValueNumber>(Color.B));
			ColorArray.Add(MakeShared<FJsonValueNumber>(Color.A));
			Result->SetArrayField(TEXT("value"), ColorArray);
		}
		else
		{
			return MakeErrorJson(TEXT("Vector/color must be array of 3-4 numbers"));
		}
	}
	else if (BodyJson->HasField(TEXT("texture")))
	{
		FString TexturePath = BodyJson->GetStringField(TEXT("texture"));
		UTexture* Texture = LoadObject<UTexture>(nullptr, *TexturePath);
		if (Texture)
		{
			DynMat->SetTextureParameterValue(FName(*ParamName), Texture);
			bSuccess = true;
			ParamType = TEXT("texture");
			Result->SetStringField(TEXT("value"), Texture->GetPathName());
		}
		else
		{
			return MakeErrorJson(FString::Printf(TEXT("Texture not found: %s"), *TexturePath));
		}
	}
	else if (BodyJson->HasField(TEXT("value")))
	{
		// Auto-detect: if number, treat as scalar
		if (BodyJson->HasTypedField<EJson::Number>(TEXT("value")))
		{
			float Value = BodyJson->GetNumberField(TEXT("value"));
			DynMat->SetScalarParameterValue(FName(*ParamName), Value);
			bSuccess = true;
			ParamType = TEXT("scalar");
			Result->SetNumberField(TEXT("value"), Value);
		}
	}

	if (!bSuccess)
	{
		return MakeErrorJson(TEXT("Provide 'scalar', 'vector', 'color', or 'texture' field with value"));
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), TargetActor->GetName());
	Result->SetStringField(TEXT("paramName"), ParamName);
	Result->SetStringField(TEXT("paramType"), ParamType);
	Result->SetNumberField(TEXT("materialIndex"), MaterialIndex);
	Result->SetStringField(TEXT("material"), DynMat->GetName());

	return JsonToString(Result);
}
