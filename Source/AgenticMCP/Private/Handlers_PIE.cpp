// Handlers_PIE.cpp
// Play-In-Editor (PIE) control and Console command endpoints for AgenticMCP

#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "LevelEditor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "HAL/IConsoleManager.h"

FString FAgenticMCPServer::HandleStartPIE(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		return MakeErrorJson(TEXT("GEditor not available"));
	}

	if (GEditor->PlayWorld)
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("message"), TEXT("PIE session already running"));
		Result->SetBoolField(TEXT("alreadyRunning"), true);
		return JsonToString(Result);
	}

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	FJsonSerializer::Deserialize(Reader, BodyJson);

	FString Mode = TEXT("SelectedViewport");
	if (BodyJson.IsValid() && BodyJson->HasField(TEXT("mode")))
	{
		Mode = BodyJson->GetStringField(TEXT("mode"));
	}

	FRequestPlaySessionParams SessionParams;

	if (Mode.Equals(TEXT("NewEditorWindow"), ESearchCase::IgnoreCase))
	{
		SessionParams.DestinationSlateViewport = nullptr;
	}
	else if (Mode.Equals(TEXT("VR"), ESearchCase::IgnoreCase))
	{
		SessionParams.SessionPreviewTypeOverride = EPlaySessionPreviewType::VRPreview;
	}
	else if (Mode.Equals(TEXT("MobilePreview"), ESearchCase::IgnoreCase))
	{
		SessionParams.SessionPreviewTypeOverride = EPlaySessionPreviewType::MobilePreview;
	}

	GEditor->RequestPlaySession(SessionParams);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("PIE session starting"));
	Result->SetStringField(TEXT("mode"), Mode);
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleStopPIE(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		return MakeErrorJson(TEXT("GEditor not available"));
	}

	if (!GEditor->PlayWorld)
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("message"), TEXT("No PIE session running"));
		Result->SetBoolField(TEXT("wasRunning"), false);
		return JsonToString(Result);
	}

	GEditor->RequestEndPlayMap();

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("PIE session stopping"));
	Result->SetBoolField(TEXT("wasRunning"), true);
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandlePausePIE(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		return MakeErrorJson(TEXT("GEditor not available"));
	}

	if (!GEditor->PlayWorld)
	{
		return MakeErrorJson(TEXT("No PIE session running"));
	}

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	FJsonSerializer::Deserialize(Reader, BodyJson);

	bool bPause = true;
	if (BodyJson.IsValid() && BodyJson->HasField(TEXT("pause")))
	{
		bPause = BodyJson->GetBoolField(TEXT("pause"));
	}

	GEditor->PlayWorld->bDebugPauseExecution = bPause;

	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("isPaused"), bPause);
	Result->SetStringField(TEXT("message"), bPause ? TEXT("PIE paused") : TEXT("PIE resumed"));
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleStepPIE(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		return MakeErrorJson(TEXT("GEditor not available"));
	}

	if (!GEditor->PlayWorld)
	{
		return MakeErrorJson(TEXT("No PIE session running"));
	}

	if (!GUnrealEd)
	{
		return MakeErrorJson(TEXT("GUnrealEd not available"));
	}

	GUnrealEd->PlaySessionSingleStepped();

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("Single step executed"));
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleGetPIEState(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		return MakeErrorJson(TEXT("GEditor not available"));
	}

	bool bIsRunning = GEditor->PlayWorld != nullptr;
	bool bIsPaused = bIsRunning && GEditor->PlayWorld->bDebugPauseExecution;

	Result->SetBoolField(TEXT("isRunning"), bIsRunning);
	Result->SetBoolField(TEXT("isPaused"), bIsPaused);

	if (bIsRunning && GEditor->PlayWorld)
	{
		Result->SetNumberField(TEXT("timeSeconds"), GEditor->PlayWorld->GetTimeSeconds());
		Result->SetNumberField(TEXT("realTimeSeconds"), GEditor->PlayWorld->GetRealTimeSeconds());
	}

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleExecuteConsole(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	if (!BodyJson->HasField(TEXT("command")))
	{
		return MakeErrorJson(TEXT("Missing 'command' field"));
	}

	FString Command = BodyJson->GetStringField(TEXT("command"));
	if (Command.IsEmpty())
	{
		return MakeErrorJson(TEXT("Empty command"));
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return MakeErrorJson(TEXT("No world context available"));
	}

	bool bSuccess = GEngine->Exec(World, *Command);

	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("command"), Command);
	Result->SetStringField(TEXT("message"), bSuccess ? TEXT("Command executed") : TEXT("Command failed"));
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleGetCVar(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	if (!BodyJson->HasField(TEXT("name")))
	{
		return MakeErrorJson(TEXT("Missing 'name' field"));
	}

	FString Name = BodyJson->GetStringField(TEXT("name"));

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!CVar)
	{
		return MakeErrorJson(FString::Printf(TEXT("CVar '%s' not found"), *Name));
	}

	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("value"), CVar->GetString());
	Result->SetStringField(TEXT("help"), CVar->GetHelp());

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleSetCVar(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	if (!BodyJson->HasField(TEXT("name")) || !BodyJson->HasField(TEXT("value")))
	{
		return MakeErrorJson(TEXT("Missing 'name' or 'value' field"));
	}

	FString Name = BodyJson->GetStringField(TEXT("name"));
	FString Value = BodyJson->GetStringField(TEXT("value"));

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!CVar)
	{
		return MakeErrorJson(FString::Printf(TEXT("CVar '%s' not found"), *Name));
	}

	FString OldValue = CVar->GetString();
	CVar->Set(*Value);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("oldValue"), OldValue);
	Result->SetStringField(TEXT("newValue"), Value);
	return JsonToString(Result);
}
