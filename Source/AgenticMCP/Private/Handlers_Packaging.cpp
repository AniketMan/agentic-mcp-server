// Handlers_Packaging.cpp
// Build, packaging, and source control handlers for AgenticMCP.
// Provides build status, packaging info, and source control operations.
//
// Endpoints:
//   buildGetStatus         - Get current build/compile/cook/package status
//   buildCook              - Trigger content cooking for a target platform
//   buildPackage           - Trigger project packaging
//   buildGetLog            - Get current/last build log output
//   buildLighting          - Build lighting for the current level
//   sourceControlGetStatus - Get source control status of files
//   sourceControlCheckout  - Check out a file for editing

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Kismet2/CompilerResultsLog.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"
#include "SourceControlHelpers.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

namespace
{
	FCriticalSection GBuildStateMutex;
	FProcHandle GBuildProcHandle;
	bool bBuildInProgress = false;
	FString GBuildOperation = TEXT("idle");
	FString GBuildLogPath;
	int32 GBuildExitCode = 0;

	FString GetRunUATPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Build/BatchFiles/RunUAT.bat"));
	}

	FString GetBuildLogPath(const FString& Operation)
	{
		const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir() / FString::Printf(TEXT("AgenticMCP_%s_%s.log"), *Operation, *Timestamp));
	}

	bool StartBuildCookRunProcess(
		const FString& Operation,
		const FString& ProjectFile,
		const FString& Platform,
		const FString& Configuration,
		const FString& OptionalMap,
		bool bDoCook,
		bool bDoPackage,
		FString& OutError)
	{
		const FString RunUATPath = GetRunUATPath();
		if (!FPaths::FileExists(RunUATPath))
		{
			OutError = FString::Printf(TEXT("RunUAT not found: %s"), *RunUATPath);
			return false;
		}

		if (ProjectFile.IsEmpty() || !FPaths::FileExists(ProjectFile))
		{
			OutError = FString::Printf(TEXT("Project file not found: %s"), *ProjectFile);
			return false;
		}

		FString Args = FString::Printf(
			TEXT("BuildCookRun -project=\"%s\" -noP4 -platform=%s -clientconfig=%s -utf8output"),
			*ProjectFile,
			*Platform,
			*Configuration);

		if (bDoCook)
		{
			Args += TEXT(" -cook");
		}

		if (bDoPackage)
		{
			Args += TEXT(" -stage -package -archive");
		}

		if (!OptionalMap.IsEmpty())
		{
			Args += FString::Printf(TEXT(" -map=\"%s\""), *OptionalMap);
		}

		const FString LogPath = GetBuildLogPath(Operation);
		Args += FString::Printf(TEXT(" -log=\"%s\""), *LogPath);

		FProcHandle ProcHandle = FPlatformProcess::CreateProc(
			*RunUATPath,
			*Args,
			true,
			false,
			false,
			nullptr,
			0,
			nullptr,
			nullptr);

		if (!ProcHandle.IsValid())
		{
			OutError = TEXT("Failed to start RunUAT process");
			return false;
		}

		{
			FScopeLock Lock(&GBuildStateMutex);
			GBuildProcHandle = ProcHandle;
			bBuildInProgress = true;
			GBuildOperation = Operation;
			GBuildLogPath = LogPath;
			GBuildExitCode = 0;
		}

		return true;
	}
}

// ============================================================
// buildGetStatus - Get current build/compile/cook/package status
// ============================================================
FString FAgenticMCPServer::HandleBuildGetStatus(const FString& Body)
{
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	bool bInProgress = false;
	FString Operation = TEXT("idle");
	FString LogPath;
	int32 ExitCode = 0;

	{
		FScopeLock Lock(&GBuildStateMutex);
		if (bBuildInProgress && GBuildProcHandle.IsValid() && !FPlatformProcess::IsProcRunning(GBuildProcHandle))
		{
			FPlatformProcess::GetProcReturnCode(GBuildProcHandle, &GBuildExitCode);
			bBuildInProgress = false;
		}

		bInProgress = bBuildInProgress;
		Operation = GBuildOperation;
		LogPath = GBuildLogPath;
		ExitCode = GBuildExitCode;
	}

	OutJson->SetBoolField(TEXT("inProgress"), bInProgress);
	OutJson->SetStringField(TEXT("operation"), Operation);
	OutJson->SetStringField(TEXT("logPath"), LogPath);
	OutJson->SetNumberField(TEXT("exitCode"), ExitCode);
	OutJson->SetBoolField(TEXT("isPIERunning"), GEditor && GEditor->PlayWorld != nullptr);
	OutJson->SetStringField(TEXT("projectDir"), FPaths::ProjectDir());
	OutJson->SetStringField(TEXT("projectName"), FApp::GetProjectName());

	return JsonToString(OutJson);
}

// ============================================================
// buildCook - Trigger content cooking using RunUAT BuildCookRun
// ============================================================
FString FAgenticMCPServer::HandleBuildCook(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	const FString Platform = (Json.IsValid() && Json->HasField(TEXT("platform"))) ? Json->GetStringField(TEXT("platform")) : TEXT("Win64");
	const FString Configuration = (Json.IsValid() && Json->HasField(TEXT("configuration"))) ? Json->GetStringField(TEXT("configuration")) : TEXT("Development");
	const FString Map = (Json.IsValid() && Json->HasField(TEXT("map"))) ? Json->GetStringField(TEXT("map")) : TEXT("");
	const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());

	FString Error;
	if (!StartBuildCookRunProcess(TEXT("cook"), ProjectFile, Platform, Configuration, Map, true, false, Error))
	{
		return MakeErrorJson(Error);
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("operation"), TEXT("cook"));
	OutJson->SetStringField(TEXT("platform"), Platform);
	OutJson->SetStringField(TEXT("configuration"), Configuration);
	OutJson->SetStringField(TEXT("map"), Map);
	return JsonToString(OutJson);
}

// ============================================================
// buildPackage - Trigger project packaging using RunUAT BuildCookRun
// ============================================================
FString FAgenticMCPServer::HandleBuildPackage(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	const FString Platform = (Json.IsValid() && Json->HasField(TEXT("platform"))) ? Json->GetStringField(TEXT("platform")) : TEXT("Win64");
	const FString Configuration = (Json.IsValid() && Json->HasField(TEXT("configuration"))) ? Json->GetStringField(TEXT("configuration")) : TEXT("Development");
	const FString Map = (Json.IsValid() && Json->HasField(TEXT("map"))) ? Json->GetStringField(TEXT("map")) : TEXT("");
	const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());

	FString Error;
	if (!StartBuildCookRunProcess(TEXT("package"), ProjectFile, Platform, Configuration, Map, true, true, Error))
	{
		return MakeErrorJson(Error);
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("operation"), TEXT("package"));
	OutJson->SetStringField(TEXT("platform"), Platform);
	OutJson->SetStringField(TEXT("configuration"), Configuration);
	OutJson->SetStringField(TEXT("map"), Map);
	return JsonToString(OutJson);
}

// ============================================================
// buildGetLog - Return current/last build log content
// ============================================================
FString FAgenticMCPServer::HandleBuildGetLog(const FString& Body)
{
	FString LogPath;
	{
		FScopeLock Lock(&GBuildStateMutex);
		LogPath = GBuildLogPath;
	}

	if (LogPath.IsEmpty() || !FPaths::FileExists(LogPath))
	{
		return MakeErrorJson(TEXT("No build log available"));
	}

	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *LogPath))
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to read build log: %s"), *LogPath));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("logPath"), LogPath);
	OutJson->SetStringField(TEXT("content"), Content);
	return JsonToString(OutJson);
}

// ============================================================
// buildLighting - Build lighting for the current level
// ============================================================
FString FAgenticMCPServer::HandleBuildLighting(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	FString Quality = TEXT("Preview");
	if (Json.IsValid() && Json->HasField(TEXT("quality")))
		Quality = Json->GetStringField(TEXT("quality"));

	FString Command;
	if (Quality == TEXT("Preview"))
		Command = TEXT("MAP REBUILD LIGHTING QUALITY=0");
	else if (Quality == TEXT("Medium"))
		Command = TEXT("MAP REBUILD LIGHTING QUALITY=1");
	else if (Quality == TEXT("High"))
		Command = TEXT("MAP REBUILD LIGHTING QUALITY=2");
	else if (Quality == TEXT("Production"))
		Command = TEXT("MAP REBUILD LIGHTING QUALITY=3");
	else
		return MakeErrorJson(FString::Printf(TEXT("Unknown quality: %s. Supported: Preview, Medium, High, Production"), *Quality));

	GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Command);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("quality"), Quality);
	OutJson->SetStringField(TEXT("note"), TEXT("Lighting build started. Check editor progress bar for completion."));
	return JsonToString(OutJson);
}

// ============================================================
// sourceControlGetStatus - Get source control status
// ============================================================
FString FAgenticMCPServer::HandleSourceControlGetStatus(const FString& Body)
{
	if (!ISourceControlModule::Get().IsEnabled())
	{
		return MakeErrorJson(TEXT("Source control not enabled"));
	}
	ISourceControlModule& SCModule = ISourceControlModule::Get();
	ISourceControlProvider& Provider = SCModule.GetProvider();

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("isEnabled"), SCModule.IsEnabled());
	OutJson->SetStringField(TEXT("providerName"), Provider.GetName().ToString());

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (Json.IsValid() && Json->HasField(TEXT("filePath")))
	{
		FString FilePath = Json->GetStringField(TEXT("filePath"));
		FSourceControlStatePtr State = Provider.GetState(FilePath, EStateCacheUsage::Use);
		if (State.IsValid())
		{
			OutJson->SetStringField(TEXT("filePath"), FilePath);
			OutJson->SetBoolField(TEXT("isCheckedOut"), State->IsCheckedOut());
			OutJson->SetBoolField(TEXT("isCurrent"), State->IsCurrent());
			OutJson->SetBoolField(TEXT("isAdded"), State->IsAdded());
			OutJson->SetBoolField(TEXT("isDeleted"), State->IsDeleted());
			OutJson->SetBoolField(TEXT("isModified"), State->IsModified());
			OutJson->SetBoolField(TEXT("canCheckout"), State->CanCheckout());
			OutJson->SetBoolField(TEXT("canEdit"), State->CanEdit());
			OutJson->SetStringField(TEXT("displayName"), State->GetDisplayName().ToString());
		}
		else
		{
			OutJson->SetStringField(TEXT("fileStatus"), TEXT("unknown"));
		}
	}

	return JsonToString(OutJson);
}

// ============================================================
// sourceControlCheckout - Check out a file
// ============================================================
FString FAgenticMCPServer::HandleSourceControlCheckout(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString FilePath = Json->GetStringField(TEXT("filePath"));
	if (FilePath.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: filePath"));

	ISourceControlModule& SCModule = ISourceControlModule::Get();
	if (!SCModule.IsEnabled())
		return MakeErrorJson(TEXT("Source control is not enabled in this project"));

	bool bSuccess = USourceControlHelpers::CheckOutFile(FilePath);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), bSuccess);
	OutJson->SetStringField(TEXT("filePath"), FilePath);
	if (!bSuccess)
		OutJson->SetStringField(TEXT("error"), TEXT("Checkout failed - file may already be checked out or source control unavailable"));
	return JsonToString(OutJson);
}
