// Handlers_LiveLink.cpp
// Live Link source management for AgenticMCP.
// UE 5.6 target. Mocap, face capture, virtual camera sources.
#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPLiveLink, Log, All);

// ============================================================
// liveLinkGetStatus
// Get Live Link client status and connected sources
// ============================================================
FString FAgenticMCPServer::HandleLiveLinkGetStatus(const FString& Body)
{
    if (!FModuleManager::Get().IsModuleLoaded(TEXT("LiveLink")))
    {
        return MakeErrorJson(TEXT("LiveLink module is not loaded. Enable the Live Link plugin."));
    }

    // Use Python to query Live Link -- the C++ API requires LiveLinkClient which is runtime
    FString PythonCmd = TEXT(
        "import unreal; "
        "ll = unreal.LiveLinkBlueprintLibrary; "
        "subjects = ll.get_live_link_enabled_subject_names(); "
        "result = {'success': True, 'subjects': [str(s) for s in subjects], 'count': len(subjects)}; "
        "print(result)");

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteValue(TEXT("modulesLoaded"), true);

    // Check which Live Link related modules are available
    Writer->WriteArrayStart(TEXT("availableModules"));
    TArray<FString> ModulesToCheck = {
        TEXT("LiveLink"), TEXT("LiveLinkInterface"), TEXT("LiveLinkEditor"),
        TEXT("LiveLinkMovieScene"), TEXT("LiveLinkComponents")
    };
    for (const FString& Mod : ModulesToCheck)
    {
        if (FModuleManager::Get().IsModuleLoaded(*Mod))
        {
            Writer->WriteValue(Mod);
        }
    }
    Writer->WriteArrayEnd();
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// liveLinkListSources
// List all registered Live Link sources
// ============================================================
FString FAgenticMCPServer::HandleLiveLinkListSources(const FString& Body)
{
    FString PythonCmd = TEXT(
        "import unreal; "
        "subjects = unreal.LiveLinkBlueprintLibrary.get_live_link_enabled_subject_names(); "
        "for s in subjects: print(f'SUBJECT:{s}')");

    if (!FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
    }

    FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
    GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
    if (!Python)
        return MakeErrorJson(TEXT("Failed to get PythonScriptPlugin"));
    }


    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteValue(TEXT("message"), TEXT("Live Link sources queried. Check output log for SUBJECT: entries."));
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// liveLinkAddSource
// Add a Live Link source by type
// Params: sourceType (string: MessageBus/FreeD/MVN/OptiTrack/ARKit)
//         connectionString (string, optional - IP:Port for network sources)
// ============================================================
FString FAgenticMCPServer::HandleLiveLinkAddSource(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString SourceType = Json->GetStringField(TEXT("sourceType"));
    FString ConnectionString = Json->HasField(TEXT("connectionString")) ? Json->GetStringField(TEXT("connectionString")) : TEXT("");

    if (SourceType.IsEmpty())
    {
        return MakeErrorJson(TEXT("sourceType is required"));
    }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "preset = unreal.LiveLinkSourcePreset(); "
             "preset.source_type = unreal.LiveLinkSourceType.%s; "
             "unreal.LiveLinkBlueprintLibrary.add_source(preset)"),
        *SourceType);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        {
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("sourceType"), SourceType);
            Writer->WriteValue(TEXT("connectionString"), ConnectionString);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}

// ============================================================
// liveLinkRemoveSource
// Remove a Live Link source by subject name
// Params: subjectName (string)
// ============================================================
FString FAgenticMCPServer::HandleLiveLinkRemoveSource(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString SubjectName = Json->GetStringField(TEXT("subjectName"));
    if (SubjectName.IsEmpty())
    {
        return MakeErrorJson(TEXT("subjectName is required"));
    }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "unreal.LiveLinkBlueprintLibrary.remove_source('%s')"),
        *SubjectName);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        {
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("removed"), SubjectName);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}
