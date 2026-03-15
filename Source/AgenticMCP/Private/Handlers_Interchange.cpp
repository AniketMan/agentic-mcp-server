// Handlers_Interchange.cpp
// Interchange pipeline handlers for AgenticMCP.
// UE 5.6 target. FBX/USD/glTF import/export pipeline configuration.
#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Editor.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPInterchange, Log, All);

// ============================================================
// interchangeGetStatus
// Get Interchange module status and supported formats
// ============================================================
FString FAgenticMCPServer::HandleInterchangeGetStatus(const FString& Body)
{
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);

    bool bCoreLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("InterchangeCore"));
    bool bEngineLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("InterchangeEngine"));
    bool bEditorLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("InterchangeEditor"));
    bool bFbxLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("InterchangeFbxParser"));

    Writer->WriteValue(TEXT("interchangeCoreLoaded"), bCoreLoaded);
    Writer->WriteValue(TEXT("interchangeEngineLoaded"), bEngineLoaded);
    Writer->WriteValue(TEXT("interchangeEditorLoaded"), bEditorLoaded);
    Writer->WriteValue(TEXT("interchangeFbxLoaded"), bFbxLoaded);

    Writer->WriteArrayStart(TEXT("supportedFormats"));
    Writer->WriteValue(TEXT("FBX"));
    Writer->WriteValue(TEXT("glTF"));
    Writer->WriteValue(TEXT("USD"));
    Writer->WriteValue(TEXT("OBJ"));
    Writer->WriteValue(TEXT("ABC (Alembic)"));
    Writer->WriteArrayEnd();

    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// interchangeImport
// Import an asset using the Interchange pipeline
// Params: filePath (string), destinationPath (string, default /Game/Imports)
//         pipelineOverride (string, optional - pipeline stack name)
// ============================================================
FString FAgenticMCPServer::HandleInterchangeImport(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString FilePath = Json->GetStringField(TEXT("filePath"));
    FString DestPath = Json->HasField(TEXT("destinationPath")) ? Json->GetStringField(TEXT("destinationPath")) : TEXT("/Game/Imports");

    if (FilePath.IsEmpty())
    {
        return MakeErrorJson(TEXT("filePath is required"));
    }

    // Use AssetTools import which routes through Interchange in UE 5.6
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    TArray<FString> Files;
    Files.Add(FilePath);
    TArray<UObject*> ImportedAssets = AssetTools.ImportAssets(Files, DestPath);

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), ImportedAssets.Num() > 0);
    Writer->WriteValue(TEXT("importedCount"), ImportedAssets.Num());
    Writer->WriteArrayStart(TEXT("assets"));
    for (UObject* Asset : ImportedAssets)
    {
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("name"), Asset->GetName());
        Writer->WriteValue(TEXT("path"), Asset->GetPathName());
        Writer->WriteValue(TEXT("class"), Asset->GetClass()->GetName());
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// interchangeExport
// Export an asset using the Interchange pipeline
// Params: assetPath (string), outputPath (string), format (string: fbx/gltf/obj)
// ============================================================
FString FAgenticMCPServer::HandleInterchangeExport(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString AssetPath = Json->GetStringField(TEXT("assetPath"));
    FString OutputPath = Json->GetStringField(TEXT("outputPath"));
    FString Format = Json->HasField(TEXT("format")) ? Json->GetStringField(TEXT("format")) : TEXT("fbx");

    if (AssetPath.IsEmpty() || OutputPath.IsEmpty())
    {
        return MakeErrorJson(TEXT("assetPath and outputPath are required"));
    }

    UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
    if (!Asset)
    {
        return MakeErrorJson(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
    }

    // Use Python for export -- Interchange export API is Python-friendly
    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "asset = unreal.load_asset('%s'); "
             "task = unreal.AssetExportTask(); "
             "task.object = asset; "
             "task.filename = '%s'; "
             "task.automated = True; "
             "task.replace_identical = True; "
             "result = unreal.Exporter.run_asset_export_task(task); "
             "print('SUCCESS' if result else 'FAIL')"),
        *AssetPath, *OutputPath);

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
            Writer->WriteValue(TEXT("outputPath"), OutputPath);
            Writer->WriteValue(TEXT("format"), Format);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}
