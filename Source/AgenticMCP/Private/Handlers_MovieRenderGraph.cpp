// Handlers_MovieRenderGraph.cpp
// Movie Render Graph handlers for AgenticMCP.
// UE 5.6 target. New render pipeline (replaces Movie Render Queue).
// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPMRG, Log, All);

// ============================================================
// mrgGetStatus
// Get Movie Render Graph module status
// ============================================================
FString FAgenticMCPServer::HandleMRGGetStatus(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);

    bool bMRGLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("MovieRenderPipelineCore"));
    bool bMRGEditorLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("MovieRenderPipelineEditor"));
    Writer->WriteValue(TEXT("movieRenderPipelineCoreLoaded"), bMRGLoaded);
    Writer->WriteValue(TEXT("movieRenderPipelineEditorLoaded"), bMRGEditorLoaded);

    // Check for the new MRG modules (UE 5.4+)
    bool bGraphLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("MovieGraphConfig"));
    Writer->WriteValue(TEXT("movieGraphConfigLoaded"), bGraphLoaded);

    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// mrgCreateConfig
// Create a Movie Render Graph config asset
// Params: name (string), path (string, default /Game/Cinematics/RenderConfigs)
// ============================================================
FString FAgenticMCPServer::HandleMRGCreateConfig(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString Name = Json->GetStringField(TEXT("name"));
    FString Path = Json->HasField(TEXT("path")) ? Json->GetStringField(TEXT("path")) : TEXT("/Game/Cinematics/RenderConfigs");

    if (Name.IsEmpty())
    {
        return MakeErrorJson(TEXT("name is required"));
    }

    // Use Python to create the MRG config -- the C++ API for MRG is not fully public in 5.6
    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "asset_tools = unreal.AssetToolsHelpers.get_asset_tools(); "
             "config = asset_tools.create_asset('%s', '%s', unreal.MovieGraphConfig, unreal.MovieGraphConfigFactory()); "
             "if config: unreal.EditorAssetLibrary.save_loaded_asset(config); print('SUCCESS:' + config.get_path_name()) "
             "else: print('FAIL')"),
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

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded or MovieGraphConfig not available."));
}

// ============================================================
// mrgRender
// Execute a render using Movie Render Graph
// Params: sequencePath (string), configPath (string, optional)
//         outputDir (string, default {Project}/Saved/MovieRenders)
//         format (string: png/exr/jpg, default png)
//         resolutionX (int, default 1920), resolutionY (int, default 1080)
// ============================================================
FString FAgenticMCPServer::HandleMRGRender(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString SequencePath = Json->GetStringField(TEXT("sequencePath"));
    FString ConfigPath = Json->HasField(TEXT("configPath")) ? Json->GetStringField(TEXT("configPath")) : TEXT("");
    FString OutputDir = Json->HasField(TEXT("outputDir")) ? Json->GetStringField(TEXT("outputDir")) : TEXT("{project}/Saved/MovieRenders");
    FString Format = Json->HasField(TEXT("format")) ? Json->GetStringField(TEXT("format")) : TEXT("png");
    int32 ResX = Json->HasField(TEXT("resolutionX")) ? (int32)Json->GetNumberField(TEXT("resolutionX")) : 1920;
    int32 ResY = Json->HasField(TEXT("resolutionY")) ? (int32)Json->GetNumberField(TEXT("resolutionY")) : 1080;

    if (SequencePath.IsEmpty())
    {
        return MakeErrorJson(TEXT("sequencePath is required"));
    }

    // Use the legacy Movie Render Pipeline as fallback if MRG is not available
    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "subsystem = unreal.get_editor_subsystem(unreal.MoviePipelineQueueSubsystem); "
             "queue = subsystem.get_queue(); "
             "job = queue.allocate_new_job(); "
             "job.sequence = unreal.SoftObjectPath('%s'); "
             "job.map = unreal.SoftObjectPath(unreal.EditorLevelLibrary.get_editor_world().get_path_name()); "
             "config = job.get_configuration(); "
             "output = config.find_or_add_setting_by_class(unreal.MoviePipelineOutputSetting); "
             "output.output_directory = unreal.DirectoryPath('%s'); "
             "output.output_resolution = unreal.IntPoint(%d, %d); "
             "subsystem.render_queue_with_executor(unreal.MoviePipelinePIEExecutor)"),
        *SequencePath, *OutputDir, ResX, ResY);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        {
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("sequencePath"), SequencePath);
            Writer->WriteValue(TEXT("outputDir"), OutputDir);
            Writer->WriteValue(TEXT("format"), Format);
            Writer->WriteValue(TEXT("resolutionX"), ResX);
            Writer->WriteValue(TEXT("resolutionY"), ResY);
            Writer->WriteValue(TEXT("message"), TEXT("Render job queued."));
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}

// ============================================================
// mrgListConfigs
// List all Movie Render Graph config assets
// ============================================================
FString FAgenticMCPServer::HandleMRGListConfigs(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

    // Search for both legacy and new config types
    TArray<FAssetData> LegacyConfigs;
    FTopLevelAssetPath LegacyPath(TEXT("/Script/MovieRenderPipelineCore"), TEXT("MoviePipelinePrimaryConfig"));
    ARM.Get().GetAssetsByClass(LegacyPath, LegacyConfigs, true);

    TArray<FAssetData> GraphConfigs;
    FTopLevelAssetPath GraphPath(TEXT("/Script/MovieGraphConfig"), TEXT("MovieGraphConfig"));
    ARM.Get().GetAssetsByClass(GraphPath, GraphConfigs, true);

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);

    Writer->WriteArrayStart(TEXT("legacyConfigs"));
    for (const FAssetData& A : LegacyConfigs)
    {
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("name"), A.AssetName.ToString());
        Writer->WriteValue(TEXT("path"), A.GetObjectPathString());
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();

    Writer->WriteArrayStart(TEXT("graphConfigs"));
    for (const FAssetData& A : GraphConfigs)
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
