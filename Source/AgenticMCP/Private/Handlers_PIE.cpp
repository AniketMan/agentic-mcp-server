// Handlers_PIE.cpp
// Play-In-Editor control handlers for AgenticMCP.
// Provides: start PIE, stop PIE, pause, resume, step frame, get state

#include "AgenticMCPServer.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "ILevelViewport.h"
#include "EditorModeManager.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Kismet/GameplayStatics.h"

FString FAgenticMCPServer::HandleStartPIE(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

	// Check if already playing
	if (GEditor->PlayWorld)
	{
		return MakeErrorJson(TEXT("PIE session already active"));
	}

	// Get play mode from params (default: PlayInViewport)
	FString Mode = Params.FindRef(TEXT("mode"));

	// Configure play settings
	FRequestPlaySessionParams SessionParams;

	if (Mode.Equals(TEXT("simulate"), ESearchCase::IgnoreCase))
	{
		SessionParams.WorldType = EPlaySessionWorldType::SimulateInEditor;
	}
	else if (Mode.Equals(TEXT("standalone"), ESearchCase::IgnoreCase))
	{
		SessionParams.WorldType = EPlaySessionWorldType::PlayInEditor;
		SessionParams.DestinationSlateViewport = nullptr; // Standalone window
	}
	else if (Mode.Equals(TEXT("mobile"), ESearchCase::IgnoreCase))
	{
		SessionParams.WorldType = EPlaySessionWorldType::PlayInMobilePreview;
	}
	else if (Mode.Equals(TEXT("vr"), ESearchCase::IgnoreCase))
	{
		SessionParams.WorldType = EPlaySessionWorldType::PlayInVR;
	}
	else
	{
		// Default: Play in active viewport
		SessionParams.WorldType = EPlaySessionWorldType::PlayInEditor;
	}

	// Start PIE session
	GEditor->RequestPlaySession(SessionParams);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("PIE session started"));
	Result->SetStringField(TEXT("mode"), Mode.IsEmpty() ? TEXT("viewport") : Mode);

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleStopPIE(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

	if (!GEditor->PlayWorld)
	{
		return MakeErrorJson(TEXT("No active PIE session"));
	}

	GEditor->RequestEndPlayMap();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("PIE session stopped"));

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandlePausePIE(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

	if (!GEditor->PlayWorld)
	{
		return MakeErrorJson(TEXT("No active PIE session"));
	}

	// Toggle pause
	FString Action = Params.FindRef(TEXT("action"));
	bool bPause = true;

	if (Action.Equals(TEXT("resume"), ESearchCase::IgnoreCase))
	{
		bPause = false;
	}
	else if (Action.Equals(TEXT("toggle"), ESearchCase::IgnoreCase))
	{
		bPause = !GEditor->PlayWorld->IsPaused();
	}

	GEditor->PlayWorld->bDebugPauseExecution = bPause;

	// Also pause/unpause the game
	if (APlayerController* PC = GEditor->PlayWorld->GetFirstPlayerController())
	{
		PC->SetPause(bPause);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("paused"), bPause);
	Result->SetStringField(TEXT("message"), bPause ? TEXT("PIE paused") : TEXT("PIE resumed"));

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleStepPIE(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

	if (!GEditor->PlayWorld)
	{
		return MakeErrorJson(TEXT("No active PIE session"));
	}

	// Get number of frames to step (default: 1)
	int32 Frames = 1;
	FString FramesStr = Params.FindRef(TEXT("frames"));
	if (!FramesStr.IsEmpty())
	{
		Frames = FCString::Atoi(*FramesStr);
		if (Frames < 1) Frames = 1;
	}

	// Step the simulation
	for (int32 i = 0; i < Frames; ++i)
	{
		GEditor->PlayWorld->Tick(LEVELTICK_All, GEditor->PlayWorld->GetDeltaSeconds());
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("framesAdvanced"), Frames);

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleGetPIEState(const TMap<FString, FString>& Params, const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);

	bool bIsPlaying = GEditor->PlayWorld != nullptr;
	Result->SetBoolField(TEXT("isPlaying"), bIsPlaying);

	if (bIsPlaying)
	{
		Result->SetBoolField(TEXT("isPaused"), GEditor->PlayWorld->IsPaused());
		Result->SetNumberField(TEXT("timeSeconds"), GEditor->PlayWorld->GetTimeSeconds());
		Result->SetNumberField(TEXT("realTimeSeconds"), GEditor->PlayWorld->GetRealTimeSeconds());
		Result->SetNumberField(TEXT("deltaSeconds"), GEditor->PlayWorld->GetDeltaSeconds());

		// Get player info
		if (APlayerController* PC = GEditor->PlayWorld->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				TSharedPtr<FJsonObject> PlayerObj = MakeShared<FJsonObject>();
				FVector Loc = Pawn->GetActorLocation();
				FRotator Rot = Pawn->GetActorRotation();
				PlayerObj->SetNumberField(TEXT("x"), Loc.X);
				PlayerObj->SetNumberField(TEXT("y"), Loc.Y);
				PlayerObj->SetNumberField(TEXT("z"), Loc.Z);
				PlayerObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
				PlayerObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
				PlayerObj->SetNumberField(TEXT("roll"), Rot.Roll);
				PlayerObj->SetStringField(TEXT("pawnClass"), Pawn->GetClass()->GetName());
				Result->SetObjectField(TEXT("player"), PlayerObj);
			}
		}

		// Get world type
		FString WorldType;
		switch (GEditor->GetPlaySessionWorldType())
		{
			case EPlaySessionWorldType::PlayInEditor: WorldType = TEXT("PIE"); break;
			case EPlaySessionWorldType::SimulateInEditor: WorldType = TEXT("Simulate"); break;
			case EPlaySessionWorldType::PlayInVR: WorldType = TEXT("VR"); break;
			case EPlaySessionWorldType::PlayInMobilePreview: WorldType = TEXT("Mobile"); break;
			default: WorldType = TEXT("Unknown"); break;
		}
		Result->SetStringField(TEXT("worldType"), WorldType);
	}

	return JsonToString(Result);
}
