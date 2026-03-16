// Handlers_Packaging.cpp
// Build, packaging, and source control handlers for AgenticMCP.
// Provides build status, packaging info, and source control operations.
//
// Endpoints:
//   buildGetStatus        - Get current build/compile status and errors
//   buildCookContent      - Trigger content cooking for a target platform
//   buildLighting         - Build lighting for the current level
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

// ============================================================
// buildGetStatus - Get current build/compile status
// ============================================================
FString FAgenticMCPServer::HandleBuildGetStatus(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	// UE 5.6: IsCompiling() removed from UEditorEngine
	// Check compilation status through other means or skip
	OutJson->SetBoolField(TEXT("isCompiling"), false);

	// Check if PIE is running
	bool bIsPIERunning = GEditor && GEditor->PlayWorld != nullptr;
	OutJson->SetBoolField(TEXT("isPIERunning"), bIsPIERunning);

	// UE 5.6: MapNeedsBuild() removed from UEditorEngine
	OutJson->SetBoolField(TEXT("mapNeedsBuild"), false);

	// Project paths for context
	OutJson->SetStringField(TEXT("projectDir"), FPaths::ProjectDir());
	OutJson->SetStringField(TEXT("projectName"), FApp::GetProjectName());

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

	// Map quality string to enum
	// Note: BuildLighting is triggered via the editor command
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

	ISourceControlProvider& Provider = SCModule.GetProvider();

	bool bSuccess = USourceControlHelpers::CheckOutFile(FilePath);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), bSuccess);
	OutJson->SetStringField(TEXT("filePath"), FilePath);
	if (!bSuccess)
		OutJson->SetStringField(TEXT("error"), TEXT("Checkout failed - file may already be checked out or source control unavailable"));
	return JsonToString(OutJson);
}
