// Handlers_Landscape.cpp
// Landscape and Foliage inspection handlers for AgenticMCP.
// The Landscape module is already linked in Build.cs but had no handlers.
//
// Endpoints:
//   landscapeList         - List all landscape actors in the level
//   landscapeGetInfo      - Get detailed info about a landscape (components, layers, size)
//   landscapeGetLayers    - Get landscape layer/paint layer info
//   foliageList           - List all foliage types in the level
//   foliageGetStats       - Get foliage instance counts and distribution stats

#include "AgenticMCPServer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "InstancedFoliageActor.h"
#include "FoliageType.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

static UWorld* GetEditorWorld_Land()
{
	if (GEditor) return GEditor->GetEditorWorldContext().World();
	return nullptr;
}

// ============================================================
// landscapeList - List all landscape actors
// ============================================================
FString FAgenticMCPServer::HandleLandscapeList(const FString& Body)
{
	UWorld* World = GetEditorWorld_Land();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	TArray<TSharedPtr<FJsonValue>> LandArray;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Proxy = *It;
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Proxy->GetActorLabel());
		Entry->SetStringField(TEXT("class"), Proxy->GetClass()->GetName());
		Entry->SetNumberField(TEXT("componentCount"), Proxy->LandscapeComponents.Num());

		FVector Loc = Proxy->GetActorLocation();
		Entry->SetNumberField(TEXT("locationX"), Loc.X);
		Entry->SetNumberField(TEXT("locationY"), Loc.Y);
		Entry->SetNumberField(TEXT("locationZ"), Loc.Z);

		// Overall bounds
		FBox Bounds = Proxy->GetComponentsBoundingBox(true);
		Entry->SetNumberField(TEXT("boundsMinX"), Bounds.Min.X);
		Entry->SetNumberField(TEXT("boundsMinY"), Bounds.Min.Y);
		Entry->SetNumberField(TEXT("boundsMaxX"), Bounds.Max.X);
		Entry->SetNumberField(TEXT("boundsMaxY"), Bounds.Max.Y);

		LandArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), LandArray.Num());
	Result->SetArrayField(TEXT("landscapes"), LandArray);
	return JsonToString(Result);
}

// ============================================================
// landscapeGetInfo - Detailed landscape info
// ============================================================
FString FAgenticMCPServer::HandleLandscapeGetInfo(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	if (Name.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: name"));

	UWorld* World = GetEditorWorld_Land();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	ALandscapeProxy* Found = nullptr;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == Name || (*It)->GetName() == Name)
		{
			Found = *It;
			break;
		}
	}
	if (!Found) return MakeErrorJson(FString::Printf(TEXT("Landscape not found: %s"), *Name));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), Found->GetActorLabel());
	Result->SetStringField(TEXT("class"), Found->GetClass()->GetName());
	Result->SetNumberField(TEXT("componentCount"), Found->LandscapeComponents.Num());

	// Component size info
	if (Found->LandscapeComponents.Num() > 0)
	{
		ULandscapeComponent* FirstComp = Found->LandscapeComponents[0];
		if (FirstComp)
		{
			Result->SetNumberField(TEXT("componentSizeQuads"), FirstComp->ComponentSizeQuads);
			Result->SetNumberField(TEXT("subsectionSizeQuads"), FirstComp->SubsectionSizeQuads);
			Result->SetNumberField(TEXT("numSubsections"), FirstComp->NumSubsections);
		}
	}

	// Layer info
	TArray<TSharedPtr<FJsonValue>> LayerArr;
	for (const FLandscapeInfoLayerSettings& LayerSetting : Found->GetLandscapeInfo()->Layers)
	{
		if (!LayerSetting.LayerInfoObj) continue;
		TSharedRef<FJsonObject> LayerJson = MakeShared<FJsonObject>();
		LayerJson->SetStringField(TEXT("name"), LayerSetting.LayerName.ToString());
		LayerJson->SetStringField(TEXT("layerInfoName"), LayerSetting.LayerInfoObj->GetName());
		LayerArr.Add(MakeShared<FJsonValueObject>(LayerJson));
	}
	Result->SetArrayField(TEXT("layers"), LayerArr);
	Result->SetNumberField(TEXT("layerCount"), LayerArr.Num());

	return JsonToString(Result);
}

// ============================================================
// landscapeGetLayers - Get paint layer info
// ============================================================
FString FAgenticMCPServer::HandleLandscapeGetLayers(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	if (Name.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: name"));

	UWorld* World = GetEditorWorld_Land();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	ALandscapeProxy* Found = nullptr;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == Name || (*It)->GetName() == Name)
		{
			Found = *It;
			break;
		}
	}
	if (!Found) return MakeErrorJson(FString::Printf(TEXT("Landscape not found: %s"), *Name));

	ULandscapeInfo* Info = Found->GetLandscapeInfo();
	if (!Info) return MakeErrorJson(TEXT("No LandscapeInfo available"));

	TArray<TSharedPtr<FJsonValue>> LayerArr;
	for (const FLandscapeInfoLayerSettings& LayerSetting : Info->Layers)
	{
		TSharedRef<FJsonObject> LayerJson = MakeShared<FJsonObject>();
		LayerJson->SetStringField(TEXT("layerName"), LayerSetting.LayerName.ToString());

		if (LayerSetting.LayerInfoObj)
		{
			ULandscapeLayerInfoObject* LayerInfo = LayerSetting.LayerInfoObj;
			LayerJson->SetStringField(TEXT("layerInfoPath"), LayerInfo->GetPathName());
			LayerJson->SetBoolField(TEXT("isWeightBlended"), !LayerInfo->bNoWeightBlend);
		}

		LayerArr.Add(MakeShared<FJsonValueObject>(LayerJson));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("landscape"), Found->GetActorLabel());
	Result->SetNumberField(TEXT("layerCount"), LayerArr.Num());
	Result->SetArrayField(TEXT("layers"), LayerArr);
	return JsonToString(Result);
}

// ============================================================
// foliageList - List all foliage types in the level
// ============================================================
FString FAgenticMCPServer::HandleFoliageList(const FString& Body)
{
	UWorld* World = GetEditorWorld_Land();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	TArray<TSharedPtr<FJsonValue>> FoliageArr;
	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* IFA = *It;
		for (auto& Pair : IFA->GetFoliageInfos())
		{
			const UFoliageType* FoliageType = Pair.Key;
			const FFoliageInfo& FoliageInfo = *Pair.Value;

			if (!FoliageType) continue;

			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), FoliageType->GetName());
			Entry->SetStringField(TEXT("path"), FoliageType->GetPathName());
			Entry->SetNumberField(TEXT("instanceCount"), FoliageInfo.Instances.Num());

			FoliageArr.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("foliageTypeCount"), FoliageArr.Num());
	Result->SetArrayField(TEXT("foliageTypes"), FoliageArr);
	return JsonToString(Result);
}

// ============================================================
// foliageGetStats - Foliage distribution stats
// ============================================================
FString FAgenticMCPServer::HandleFoliageGetStats(const FString& Body)
{
	UWorld* World = GetEditorWorld_Land();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	int32 TotalInstances = 0;
	int32 TotalTypes = 0;

	TArray<TSharedPtr<FJsonValue>> TypeArr;
	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		AInstancedFoliageActor* IFA = *It;
		for (auto& Pair : IFA->GetFoliageInfos())
		{
			const UFoliageType* FoliageType = Pair.Key;
			const FFoliageInfo& FoliageInfo = *Pair.Value;
			if (!FoliageType) continue;

			int32 Count = FoliageInfo.Instances.Num();
			TotalInstances += Count;
			TotalTypes++;

			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), FoliageType->GetName());
			Entry->SetNumberField(TEXT("instanceCount"), Count);

			// Density and scaling info from the foliage type
			Entry->SetNumberField(TEXT("density"), FoliageType->Density);
			Entry->SetNumberField(TEXT("minScale"), FoliageType->MinScale_DEPRECATED);
			Entry->SetNumberField(TEXT("maxScale"), FoliageType->MaxScale_DEPRECATED);

			TypeArr.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("totalTypes"), TotalTypes);
	Result->SetNumberField(TEXT("totalInstances"), TotalInstances);
	Result->SetArrayField(TEXT("types"), TypeArr);
	return JsonToString(Result);
}
