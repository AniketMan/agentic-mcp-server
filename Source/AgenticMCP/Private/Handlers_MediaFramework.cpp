// Handlers_MediaFramework.cpp
// Media Framework handlers for AgenticMCP.
// UE 5.6 target. Media Player, Media Source, Media Texture.
// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "MediaSource.h"
#include "FileMediaSource.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPMedia, Log, All);

// ============================================================
// mediaList
// List all media assets (players, sources, textures)
// ============================================================
FString FAgenticMCPServer::HandleMediaList(const FString& Body)
{
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

    auto ListByClass = [&](const TCHAR* Module, const TCHAR* ClassName) -> TArray<FAssetData>
    {
        TArray<FAssetData> Assets;
        FTopLevelAssetPath ClassPath(Module, ClassName);
        ARM.Get().GetAssetsByClass(ClassPath, Assets, true);
        return Assets;
    };

    TArray<FAssetData> Players = ListByClass(TEXT("/Script/MediaAssets"), TEXT("MediaPlayer"));
    TArray<FAssetData> Sources = ListByClass(TEXT("/Script/MediaAssets"), TEXT("FileMediaSource"));
    TArray<FAssetData> Textures = ListByClass(TEXT("/Script/MediaAssets"), TEXT("MediaTexture"));

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);

    auto WriteArray = [&](const TCHAR* Name, const TArray<FAssetData>& Arr)
    {
        Writer->WriteArrayStart(Name);
        for (const FAssetData& A : Arr)
        {
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("name"), A.AssetName.ToString());
            Writer->WriteValue(TEXT("path"), A.GetObjectPathString());
            Writer->WriteObjectEnd();
        }
        Writer->WriteArrayEnd();
    };

    WriteArray(TEXT("players"), Players);
    WriteArray(TEXT("sources"), Sources);
    WriteArray(TEXT("textures"), Textures);
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// mediaCreatePlayer
// Create a Media Player asset with optional auto-created Media Texture
// Params: name (string), path (string, default /Game/Media), createTexture (bool, default true)
// ============================================================
FString FAgenticMCPServer::HandleMediaCreatePlayer(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString Name = Json->GetStringField(TEXT("name"));
    FString Path = Json->HasField(TEXT("path")) ? Json->GetStringField(TEXT("path")) : TEXT("/Game/Media");
    bool bCreateTexture = Json->HasField(TEXT("createTexture")) ? Json->GetBoolField(TEXT("createTexture")) : true;

    if (Name.IsEmpty())
    {
        return MakeErrorJson(TEXT("name is required"));
    }

    FString PackagePath = FString::Printf(TEXT("%s/%s"), *Path, *Name);
    UPackage* Package = CreatePackage(*PackagePath);
    if (!Package)
    {
        return MakeErrorJson(TEXT("Failed to create package"));
    }

    UMediaPlayer* Player = NewObject<UMediaPlayer>(Package, *Name, RF_Public | RF_Standalone);
    if (!Player)
    {
        return MakeErrorJson(TEXT("Failed to create MediaPlayer"));
    }
    Player->MarkPackageDirty();

    FString TexturePath;
    if (bCreateTexture)
    {
        FString TexName = Name + TEXT("_Tex");
        FString TexPackagePath = FString::Printf(TEXT("%s/%s"), *Path, *TexName);
        UPackage* TexPackage = CreatePackage(*TexPackagePath);
        UMediaTexture* Tex = NewObject<UMediaTexture>(TexPackage, *TexName, RF_Public | RF_Standalone);
        if (Tex)
        {
            Tex->SetMediaPlayer(Player);
            Tex->MarkPackageDirty();
            TexturePath = Tex->GetPathName();
        }
    }

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteValue(TEXT("name"), Name);
    Writer->WriteValue(TEXT("playerPath"), Player->GetPathName());
    if (!TexturePath.IsEmpty())
    {
        Writer->WriteValue(TEXT("texturePath"), TexturePath);
    }
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// mediaCreateSource
// Create a File Media Source asset
// Params: name (string), filePath (string - path to media file), path (string, default /Game/Media)
// ============================================================
FString FAgenticMCPServer::HandleMediaCreateSource(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString Name = Json->GetStringField(TEXT("name"));
    FString FilePath = Json->GetStringField(TEXT("filePath"));
    FString SavePath = Json->HasField(TEXT("path")) ? Json->GetStringField(TEXT("path")) : TEXT("/Game/Media");

    if (Name.IsEmpty() || FilePath.IsEmpty())
    {
        return MakeErrorJson(TEXT("name and filePath are required"));
    }

    FString PackagePath = FString::Printf(TEXT("%s/%s"), *SavePath, *Name);
    UPackage* Package = CreatePackage(*PackagePath);
    UFileMediaSource* Source = NewObject<UFileMediaSource>(Package, *Name, RF_Public | RF_Standalone);
    if (!Source)
    {
        return MakeErrorJson(TEXT("Failed to create FileMediaSource"));
    }

    Source->SetFilePath(FilePath);
    Source->MarkPackageDirty();

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteValue(TEXT("name"), Name);
    Writer->WriteValue(TEXT("path"), Source->GetPathName());
    Writer->WriteValue(TEXT("filePath"), FilePath);
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// mediaSetSource
// Assign a media source to a media player
// Params: playerPath (string), sourcePath (string)
// ============================================================
FString FAgenticMCPServer::HandleMediaSetSource(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString PlayerPath = Json->GetStringField(TEXT("playerPath"));
    FString SourcePath = Json->GetStringField(TEXT("sourcePath"));

    if (PlayerPath.IsEmpty() || SourcePath.IsEmpty())
    {
        return MakeErrorJson(TEXT("playerPath and sourcePath are required"));
    }

    UMediaPlayer* Player = LoadObject<UMediaPlayer>(nullptr, *PlayerPath);
    if (!Player)
    {
        return MakeErrorJson(FString::Printf(TEXT("MediaPlayer not found: %s"), *PlayerPath));
    }

    UMediaSource* Source = LoadObject<UMediaSource>(nullptr, *SourcePath);
    if (!Source)
    {
        return MakeErrorJson(FString::Printf(TEXT("MediaSource not found: %s"), *SourcePath));
    }

    bool bOpened = Player->OpenSource(Source);

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), bOpened);
    Writer->WriteValue(TEXT("playerPath"), PlayerPath);
    Writer->WriteValue(TEXT("sourcePath"), SourcePath);
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}
