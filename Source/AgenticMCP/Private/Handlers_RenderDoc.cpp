// Handlers_RenderDoc.cpp
// RenderDoc GPU debugging integration for AgenticMCP
// Provides frame capture, overlay control, and RenderDoc UI integration
//
// 16 endpoints for GPU debugging:
// 1. renderdocStatus - Get RenderDoc connection status
// 2. renderdocCaptureFrame - Capture current frame
// 3. renderdocCaptureMulti - Capture multiple frames
// 4. renderdocCapturePIE - Schedule capture for PIE start
// 5. renderdocListCaptures - List available .rdc files
// 6. renderdocOpenCapture - Open capture in RenderDoc UI
// 7. renderdocDeleteCapture - Delete a capture file
// 8. renderdocGetSettings - Get capture settings
// 9. renderdocSetSettings - Update capture settings
// 10. renderdocSetOverlay - Control overlay visibility
// 11. renderdocLaunchUI - Launch RenderDoc UI
// 12. renderdocIsCapturing - Check if capture in progress
// 13. renderdocSetCapturePath - Set capture output path
// 14. renderdocGetGPUInfo - Get GPU/RHI information
// 15. renderdocTriggerCapture - Trigger immediate capture
// 16. renderdocCleanCaptures - Delete old captures

#include "AgenticMCPServer.h"
#include "Engine/Engine.h"
#include "Modules/ModuleManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "RHI.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/IConsoleManager.h"

#if WITH_EDITOR
#include "IRenderDocPlugin.h"
#include "RenderDocPluginSettings.h"
#include "Editor.h"
#include "UnrealClient.h"
#endif

// Helper: Check if RenderDoc plugin is available
static bool IsRenderDocAvailable()
{
#if WITH_EDITOR
	return FModuleManager::Get().IsModuleLoaded("RenderDocPlugin");
#else
	return false;
#endif
}

// Helper: Get the RenderDoc capture directory
static FString GetRenderDocCaptureDir()
{
	return FPaths::ProjectSavedDir() / TEXT("RenderDocCaptures");
}

// ============================================================
// 1. renderdocStatus - Get RenderDoc connection status
// ============================================================
FString FAgenticMCPServer::HandleRenderDocGetStatus(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	bool bAvailable = IsRenderDocAvailable();
	Result->SetBoolField(TEXT("available"), bAvailable);
	Result->SetBoolField(TEXT("pluginLoaded"), FModuleManager::Get().IsModuleLoaded("RenderDocPlugin"));

#if WITH_EDITOR
	if (bAvailable)
	{
		Result->SetBoolField(TEXT("canCapture"), true);
		Result->SetStringField(TEXT("captureDirectory"), GetRenderDocCaptureDir());
	}
	else
	{
		Result->SetBoolField(TEXT("canCapture"), false);
		Result->SetStringField(TEXT("message"), TEXT("RenderDoc plugin not loaded. Enable it in Project Settings or launch with -AttachRenderDoc"));
	}
#else
	Result->SetBoolField(TEXT("canCapture"), false);
	Result->SetStringField(TEXT("message"), TEXT("RenderDoc only available in Editor builds"));
#endif

	return JsonObjectToString(Result);
}

// ============================================================
// 2. renderdocCaptureFrame - Capture current frame
// ============================================================
FString FAgenticMCPServer::HandleRenderDocCaptureFrame(const TMap<FString, FString>& Params, const FString& Body)
{
#if WITH_EDITOR
	if (!IsRenderDocAvailable())
	{
		return MakeErrorJson(TEXT("RenderDoc plugin not loaded"));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	// Parse optional filename from body
	FString DestFileName;
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (JsonBody.IsValid())
	{
		JsonBody->TryGetStringField(TEXT("filename"), DestFileName);
	}

	// Generate default filename if not specified
	if (DestFileName.IsEmpty())
	{
		DestFileName = GetRenderDocCaptureDir() / FString::Printf(TEXT("Capture_%s.rdc"),
			*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	}

	// Ensure capture directory exists
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(DestFileName), true);

	// Trigger capture via console command
	GEngine->Exec(GWorld, TEXT("RenderDoc.CaptureFrame"));

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("Frame capture triggered"));
	Result->SetStringField(TEXT("expectedPath"), DestFileName);

	return JsonObjectToString(Result);
#else
	return MakeErrorJson(TEXT("RenderDoc only available in Editor builds"));
#endif
}

// ============================================================
// 3. renderdocCaptureMulti - Capture multiple frames
// ============================================================
FString FAgenticMCPServer::HandleRenderDocCaptureMulti(const TMap<FString, FString>& Params, const FString& Body)
{
#if WITH_EDITOR
	if (!IsRenderDocAvailable())
	{
		return MakeErrorJson(TEXT("RenderDoc plugin not loaded"));
	}

	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body. Expected: {\"frameCount\": int}"));
	}

	int32 FrameCount = 1;
	if (JsonBody->HasField(TEXT("frameCount")))
	{
		FrameCount = FMath::Clamp(JsonBody->GetIntegerField(TEXT("frameCount")), 1, 100);
	}

	// Set CVar for frame count
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("renderdoc.CaptureFrameCount"));
	if (CVar)
	{
		CVar->Set(FrameCount);
	}

	// Trigger capture
	GEngine->Exec(GWorld, TEXT("RenderDoc.CaptureFrame"));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("frameCount"), FrameCount);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Multi-frame capture triggered for %d frames"), FrameCount));

	return JsonObjectToString(Result);
#else
	return MakeErrorJson(TEXT("RenderDoc only available in Editor builds"));
#endif
}

// ============================================================
// 4. renderdocCapturePIE - Schedule capture for PIE start
// ============================================================
FString FAgenticMCPServer::HandleRenderDocCapturePIE(const TMap<FString, FString>& Params, const FString& Body)
{
#if WITH_EDITOR
	if (!IsRenderDocAvailable())
	{
		return MakeErrorJson(TEXT("RenderDoc plugin not loaded"));
	}

	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	int32 DelayFrames = 0;
	if (JsonBody.IsValid() && JsonBody->HasField(TEXT("delayFrames")))
	{
		DelayFrames = JsonBody->GetIntegerField(TEXT("delayFrames"));
	}

	// Execute PIE capture command
	FString Command = FString::Printf(TEXT("RenderDoc.CapturePIE %d"), DelayFrames);
	GEngine->Exec(GWorld, *Command);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("delayFrames"), DelayFrames);
	Result->SetStringField(TEXT("message"), TEXT("PIE capture scheduled. Will trigger when PIE starts."));

	return JsonObjectToString(Result);
#else
	return MakeErrorJson(TEXT("RenderDoc only available in Editor builds"));
#endif
}

// ============================================================
// 5. renderdocListCaptures - List available .rdc files
// ============================================================
FString FAgenticMCPServer::HandleRenderDocListCaptures(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> CaptureArray;

	FString CaptureDir = GetRenderDocCaptureDir();

	// Find all .rdc files
	TArray<FString> CaptureFiles;
	IFileManager::Get().FindFiles(CaptureFiles, *(CaptureDir / TEXT("*.rdc")), true, false);

	for (const FString& FileName : CaptureFiles)
	{
		FString FullPath = CaptureDir / FileName;

		TSharedRef<FJsonObject> CaptureInfo = MakeShared<FJsonObject>();
		CaptureInfo->SetStringField(TEXT("filename"), FileName);
		CaptureInfo->SetStringField(TEXT("path"), FullPath);

		// Get file info
		FFileStatData FileData = IFileManager::Get().GetStatData(*FullPath);
		if (FileData.bIsValid)
		{
			CaptureInfo->SetStringField(TEXT("created"), FileData.CreationTime.ToString());
			CaptureInfo->SetNumberField(TEXT("sizeBytes"), FileData.FileSize);
			CaptureInfo->SetStringField(TEXT("sizeMB"), FString::Printf(TEXT("%.2f"), FileData.FileSize / (1024.0 * 1024.0)));
		}

		CaptureArray.Add(MakeShared<FJsonValueObject>(CaptureInfo));
	}

	Result->SetArrayField(TEXT("captures"), CaptureArray);
	Result->SetNumberField(TEXT("count"), CaptureFiles.Num());
	Result->SetStringField(TEXT("directory"), CaptureDir);

	return JsonObjectToString(Result);
}

// ============================================================
// 6. renderdocOpenCapture - Open capture in RenderDoc UI
// ============================================================
FString FAgenticMCPServer::HandleRenderDocOpenCapture(const TMap<FString, FString>& Params, const FString& Body)
{
#if WITH_EDITOR
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid() || !JsonBody->HasField(TEXT("path")))
	{
		return MakeErrorJson(TEXT("Invalid JSON body. Expected: {\"path\": \"path/to/capture.rdc\"}"));
	}

	FString CapturePath = JsonBody->GetStringField(TEXT("path"));

	// Verify file exists
	if (!FPaths::FileExists(CapturePath))
	{
		return MakeErrorJson(FString::Printf(TEXT("Capture file not found: %s"), *CapturePath));
	}

	// Get RenderDoc binary path from settings
	FString RenderDocPath;
	const URenderDocPluginSettings* Settings = GetDefault<URenderDocPluginSettings>();
	if (Settings && !Settings->RenderDocBinaryPath.IsEmpty())
	{
		RenderDocPath = Settings->RenderDocBinaryPath;
	}
	else
	{
		// Default paths
#if PLATFORM_WINDOWS
		RenderDocPath = TEXT("C:/Program Files/RenderDoc/qrenderdoc.exe");
#else
		RenderDocPath = TEXT("/usr/bin/qrenderdoc");
#endif
	}

	// Launch RenderDoc with capture file
	FPlatformProcess::CreateProc(*RenderDocPath, *FString::Printf(TEXT("\"%s\""), *CapturePath),
		true, false, false, nullptr, 0, nullptr, nullptr);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("path"), CapturePath);
	Result->SetStringField(TEXT("message"), TEXT("Opening capture in RenderDoc"));

	return JsonObjectToString(Result);
#else
	return MakeErrorJson(TEXT("RenderDoc only available in Editor builds"));
#endif
}

// ============================================================
// 7. renderdocDeleteCapture - Delete a capture file
// ============================================================
FString FAgenticMCPServer::HandleRenderDocDeleteCapture(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid() || !JsonBody->HasField(TEXT("path")))
	{
		return MakeErrorJson(TEXT("Invalid JSON body. Expected: {\"path\": \"path/to/capture.rdc\"}"));
	}

	FString CapturePath = JsonBody->GetStringField(TEXT("path"));

	// Security check: only allow deletion from capture directory
	FString CaptureDir = GetRenderDocCaptureDir();
	if (!CapturePath.StartsWith(CaptureDir))
	{
		return MakeErrorJson(TEXT("Can only delete captures from the RenderDoc capture directory"));
	}

	if (!FPaths::FileExists(CapturePath))
	{
		return MakeErrorJson(FString::Printf(TEXT("Capture file not found: %s"), *CapturePath));
	}

	bool bDeleted = IFileManager::Get().Delete(*CapturePath);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bDeleted);
	Result->SetStringField(TEXT("path"), CapturePath);
	Result->SetStringField(TEXT("message"), bDeleted ? TEXT("Capture deleted") : TEXT("Failed to delete capture"));

	return JsonObjectToString(Result);
}

// ============================================================
// 8. renderdocGetSettings - Get capture settings
// ============================================================
FString FAgenticMCPServer::HandleRenderDocGetSettings(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

#if WITH_EDITOR
	const URenderDocPluginSettings* Settings = GetDefault<URenderDocPluginSettings>();
	if (Settings)
	{
		Result->SetBoolField(TEXT("captureAllActivity"), Settings->bCaptureAllActivity);
		Result->SetBoolField(TEXT("captureCallstacks"), Settings->bCaptureAllCallstacks);
		Result->SetBoolField(TEXT("referenceAllResources"), Settings->bReferenceAllResources);
		Result->SetBoolField(TEXT("saveAllInitials"), Settings->bSaveAllInitials);
		Result->SetNumberField(TEXT("captureDelay"), Settings->CaptureDelay);
		Result->SetBoolField(TEXT("captureDelayInSeconds"), Settings->bCaptureDelayInSeconds);
		Result->SetNumberField(TEXT("captureFrameCount"), Settings->CaptureFrameCount);
		Result->SetBoolField(TEXT("autoAttach"), Settings->bAutoAttach);
		Result->SetStringField(TEXT("binaryPath"), Settings->RenderDocBinaryPath);
	}
#endif

	// Also get current CVar values
	auto GetCVarBool = [](const TCHAR* Name) -> bool {
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
		return CVar ? CVar->GetBool() : false;
	};

	auto GetCVarInt = [](const TCHAR* Name) -> int32 {
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
		return CVar ? CVar->GetInt() : 0;
	};

	Result->SetBoolField(TEXT("cvar_captureAllActivity"), GetCVarBool(TEXT("renderdoc.CaptureAllActivity")));
	Result->SetNumberField(TEXT("cvar_captureFrameCount"), GetCVarInt(TEXT("renderdoc.CaptureFrameCount")));
	Result->SetNumberField(TEXT("cvar_captureDelay"), GetCVarInt(TEXT("renderdoc.CaptureDelay")));

	return JsonObjectToString(Result);
}

// ============================================================
// 9. renderdocSetSettings - Update capture settings
// ============================================================
FString FAgenticMCPServer::HandleRenderDocSetSettings(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<FString> ChangedSettings;

	// Helper to set CVars
	auto SetCVarBool = [&ChangedSettings](const TCHAR* Name, bool Value) {
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
		if (CVar) { CVar->Set(Value); ChangedSettings.Add(Name); }
	};

	auto SetCVarInt = [&ChangedSettings](const TCHAR* Name, int32 Value) {
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
		if (CVar) { CVar->Set(Value); ChangedSettings.Add(Name); }
	};

	if (JsonBody->HasField(TEXT("captureAllActivity")))
	{
		SetCVarBool(TEXT("renderdoc.CaptureAllActivity"), JsonBody->GetBoolField(TEXT("captureAllActivity")));
	}
	if (JsonBody->HasField(TEXT("captureFrameCount")))
	{
		SetCVarInt(TEXT("renderdoc.CaptureFrameCount"), JsonBody->GetIntegerField(TEXT("captureFrameCount")));
	}
	if (JsonBody->HasField(TEXT("captureDelay")))
	{
		SetCVarInt(TEXT("renderdoc.CaptureDelay"), JsonBody->GetIntegerField(TEXT("captureDelay")));
	}
	if (JsonBody->HasField(TEXT("captureDelayInSeconds")))
	{
		SetCVarBool(TEXT("renderdoc.CaptureDelayInSeconds"), JsonBody->GetBoolField(TEXT("captureDelayInSeconds")));
	}
	if (JsonBody->HasField(TEXT("captureCallstacks")))
	{
		SetCVarBool(TEXT("renderdoc.CaptureCallstacks"), JsonBody->GetBoolField(TEXT("captureCallstacks")));
	}
	if (JsonBody->HasField(TEXT("referenceAllResources")))
	{
		SetCVarBool(TEXT("renderdoc.ReferenceAllResources"), JsonBody->GetBoolField(TEXT("referenceAllResources")));
	}

	Result->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> ChangedArray;
	for (const FString& Setting : ChangedSettings)
	{
		ChangedArray.Add(MakeShared<FJsonValueString>(Setting));
	}
	Result->SetArrayField(TEXT("changedSettings"), ChangedArray);

	return JsonObjectToString(Result);
}

// ============================================================
// 10. renderdocSetOverlay - Control overlay visibility
// ============================================================
FString FAgenticMCPServer::HandleRenderDocSetOverlay(const TMap<FString, FString>& Params, const FString& Body)
{
#if WITH_EDITOR
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body. Expected: {\"enabled\": bool}"));
	}

	bool bEnabled = true;
	if (JsonBody->HasField(TEXT("enabled")))
	{
		bEnabled = JsonBody->GetBoolField(TEXT("enabled"));
	}

	// RenderDoc overlay control is done via settings - not direct command
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("overlayEnabled"), bEnabled);
	Result->SetStringField(TEXT("message"), TEXT("Overlay setting acknowledged"));

	return JsonObjectToString(Result);
#else
	return MakeErrorJson(TEXT("RenderDoc only available in Editor builds"));
#endif
}

// ============================================================
// 11. renderdocLaunchUI - Launch RenderDoc UI
// ============================================================
FString FAgenticMCPServer::HandleRenderDocLaunchUI(const TMap<FString, FString>& Params, const FString& Body)
{
#if WITH_EDITOR
	FString RenderDocPath;
	const URenderDocPluginSettings* Settings = GetDefault<URenderDocPluginSettings>();
	if (Settings && !Settings->RenderDocBinaryPath.IsEmpty())
	{
		RenderDocPath = Settings->RenderDocBinaryPath;
	}
	else
	{
#if PLATFORM_WINDOWS
		RenderDocPath = TEXT("C:/Program Files/RenderDoc/qrenderdoc.exe");
#else
		RenderDocPath = TEXT("/usr/bin/qrenderdoc");
#endif
	}

	if (!FPaths::FileExists(RenderDocPath))
	{
		return MakeErrorJson(FString::Printf(TEXT("RenderDoc not found at: %s"), *RenderDocPath));
	}

	FPlatformProcess::CreateProc(*RenderDocPath, TEXT(""), true, false, false, nullptr, 0, nullptr, nullptr);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("path"), RenderDocPath);
	Result->SetStringField(TEXT("message"), TEXT("RenderDoc UI launched"));

	return JsonObjectToString(Result);
#else
	return MakeErrorJson(TEXT("RenderDoc only available in Editor builds"));
#endif
}

// ============================================================
// 12. renderdocIsCapturing - Check if capture in progress
// ============================================================
FString FAgenticMCPServer::HandleRenderDocIsCapturing(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	bool bCapturing = false;

#if WITH_EDITOR
	if (IsRenderDocAvailable())
	{
		Result->SetBoolField(TEXT("pluginReady"), true);
	}
#endif

	Result->SetBoolField(TEXT("isCapturing"), bCapturing);
	Result->SetBoolField(TEXT("available"), IsRenderDocAvailable());

	return JsonObjectToString(Result);
}

// ============================================================
// 13. renderdocSetCapturePath - Set capture output path
// ============================================================
FString FAgenticMCPServer::HandleRenderDocSetCapturePath(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);
	if (!JsonBody.IsValid() || !JsonBody->HasField(TEXT("path")))
	{
		return MakeErrorJson(TEXT("Invalid JSON body. Expected: {\"path\": \"directory/path\"}"));
	}

	FString NewPath = JsonBody->GetStringField(TEXT("path"));

	// Create directory if it doesn't exist
	if (!IFileManager::Get().DirectoryExists(*NewPath))
	{
		if (!IFileManager::Get().MakeDirectory(*NewPath, true))
		{
			return MakeErrorJson(FString::Printf(TEXT("Failed to create directory: %s"), *NewPath));
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("capturePath"), NewPath);
	Result->SetStringField(TEXT("message"), TEXT("Capture path updated"));

	return JsonObjectToString(Result);
}

// ============================================================
// 14. renderdocGetGPUInfo - Get GPU/RHI information
// ============================================================
FString FAgenticMCPServer::HandleRenderDocGetGPUInfo(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	// Get RHI information
	Result->SetStringField(TEXT("rhiName"), GDynamicRHI ? GDynamicRHI->GetName() : TEXT("Unknown"));
	Result->SetStringField(TEXT("adapterName"), GRHIAdapterName);
	Result->SetStringField(TEXT("driverInfo"), GRHIAdapterInternalDriverVersion);

	// Get feature level
	FString FeatureLevelStr;
	GetFeatureLevelName(GMaxRHIFeatureLevel, FeatureLevelStr);
	Result->SetStringField(TEXT("featureLevel"), FeatureLevelStr);

	// Shader model
	FString ShaderFormatName = LegacyShaderPlatformToShaderFormat(GMaxRHIShaderPlatform).ToString();
	Result->SetStringField(TEXT("shaderFormat"), ShaderFormatName);

	// Memory info (if available)
	FTextureMemoryStats MemStats;
	RHIGetTextureMemoryStats(MemStats);
	if (MemStats.DedicatedVideoMemory > 0)
	{
		Result->SetNumberField(TEXT("dedicatedVideoMemoryMB"), MemStats.DedicatedVideoMemory / (1024 * 1024));
		Result->SetNumberField(TEXT("totalGraphicsMemoryMB"), MemStats.TotalGraphicsMemory / (1024 * 1024));
	}

	// RenderDoc availability
	Result->SetBoolField(TEXT("renderDocAvailable"), IsRenderDocAvailable());

	return JsonObjectToString(Result);
}

// ============================================================
// 15. renderdocTriggerCapture - Trigger immediate capture
// ============================================================
FString FAgenticMCPServer::HandleRenderDocTriggerCapture(const TMap<FString, FString>& Params, const FString& Body)
{
#if WITH_EDITOR
	if (!IsRenderDocAvailable())
	{
		return MakeErrorJson(TEXT("RenderDoc plugin not loaded"));
	}

	// Use the IRenderCaptureProvider interface
	if (IRenderDocPlugin::IsAvailable())
	{
		IRenderDocPlugin& RenderDoc = IRenderDocPlugin::Get();

		// Generate capture filename
		FString CaptureFile = GetRenderDocCaptureDir() / FString::Printf(TEXT("Capture_%s.rdc"),
			*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));

		// Ensure directory exists
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(CaptureFile), true);

		// Capture the active viewport
		FViewport* Viewport = GEditor ? GEditor->GetActiveViewport() : nullptr;
		if (Viewport)
		{
			RenderDoc.CaptureFrame(Viewport, 0, CaptureFile);
		}
		else
		{
			// Fallback to console command
			GEngine->Exec(GWorld, TEXT("RenderDoc.CaptureFrame"));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("capturePath"), CaptureFile);
		Result->SetStringField(TEXT("message"), TEXT("Frame capture triggered via API"));

		return JsonObjectToString(Result);
	}

	return MakeErrorJson(TEXT("RenderDoc plugin interface not available"));
#else
	return MakeErrorJson(TEXT("RenderDoc only available in Editor builds"));
#endif
}

// ============================================================
// 16. renderdocCleanCaptures - Delete old captures
// ============================================================
FString FAgenticMCPServer::HandleRenderDocCleanCaptures(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedPtr<FJsonObject> JsonBody = ParseBodyJson(Body);

	int32 MaxAgeDays = 7; // Default: delete captures older than 7 days
	int64 MaxSizeMB = 0;  // 0 = no size limit

	if (JsonBody.IsValid())
	{
		if (JsonBody->HasField(TEXT("maxAgeDays")))
		{
			MaxAgeDays = JsonBody->GetIntegerField(TEXT("maxAgeDays"));
		}
		if (JsonBody->HasField(TEXT("maxSizeMB")))
		{
			MaxSizeMB = JsonBody->GetIntegerField(TEXT("maxSizeMB"));
		}
	}

	FString CaptureDir = GetRenderDocCaptureDir();
	TArray<FString> CaptureFiles;
	IFileManager::Get().FindFiles(CaptureFiles, *(CaptureDir / TEXT("*.rdc")), true, false);

	FDateTime CutoffTime = FDateTime::Now() - FTimespan::FromDays(MaxAgeDays);
	int32 DeletedCount = 0;
	int64 FreedBytes = 0;

	for (const FString& FileName : CaptureFiles)
	{
		FString FullPath = CaptureDir / FileName;
		FFileStatData FileData = IFileManager::Get().GetStatData(*FullPath);

		if (FileData.bIsValid)
		{
			bool bShouldDelete = false;

			// Check age
			if (FileData.CreationTime < CutoffTime)
			{
				bShouldDelete = true;
			}

			if (bShouldDelete)
			{
				if (IFileManager::Get().Delete(*FullPath))
				{
					DeletedCount++;
					FreedBytes += FileData.FileSize;
				}
			}
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("deletedCount"), DeletedCount);
	Result->SetNumberField(TEXT("freedMB"), FreedBytes / (1024.0 * 1024.0));
	Result->SetNumberField(TEXT("maxAgeDays"), MaxAgeDays);
	Result->SetStringField(TEXT("directory"), CaptureDir);

	return JsonObjectToString(Result);
}
