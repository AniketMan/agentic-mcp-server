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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("importedCount"), ImportedArr.Num());
	Result->SetArrayField(TEXT("importedAssets"), ImportedArr);
	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), FoundAsset.AssetName.ToString());
	Result->SetStringField(TEXT("path"), FoundAsset.GetObjectPathString());
	Result->SetStringField(TEXT("class"), FoundAsset.AssetClassPath.GetAssetName().ToString());
	Result->SetStringField(TEXT("packageName"), FoundAsset.PackageName.ToString());

	// Disk size
	int64 DiskSize = FoundAsset.GetTagValueRef<int64>(FName("Size"));
	if (DiskSize > 0)
	{
		Result->SetNumberField(TEXT("diskSizeBytes"), (double)DiskSize);
	}

	// Type-specific info
	UObject* LoadedAsset = FoundAsset.GetAsset();
	if (UStaticMesh* SM = Cast<UStaticMesh>(LoadedAsset))
	{
		Result->SetNumberField(TEXT("numLODs"), SM->GetNumLODs());
		if (SM->GetRenderData() && SM->GetRenderData()->LODResources.Num() > 0)
		{
			Result->SetNumberField(TEXT("numVertices"), SM->GetRenderData()->LODResources[0].GetNumVertices());
			Result->SetNumberField(TEXT("numTriangles"), SM->GetRenderData()->LODResources[0].GetNumTriangles());
		}
	}
	else if (UTexture2D* Tex = Cast<UTexture2D>(LoadedAsset))
	{
		Result->SetNumberField(TEXT("width"), Tex->GetSizeX());
		Result->SetNumberField(TEXT("height"), Tex->GetSizeY());
		Result->SetStringField(TEXT("pixelFormat"), GetPixelFormatString(Tex->GetPixelFormat()));
		Result->SetNumberField(TEXT("numMips"), Tex->GetNumMips());
	}
	else if (USoundWave* Sound = Cast<USoundWave>(LoadedAsset))
	{
		Result->SetNumberField(TEXT("duration"), Sound->Duration);
		Result->SetNumberField(TEXT("sampleRate"), Sound->GetSampleRateForCurrentPlatform());
		Result->SetNumberField(TEXT("numChannels"), Sound->NumChannels);
	}

	return JsonToString(Result);
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
	UObject* DupAsset = AssetTools.DuplicateAsset(NewName, DestPath, SourcePath);

	if (!DupAsset)
		return MakeErrorJson(FString::Printf(TEXT("Failed to duplicate: %s"), *SourcePath));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), DupAsset->GetName());
	Result->SetStringField(TEXT("path"), DupAsset->GetPathName());
	Result->SetStringField(TEXT("class"), DupAsset->GetClass()->GetName());
	return JsonToString(Result);
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
	RenameData.Add(FAssetRenameData(OldPath, FPaths::GetPath(OldPath), NewName));

	bool bSuccess = AssetTools.RenameAssets(RenameData);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("oldPath"), OldPath);
	Result->SetStringField(TEXT("newName"), NewName);
	if (!bSuccess)
		Result->SetStringField(TEXT("error"), TEXT("Rename failed - asset may be in use or name conflicts"));
	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), Deleted > 0);
	Result->SetStringField(TEXT("deletedAsset"), AssetName);
	Result->SetStringField(TEXT("class"), AssetClass);
	Result->SetNumberField(TEXT("deletedCount"), Deleted);
	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("type"), TypeName);
	Result->SetNumberField(TEXT("count"), AssetArr.Num());
	Result->SetArrayField(TEXT("assets"), AssetArr);
	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("from"), SourcePath);
	Result->SetStringField(TEXT("to"), DestPath);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, Writer);
	return Out;
}
