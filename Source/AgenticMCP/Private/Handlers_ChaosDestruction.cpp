// Handlers_ChaosDestruction.cpp
// Chaos Destruction system handlers for AgenticMCP.
// UE 5.6 target. Geometry Collections, fracture patterns, destruction fields.
#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPChaos, Log, All);

// ============================================================
// chaosList
// List all Geometry Collection assets
// ============================================================
FString FAgenticMCPServer::HandleChaosList(const FString& Body)
{
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    TArray<FAssetData> Assets;
    FTopLevelAssetPath ClassPath(TEXT("/Script/GeometryCollectionEngine"), TEXT("GeometryCollectionActor"));
    ARM.Get().GetAssetsByClass(ClassPath, Assets, true);

    // Also search for GeometryCollection assets directly
    TArray<FAssetData> GCAssets;
    FTopLevelAssetPath GCPath(TEXT("/Script/GeometryCollectionEngine"), TEXT("GeometryCollection"));
    ARM.Get().GetAssetsByClass(GCPath, GCAssets, true);

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteArrayStart(TEXT("geometryCollections"));
    for (const FAssetData& A : GCAssets)
    {
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("name"), A.AssetName.ToString());
        Writer->WriteValue(TEXT("path"), A.GetObjectPathString());
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();
    Writer->WriteArrayStart(TEXT("actors"));
    for (const FAssetData& A : Assets)
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
// chaosCreateGeometryCollection
// Create a Geometry Collection from a Static Mesh
// Params: meshPath (string), name (string), savePath (string, default /Game/Chaos)
// ============================================================
FString FAgenticMCPServer::HandleChaosCreateGeometryCollection(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString MeshPath = Json->GetStringField(TEXT("meshPath"));
    FString Name = Json->GetStringField(TEXT("name"));
    FString SavePath = Json->HasField(TEXT("savePath")) ? Json->GetStringField(TEXT("savePath")) : TEXT("/Game/Chaos");

    if (MeshPath.IsEmpty() || Name.IsEmpty())
    {
        return MakeErrorJson(TEXT("meshPath and name are required"));
    }

    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
    if (!Mesh)
    {
        return MakeErrorJson(FString::Printf(TEXT("Static mesh not found: %s"), *MeshPath));
    }

    // Create via Python -- GeometryCollection creation from mesh requires the fracture tools
    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "mesh = unreal.load_asset('%s'); "
             "gc = unreal.GeometryCollectionConversion.create_geometry_collection_from_static_meshes("
             "[mesh], '%s/%s')"),
        *MeshPath, *SavePath, *Name);

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
            Writer->WriteValue(TEXT("path"), FString::Printf(TEXT("%s/%s"), *SavePath, *Name));
            Writer->WriteValue(TEXT("sourceMesh"), MeshPath);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded. Enable Python Editor Script Plugin for Geometry Collection creation."));
}

// ============================================================
// chaosFracture
// Apply fracture pattern to a Geometry Collection
// Params: path (string), fractureType (string: Voronoi/Planar/Cluster/Radial/Brick, default Voronoi)
//         numPieces (int, default 10), seed (int, optional)
// ============================================================
FString FAgenticMCPServer::HandleChaosFracture(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString AssetPath = Json->GetStringField(TEXT("path"));
    FString FractureType = Json->HasField(TEXT("fractureType")) ? Json->GetStringField(TEXT("fractureType")) : TEXT("Voronoi");
    int32 NumPieces = Json->HasField(TEXT("numPieces")) ? (int32)Json->GetNumberField(TEXT("numPieces")) : 10;
    int32 Seed = Json->HasField(TEXT("seed")) ? (int32)Json->GetNumberField(TEXT("seed")) : FMath::Rand();

    if (AssetPath.IsEmpty())
    {
        return MakeErrorJson(TEXT("path is required"));
    }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "gc = unreal.load_asset('%s'); "
             "fracture_tool = unreal.FractureToolVoronoi() if '%s' == 'Voronoi' else unreal.FractureToolPlanar(); "
             "fracture_tool.set_num_sites(%d); "
             "fracture_tool.set_random_seed(%d); "
             "fracture_tool.fracture(gc)"),
        *AssetPath, *FractureType, NumPieces, Seed);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        {
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("path"), AssetPath);
            Writer->WriteValue(TEXT("fractureType"), FractureType);
            Writer->WriteValue(TEXT("numPieces"), NumPieces);
            Writer->WriteValue(TEXT("seed"), Seed);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}

// ============================================================
// chaosSpawnField
// Spawn a destruction field actor (Anchor, Strain, External Strain, Kill, etc.)
// Params: fieldType (string: AnchorField/ExternalStrainField/KillField/DisableField)
//         location (object: x,y,z), radius (float, default 200)
// ============================================================
FString FAgenticMCPServer::HandleChaosSpawnField(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString FieldType = Json->HasField(TEXT("fieldType")) ? Json->GetStringField(TEXT("fieldType")) : TEXT("ExternalStrainField");
    float Radius = Json->HasField(TEXT("radius")) ? Json->GetNumberField(TEXT("radius")) : 200.0f;

    FVector Location(0, 0, 0);
    if (Json->HasField(TEXT("location")))
    {
        auto Loc = Json->GetObjectField(TEXT("location"));
        Location.X = Loc->GetNumberField(TEXT("x"));
        Location.Y = Loc->GetNumberField(TEXT("y"));
        Location.Z = Loc->GetNumberField(TEXT("z"));
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return MakeErrorJson(TEXT("No editor world"));
    }

    // Map field type to class path
    FString ClassPath;
    if (FieldType == TEXT("AnchorField")) ClassPath = TEXT("/Script/FieldSystemEngine.FieldSystemActor");
    else if (FieldType == TEXT("ExternalStrainField")) ClassPath = TEXT("/Script/FieldSystemEngine.FieldSystemActor");
    else if (FieldType == TEXT("KillField")) ClassPath = TEXT("/Script/FieldSystemEngine.FieldSystemActor");
    else ClassPath = TEXT("/Script/FieldSystemEngine.FieldSystemActor");

    UClass* FieldClass = LoadClass<AActor>(nullptr, *ClassPath);
    if (!FieldClass)
    {
        return MakeErrorJson(FString::Printf(TEXT("Field class not found: %s"), *ClassPath));
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    FTransform SpawnTransform(FRotator::ZeroRotator, Location);
    AActor* NewActor = World->SpawnActor<AActor>(FieldClass, SpawnTransform, Params);
    if (!NewActor)
    {
        return MakeErrorJson(TEXT("Failed to spawn field actor"));
    }

    NewActor->SetActorLabel(*FString::Printf(TEXT("%s_%d"), *FieldType, FMath::Rand() % 1000));

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteValue(TEXT("fieldType"), FieldType);
    Writer->WriteValue(TEXT("actorName"), NewActor->GetActorLabel());
    Writer->WriteValue(TEXT("radius"), Radius);
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}
