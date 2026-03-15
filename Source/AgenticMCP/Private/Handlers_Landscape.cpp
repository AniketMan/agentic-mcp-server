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
//   landscapeSculpt       - Sculpt heightmap (raise/lower/flatten/smooth)
//   landscapePaint        - Paint layer weight at location
//   landscapeAddLayer     - Add a new paint layer
//   landscapeRemoveLayer  - Remove a paint layer
//   landscapeImportHeightmap - Import heightmap from R16 file
//   landscapeExportHeightmap - Export heightmap to R16 file
//   foliageAdd            - Add foliage instances at locations
//   foliageRemove         - Remove foliage instances in radius
//   foliageSetDensity     - Set density scaling for foliage type

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
#include "FoliageType_InstancedStaticMesh.h"
#include "LandscapeEdit.h"
#include "Misc/FileHelper.h"
#include "AssetRegistry/AssetRegistryModule.h"
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
// ============================================================================
// LANDSCAPE + FOLIAGE MUTATION HANDLERS
// Append to end of Handlers_Landscape.cpp
// ============================================================================

// --- landscapeSculpt ---
// Sculpt the landscape heightmap at a world location.
// Body: { "mode": "raise"|"lower"|"flatten"|"smooth", "location": [x,y,z], "radius": 1000, "strength": 0.5 }
FString FAgenticMCPServer::HandleLandscapeSculpt(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString Mode = Json->GetStringField(TEXT("mode")).ToLower();
	double Radius = Json->GetNumberField(TEXT("radius"));
	double Strength = 0.5;
	Json->TryGetNumberField(TEXT("strength"), Strength);

	const TArray<TSharedPtr<FJsonValue>>* LocArray = nullptr;
	if (!Json->TryGetArrayField(TEXT("location"), LocArray) || LocArray->Num() < 3)
	{
		return MakeErrorJson(TEXT("Missing 'location' array [x,y,z]"));
	}
	FVector Location((*LocArray)[0]->AsNumber(), (*LocArray)[1]->AsNumber(), (*LocArray)[2]->AsNumber());

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world"));
	}

	// Find the landscape
	ALandscapeProxy* Landscape = nullptr;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		Landscape = *It;
		break;
	}
	if (!Landscape)
	{
		return MakeErrorJson(TEXT("No landscape found in level"));
	}

	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		return MakeErrorJson(TEXT("No landscape info available"));
	}

	// Convert world location to landscape component coordinates
	FVector LandscapeScale = Landscape->GetActorScale3D();
	FVector LandscapeOrigin = Landscape->GetActorLocation();
	int32 CenterX = FMath::RoundToInt((Location.X - LandscapeOrigin.X) / LandscapeScale.X);
	int32 CenterY = FMath::RoundToInt((Location.Y - LandscapeOrigin.Y) / LandscapeScale.Y);
	int32 RadiusInQuads = FMath::Max(1, FMath::RoundToInt(Radius / LandscapeScale.X));

	int32 X1 = CenterX - RadiusInQuads;
	int32 Y1 = CenterY - RadiusInQuads;
	int32 X2 = CenterX + RadiusInQuads;
	int32 Y2 = CenterY + RadiusInQuads;

	// Read existing heightmap data
	TArray<uint16> HeightData;
	int32 ReadX1 = X1, ReadY1 = Y1, ReadX2 = X2, ReadY2 = Y2;
	FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);
	LandscapeEdit.GetHeightDataFast(ReadX1, ReadY1, ReadX2, ReadY2, HeightData.GetData(), 0);

	if (HeightData.Num() == 0)
	{
		// Allocate and read
		int32 Width = ReadX2 - ReadX1 + 1;
		int32 Height = ReadY2 - ReadY1 + 1;
		HeightData.SetNumZeroed(Width * Height);
		LandscapeEdit.GetHeightDataFast(ReadX1, ReadY1, ReadX2, ReadY2, HeightData.GetData(), 0);
	}

	int32 Width = ReadX2 - ReadX1 + 1;
	int32 Height = ReadY2 - ReadY1 + 1;

	// Apply sculpt operation
	int32 ModifiedCount = 0;
	for (int32 Y = 0; Y < Height; Y++)
	{
		for (int32 X = 0; X < Width; X++)
		{
			int32 WorldX = ReadX1 + X;
			int32 WorldY = ReadY1 + Y;
			float DistFromCenter = FMath::Sqrt(FMath::Square((float)(WorldX - CenterX)) + FMath::Square((float)(WorldY - CenterY)));
			float Falloff = FMath::Max(0.0f, 1.0f - (DistFromCenter / (float)RadiusInQuads));

			int32 Idx = Y * Width + X;
			uint16 CurrentHeight = HeightData[Idx];

			if (Mode == TEXT("raise"))
			{
				int32 Delta = FMath::RoundToInt(Strength * Falloff * 256.0f);
				HeightData[Idx] = (uint16)FMath::Clamp((int32)CurrentHeight + Delta, 0, 65535);
			}
			else if (Mode == TEXT("lower"))
			{
				int32 Delta = FMath::RoundToInt(Strength * Falloff * 256.0f);
				HeightData[Idx] = (uint16)FMath::Clamp((int32)CurrentHeight - Delta, 0, 65535);
			}
			else if (Mode == TEXT("flatten"))
			{
				uint16 TargetHeight = HeightData[(Height / 2) * Width + (Width / 2)];
				HeightData[Idx] = (uint16)FMath::Lerp((float)CurrentHeight, (float)TargetHeight, Falloff * (float)Strength);
			}
			else if (Mode == TEXT("smooth"))
			{
				// Average with neighbors
				int32 Sum = CurrentHeight;
				int32 Count = 1;
				if (X > 0) { Sum += HeightData[Idx - 1]; Count++; }
				if (X < Width - 1) { Sum += HeightData[Idx + 1]; Count++; }
				if (Y > 0) { Sum += HeightData[Idx - Width]; Count++; }
				if (Y < Height - 1) { Sum += HeightData[Idx + Width]; Count++; }
				uint16 Avg = (uint16)(Sum / Count);
				HeightData[Idx] = (uint16)FMath::Lerp((float)CurrentHeight, (float)Avg, Falloff * (float)Strength);
			}
			else
			{
				return MakeErrorJson(FString::Printf(TEXT("Unknown sculpt mode: %s. Use raise/lower/flatten/smooth"), *Mode));
			}

			if (HeightData[Idx] != CurrentHeight)
			{
				ModifiedCount++;
			}
		}
	}

	// Write back
	LandscapeEdit.SetHeightData(ReadX1, ReadY1, ReadX2, ReadY2, HeightData.GetData(), 0, true);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetNumberField(TEXT("modifiedQuads"), ModifiedCount);
	Result->SetNumberField(TEXT("centerX"), CenterX);
	Result->SetNumberField(TEXT("centerY"), CenterY);
	Result->SetNumberField(TEXT("radius"), RadiusInQuads);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, Writer);
	return Out;
}

// --- landscapePaint ---
// Paint a landscape layer at a world location.
// Body: { "layerName": "Grass", "location": [x,y,z], "radius": 500, "weight": 1.0 }
FString FAgenticMCPServer::HandleLandscapePaint(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString LayerName = Json->GetStringField(TEXT("layerName"));
	if (LayerName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'layerName'"));
	}

	double Radius = Json->GetNumberField(TEXT("radius"));
	double Weight = 1.0;
	Json->TryGetNumberField(TEXT("weight"), Weight);

	const TArray<TSharedPtr<FJsonValue>>* LocArray = nullptr;
	if (!Json->TryGetArrayField(TEXT("location"), LocArray) || LocArray->Num() < 3)
	{
		return MakeErrorJson(TEXT("Missing 'location' array [x,y,z]"));
	}
	FVector Location((*LocArray)[0]->AsNumber(), (*LocArray)[1]->AsNumber(), (*LocArray)[2]->AsNumber());

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world"));
	}

	ALandscapeProxy* Landscape = nullptr;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		Landscape = *It;
		break;
	}
	if (!Landscape)
	{
		return MakeErrorJson(TEXT("No landscape found in level"));
	}

	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		return MakeErrorJson(TEXT("No landscape info available"));
	}

	// Find the target layer
	ULandscapeLayerInfoObject* TargetLayer = nullptr;
	for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
	{
		if (LayerSettings.LayerInfoObj && LayerSettings.LayerName.ToString() == LayerName)
		{
			TargetLayer = LayerSettings.LayerInfoObj;
			break;
		}
	}
	if (!TargetLayer)
	{
		return MakeErrorJson(FString::Printf(TEXT("Layer not found: %s"), *LayerName));
	}

	FVector LandscapeScale = Landscape->GetActorScale3D();
	FVector LandscapeOrigin = Landscape->GetActorLocation();
	int32 CenterX = FMath::RoundToInt((Location.X - LandscapeOrigin.X) / LandscapeScale.X);
	int32 CenterY = FMath::RoundToInt((Location.Y - LandscapeOrigin.Y) / LandscapeScale.Y);
	int32 RadiusInQuads = FMath::Max(1, FMath::RoundToInt(Radius / LandscapeScale.X));

	int32 X1 = CenterX - RadiusInQuads;
	int32 Y1 = CenterY - RadiusInQuads;
	int32 X2 = CenterX + RadiusInQuads;
	int32 Y2 = CenterY + RadiusInQuads;

	int32 Width = X2 - X1 + 1;
	int32 Height = Y2 - Y1 + 1;

	TArray<uint8> WeightData;
	WeightData.SetNumZeroed(Width * Height);

	// Fill weight data with circular falloff
	for (int32 Y = 0; Y < Height; Y++)
	{
		for (int32 X = 0; X < Width; X++)
		{
			float DistFromCenter = FMath::Sqrt(FMath::Square((float)(X1 + X - CenterX)) + FMath::Square((float)(Y1 + Y - CenterY)));
			float Falloff = FMath::Max(0.0f, 1.0f - (DistFromCenter / (float)RadiusInQuads));
			WeightData[Y * Width + X] = (uint8)FMath::Clamp(FMath::RoundToInt(Falloff * Weight * 255.0f), 0, 255);
		}
	}

	FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);
	LandscapeEdit.SetAlphaData(TargetLayer, X1, Y1, X2, Y2, WeightData.GetData(), 0);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("layer"), LayerName);
	Result->SetNumberField(TEXT("centerX"), CenterX);
	Result->SetNumberField(TEXT("centerY"), CenterY);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, Writer);
	return Out;
}

// --- landscapeAddLayer ---
// Add a new paint layer to the landscape.
// Body: { "layerName": "Sand", "hardness": 0.5, "noWeightBlend": false }
FString FAgenticMCPServer::HandleLandscapeAddLayer(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString LayerName = Json->GetStringField(TEXT("layerName"));
	if (LayerName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'layerName'"));
	}

	double Hardness = 0.5;
	Json->TryGetNumberField(TEXT("hardness"), Hardness);
	bool bNoWeightBlend = false;
	Json->TryGetBoolField(TEXT("noWeightBlend"), bNoWeightBlend);

	// Create a new ULandscapeLayerInfoObject asset
	FString PackagePath = TEXT("/Game/Landscape/Layers/");
	FString AssetName = FString::Printf(TEXT("LI_%s"), *LayerName);
	FString FullPath = PackagePath + AssetName;

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		return MakeErrorJson(TEXT("Failed to create package for layer info"));
	}

	ULandscapeLayerInfoObject* LayerInfo = NewObject<ULandscapeLayerInfoObject>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!LayerInfo)
	{
		return MakeErrorJson(TEXT("Failed to create layer info object"));
	}

	LayerInfo->LayerName = FName(*LayerName);
	LayerInfo->Hardness = (float)Hardness;
	LayerInfo->bNoWeightBlend = bNoWeightBlend;

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(LayerInfo);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("layerName"), LayerName);
	Result->SetStringField(TEXT("assetPath"), FullPath);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, Writer);
	return Out;
}

// --- landscapeRemoveLayer ---
// Remove a paint layer from the landscape.
// Body: { "layerName": "Sand" }
FString FAgenticMCPServer::HandleLandscapeRemoveLayer(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString LayerName = Json->GetStringField(TEXT("layerName"));
	if (LayerName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'layerName'"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world"));
	}

	ALandscapeProxy* Landscape = nullptr;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		Landscape = *It;
		break;
	}
	if (!Landscape)
	{
		return MakeErrorJson(TEXT("No landscape found in level"));
	}

	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		return MakeErrorJson(TEXT("No landscape info available"));
	}

	bool bFound = false;
	for (int32 i = LandscapeInfo->Layers.Num() - 1; i >= 0; i--)
	{
		if (LandscapeInfo->Layers[i].LayerName.ToString() == LayerName)
		{
			LandscapeInfo->Layers.RemoveAt(i);
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		return MakeErrorJson(FString::Printf(TEXT("Layer not found: %s"), *LayerName));
	}

	Landscape->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("removedLayer"), LayerName);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, Writer);
	return Out;
}

// --- landscapeImportHeightmap ---
// Import a heightmap from a file.
// Body: { "filePath": "/path/to/heightmap.png" }
FString FAgenticMCPServer::HandleLandscapeImportHeightmap(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString FilePath = Json->GetStringField(TEXT("filePath"));
	if (FilePath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'filePath'"));
	}

	if (!FPaths::FileExists(FilePath))
	{
		return MakeErrorJson(FString::Printf(TEXT("File not found: %s"), *FilePath));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world"));
	}

	ALandscapeProxy* Landscape = nullptr;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		Landscape = *It;
		break;
	}
	if (!Landscape)
	{
		return MakeErrorJson(TEXT("No landscape found in level"));
	}

	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		return MakeErrorJson(TEXT("No landscape info available"));
	}

	// Read the raw file data
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to read file: %s"), *FilePath));
	}

	// Determine if R16 or PNG based on extension
	FString Extension = FPaths::GetExtension(FilePath).ToLower();
	TArray<uint16> HeightData;

	if (Extension == TEXT("r16") || Extension == TEXT("raw"))
	{
		// Raw 16-bit heightmap
		if (FileData.Num() % 2 != 0)
		{
			return MakeErrorJson(TEXT("R16 file has odd byte count"));
		}
		HeightData.SetNum(FileData.Num() / 2);
		FMemory::Memcpy(HeightData.GetData(), FileData.GetData(), FileData.Num());
	}
	else
	{
		return MakeErrorJson(TEXT("Supported formats: .r16, .raw. For PNG, convert to R16 first."));
	}

	// Get landscape extents
	int32 MinX, MinY, MaxX, MaxY;
	if (!LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		return MakeErrorJson(TEXT("Failed to get landscape extents"));
	}

	int32 LandscapeWidth = MaxX - MinX + 1;
	int32 LandscapeHeight = MaxY - MinY + 1;
	int32 ExpectedSize = LandscapeWidth * LandscapeHeight;

	if (HeightData.Num() != ExpectedSize)
	{
		return MakeErrorJson(FString::Printf(TEXT("Heightmap size mismatch. Expected %dx%d (%d pixels), got %d pixels"), LandscapeWidth, LandscapeHeight, ExpectedSize, HeightData.Num()));
	}

	FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);
	LandscapeEdit.SetHeightData(MinX, MinY, MaxX, MaxY, HeightData.GetData(), 0, true);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("file"), FilePath);
	Result->SetNumberField(TEXT("width"), LandscapeWidth);
	Result->SetNumberField(TEXT("height"), LandscapeHeight);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, Writer);
	return Out;
}

// --- landscapeExportHeightmap ---
// Export the landscape heightmap to a file.
// Body: { "filePath": "/path/to/export.r16" }
FString FAgenticMCPServer::HandleLandscapeExportHeightmap(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString FilePath = Json->GetStringField(TEXT("filePath"));
	if (FilePath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'filePath'"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world"));
	}

	ALandscapeProxy* Landscape = nullptr;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		Landscape = *It;
		break;
	}
	if (!Landscape)
	{
		return MakeErrorJson(TEXT("No landscape found in level"));
	}

	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		return MakeErrorJson(TEXT("No landscape info available"));
	}

	int32 MinX, MinY, MaxX, MaxY;
	if (!LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		return MakeErrorJson(TEXT("Failed to get landscape extents"));
	}

	int32 Width = MaxX - MinX + 1;
	int32 Height = MaxY - MinY + 1;

	TArray<uint16> HeightData;
	HeightData.SetNum(Width * Height);

	FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);
	LandscapeEdit.GetHeightDataFast(MinX, MinY, MaxX, MaxY, HeightData.GetData(), 0);

	TArray<uint8> RawBytes;
	RawBytes.SetNum(HeightData.Num() * 2);
	FMemory::Memcpy(RawBytes.GetData(), HeightData.GetData(), RawBytes.Num());

	if (!FFileHelper::SaveArrayToFile(RawBytes, *FilePath))
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to write file: %s"), *FilePath));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("file"), FilePath);
	Result->SetNumberField(TEXT("width"), Width);
	Result->SetNumberField(TEXT("height"), Height);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, Writer);
	return Out;
}

// --- foliageAdd ---
// Add foliage instances at specified locations.
// Body: { "foliageType": "/Game/Foliage/Tree_Oak", "locations": [[x,y,z], [x,y,z], ...], "scale": 1.0, "randomRotation": true }
FString FAgenticMCPServer::HandleFoliageAdd(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString FoliageTypePath = Json->GetStringField(TEXT("foliageType"));
	if (FoliageTypePath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'foliageType' asset path"));
	}

	const TArray<TSharedPtr<FJsonValue>>* LocationsArray = nullptr;
	if (!Json->TryGetArrayField(TEXT("locations"), LocationsArray) || LocationsArray->Num() == 0)
	{
		return MakeErrorJson(TEXT("Missing 'locations' array of [x,y,z] arrays"));
	}

	double Scale = 1.0;
	Json->TryGetNumberField(TEXT("scale"), Scale);
	bool bRandomRotation = true;
	Json->TryGetBoolField(TEXT("randomRotation"), bRandomRotation);

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world"));
	}

	// Load the static mesh for foliage
	UStaticMesh* FoliageMesh = LoadObject<UStaticMesh>(nullptr, *FoliageTypePath);
	if (!FoliageMesh)
	{
		return MakeErrorJson(FString::Printf(TEXT("Foliage mesh not found: %s"), *FoliageTypePath));
	}

	// Find or create the InstancedFoliageActor
	AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, true);
	if (!IFA)
	{
		return MakeErrorJson(TEXT("Failed to get or create InstancedFoliageActor"));
	}

	// Find or create the foliage type
	UFoliageType* FoliageType = nullptr;
	for (auto& Pair : IFA->GetFoliageInfos())
	{
		if (Pair.Key && Pair.Key->GetSource() == FoliageMesh)
		{
			FoliageType = Pair.Key;
			break;
		}
	}

	if (!FoliageType)
	{
		// Create a new foliage type for this mesh
		FoliageType = NewObject<UFoliageType_InstancedStaticMesh>(IFA);
		Cast<UFoliageType_InstancedStaticMesh>(FoliageType)->SetStaticMesh(FoliageMesh);
		IFA->AddFoliageType(FoliageType);
	}

	FFoliageInfo* FoliageInfo = IFA->FindInfo(FoliageType);
	if (!FoliageInfo)
	{
		return MakeErrorJson(TEXT("Failed to find foliage info after creation"));
	}

	int32 AddedCount = 0;
	for (const TSharedPtr<FJsonValue>& LocVal : *LocationsArray)
	{
		const TArray<TSharedPtr<FJsonValue>>* Coords = nullptr;
		if (!LocVal->TryGetArray(Coords) || Coords->Num() < 3)
		{
			continue;
		}

		FFoliageInstance Instance;
		Instance.Location = FVector((*Coords)[0]->AsNumber(), (*Coords)[1]->AsNumber(), (*Coords)[2]->AsNumber());
		Instance.DrawScale3D = FVector(Scale, Scale, Scale);

		if (bRandomRotation)
		{
			Instance.Rotation = FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f);
		}

		FoliageInfo->AddInstance(IFA, FoliageType, Instance);
		AddedCount++;
	}

	IFA->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetNumberField(TEXT("instancesAdded"), AddedCount);
	Result->SetStringField(TEXT("foliageType"), FoliageTypePath);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, Writer);
	return Out;
}

// --- foliageRemove ---
// Remove foliage instances within a radius of a location.
// Body: { "location": [x,y,z], "radius": 500, "foliageType": "/Game/Foliage/Tree_Oak" (optional, all if omitted) }
FString FAgenticMCPServer::HandleFoliageRemove(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	const TArray<TSharedPtr<FJsonValue>>* LocArray = nullptr;
	if (!Json->TryGetArrayField(TEXT("location"), LocArray) || LocArray->Num() < 3)
	{
		return MakeErrorJson(TEXT("Missing 'location' array [x,y,z]"));
	}
	FVector Location((*LocArray)[0]->AsNumber(), (*LocArray)[1]->AsNumber(), (*LocArray)[2]->AsNumber());

	double Radius = Json->GetNumberField(TEXT("radius"));
	if (Radius <= 0)
	{
		return MakeErrorJson(TEXT("'radius' must be positive"));
	}

	FString FilterType;
	Json->TryGetStringField(TEXT("foliageType"), FilterType);

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world"));
	}

	AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, false);
	if (!IFA)
	{
		return MakeErrorJson(TEXT("No foliage actor in current level"));
	}

	int32 RemovedCount = 0;
	float RadiusSq = (float)(Radius * Radius);

	for (auto& Pair : IFA->GetFoliageInfos())
	{
		if (!FilterType.IsEmpty() && Pair.Key)
		{
			UStaticMesh* Mesh = nullptr;
			if (UFoliageType_InstancedStaticMesh* ISM = Cast<UFoliageType_InstancedStaticMesh>(Pair.Key))
			{
				Mesh = ISM->GetStaticMesh();
			}
			if (!Mesh || Mesh->GetPathName() != FilterType)
			{
				continue;
			}
		}

		FFoliageInfo* Info = Pair.Value.Get();
		if (!Info)
		{
			continue;
		}

		TArray<int32> InstancesToRemove;
		for (int32 i = 0; i < Info->Instances.Num(); i++)
		{
			if (FVector::DistSquared(Info->Instances[i].Location, Location) <= RadiusSq)
			{
				InstancesToRemove.Add(i);
			}
		}

		// Remove in reverse order to preserve indices
		for (int32 i = InstancesToRemove.Num() - 1; i >= 0; i--)
		{
			Info->RemoveInstances(IFA, {InstancesToRemove[i]}, true);
			RemovedCount++;
		}
	}

	IFA->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetNumberField(TEXT("instancesRemoved"), RemovedCount);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, Writer);
	return Out;
}

// --- foliageSetDensity ---
// Set density scaling for a foliage type.
// Body: { "foliageType": "/Game/Foliage/Tree_Oak", "density": 0.5 }
FString FAgenticMCPServer::HandleFoliageSetDensity(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString FoliageTypePath = Json->GetStringField(TEXT("foliageType"));
	if (FoliageTypePath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'foliageType'"));
	}

	double Density = 1.0;
	if (!Json->TryGetNumberField(TEXT("density"), Density))
	{
		return MakeErrorJson(TEXT("Missing 'density' value (0.0 - 1.0)"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world"));
	}

	AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, false);
	if (!IFA)
	{
		return MakeErrorJson(TEXT("No foliage actor in current level"));
	}

	UStaticMesh* TargetMesh = LoadObject<UStaticMesh>(nullptr, *FoliageTypePath);
	if (!TargetMesh)
	{
		return MakeErrorJson(FString::Printf(TEXT("Mesh not found: %s"), *FoliageTypePath));
	}

	bool bFound = false;
	for (auto& Pair : IFA->GetFoliageInfos())
	{
		if (UFoliageType_InstancedStaticMesh* ISM = Cast<UFoliageType_InstancedStaticMesh>(Pair.Key))
		{
			if (ISM->GetStaticMesh() == TargetMesh)
			{
				ISM->Density = (float)Density;
				ISM->MarkPackageDirty();
				bFound = true;
				break;
			}
		}
	}

	if (!bFound)
	{
		return MakeErrorJson(FString::Printf(TEXT("Foliage type not found for mesh: %s"), *FoliageTypePath));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("foliageType"), FoliageTypePath);
	Result->SetNumberField(TEXT("density"), Density);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, Writer);
	return Out;
}
