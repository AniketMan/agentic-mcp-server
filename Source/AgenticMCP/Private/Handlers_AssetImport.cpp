// Handlers_AssetImport.cpp
// Asset import and management handlers for AgenticMCP.
// Provides asset import (FBX, textures), asset info, and asset operations.
//
// Endpoints:
//   assetImport           - Import an asset file (FBX, OBJ, PNG, TGA, WAV, etc.)
//   assetGetInfo          - Get detailed info about any asset
//   assetDuplicate        - Duplicate an existing asset
//   assetRename           - Rename an asset
//   assetDelete           - Delete an asset (with confirmation flag)
//   assetListByType       - List assets filtered by type (StaticMesh, Texture, Sound, etc.)

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ObjectTools.h"
#include "FileHelpers.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Sound/SoundWave.h"

// ============================================================
// assetImport - Import an asset file
// ============================================================
FString FAgenticMCPServer::HandleAssetImport(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString FilePath = Json->GetStringField(TEXT("filePath"));
	if (FilePath.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: filePath"));

	FString DestPath = Json->HasField(TEXT("destinationPath")) ? Json->GetStringField(TEXT("destinationPath")) : TEXT("/Game/Imports");

	// Verify file exists
	if (!FPaths::FileExists(FilePath))
		return MakeErrorJson(FString::Printf(TEXT("File not found: %s"), *FilePath));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	TArray<FString> Files;
	Files.Add(FilePath);

	TArray<UObject*> ImportedAssets = AssetTools.ImportAssets(Files, DestPath);

	if (ImportedAssets.Num() == 0)
		return MakeErrorJson(FString::Printf(TEXT("Failed to import: %s"), *FilePath));

	TArray<TSharedPtr<FJsonValue>> ImportedArr;
	for (UObject* Asset : ImportedAssets)
	{
		if (!Asset) continue;
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Asset->GetName());
		Entry->SetStringField(TEXT("path"), Asset->GetPathName());
		Entry->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
		ImportedArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetNumberField(TEXT("importedCount"), ImportedArr.Num());
	OutJson->SetArrayField(TEXT("importedAssets"), ImportedArr);
	return JsonToString(OutJson);
}

// ============================================================
// assetGetInfo - Get detailed info about any asset
// ============================================================
FString FAgenticMCPServer::HandleAssetGetInfo(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	if (Name.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: name"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	// Search by name across all asset types
	TArray<FAssetData> AllAssets;
	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	AR.GetAssets(Filter, AllAssets);

	FAssetData FoundAsset;
	bool bFound = false;
	for (const FAssetData& Asset : AllAssets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			FoundAsset = Asset;
			bFound = true;
			break;
		}
	}
	if (!bFound) return MakeErrorJson(FString::Printf(TEXT("Asset not found: %s"), *Name));

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("name"), FoundAsset.AssetName.ToString());
	OutJson->SetStringField(TEXT("path"), FoundAsset.GetObjectPathString());
	OutJson->SetStringField(TEXT("class"), FoundAsset.AssetClassPath.GetAssetName().ToString());
	OutJson->SetStringField(TEXT("packageName"), FoundAsset.PackageName.ToString());

	// Disk size
	int64 DiskSize = FoundAsset.GetTagValueRef<int64>(FName("Size"));
	if (DiskSize > 0)
	{
		OutJson->SetNumberField(TEXT("diskSizeBytes"), (double)DiskSize);
	}

	// Type-specific info
	UObject* LoadedAsset = FoundAsset.GetAsset();
	if (UStaticMesh* SM = Cast<UStaticMesh>(LoadedAsset))
	{
		OutJson->SetNumberField(TEXT("numLODs"), SM->GetNumLODs());
		if (SM->GetRenderData() && SM->GetRenderData()->LODResources.Num() > 0)
		{
			OutJson->SetNumberField(TEXT("numVertices"), SM->GetRenderData()->LODResources[0].GetNumVertices());
			OutJson->SetNumberField(TEXT("numTriangles"), SM->GetRenderData()->LODResources[0].GetNumTriangles());
		}
	}
	else if (UTexture2D* Tex = Cast<UTexture2D>(LoadedAsset))
	{
		OutJson->SetNumberField(TEXT("width"), Tex->GetSizeX());
		OutJson->SetNumberField(TEXT("height"), Tex->GetSizeY());
		OutJson->SetStringField(TEXT("pixelFormat"), GetPixelFormatString(Tex->GetPixelFormat()));
		OutJson->SetNumberField(TEXT("numMips"), Tex->GetNumMips());
	}
	else if (USoundWave* Sound = Cast<USoundWave>(LoadedAsset))
	{
		OutJson->SetNumberField(TEXT("duration"), Sound->Duration);
		OutJson->SetNumberField(TEXT("sampleRate"), Sound->GetSampleRateForCurrentPlatform());
		OutJson->SetNumberField(TEXT("numChannels"), Sound->NumChannels);
	}

	return JsonToString(OutJson);
}

// ============================================================
// assetDuplicate - Duplicate an existing asset
// ============================================================
FString FAgenticMCPServer::HandleAssetDuplicate(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SourcePath = Json->GetStringField(TEXT("sourcePath"));
	FString NewName = Json->GetStringField(TEXT("newName"));
	if (SourcePath.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: sourcePath"));
	if (NewName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: newName"));

	FString DestPath = Json->HasField(TEXT("destinationPath")) ? Json->GetStringField(TEXT("destinationPath")) : FPaths::GetPath(SourcePath);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	// UE 5.6: DuplicateAsset takes UObject* not path
    UObject* SourceAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *SourcePath);
    if (!SourceAsset) return MakeErrorJson(FString::Printf(TEXT("Source asset not found: %s"), *SourcePath));
    UObject* DupAsset = AssetTools.DuplicateAsset(NewName, DestPath, SourceAsset);

	if (!DupAsset)
		return MakeErrorJson(FString::Printf(TEXT("Failed to duplicate: %s"), *SourcePath));

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("name"), DupAsset->GetName());
	OutJson->SetStringField(TEXT("path"), DupAsset->GetPathName());
	OutJson->SetStringField(TEXT("class"), DupAsset->GetClass()->GetName());
	return JsonToString(OutJson);
}

// ============================================================
// assetRename - Rename an asset
// ============================================================
FString FAgenticMCPServer::HandleAssetRename(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString OldPath = Json->GetStringField(TEXT("assetPath"));
	FString NewName = Json->GetStringField(TEXT("newName"));
	if (OldPath.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: assetPath"));
	if (NewName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: newName"));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	FString NewPath = FPaths::GetPath(OldPath) / NewName;

	TArray<FAssetRenameData> RenameData;
	// UE 5.6: FAssetRenameData takes TWeakObjectPtr<UObject> not path
    UObject* AssetToRename = StaticLoadObject(UObject::StaticClass(), nullptr, *OldPath);
    if (!AssetToRename) return MakeErrorJson(FString::Printf(TEXT("Asset not found: %s"), *OldPath));
    RenameData.Add(FAssetRenameData(AssetToRename, FPaths::GetPath(OldPath), NewName));

	bool bSuccess = AssetTools.RenameAssets(RenameData);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), bSuccess);
	OutJson->SetStringField(TEXT("oldPath"), OldPath);
	OutJson->SetStringField(TEXT("newName"), NewName);
	if (!bSuccess)
		OutJson->SetStringField(TEXT("error"), TEXT("Rename failed - asset may be in use or name conflicts"));
	return JsonToString(OutJson);
}

// ============================================================
// assetDelete - Delete an asset (requires confirm flag)
// ============================================================
FString FAgenticMCPServer::HandleAssetDelete(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString AssetPath = Json->GetStringField(TEXT("assetPath"));
	if (AssetPath.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: assetPath"));

	// Safety: require explicit confirmation
	bool bConfirm = Json->HasField(TEXT("confirm")) ? Json->GetBoolField(TEXT("confirm")) : false;
	if (!bConfirm)
		return MakeErrorJson(TEXT("Destructive operation: set 'confirm': true to proceed with asset deletion"));

	UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	if (!Asset)
		return MakeErrorJson(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));

	FString AssetName = Asset->GetName();
	FString AssetClass = Asset->GetClass()->GetName();

	TArray<UObject*> ObjectsToDelete;
	ObjectsToDelete.Add(Asset);
	int32 Deleted = ObjectTools::DeleteObjects(ObjectsToDelete, false);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), Deleted > 0);
	OutJson->SetStringField(TEXT("deletedAsset"), AssetName);
	OutJson->SetStringField(TEXT("class"), AssetClass);
	OutJson->SetNumberField(TEXT("deletedCount"), Deleted);
	return JsonToString(OutJson);
}

// ============================================================
// assetListByType - List assets by type
// ============================================================
FString FAgenticMCPServer::HandleAssetListByType(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString TypeName = Json->GetStringField(TEXT("type"));
	if (TypeName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: type (e.g. StaticMesh, Texture2D, SoundWave, SkeletalMesh)"));

	FString Filter;
	if (Json->HasField(TEXT("filter")))
		Filter = Json->GetStringField(TEXT("filter"));

	FString Path;
	if (Json->HasField(TEXT("path")))
		Path = Json->GetStringField(TEXT("path"));

	UClass* AssetClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *TypeName));
	if (!AssetClass)
	{
		// Try CoreUObject
		AssetClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/CoreUObject.%s"), *TypeName));
	}
	if (!AssetClass)
		return MakeErrorJson(FString::Printf(TEXT("Unknown asset type: %s"), *TypeName));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> Assets;
	AR.GetAssetsByClass(AssetClass->GetClassPathName(), Assets, true);

	TArray<TSharedPtr<FJsonValue>> AssetArr;
	for (const FAssetData& Asset : Assets)
	{
		FString Name = Asset.AssetName.ToString();
		if (!Filter.IsEmpty() && !Name.Contains(Filter))
			continue;
		if (!Path.IsEmpty() && !Asset.GetObjectPathString().Contains(Path))
			continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Name);
		Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		AssetArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("type"), TypeName);
	OutJson->SetNumberField(TEXT("count"), AssetArr.Num());
	OutJson->SetArrayField(TEXT("assets"), AssetArr);
	return JsonToString(OutJson);
}

// ============================================================================
// ASSET MOVE HANDLER
// ============================================================================

// --- assetMove ---
// Move an asset to a different content folder.
// Body: { "sourcePath": "/Game/OldFolder/MyAsset", "destPath": "/Game/NewFolder/MyAsset" }
FString FAgenticMCPServer::HandleAssetMove(const FString& Body)
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

	FString SourcePath = Json->GetStringField(TEXT("sourcePath"));
	FString DestPath = Json->GetStringField(TEXT("destPath"));

	if (SourcePath.IsEmpty() || DestPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'sourcePath' or 'destPath'"));
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(SourcePath));
	if (!AssetData.IsValid())
	{
		return MakeErrorJson(FString::Printf(TEXT("Asset not found: %s"), *SourcePath));
	}

	UObject* Asset = AssetData.GetAsset();
	if (!Asset)
	{
		return MakeErrorJson(TEXT("Failed to load asset"));
	}

	TArray<FAssetRenameData> RenameData;
	RenameData.Add(FAssetRenameData(Asset, FPaths::GetPath(DestPath), FPaths::GetBaseFilename(DestPath)));

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	bool bSuccess = AssetToolsModule.Get().RenameAssets(RenameData);

	if (!bSuccess)
	{
		return MakeErrorJson(TEXT("Asset move failed"));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetStringField(TEXT("from"), SourcePath);
	OutJson->SetStringField(TEXT("to"), DestPath);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}
