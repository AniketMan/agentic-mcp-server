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
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	// Check if a build is in progress
	bool bIsCompiling = GEditor ? GEditor->IsCompiling() : false;
	Result->SetBoolField(TEXT("isCompiling"), bIsCompiling);

	// Get last compile errors from the message log
	// Note: Direct access to compile errors varies by engine version.
	// We use the output log approach for maximum compatibility.
	
	// Check if PIE is running
	bool bIsPIERunning = GEditor && GEditor->PlayWorld != nullptr;
	Result->SetBoolField(TEXT("isPIERunning"), bIsPIERunning);

	// Map build status
	Result->SetBoolField(TEXT("mapNeedsBuild"), GEditor ? GEditor->MapNeedsBuild() : false);

	// Project paths for context
	Result->SetStringField(TEXT("projectDir"), FPaths::ProjectDir());
	Result->SetStringField(TEXT("projectName"), FApp::GetProjectName());

	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("quality"), Quality);
	Result->SetStringField(TEXT("note"), TEXT("Lighting build started. Check editor progress bar for completion."));
	return JsonToString(Result);
}

// ============================================================
// sourceControlGetStatus - Get source control status
// ============================================================
FString FAgenticMCPServer::HandleSourceControlGetStatus(const FString& Body)
{
	ISourceControlModule& SCModule = ISourceControlModule::Get();
	ISourceControlProvider& Provider = SCModule.GetProvider();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("isEnabled"), SCModule.IsEnabled());
	Result->SetStringField(TEXT("providerName"), Provider.GetName().ToString());

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (Json.IsValid() && Json->HasField(TEXT("filePath")))
	{
		FString FilePath = Json->GetStringField(TEXT("filePath"));
		FSourceControlStatePtr State = Provider.GetState(FilePath, EStateCacheUsage::Use);
		if (State.IsValid())
		{
			Result->SetStringField(TEXT("filePath"), FilePath);
			Result->SetBoolField(TEXT("isCheckedOut"), State->IsCheckedOut());
			Result->SetBoolField(TEXT("isCurrent"), State->IsCurrent());
			Result->SetBoolField(TEXT("isAdded"), State->IsAdded());
			Result->SetBoolField(TEXT("isDeleted"), State->IsDeleted());
			Result->SetBoolField(TEXT("isModified"), State->IsModified());
			Result->SetBoolField(TEXT("canCheckout"), State->CanCheckout());
			Result->SetBoolField(TEXT("canEdit"), State->CanEdit());
			Result->SetStringField(TEXT("displayName"), State->GetDisplayName().ToString());
		}
		else
		{
			Result->SetStringField(TEXT("fileStatus"), TEXT("unknown"));
		}
	}

	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("filePath"), FilePath);
	if (!bSuccess)
		Result->SetStringField(TEXT("error"), TEXT("Checkout failed - file may already be checked out or source control unavailable"));
	return JsonToString(Result);
}
