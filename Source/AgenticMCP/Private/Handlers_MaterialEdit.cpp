// Handlers_MaterialEdit.cpp
// Material creation and editing handlers for AgenticMCP.
// Extends the existing materialSetParam with full material inspection and creation.
//
// Endpoints:
//   materialList          - List all material assets
//   materialGetInfo       - Get material details (params, textures, shading model)
//   materialCreate        - Create a new material instance
//   materialGetTextures   - Get all texture parameter values on a material
//   materialListInstances - List all material instances of a parent material

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Engine/Texture.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"

// ============================================================
// materialList - List all material assets
// ============================================================
FString FAgenticMCPServer::HandleMaterialList(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TSharedPtr<FJsonObject> BodyJson = ParseBodyJson(Body);
	FString Filter;
	if (BodyJson.IsValid() && BodyJson->HasField(TEXT("filter")))
		Filter = BodyJson->GetStringField(TEXT("filter"));

	bool bInstancesOnly = false;
	if (BodyJson.IsValid() && BodyJson->HasField(TEXT("instancesOnly")))
		bInstancesOnly = BodyJson->GetBoolField(TEXT("instancesOnly"));

	TArray<FAssetData> MatAssets;
	if (bInstancesOnly)
	{
		AR.GetAssetsByClass(UMaterialInstanceConstant::StaticClass()->GetClassPathName(), MatAssets, true);
	}
	else
	{
		AR.GetAssetsByClass(UMaterialInterface::StaticClass()->GetClassPathName(), MatAssets, true);
	}

	TArray<TSharedPtr<FJsonValue>> MatArray;
	for (const FAssetData& Asset : MatAssets)
	{
		FString Name = Asset.AssetName.ToString();
		if (!Filter.IsEmpty() && !Name.Contains(Filter))
			continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Name);
		Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		Entry->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
		MatArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetNumberField(TEXT("count"), MatArray.Num());
	OutJson->SetArrayField(TEXT("materials"), MatArray);
	return JsonToString(OutJson);
}

// ============================================================
// materialGetInfo - Get material details
// ============================================================
FString FAgenticMCPServer::HandleMaterialGetInfo(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	if (Name.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: name"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> MatAssets;
	AR.GetAssetsByClass(UMaterialInterface::StaticClass()->GetClassPathName(), MatAssets, true);

	UMaterialInterface* MatInterface = nullptr;
	for (const FAssetData& Asset : MatAssets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			MatInterface = Cast<UMaterialInterface>(Asset.GetAsset());
			break;
		}
	}
	if (!MatInterface) return MakeErrorJson(FString::Printf(TEXT("Material not found: %s"), *Name));

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("name"), MatInterface->GetName());
	OutJson->SetStringField(TEXT("path"), MatInterface->GetPathName());
	OutJson->SetStringField(TEXT("class"), MatInterface->GetClass()->GetName());

	UMaterial* BaseMaterial = MatInterface->GetMaterial();
	if (BaseMaterial)
	{
		OutJson->SetStringField(TEXT("baseMaterial"), BaseMaterial->GetName());
		OutJson->SetStringField(TEXT("shadingModel"), 
			StaticEnum<EMaterialShadingModel>()->GetNameStringByValue((int64)BaseMaterial->GetShadingModels().GetFirstShadingModel()));
		OutJson->SetStringField(TEXT("blendMode"),
			StaticEnum<EBlendMode>()->GetNameStringByValue((int64)BaseMaterial->BlendMode));
		OutJson->SetBoolField(TEXT("twoSided"), BaseMaterial->IsTwoSided());
	}

	// Scalar parameters
	TArray<TSharedPtr<FJsonValue>> ScalarArr;
	TArray<FMaterialParameterInfo> ScalarParams;
	TArray<FGuid> ScalarGuids;
	MatInterface->GetAllScalarParameterInfo(ScalarParams, ScalarGuids);
	for (const FMaterialParameterInfo& Param : ScalarParams)
	{
		float Value = 0.f;
		MatInterface->GetScalarParameterValue(Param, Value);
		TSharedRef<FJsonObject> PJson = MakeShared<FJsonObject>();
		PJson->SetStringField(TEXT("name"), Param.Name.ToString());
		PJson->SetNumberField(TEXT("value"), Value);
		ScalarArr.Add(MakeShared<FJsonValueObject>(PJson));
	}
	OutJson->SetArrayField(TEXT("scalarParameters"), ScalarArr);

	// Vector parameters
	TArray<TSharedPtr<FJsonValue>> VectorArr;
	TArray<FMaterialParameterInfo> VectorParams;
	TArray<FGuid> VectorGuids;
	MatInterface->GetAllVectorParameterInfo(VectorParams, VectorGuids);
	for (const FMaterialParameterInfo& Param : VectorParams)
	{
		FLinearColor Value;
		MatInterface->GetVectorParameterValue(Param, Value);
		TSharedRef<FJsonObject> PJson = MakeShared<FJsonObject>();
		PJson->SetStringField(TEXT("name"), Param.Name.ToString());
		PJson->SetNumberField(TEXT("r"), Value.R);
		PJson->SetNumberField(TEXT("g"), Value.G);
		PJson->SetNumberField(TEXT("b"), Value.B);
		PJson->SetNumberField(TEXT("a"), Value.A);
		VectorArr.Add(MakeShared<FJsonValueObject>(PJson));
	}
	OutJson->SetArrayField(TEXT("vectorParameters"), VectorArr);

	// Texture parameters
	TArray<TSharedPtr<FJsonValue>> TexArr;
	TArray<FMaterialParameterInfo> TexParams;
	TArray<FGuid> TexGuids;
	MatInterface->GetAllTextureParameterInfo(TexParams, TexGuids);
	for (const FMaterialParameterInfo& Param : TexParams)
	{
		UTexture* Tex = nullptr;
		MatInterface->GetTextureParameterValue(Param, Tex);
		TSharedRef<FJsonObject> PJson = MakeShared<FJsonObject>();
		PJson->SetStringField(TEXT("name"), Param.Name.ToString());
		PJson->SetStringField(TEXT("texture"), Tex ? Tex->GetName() : TEXT("(none)"));
		TexArr.Add(MakeShared<FJsonValueObject>(PJson));
	}
	OutJson->SetArrayField(TEXT("textureParameters"), TexArr);

	return JsonToString(OutJson);
}

// ============================================================
// materialCreate - Create a new material instance constant
// ============================================================
FString FAgenticMCPServer::HandleMaterialCreate(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	FString ParentName = Json->GetStringField(TEXT("parentMaterial"));
	if (Name.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: name"));
	if (ParentName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: parentMaterial"));

	FString Path = Json->HasField(TEXT("path")) ? Json->GetStringField(TEXT("path")) : TEXT("/Game/Materials");

	// Find parent material
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> MatAssets;
	AR.GetAssetsByClass(UMaterialInterface::StaticClass()->GetClassPathName(), MatAssets, true);

	UMaterialInterface* ParentMat = nullptr;
	for (const FAssetData& Asset : MatAssets)
	{
		if (Asset.AssetName.ToString() == ParentName || Asset.GetObjectPathString().Contains(ParentName))
		{
			ParentMat = Cast<UMaterialInterface>(Asset.GetAsset());
			break;
		}
	}
	if (!ParentMat) return MakeErrorJson(FString::Printf(TEXT("Parent material not found: %s"), *ParentName));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	Factory->InitialParent = ParentMat;

	UObject* NewAsset = AssetTools.CreateAsset(Name, Path, UMaterialInstanceConstant::StaticClass(), Factory);
	if (!NewAsset)
		return MakeErrorJson(TEXT("Failed to create material instance"));

	UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(NewAsset);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("name"), MIC->GetName());
	OutJson->SetStringField(TEXT("path"), MIC->GetPathName());
	OutJson->SetStringField(TEXT("parentMaterial"), ParentMat->GetName());
	return JsonToString(OutJson);
}

// ============================================================
// materialListInstances - List all instances of a parent material
// ============================================================
FString FAgenticMCPServer::HandleMaterialListInstances(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ParentName = Json->GetStringField(TEXT("parentMaterial"));
	if (ParentName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: parentMaterial"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> MICAssets;
	AR.GetAssetsByClass(UMaterialInstanceConstant::StaticClass()->GetClassPathName(), MICAssets, true);

	TArray<TSharedPtr<FJsonValue>> InstanceArr;
	for (const FAssetData& Asset : MICAssets)
	{
		UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Asset.GetAsset());
		if (!MIC) continue;

		if (MIC->Parent && (MIC->Parent->GetName() == ParentName || MIC->Parent->GetPathName().Contains(ParentName)))
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), MIC->GetName());
			Entry->SetStringField(TEXT("path"), MIC->GetPathName());
			InstanceArr.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("parentMaterial"), ParentName);
	OutJson->SetNumberField(TEXT("count"), InstanceArr.Num());
	OutJson->SetArrayField(TEXT("instances"), InstanceArr);
	return JsonToString(OutJson);
}
