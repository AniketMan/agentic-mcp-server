// Handlers_SourceControl.cpp
// Source Control handlers for AgenticMCP.
// UE 5.6 target. Submit, revert, status, checkout, history.
// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"
#include "ISourceControlRevision.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPSourceControl, Log, All);

// ============================================================
// scGetStatus
// Get source control status for files
// Params: paths (array of strings)
// ============================================================
FString FAgenticMCPServer::HandleSCGetStatus(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    ISourceControlModule& SCModule = ISourceControlModule::Get();
    ISourceControlProvider& Provider = SCModule.GetProvider();

    if (!Provider.IsEnabled())
    {
        return MakeErrorJson(TEXT("Source control is not enabled"));
    }

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteValue(TEXT("provider"), Provider.GetName().ToString());
    Writer->WriteValue(TEXT("isAvailable"), Provider.IsAvailable());

    const TArray<TSharedPtr<FJsonValue>>* Paths;
    if (Json->TryGetArrayField(TEXT("paths"), Paths))
    {
        TArray<FString> FilePaths;
        for (const auto& P : *Paths)
        {
            FilePaths.Add(P->AsString());
        }

        Provider.Execute(ISourceControlOperation::Create<FUpdateStatus>(), FilePaths);

        Writer->WriteArrayStart(TEXT("files"));
        for (const FString& FilePath : FilePaths)
        {
            FSourceControlStatePtr State = Provider.GetState(FilePath, EStateCacheUsage::Use);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("path"), FilePath);
            if (State.IsValid())
            {
                Writer->WriteValue(TEXT("checkedOut"), State->IsCheckedOut());
                Writer->WriteValue(TEXT("current"), State->IsCurrent());
                Writer->WriteValue(TEXT("added"), State->IsAdded());
                Writer->WriteValue(TEXT("deleted"), State->IsDeleted());
                Writer->WriteValue(TEXT("modified"), State->IsModified());
                Writer->WriteValue(TEXT("conflicted"), State->IsConflicted());
                Writer->WriteValue(TEXT("canCheckout"), State->CanCheckout());
                Writer->WriteValue(TEXT("canEdit"), State->CanEdit());
            }
            else
            {
                Writer->WriteValue(TEXT("status"), TEXT("unknown"));
            }
            Writer->WriteObjectEnd();
        }
        Writer->WriteArrayEnd();
    }

    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// scCheckout
// Check out files from source control
// Params: paths (array of strings)
// ============================================================
FString FAgenticMCPServer::HandleSCCheckout(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
    if (!Provider.IsEnabled())
    {
        return MakeErrorJson(TEXT("Source control is not enabled"));
    }

    const TArray<TSharedPtr<FJsonValue>>* Paths;
    if (!Json->TryGetArrayField(TEXT("paths"), Paths) || Paths->Num() == 0)
    {
        return MakeErrorJson(TEXT("paths array is required"));
    }

    TArray<FString> FilePaths;
    for (const auto& P : *Paths) FilePaths.Add(P->AsString());

    ECommandResult::Type CmdResult = Provider.Execute(
        ISourceControlOperation::Create<FCheckOut>(), FilePaths);

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), CmdResult == ECommandResult::Succeeded);
    Writer->WriteValue(TEXT("filesProcessed"), FilePaths.Num());
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// scSubmit
// Submit files to source control
// Params: paths (array of strings), description (string)
// ============================================================
FString FAgenticMCPServer::HandleSCSubmit(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
    if (!Provider.IsEnabled())
    {
        return MakeErrorJson(TEXT("Source control is not enabled"));
    }

    FString Description = Json->GetStringField(TEXT("description"));
    if (Description.IsEmpty())
    {
        return MakeErrorJson(TEXT("description is required"));
    }

    const TArray<TSharedPtr<FJsonValue>>* Paths;
    if (!Json->TryGetArrayField(TEXT("paths"), Paths) || Paths->Num() == 0)
    {
        return MakeErrorJson(TEXT("paths array is required"));
    }

    TArray<FString> FilePaths;
    for (const auto& P : *Paths) FilePaths.Add(P->AsString());

    TSharedRef<FCheckIn, ESPMode::ThreadSafe> CheckInOp = ISourceControlOperation::Create<FCheckIn>();
    CheckInOp->SetDescription(FText::FromString(Description));

    ECommandResult::Type CmdResult = Provider.Execute(CheckInOp, FilePaths);

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), CmdResult == ECommandResult::Succeeded);
    Writer->WriteValue(TEXT("filesSubmitted"), FilePaths.Num());
    Writer->WriteValue(TEXT("description"), Description);
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// scRevert
// Revert files in source control
// Params: paths (array of strings)
// ============================================================
FString FAgenticMCPServer::HandleSCRevert(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
    if (!Provider.IsEnabled())
    {
        return MakeErrorJson(TEXT("Source control is not enabled"));
    }

    const TArray<TSharedPtr<FJsonValue>>* Paths;
    if (!Json->TryGetArrayField(TEXT("paths"), Paths) || Paths->Num() == 0)
    {
        return MakeErrorJson(TEXT("paths array is required"));
    }

    TArray<FString> FilePaths;
    for (const auto& P : *Paths) FilePaths.Add(P->AsString());

    ECommandResult::Type CmdResult = Provider.Execute(
        ISourceControlOperation::Create<FRevert>(), FilePaths);

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), CmdResult == ECommandResult::Succeeded);
    Writer->WriteValue(TEXT("filesReverted"), FilePaths.Num());
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// scHistory
// Get source control history for a file
// Params: path (string), maxRevisions (int, default 10)
// ============================================================
FString FAgenticMCPServer::HandleSCHistory(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString FilePath = Json->GetStringField(TEXT("path"));
    if (FilePath.IsEmpty())
    {
        return MakeErrorJson(TEXT("path is required"));
    }

    ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
    if (!Provider.IsEnabled())
    {
        return MakeErrorJson(TEXT("Source control is not enabled"));
    }

    TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> UpdateOp = ISourceControlOperation::Create<FUpdateStatus>();
    UpdateOp->SetUpdateHistory(true);

    TArray<FString> Files;
    Files.Add(FilePath);
    Provider.Execute(UpdateOp, Files);

    FSourceControlStatePtr State = Provider.GetState(FilePath, EStateCacheUsage::Use);

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), State.IsValid());
    Writer->WriteValue(TEXT("path"), FilePath);

    int32 MaxRevisions = Json->HasField(TEXT("maxRevisions")) ? (int32)Json->GetNumberField(TEXT("maxRevisions")) : 10;

    Writer->WriteArrayStart(TEXT("history"));
    if (State.IsValid())
    {
        int32 HistoryCount = FMath::Min(State->GetHistorySize(), MaxRevisions);
        for (int32 i = 0; i < HistoryCount; i++)
        {
            TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Rev = State->GetHistoryItem(i);
            if (Rev.IsValid())
            {
                Writer->WriteObjectStart();
                Writer->WriteValue(TEXT("revision"), Rev->GetRevision());
                Writer->WriteValue(TEXT("description"), Rev->GetDescription());
                Writer->WriteValue(TEXT("user"), Rev->GetUserName());
                Writer->WriteValue(TEXT("date"), Rev->GetDate().ToString());
                Writer->WriteObjectEnd();
            }
        }
    }
    Writer->WriteArrayEnd();

    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}
