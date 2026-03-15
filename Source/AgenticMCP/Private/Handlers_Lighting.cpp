// Handlers_Lighting.cpp
// Lighting creation and configuration handlers for AgenticMCP.
// UE 5.6 target.
#include "AgenticMCPServer.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/RectLight.h"
#include "Engine/SkyLight.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// Helper: find actor by name
static AActor* FindActorByName_Light(UWorld* World, const FString& Name)
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == Name || It->GetName() == Name)
			return *It;
	}
	return nullptr;
}

// --- lightCreate ---
FString FAgenticMCPServer::HandleLightCreate(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Type = Json->GetStringField(TEXT("type")).ToLower();
	FVector Location = FVector::ZeroVector;
	const TArray<TSharedPtr<FJsonValue>>* LocArray = nullptr;
	if (Json->TryGetArrayField(TEXT("location"), LocArray) && LocArray->Num() >= 3)
		Location = FVector((*LocArray)[0]->AsNumber(), (*LocArray)[1]->AsNumber(), (*LocArray)[2]->AsNumber());

	FRotator Rotation = FRotator::ZeroRotator;
	const TArray<TSharedPtr<FJsonValue>>* RotArray = nullptr;
	if (Json->TryGetArrayField(TEXT("rotation"), RotArray) && RotArray->Num() >= 3)
		Rotation = FRotator((*RotArray)[0]->AsNumber(), (*RotArray)[1]->AsNumber(), (*RotArray)[2]->AsNumber());

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
		return MakeErrorJson(TEXT("No editor world"));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FTransform SpawnTransform(Rotation, Location);
	AActor* LightActor = nullptr;

	if (Type == TEXT("point"))
		LightActor = World->SpawnActor<APointLight>(APointLight::StaticClass(), &SpawnTransform, SpawnParams);
	else if (Type == TEXT("spot"))
		LightActor = World->SpawnActor<ASpotLight>(ASpotLight::StaticClass(), &SpawnTransform, SpawnParams);
	else if (Type == TEXT("directional"))
		LightActor = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), &SpawnTransform, SpawnParams);
	else if (Type == TEXT("rect"))
		LightActor = World->SpawnActor<ARectLight>(ARectLight::StaticClass(), &SpawnTransform, SpawnParams);
	else if (Type == TEXT("sky"))
		LightActor = World->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), &SpawnTransform, SpawnParams);
	else
		return MakeErrorJson(TEXT("Invalid type. Use: point, spot, directional, rect, sky"));

	if (!LightActor)
		return MakeErrorJson(TEXT("Failed to spawn light actor"));

	FString Label;
	if (Json->TryGetStringField(TEXT("label"), Label) && !Label.IsEmpty())
		LightActor->SetActorLabel(Label);

	// Apply initial properties
	double Intensity = 0;
	if (Json->TryGetNumberField(TEXT("intensity"), Intensity))
	{
		ULightComponent* LC = LightActor->FindComponentByClass<ULightComponent>();
		if (LC) LC->SetIntensity((float)Intensity);
	}

	const TArray<TSharedPtr<FJsonValue>>* ColorArray = nullptr;
	if (Json->TryGetArrayField(TEXT("color"), ColorArray) && ColorArray->Num() >= 3)
	{
		ULightComponent* LC = LightActor->FindComponentByClass<ULightComponent>();
		if (LC) LC->SetLightColor(FLinearColor(
			(float)(*ColorArray)[0]->AsNumber(),
			(float)(*ColorArray)[1]->AsNumber(),
			(float)(*ColorArray)[2]->AsNumber()));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("type"), Type);
	Result->SetStringField(TEXT("actor"), LightActor->GetName());
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- lightSetProperties ---
FString FAgenticMCPServer::HandleLightSetProperties(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
		return MakeErrorJson(TEXT("No editor world"));

	AActor* Actor = FindActorByName_Light(World, ActorName);
	if (!Actor)
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	ULightComponent* LC = Actor->FindComponentByClass<ULightComponent>();
	if (!LC)
		return MakeErrorJson(TEXT("Actor has no LightComponent"));

	int32 Changed = 0;
	double NumVal;
	bool BoolVal;

	if (Json->TryGetNumberField(TEXT("intensity"), NumVal)) { LC->SetIntensity((float)NumVal); Changed++; }
	if (Json->TryGetNumberField(TEXT("temperature"), NumVal)) { LC->SetTemperature((float)NumVal); Changed++; }
	if (Json->TryGetBoolField(TEXT("castShadows"), BoolVal)) { LC->SetCastShadows(BoolVal); Changed++; }
	if (Json->TryGetNumberField(TEXT("indirectIntensity"), NumVal)) { LC->SetIndirectLightingIntensity((float)NumVal); Changed++; }
	if (Json->TryGetNumberField(TEXT("volumetricScattering"), NumVal)) { LC->SetVolumetricScatteringIntensity((float)NumVal); Changed++; }
	if (Json->TryGetBoolField(TEXT("useTemperature"), BoolVal)) { LC->bUseTemperature = BoolVal; Changed++; }

	const TArray<TSharedPtr<FJsonValue>>* ColorArray = nullptr;
	if (Json->TryGetArrayField(TEXT("color"), ColorArray) && ColorArray->Num() >= 3)
	{
		LC->SetLightColor(FLinearColor(
			(float)(*ColorArray)[0]->AsNumber(),
			(float)(*ColorArray)[1]->AsNumber(),
			(float)(*ColorArray)[2]->AsNumber()));
		Changed++;
	}

	// Spot light specific
	if (USpotLightComponent* Spot = Cast<USpotLightComponent>(LC))
	{
		if (Json->TryGetNumberField(TEXT("innerConeAngle"), NumVal)) { Spot->SetInnerConeAngle((float)NumVal); Changed++; }
		if (Json->TryGetNumberField(TEXT("outerConeAngle"), NumVal)) { Spot->SetOuterConeAngle((float)NumVal); Changed++; }
	}

	// Point/Spot attenuation
	if (UPointLightComponent* Point = Cast<UPointLightComponent>(LC))
	{
		if (Json->TryGetNumberField(TEXT("attenuationRadius"), NumVal)) { Point->SetAttenuationRadius((float)NumVal); Changed++; }
		if (Json->TryGetNumberField(TEXT("sourceRadius"), NumVal)) { Point->SourceRadius = (float)NumVal; Changed++; }
		if (Json->TryGetNumberField(TEXT("softSourceRadius"), NumVal)) { Point->SoftSourceRadius = (float)NumVal; Changed++; }
	}

	// Rect light specific
	if (URectLightComponent* Rect = Cast<URectLightComponent>(LC))
	{
		if (Json->TryGetNumberField(TEXT("sourceWidth"), NumVal)) { Rect->SourceWidth = (float)NumVal; Changed++; }
		if (Json->TryGetNumberField(TEXT("sourceHeight"), NumVal)) { Rect->SourceHeight = (float)NumVal; Changed++; }
	}

	Actor->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetNumberField(TEXT("fieldsChanged"), Changed);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- lightList ---
FString FAgenticMCPServer::HandleLightList(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
		return MakeErrorJson(TEXT("No editor world"));

	TArray<TSharedPtr<FJsonValue>> LightsArr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		ULightComponent* LC = It->FindComponentByClass<ULightComponent>();
		if (!LC) continue;

		TSharedRef<FJsonObject> LightObj = MakeShared<FJsonObject>();
		LightObj->SetStringField(TEXT("name"), It->GetActorLabel().IsEmpty() ? It->GetName() : It->GetActorLabel());
		LightObj->SetStringField(TEXT("class"), It->GetClass()->GetName());
		LightObj->SetNumberField(TEXT("intensity"), LC->Intensity);
		LightObj->SetBoolField(TEXT("castShadows"), LC->CastShadows);

		FLinearColor Color = LC->GetLightColor();
		TArray<TSharedPtr<FJsonValue>> ColorArr;
		ColorArr.Add(MakeShared<FJsonValueNumber>(Color.R));
		ColorArr.Add(MakeShared<FJsonValueNumber>(Color.G));
		ColorArr.Add(MakeShared<FJsonValueNumber>(Color.B));
		LightObj->SetArrayField(TEXT("color"), ColorArr);

		FVector Loc = It->GetActorLocation();
		TArray<TSharedPtr<FJsonValue>> LocArr;
		LocArr.Add(MakeShared<FJsonValueNumber>(Loc.X));
		LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Y));
		LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Z));
		LightObj->SetArrayField(TEXT("location"), LocArr);

		LightsArr.Add(MakeShared<FJsonValueObject>(LightObj));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), LightsArr.Num());
	Result->SetArrayField(TEXT("lights"), LightsArr);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}
