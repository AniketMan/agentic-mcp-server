// Handlers_PIE.cpp
// Play-In-Editor (PIE) control and Console command endpoints for AgenticMCP

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
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
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		return MakeErrorJson(TEXT("GEditor not available"));
	}

	if (GEditor->PlayWorld)
	{
		OutJson->SetBoolField(TEXT("success"), true);
		OutJson->SetStringField(TEXT("message"), TEXT("PIE session already running"));
		OutJson->SetBoolField(TEXT("alreadyRunning"), true);
		return JsonToString(OutJson);
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

	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("message"), TEXT("PIE session starting"));
	OutJson->SetStringField(TEXT("mode"), Mode);
	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandleStopPIE(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		return MakeErrorJson(TEXT("GEditor not available"));
	}

	if (!GEditor->PlayWorld)
	{
		OutJson->SetBoolField(TEXT("success"), true);
		OutJson->SetStringField(TEXT("message"), TEXT("No PIE session running"));
		OutJson->SetBoolField(TEXT("wasRunning"), false);
		return JsonToString(OutJson);
	}

	GEditor->RequestEndPlayMap();

	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("message"), TEXT("PIE session stopping"));
	OutJson->SetBoolField(TEXT("wasRunning"), true);
	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandlePausePIE(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

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

	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetBoolField(TEXT("isPaused"), bPause);
	OutJson->SetStringField(TEXT("message"), bPause ? TEXT("PIE paused") : TEXT("PIE resumed"));
	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandleStepPIE(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

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

	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("message"), TEXT("Single step executed"));
	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandleGetPIEState(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		return MakeErrorJson(TEXT("GEditor not available"));
	}

	bool bIsRunning = GEditor->PlayWorld != nullptr;
	bool bIsPaused = bIsRunning && GEditor->PlayWorld->bDebugPauseExecution;

	OutJson->SetBoolField(TEXT("isRunning"), bIsRunning);
	OutJson->SetBoolField(TEXT("isPaused"), bIsPaused);

	if (bIsRunning && GEditor->PlayWorld)
	{
		OutJson->SetNumberField(TEXT("timeSeconds"), GEditor->PlayWorld->GetTimeSeconds());
		OutJson->SetNumberField(TEXT("realTimeSeconds"), GEditor->PlayWorld->GetRealTimeSeconds());
	}

	return JsonToString(OutJson);
}
