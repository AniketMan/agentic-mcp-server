// Handlers_VariantManager.cpp
// Variant Manager handlers for AgenticMCP.
// UE 5.6 target. Variant sets, variants, property captures.
#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPVariant, Log, All);

// ============================================================
// variantList
// List all Variant Manager assets
// ============================================================
FString FAgenticMCPServer::HandleVariantList(const FString& Body)
{
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    TArray<FAssetData> Assets;
    FTopLevelAssetPath ClassPath(TEXT("/Script/VariantManagerContent"), TEXT("LevelVariantSets"));
    ARM.Get().GetAssetsByClass(ClassPath, Assets, true);

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteArrayStart(TEXT("variantSets"));
    for (const FAssetData& A : Assets)
    {
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("name"), A.AssetName.ToString());
        Writer->WriteValue(TEXT("path"), A.GetObjectPathString());
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();
    Writer->WriteValue(TEXT("count"), Assets.Num());
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// variantCreate
// Create a Level Variant Sets asset
// Params: name (string), path (string, default /Game/Variants)
// ============================================================
FString FAgenticMCPServer::HandleVariantCreate(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString Name = Json->GetStringField(TEXT("name"));
    FString Path = Json->HasField(TEXT("path")) ? Json->GetStringField(TEXT("path")) : TEXT("/Game/Variants");

    if (Name.IsEmpty())
    {
        return MakeErrorJson(TEXT("name is required"));
    }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "asset_tools = unreal.AssetToolsHelpers.get_asset_tools(); "
             "lvs = asset_tools.create_asset('%s', '%s', unreal.LevelVariantSets, unreal.LevelVariantSetsFactory()); "
             "if lvs: unreal.EditorAssetLibrary.save_loaded_asset(lvs); print('SUCCESS')"),
        *Name, *Path);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
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

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}

// ============================================================
// variantAddSet
// Add a variant set to a Level Variant Sets asset
// Params: assetPath (string), setName (string)
// ============================================================
FString FAgenticMCPServer::HandleVariantAddSet(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString AssetPath = Json->GetStringField(TEXT("assetPath"));
    FString SetName = Json->GetStringField(TEXT("setName"));

    if (AssetPath.IsEmpty() || SetName.IsEmpty())
    {
        return MakeErrorJson(TEXT("assetPath and setName are required"));
    }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "lvs = unreal.load_asset('%s'); "
             "vs = unreal.VariantSet(); "
             "vs.set_display_text('%s'); "
             "lvs.add_variant_set(vs); "
             "unreal.EditorAssetLibrary.save_loaded_asset(lvs); print('SUCCESS')"),
        *AssetPath, *SetName);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        {
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("assetPath"), AssetPath);
            Writer->WriteValue(TEXT("setName"), SetName);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}

// ============================================================
// variantAddVariant
// Add a variant to a variant set
// Params: assetPath (string), setIndex (int), variantName (string)
//         actorName (string, optional), property (string, optional), value (string, optional)
// ============================================================
FString FAgenticMCPServer::HandleVariantAddVariant(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString AssetPath = Json->GetStringField(TEXT("assetPath"));
    int32 SetIndex = Json->HasField(TEXT("setIndex")) ? (int32)Json->GetNumberField(TEXT("setIndex")) : 0;
    FString VariantName = Json->GetStringField(TEXT("variantName"));

    if (AssetPath.IsEmpty() || VariantName.IsEmpty())
    {
        return MakeErrorJson(TEXT("assetPath and variantName are required"));
    }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "lvs = unreal.load_asset('%s'); "
             "vs = lvs.get_variant_sets()[%d]; "
             "v = unreal.Variant(); "
             "v.set_display_text('%s'); "
             "vs.add_variant(v); "
             "unreal.EditorAssetLibrary.save_loaded_asset(lvs); print('SUCCESS')"),
        *AssetPath, SetIndex, *VariantName);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        {
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("assetPath"), AssetPath);
            Writer->WriteValue(TEXT("setIndex"), SetIndex);
            Writer->WriteValue(TEXT("variantName"), VariantName);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}
