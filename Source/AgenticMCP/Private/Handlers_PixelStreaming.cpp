// Handlers_PixelStreaming.cpp
// Pixel Streaming 2 control endpoints for AgenticMCP
// Targets UE 5.6 PixelStreaming2 module exclusively (not legacy PixelStreaming v1).

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
#pragma warning(pop)

DEFINE_LOG_CATEGORY_STATIC(LogMCPPixelStreaming, Log, All);

static const TCHAR* PS2Module = TEXT("PixelStreaming2");

// ============================================================
// HandlePixelStreamingGetStatus
// GET /api/pixelstreaming/status
// ============================================================
FString FAgenticMCPServer::HandlePixelStreamingGetStatus(const TMap<FString, FString>& Params, const FString& Body)
{
	bool bModuleLoaded = FModuleManager::Get().IsModuleLoaded(PS2Module);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetBoolField(TEXT("pixelStreaming2ModuleLoaded"), bModuleLoaded);
	OutJson->SetStringField(TEXT("module"), PS2Module);

	if (!bModuleLoaded)
	{
		OutJson->SetStringField(TEXT("status"), TEXT("unavailable"));
		OutJson->SetStringField(TEXT("hint"), TEXT("Enable the PixelStreaming2 plugin in Edit > Plugins, then restart the editor."));
		return JsonToString(OutJson);
	}

	OutJson->SetStringField(TEXT("status"), TEXT("available"));

	if (IConsoleVariable* CVarEnabled = IConsoleManager::Get().FindConsoleVariable(TEXT("PixelStreaming2.Encoder.Enable")))
	{
		OutJson->SetBoolField(TEXT("encoderEnabled"), CVarEnabled->GetBool());
	}

	if (IConsoleVariable* CVarUrl = IConsoleManager::Get().FindConsoleVariable(TEXT("PixelStreaming2.Signalling.URL")))
	{
		OutJson->SetStringField(TEXT("signallingUrl"), CVarUrl->GetString());
	}

	return JsonToString(OutJson);
}

// ============================================================
// HandlePixelStreamingStart
// POST /api/pixelstreaming/start
// ============================================================
FString FAgenticMCPServer::HandlePixelStreamingStart(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!FModuleManager::Get().IsModuleLoaded(PS2Module))
	{
		return MakeErrorJson(TEXT("PixelStreaming2 module is not loaded. Enable it in Edit > Plugins."));
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
		FString Command = FString::Printf(TEXT("PixelStreaming2.Signalling.URL=%s"), *SignallingUrl);
		GEngine->Exec(World, *Command);
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("message"), TEXT("PixelStreaming2 start requested"));
	OutJson->SetStringField(TEXT("signallingUrl"), SignallingUrl);

	return JsonToString(OutJson);
}

// ============================================================
// HandlePixelStreamingStop
// POST /api/pixelstreaming/stop
// ============================================================
FString FAgenticMCPServer::HandlePixelStreamingStop(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!FModuleManager::Get().IsModuleLoaded(PS2Module))
	{
		return MakeErrorJson(TEXT("PixelStreaming2 module is not loaded. Enable it in Edit > Plugins."));
	}

	if (GEngine && GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		GEngine->Exec(World, TEXT("PixelStreaming2.Stop"));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("message"), TEXT("PixelStreaming2 stop requested"));

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

	if (!FModuleManager::Get().IsModuleLoaded(PS2Module))
	{
		OutJson->SetBoolField(TEXT("success"), false);
		OutJson->SetStringField(TEXT("error"), TEXT("PixelStreaming2 module is not loaded"));
		OutJson->SetBoolField(TEXT("moduleLoaded"), false);
		return JsonToString(OutJson);
	}

	bool bIsStreaming = false;
	FString StreamerUrl;
	FString StreamerId = TEXT("default");
	FString Status = TEXT("unknown");

	if (IConsoleVariable* CVarEnabled = IConsoleManager::Get().FindConsoleVariable(TEXT("PixelStreaming2.Encoder.Enable")))
	{
		bIsStreaming = CVarEnabled->GetBool();
		Status = bIsStreaming ? TEXT("active") : TEXT("inactive");
	}

	if (IConsoleVariable* CVarUrl = IConsoleManager::Get().FindConsoleVariable(TEXT("PixelStreaming2.Signalling.URL")))
	{
		StreamerUrl = CVarUrl->GetString();
	}

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
	if (!FModuleManager::Get().IsModuleLoaded(PS2Module))
	{
		return MakeErrorJson(TEXT("PixelStreaming2 module is not loaded"));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> CodecInfo = MakeShared<FJsonObject>();

	FString Codec = TEXT("H264");
	if (IConsoleVariable* CVarCodec = IConsoleManager::Get().FindConsoleVariable(TEXT("PixelStreaming2.Encoder.Codec")))
	{
		Codec = CVarCodec->GetString();
	}

	CodecInfo->SetStringField(TEXT("codec"), Codec);

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

	if (!FModuleManager::Get().IsModuleLoaded(PS2Module))
	{
		return MakeErrorJson(TEXT("PixelStreaming2 module is not loaded"));
	}

	FString Codec = TEXT("H264");
	JsonBody->TryGetStringField(TEXT("codec"), Codec);

	if (GEngine && GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		FString Command = FString::Printf(TEXT("PixelStreaming2.Encoder.Codec=%s"), *Codec);
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

	if (!FModuleManager::Get().IsModuleLoaded(PS2Module))
	{
		OutJson->SetBoolField(TEXT("success"), false);
		OutJson->SetStringField(TEXT("error"), TEXT("PixelStreaming2 module is not loaded"));
		OutJson->SetBoolField(TEXT("moduleLoaded"), false);
		return JsonToString(OutJson);
	}

	int32 PlayerCount = 0;
	bool bIsStreaming = false;

	if (IConsoleVariable* CVarEnabled = IConsoleManager::Get().FindConsoleVariable(TEXT("PixelStreaming2.Encoder.Enable")))
	{
		bIsStreaming = CVarEnabled->GetBool();
	}

	if (IConsoleVariable* CVarConnected = IConsoleManager::Get().FindConsoleVariable(TEXT("PixelStreaming2.WebRTC.Stats.PeerCount")))
	{
		PlayerCount = CVarConnected->GetInt();
	}

	if (bIsStreaming && PlayerCount == 0)
	{
		if (IConsoleVariable* CVarState = IConsoleManager::Get().FindConsoleVariable(TEXT("PixelStreaming2.WebRTC.State")))
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
