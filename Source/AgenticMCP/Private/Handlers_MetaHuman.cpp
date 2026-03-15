// Handlers_MetaHuman.cpp
// MetaHuman and Groom asset handlers for AgenticMCP.
// UE 5.6 target.
#include "AgenticMCPServer.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// --- metahumanList ---
FString FAgenticMCPServer::HandleMetaHumanList(const FString& Body)
{
	FAssetRegistryModule* ARM_Check = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry");
	if (!ARM_Check)
		return MakeErrorJson(TEXT("AssetRegistry not available"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	TArray<TSharedPtr<FJsonValue>> AssetsArr;

	// MetaHumans are typically Blueprint assets under /Game/MetaHumans/
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByPath(TEXT("/Game/MetaHumans"), Assets, true);

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetClassPath.GetAssetName() == TEXT("Blueprint"))
		{
			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
			Obj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
			Obj->SetStringField(TEXT("class"), Asset.AssetClassPath.ToString());
			AssetsArr.Add(MakeShared<FJsonValueObject>(Obj));
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), AssetsArr.Num());
	Result->SetArrayField(TEXT("metahumans"), AssetsArr);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- metahumanSpawn ---
FString FAgenticMCPServer::HandleMetaHumanSpawn(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString BPPath = Json->GetStringField(TEXT("blueprintPath"));
	if (BPPath.IsEmpty())
		return MakeErrorJson(TEXT("Missing 'blueprintPath'"));

	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BPPath);
	if (!BP || !BP->GeneratedClass)
		return MakeErrorJson(FString::Printf(TEXT("Blueprint not found or not compiled: %s"), *BPPath));

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
	AActor* Actor = World->SpawnActor<AActor>(BP->GeneratedClass, &SpawnTransform, SpawnParams);
	if (!Actor)
		return MakeErrorJson(TEXT("Failed to spawn MetaHuman"));

	FString Label;
	if (Json->TryGetStringField(TEXT("label"), Label) && !Label.IsEmpty())
		Actor->SetActorLabel(Label);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("actor"), Actor->GetName());
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- groomList ---
FString FAgenticMCPServer::HandleGroomList(const FString& Body)
{
	FAssetRegistryModule* ARM_Check = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry");
	if (!ARM_Check)
		return MakeErrorJson(TEXT("AssetRegistry not available"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/HairStrandsCore"), TEXT("GroomAsset")), Assets);

	TArray<TSharedPtr<FJsonValue>> AssetsArr;
	for (const FAssetData& Asset : Assets)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		Obj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		AssetsArr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), AssetsArr.Num());
	Result->SetArrayField(TEXT("grooms"), AssetsArr);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, W); return Out;
}

// --- groomSetBinding ---
FString FAgenticMCPServer::HandleGroomSetBinding(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString GroomPath = Json->GetStringField(TEXT("groomPath"));
	FString BindingPath = Json->GetStringField(TEXT("bindingPath"));

	if (ActorName.IsEmpty() || GroomPath.IsEmpty())
		return MakeErrorJson(TEXT("Missing 'actorName' or 'groomPath'"));

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
		return MakeErrorJson(TEXT("No editor world"));

	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorName || It->GetName() == ActorName)
		{ FoundActor = *It; break; }
	}
	if (!FoundActor)
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	// The groom component is typically added via addComponent
	// This handler sets the groom asset and binding on an existing GroomComponent
	TArray<UActorComponent*> Components;
	FoundActor->GetComponents(Components);

	for (UActorComponent* Comp : Components)
	{
		if (Comp->GetClass()->GetName().Contains(TEXT("GroomComponent")))
		{
			// Use reflection to set the GroomAsset property
			FProperty* GroomProp = Comp->GetClass()->FindPropertyByName(TEXT("GroomAsset"));
			if (GroomProp)
			{
				UObject* GroomAsset = LoadObject<UObject>(nullptr, *GroomPath);
				if (GroomAsset)
				{
					FObjectProperty* ObjProp = CastField<FObjectProperty>(GroomProp);
					if (ObjProp)
					{
						ObjProp->SetObjectPropertyValue(GroomProp->ContainerPtrToValuePtr<void>(Comp), GroomAsset);
					}
				}
			}

			if (!BindingPath.IsEmpty())
			{
				FProperty* BindProp = Comp->GetClass()->FindPropertyByName(TEXT("BindingAsset"));
				if (BindProp)
				{
					UObject* BindAsset = LoadObject<UObject>(nullptr, *BindingPath);
					if (BindAsset)
					{
						FObjectProperty* ObjProp = CastField<FObjectProperty>(BindProp);
						if (ObjProp)
						{
							ObjProp->SetObjectPropertyValue(BindProp->ContainerPtrToValuePtr<void>(Comp), BindAsset);
						}
					}
				}
			}

			FoundActor->MarkPackageDirty();

			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("status"), TEXT("ok"));
			Result->SetStringField(TEXT("groom"), GroomPath);
			FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Result, W); return Out;
		}
	}

	return MakeErrorJson(TEXT("No GroomComponent found on actor. Add one first with addComponent."));
}
