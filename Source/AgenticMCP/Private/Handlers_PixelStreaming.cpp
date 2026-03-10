// Handlers_PixelStreaming.cpp
// PixelStreaming control endpoints for AgenticMCP

#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Modules/ModuleManager.h"
#include "Editor.h"

// ============================================================
// HandlePixelStreamingGetStatus
// GET /api/pixelstreaming/status
// ============================================================
FString FAgenticMCPServer::HandlePixelStreamingGetStatus(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	bool bModuleLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("PixelStreaming"));

	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("pixelStreamingModuleLoaded"), bModuleLoaded);

	if (bModuleLoaded)
	{
		Result->SetStringField(TEXT("status"), TEXT("available"));
	}
	else
	{
		Result->SetStringField(TEXT("status"), TEXT("unavailable"));
	}

	return JsonToString(Result);
}

// ============================================================
// HandlePixelStreamingStart
// POST /api/pixelstreaming/start
// ============================================================
FString FAgenticMCPServer::HandlePixelStreamingStart(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

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

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("PixelStreaming start requested"));
	Result->SetStringField(TEXT("signallingUrl"), SignallingUrl);

	return JsonToString(Result);
}

// ============================================================
// HandlePixelStreamingStop
// POST /api/pixelstreaming/stop
// ============================================================
FString FAgenticMCPServer::HandlePixelStreamingStop(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("PixelStreaming")))
	{
		return MakeErrorJson(TEXT("PixelStreaming module is not loaded"));
	}

	if (GEngine && GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		GEngine->Exec(World, TEXT("PixelStreaming.Stop"));
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("PixelStreaming stop requested"));

	return JsonToString(Result);
}

// ============================================================
// HandlePixelStreamingListStreamers
// GET /api/pixelstreaming/streamers
// ============================================================
FString FAgenticMCPServer::HandlePixelStreamingListStreamers(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> StreamersArray;

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("PixelStreaming")))
	{
		return MakeErrorJson(TEXT("PixelStreaming module is not loaded"));
	}

	TSharedRef<FJsonObject> DefaultStreamer = MakeShared<FJsonObject>();
	DefaultStreamer->SetStringField(TEXT("id"), TEXT("default"));
	DefaultStreamer->SetStringField(TEXT("status"), TEXT("unknown"));
	StreamersArray.Add(MakeShared<FJsonValueObject>(DefaultStreamer));

	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("streamers"), StreamersArray);
	Result->SetNumberField(TEXT("count"), StreamersArray.Num());

	return JsonToString(Result);
}

// ============================================================
// HandlePixelStreamingGetCodec
// GET /api/pixelstreaming/codec
// ============================================================
FString FAgenticMCPServer::HandlePixelStreamingGetCodec(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("PixelStreaming")))
	{
		return MakeErrorJson(TEXT("PixelStreaming module is not loaded"));
	}

	TSharedRef<FJsonObject> CodecInfo = MakeShared<FJsonObject>();
	CodecInfo->SetStringField(TEXT("codec"), TEXT("H264"));

	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("codecInfo"), CodecInfo);

	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("codec"), Codec);

	return JsonToString(Result);
}

// ============================================================
// HandlePixelStreamingListPlayers
// GET /api/pixelstreaming/players
// ============================================================
FString FAgenticMCPServer::HandlePixelStreamingListPlayers(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> PlayersArray;

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("PixelStreaming")))
	{
		return MakeErrorJson(TEXT("PixelStreaming module is not loaded"));
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("players"), PlayersArray);
	Result->SetNumberField(TEXT("count"), 0);

	return JsonToString(Result);
}
