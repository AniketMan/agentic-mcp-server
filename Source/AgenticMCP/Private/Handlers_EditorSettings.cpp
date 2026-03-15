// Handlers_EditorSettings.cpp
// Editor preferences and project settings handlers for AgenticMCP.
// Provides structured access to project settings beyond raw CVars.
//
// Endpoints:
//   settingsGetProject    - Get key project settings (maps, rendering, packaging)
//   settingsGetEditor     - Get editor preferences (auto-save, grid, units)
//   settingsGetRendering  - Get rendering settings (AA, GI, shadows, post-process)
//   settingsGetPlugins    - List all enabled/disabled plugins

#include "AgenticMCPServer.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/RendererSettings.h"
#include "GameMapsSettings.h"
#include "GeneralProjectSettings.h"
#include "Settings/EditorExperimentalSettings.h"
#include "Interfaces/IPluginManager.h"

// ============================================================
// settingsGetProject - Get key project settings
// ============================================================
FString FAgenticMCPServer::HandleSettingsGetProject(const FString& Body)
{
	if (!GetDefault<UGeneralProjectSettings>())
	{
		return MakeErrorJson(TEXT("Project settings not available"));
	}
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	// General project settings
	const UGeneralProjectSettings* ProjectSettings = GetDefault<UGeneralProjectSettings>();
	if (ProjectSettings)
	{
		Result->SetStringField(TEXT("projectName"), ProjectSettings->ProjectName);
		Result->SetStringField(TEXT("companyName"), ProjectSettings->CompanyName);
		Result->SetStringField(TEXT("projectVersion"), ProjectSettings->ProjectVersion);
		Result->SetStringField(TEXT("description"), ProjectSettings->Description);
	}

	// Game maps settings
	const UGameMapsSettings* MapsSettings = GetDefault<UGameMapsSettings>();
	if (MapsSettings)
	{
		Result->SetStringField(TEXT("defaultGameMap"), MapsSettings->GetGameDefaultMap().ToString());
		Result->SetStringField(TEXT("editorStartupMap"), MapsSettings->EditorStartupMap.ToString());
		Result->SetStringField(TEXT("gameDefaultGameMode"), MapsSettings->GlobalDefaultGameMode.ToString());
	}

	// Engine version
	Result->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());

	return JsonToString(Result);
}

// ============================================================
// settingsGetEditor - Get editor preferences
// ============================================================
FString FAgenticMCPServer::HandleSettingsGetEditor(const FString& Body)
{
	if (!GetMutableDefault<UEditorPerProjectUserSettings>())
	{
		return MakeErrorJson(TEXT("Editor settings not available"));
	}
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	// Experimental settings
	const UEditorExperimentalSettings* ExpSettings = GetDefault<UEditorExperimentalSettings>();
	if (ExpSettings)
	{
		Result->SetBoolField(TEXT("proceduralFoliage"), ExpSettings->bProceduralFoliage);
	}

	// Editor-specific info
	Result->SetStringField(TEXT("projectDir"), FPaths::ProjectDir());
	Result->SetStringField(TEXT("projectContentDir"), FPaths::ProjectContentDir());
	Result->SetStringField(TEXT("projectSavedDir"), FPaths::ProjectSavedDir());
	Result->SetStringField(TEXT("engineDir"), FPaths::EngineDir());

	return JsonToString(Result);
}

// ============================================================
// settingsGetRendering - Get rendering settings
// ============================================================
FString FAgenticMCPServer::HandleSettingsGetRendering(const FString& Body)
{
	if (!GetDefault<URendererSettings>())
	{
		return MakeErrorJson(TEXT("Renderer settings not available"));
	}
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	const URendererSettings* RenderSettings = GetDefault<URendererSettings>();
	if (RenderSettings)
	{
		Result->SetBoolField(TEXT("mobileHDR"), RenderSettings->bMobileHDR);
		Result->SetNumberField(TEXT("antiAliasingMethod"), (int32)RenderSettings->DefaultFeatureAntiAliasing);
		Result->SetBoolField(TEXT("ambientOcclusion"), RenderSettings->DefaultFeatureAmbientOcclusion);
		Result->SetBoolField(TEXT("autoExposure"), RenderSettings->DefaultFeatureAutoExposure);
		Result->SetBoolField(TEXT("motionBlur"), RenderSettings->DefaultFeatureMotionBlur);
		Result->SetBoolField(TEXT("bloom"), RenderSettings->DefaultFeatureBloom);
	}

	return JsonToString(Result);
}

// ============================================================
// settingsGetPlugins - List all plugins and their status
// ============================================================
FString FAgenticMCPServer::HandleSettingsGetPlugins(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	TSharedPtr<FJsonObject> BodyJson = ParseBodyJson(Body);
	FString Filter;
	bool bEnabledOnly = false;
	if (BodyJson.IsValid())
	{
		if (BodyJson->HasField(TEXT("filter")))
			Filter = BodyJson->GetStringField(TEXT("filter"));
		if (BodyJson->HasField(TEXT("enabledOnly")))
			bEnabledOnly = BodyJson->GetBoolField(TEXT("enabledOnly"));
	}

	TArray<TSharedPtr<FJsonValue>> PluginArr;
	IPluginManager& PluginManager = IPluginManager::Get();

	for (const TSharedRef<IPlugin>& Plugin : PluginManager.GetDiscoveredPlugins())
	{
		const FPluginDescriptor& Desc = Plugin->GetDescriptor();
		bool bEnabled = Plugin->IsEnabled();

		if (bEnabledOnly && !bEnabled)
			continue;

		FString PluginName = Desc.FriendlyName.IsEmpty() ? Plugin->GetName() : Desc.FriendlyName;
		if (!Filter.IsEmpty() && !PluginName.Contains(Filter) && !Plugin->GetName().Contains(Filter))
			continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Plugin->GetName());
		Entry->SetStringField(TEXT("friendlyName"), Desc.FriendlyName);
		Entry->SetStringField(TEXT("category"), Desc.Category);
		Entry->SetBoolField(TEXT("enabled"), bEnabled);
		Entry->SetStringField(TEXT("version"), Desc.VersionName);
		Entry->SetStringField(TEXT("description"), Desc.Description);

		PluginArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), PluginArr.Num());
	Result->SetArrayField(TEXT("plugins"), PluginArr);
	return JsonToString(Result);
}
