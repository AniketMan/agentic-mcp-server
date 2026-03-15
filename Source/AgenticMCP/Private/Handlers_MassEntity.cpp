// Handlers_MassEntity.cpp
// Mass Entity Framework handlers for AgenticMCP.
// UE 5.6 target. Entity configs, processors, traits.
#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPMass, Log, All);

// ============================================================
// massList
// List all Mass Entity config assets
// ============================================================
FString FAgenticMCPServer::HandleMassList(const FString& Body)
{
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    TArray<FAssetData> Assets;
    FTopLevelAssetPath ClassPath(TEXT("/Script/MassEntity"), TEXT("MassEntityConfigAsset"));
    ARM.Get().GetAssetsByClass(ClassPath, Assets, true);

    // Also check for MassSpawner data assets
    TArray<FAssetData> Spawners;
    FTopLevelAssetPath SpawnerPath(TEXT("/Script/MassSpawner"), TEXT("MassSpawnerDataAsset"));
    ARM.Get().GetAssetsByClass(SpawnerPath, Spawners, true);

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);

    Writer->WriteArrayStart(TEXT("entityConfigs"));
    for (const FAssetData& A : Assets)
    {
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("name"), A.AssetName.ToString());
        Writer->WriteValue(TEXT("path"), A.GetObjectPathString());
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();

    Writer->WriteArrayStart(TEXT("spawners"));
    for (const FAssetData& A : Spawners)
    {
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("name"), A.AssetName.ToString());
        Writer->WriteValue(TEXT("path"), A.GetObjectPathString());
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();

    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// massCreateConfig
// Create a Mass Entity Config asset
// Params: name (string), path (string, default /Game/Mass)
// ============================================================
FString FAgenticMCPServer::HandleMassCreateConfig(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString Name = Json->GetStringField(TEXT("name"));
    FString Path = Json->HasField(TEXT("path")) ? Json->GetStringField(TEXT("path")) : TEXT("/Game/Mass");

    if (Name.IsEmpty())
    {
        return MakeErrorJson(TEXT("name is required"));
    }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "asset_tools = unreal.AssetToolsHelpers.get_asset_tools(); "
             "config = asset_tools.create_asset('%s', '%s', unreal.MassEntityConfigAsset, unreal.MassEntityConfigAssetFactory()); "
             "if config: unreal.EditorAssetLibrary.save_loaded_asset(config); print('SUCCESS')"),
        *Name, *Path);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        IPythonScriptPlugin* Python = FModuleManager::Get().GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
        if (Python && Python->ExecPythonCommand(*PythonCmd))
        {
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("name"), Name);
            Writer->WriteValue(TEXT("path"), FString::Printf(TEXT("%s/%s"), *Path, *Name));
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded or MassEntity not available."));
}

// ============================================================
// massAddTrait
// Add a trait to a Mass Entity Config
// Params: configPath (string), traitClass (string)
// ============================================================
FString FAgenticMCPServer::HandleMassAddTrait(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString ConfigPath = Json->GetStringField(TEXT("configPath"));
    FString TraitClass = Json->GetStringField(TEXT("traitClass"));

    if (ConfigPath.IsEmpty() || TraitClass.IsEmpty())
    {
        return MakeErrorJson(TEXT("configPath and traitClass are required"));
    }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "config = unreal.load_asset('%s'); "
             "trait_class = unreal.find_class('%s'); "
             "if trait_class: "
             "  trait = unreal.new_object(trait_class); "
             "  traits = list(config.get_editor_property('traits')); "
             "  traits.append(trait); "
             "  config.set_editor_property('traits', traits); "
             "  unreal.EditorAssetLibrary.save_loaded_asset(config); print('SUCCESS')"),
        *ConfigPath, *TraitClass);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        IPythonScriptPlugin* Python = FModuleManager::Get().GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
        if (Python && Python->ExecPythonCommand(*PythonCmd))
        {
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("configPath"), ConfigPath);
            Writer->WriteValue(TEXT("traitClass"), TraitClass);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}
