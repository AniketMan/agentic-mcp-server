// Handlers_ClothSim.cpp
// Cloth simulation handlers for AgenticMCP.
// UE 5.6 target. Cloth painting, config, and asset management.
// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPCloth, Log, All);

// ============================================================
// clothList
// List all skeletal meshes with cloth data
// ============================================================
FString FAgenticMCPServer::HandleClothList(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    TArray<FAssetData> Assets;
    ARM.Get().GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("SkeletalMesh")), Assets, true);

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteArrayStart(TEXT("meshesWithCloth"));

    for (const FAssetData& A : Assets)
    {
        USkeletalMesh* Mesh = Cast<USkeletalMesh>(A.GetAsset());
        if (Mesh && Mesh->GetMeshClothingAssets().Num() > 0)
        {
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("name"), A.AssetName.ToString());
            Writer->WriteValue(TEXT("path"), A.GetObjectPathString());
            Writer->WriteValue(TEXT("clothAssetCount"), Mesh->GetMeshClothingAssets().Num());
            Writer->WriteObjectEnd();
        }
    }

    Writer->WriteArrayEnd();
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// clothCreateAsset
// Create a cloth asset on a skeletal mesh
// Params: meshPath (string), clothName (string), section (int, default 0)
// ============================================================
FString FAgenticMCPServer::HandleClothCreateAsset(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString MeshPath = Json->GetStringField(TEXT("meshPath"));
    FString ClothName = Json->GetStringField(TEXT("clothName"));
    int32 Section = Json->HasField(TEXT("section")) ? (int32)Json->GetNumberField(TEXT("section")) : 0;

    if (MeshPath.IsEmpty() || ClothName.IsEmpty())
    {
        return MakeErrorJson(TEXT("meshPath and clothName are required"));
    }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "mesh = unreal.load_asset('%s'); "
             "cloth = unreal.ClothingAssetNv(); "
             "cloth.set_editor_property('name', '%s'); "
             "mesh.add_clothing_asset(cloth, %d); "
             "unreal.EditorAssetLibrary.save_loaded_asset(mesh)"),
        *MeshPath, *ClothName, Section);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        {
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("meshPath"), MeshPath);
            Writer->WriteValue(TEXT("clothName"), ClothName);
            Writer->WriteValue(TEXT("section"), Section);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}

// ============================================================
// clothSetConfig
// Set cloth simulation config properties
// Params: meshPath (string), clothIndex (int, default 0)
//         mass (float), friction (float), damping (float), stiffness (float)
//         gravityScale (float), windScale (float)
// ============================================================
FString FAgenticMCPServer::HandleClothSetConfig(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString MeshPath = Json->GetStringField(TEXT("meshPath"));
    int32 ClothIndex = Json->HasField(TEXT("clothIndex")) ? (int32)Json->GetNumberField(TEXT("clothIndex")) : 0;

    if (MeshPath.IsEmpty())
    {
        return MakeErrorJson(TEXT("meshPath is required"));
    }

    // Build property set commands
    TArray<FString> PropCmds;
    if (Json->HasField(TEXT("mass"))) PropCmds.Add(FString::Printf(TEXT("config.set_editor_property('mass_scale', %f)"), Json->GetNumberField(TEXT("mass"))));
    if (Json->HasField(TEXT("friction"))) PropCmds.Add(FString::Printf(TEXT("config.set_editor_property('friction', %f)"), Json->GetNumberField(TEXT("friction"))));
    if (Json->HasField(TEXT("damping"))) PropCmds.Add(FString::Printf(TEXT("config.set_editor_property('damping', %f)"), Json->GetNumberField(TEXT("damping"))));
    if (Json->HasField(TEXT("stiffness"))) PropCmds.Add(FString::Printf(TEXT("config.set_editor_property('stiffness', %f)"), Json->GetNumberField(TEXT("stiffness"))));
    if (Json->HasField(TEXT("gravityScale"))) PropCmds.Add(FString::Printf(TEXT("config.set_editor_property('gravity_scale', %f)"), Json->GetNumberField(TEXT("gravityScale"))));

    FString AllProps;
    for (const FString& Cmd : PropCmds) { AllProps += Cmd + TEXT("; "); }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "mesh = unreal.load_asset('%s'); "
             "cloth_assets = mesh.get_mesh_clothing_assets(); "
             "cloth = cloth_assets[%d]; "
             "config = cloth.get_cloth_config(); "
             "%s"
             "unreal.EditorAssetLibrary.save_loaded_asset(mesh)"),
        *MeshPath, ClothIndex, *AllProps);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        {
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("meshPath"), MeshPath);
            Writer->WriteValue(TEXT("clothIndex"), ClothIndex);
            Writer->WriteValue(TEXT("propertiesSet"), PropCmds.Num());
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}
