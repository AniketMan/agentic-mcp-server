// Handlers_PixelStreaming.cpp
// PixelStreaming control endpoints for AgenticMCP

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Modules/ModuleManager.h"
#include "Editor.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPPixelStreaming, Log, All);

// ============================================================
// HandlePixelStreamingGetStatus
// GET /api/pixelstreaming/status
// ============================================================
FString FAgenticMCPServer::HandlePixelStreamingGetStatus(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("PixelStreaming")))
	{
		return MakeErrorJson(TEXT("PixelStreaming module not loaded"));
	}
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	bool bModuleLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("PixelStreaming"));

	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetBoolField(TEXT("pixelStreamingModuleLoaded"), bModuleLoaded);

	if (bModuleLoaded)
	{
		OutJson->SetStringField(TEXT("status"), TEXT("available"));
	}
	else
	{
		OutJson->SetStringField(TEXT("status"), TEXT("unavailable"));
	}

	return JsonToString(OutJson);
}

// ============================================================
// HandlePixelStreamingStart
// POST /api/pixelstreaming/start
// ============================================================
FString FAgenticMCPServer::HandlePixelStreamingStart(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("PixelStreaming")))
	{
		return MakeErrorJson(TEXT("PixelStreaming module is not loaded"));
	}

	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);

	FString SignallingUrl = TEXT("ws://localhost:8888");
	if (JsonBody.IsValid())
	{
		JsonBody->TryGetStringField(TEXT("signallingUrl"), SignallingUrl);
	}

	if (GEngine && GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		FString Command = FString::Printf(TEXT("PixelStreaming.URL=%s"), *SignallingUrl);
		GEngine->Exec(World, *Command);
	}

	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("message"), TEXT("PixelStreaming start requested"));
	OutJson->SetStringField(TEXT("signallingUrl"), SignallingUrl);

	return JsonToString(OutJson);
}

// ============================================================
// HandlePixelStreamingStop
// POST /api/pixelstreaming/stop
// ============================================================
FString FAgenticMCPServer::HandlePixelStreamingStop(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("PixelStreaming")))
	{
		return MakeErrorJson(TEXT("PixelStreaming module is not loaded"));
	}

	if (GEngine && GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		GEngine->Exec(World, TEXT("PixelStreaming.Stop"));
	}

	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("message"), TEXT("PixelStreaming stop requested"));

	return JsonToString(OutJson);
}

// ============================================================
// HandlePixelStreamingListStreamers
// GET /api/pixelstreaming/streamers
// ============================================================
FString FAgenticMCPServer::HandlePixelStreamingListStreamers(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> StreamersArray;

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("PixelStreaming")))
	{
		OutJson->SetBoolField(TEXT("success"), false);
		OutJson->SetStringField(TEXT("error"), TEXT("PixelStreaming module is not loaded"));
		OutJson->SetBoolField(TEXT("moduleLoaded"), false);
		return JsonToString(OutJson);
	}

	// Query PixelStreaming module for actual streamer info
	// Since we can't directly access the streamer list without the full module interface,
	// we check console variables and session state

	bool bIsStreaming = false;
	FString StreamerUrl = TEXT("");
	FString StreamerId = TEXT("default");
	FString Status = TEXT("unknown");

	// Check if streaming is active via console variables
	if (IConsoleVariable* CVarEnabled = IConsoleManager::Get().FindConsoleVariable(TEXT("PixelStreaming.Enabled")))
	{
		bIsStreaming = CVarEnabled->GetBool();
		Status = bIsStreaming ? TEXT("active") : TEXT("inactive");
	}

	if (IConsoleVariable* CVarUrl = IConsoleManager::Get().FindConsoleVariable(TEXT("PixelStreaming.URL")))
	{
		StreamerUrl = CVarUrl->GetString();
	}

	// Build streamer info
	TSharedRef<FJsonObject> StreamerObj = MakeShared<FJsonObject>();
	StreamerObj->SetStringField(TEXT("id"), StreamerId);
	StreamerObj->SetStringField(TEXT("status"), Status);
	StreamerObj->SetBoolField(TEXT("isStreaming"), bIsStreaming);
	if (!StreamerUrl.IsEmpty())
	{
		StreamerObj->SetStringField(TEXT("signallingUrl"), StreamerUrl);
	}
	StreamersArray.Add(MakeShared<FJsonValueObject>(StreamerObj));

	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetBoolField(TEXT("moduleLoaded"), true);
	OutJson->SetArrayField(TEXT("streamers"), StreamersArray);
	OutJson->SetNumberField(TEXT("count"), StreamersArray.Num());

	return JsonToString(OutJson);
}

// ============================================================
// HandlePixelStreamingGetCodec
// GET /api/pixelstreaming/codec
// ============================================================
FString FAgenticMCPServer::HandlePixelStreamingGetCodec(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("PixelStreaming")))
	{
		return MakeErrorJson(TEXT("PixelStreaming module is not loaded"));
	}

	TSharedRef<FJsonObject> CodecInfo = MakeShared<FJsonObject>();
	CodecInfo->SetStringField(TEXT("codec"), TEXT("H264"));

	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetObjectField(TEXT("codecInfo"), CodecInfo);

	return JsonToString(OutJson);
}

// ============================================================
// HandlePixelStreamingSetCodec
// POST /api/pixelstreaming/set-codec
// ============================================================
FString FAgenticMCPServer::HandlePixelStreamingSetCodec(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("PixelStreaming")))
	{
		return MakeErrorJson(TEXT("PixelStreaming module is not loaded"));
	}

	FString Codec = TEXT("H264");
	JsonBody->TryGetStringField(TEXT("codec"), Codec);

	if (GEngine && GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		FString Command = FString::Printf(TEXT("PixelStreaming.Encoder.Codec=%s"), *Codec);
		GEngine->Exec(World, *Command);
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("codec"), Codec);

	return JsonToString(OutJson);
}

// ============================================================
// HandlePixelStreamingListPlayers
// GET /api/pixelstreaming/players
// ============================================================
FString FAgenticMCPServer::HandlePixelStreamingListPlayers(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> PlayersArray;

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("PixelStreaming")))
	{
		OutJson->SetBoolField(TEXT("success"), false);
		OutJson->SetStringField(TEXT("error"), TEXT("PixelStreaming module is not loaded"));
		OutJson->SetBoolField(TEXT("moduleLoaded"), false);
		return JsonToString(OutJson);
	}

	// Get player/connection info from console variables and stats
	int32 PlayerCount = 0;
	bool bIsStreaming = false;

	if (IConsoleVariable* CVarEnabled = IConsoleManager::Get().FindConsoleVariable(TEXT("PixelStreaming.Enabled")))
	{
		bIsStreaming = CVarEnabled->GetBool();
	}

	// Check for connected peers via stats if available
	if (IConsoleVariable* CVarConnected = IConsoleManager::Get().FindConsoleVariable(TEXT("PixelStreaming.WebRTC.Stats.PeerCount")))
	{
		PlayerCount = CVarConnected->GetInt();
	}

	// If streaming is active, we have at least the potential for 1 player
	if (bIsStreaming && PlayerCount == 0)
	{
		// Check if there's an active WebRTC connection
		if (IConsoleVariable* CVarState = IConsoleManager::Get().FindConsoleVariable(TEXT("PixelStreaming.WebRTC.State")))
		{
			FString State = CVarState->GetString();
			if (State.Contains(TEXT("Connected")))
			{
				PlayerCount = 1;

				TSharedRef<FJsonObject> PlayerObj = MakeShared<FJsonObject>();
				PlayerObj->SetStringField(TEXT("id"), TEXT("webrtc_peer_0"));
				PlayerObj->SetStringField(TEXT("status"), TEXT("connected"));
				PlayerObj->SetStringField(TEXT("connectionState"), State);
				PlayersArray.Add(MakeShared<FJsonValueObject>(PlayerObj));
			}
		}
	}

	// Add any enumerated players
	for (int32 i = 0; i < PlayerCount && PlayersArray.Num() < PlayerCount; i++)
	{
		TSharedRef<FJsonObject> PlayerObj = MakeShared<FJsonObject>();
		PlayerObj->SetStringField(TEXT("id"), FString::Printf(TEXT("player_%d"), i));
		PlayerObj->SetStringField(TEXT("status"), TEXT("connected"));
		PlayersArray.Add(MakeShared<FJsonValueObject>(PlayerObj));
	}

	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetBoolField(TEXT("moduleLoaded"), true);
	OutJson->SetBoolField(TEXT("isStreaming"), bIsStreaming);
	OutJson->SetArrayField(TEXT("players"), PlayersArray);
	OutJson->SetNumberField(TEXT("count"), PlayersArray.Num());

	return JsonToString(OutJson);
}
