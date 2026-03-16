// Handlers_EditorSettings.cpp
// Editor preferences and project settings handlers for AgenticMCP.
// Provides structured access to project settings beyond raw CVars.
//
// Endpoints:
//   settingsGetProject    - Get key project settings (maps, rendering, packaging)
//   settingsGetEditor     - Get editor preferences (auto-save, grid, units)
//   settingsGetRendering  - Get rendering settings (AA, GI, shadows, post-process)
//   settingsGetPlugins    - List all enabled/disabled plugins

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
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
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	// General project settings
	const UGeneralProjectSettings* ProjectSettings = GetDefault<UGeneralProjectSettings>();
	if (ProjectSettings)
	{
		OutJson->SetStringField(TEXT("projectName"), ProjectSettings->ProjectName);
		OutJson->SetStringField(TEXT("companyName"), ProjectSettings->CompanyName);
		OutJson->SetStringField(TEXT("projectVersion"), ProjectSettings->ProjectVersion);
		OutJson->SetStringField(TEXT("description"), ProjectSettings->Description);
	}

	// Game maps settings
	const UGameMapsSettings* MapsSettings = GetDefault<UGameMapsSettings>();
	if (MapsSettings)
	{
		// UE 5.6: GetGameDefaultMap() returns FString directly, use getter for game mode
		OutJson->SetStringField(TEXT("defaultGameMap"), MapsSettings->GetGameDefaultMap());
		OutJson->SetStringField(TEXT("editorStartupMap"), MapsSettings->EditorStartupMap.ToString());
		OutJson->SetStringField(TEXT("gameDefaultGameMode"), MapsSettings->GetGlobalDefaultGameMode());
	}

	// Engine version
	OutJson->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());

	return JsonToString(OutJson);
}

// ============================================================
// settingsGetEditor - Get editor preferences
// ============================================================
FString FAgenticMCPServer::HandleSettingsGetEditor(const FString& Body)
{
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	// Experimental settings
	const UEditorExperimentalSettings* ExpSettings = GetDefault<UEditorExperimentalSettings>();
	if (ExpSettings)
	{
		OutJson->SetBoolField(TEXT("proceduralFoliage"), ExpSettings->bProceduralFoliage);
	}

	// Editor-specific info
	OutJson->SetStringField(TEXT("projectDir"), FPaths::ProjectDir());
	OutJson->SetStringField(TEXT("projectContentDir"), FPaths::ProjectContentDir());
	OutJson->SetStringField(TEXT("projectSavedDir"), FPaths::ProjectSavedDir());
	OutJson->SetStringField(TEXT("engineDir"), FPaths::EngineDir());

	return JsonToString(OutJson);
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
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	const URendererSettings* RenderSettings = GetDefault<URendererSettings>();
	if (RenderSettings)
	{
		// UE 5.6: Several properties removed or renamed
		// Only use properties that still exist
		OutJson->SetNumberField(TEXT("antiAliasingMethod"), (int32)RenderSettings->DefaultFeatureAntiAliasing);
		// DefaultFeatureAutoExposure is now an enum, convert to int
		OutJson->SetNumberField(TEXT("autoExposure"), (int32)RenderSettings->DefaultFeatureAutoExposure);
	}

	return JsonToString(OutJson);
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

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetNumberField(TEXT("count"), PluginArr.Num());
	OutJson->SetArrayField(TEXT("plugins"), PluginArr);
	return JsonToString(OutJson);
}

// ============================================================================
// EDITOR SETTINGS MUTATION HANDLERS
// ============================================================================

// --- settingsSetProject ---
// Set project settings fields.
// Body: { "projectName": "MyProject", "companyName": "MyStudio", "description": "..." }
FString FAgenticMCPServer::HandleSettingsSetProject(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	UGeneralProjectSettings* Settings = GetMutableDefault<UGeneralProjectSettings>();
	if (!Settings)
	{
		return MakeErrorJson(TEXT("Project settings not available"));
	}

	int32 Changed = 0;
	FString Val;
	if (Json->TryGetStringField(TEXT("projectName"), Val)) { Settings->ProjectName = Val; Changed++; }
	if (Json->TryGetStringField(TEXT("companyName"), Val)) { Settings->CompanyName = Val; Changed++; }
	if (Json->TryGetStringField(TEXT("description"), Val)) { Settings->Description = Val; Changed++; }
	if (Json->TryGetStringField(TEXT("homepage"), Val)) { Settings->Homepage = Val; Changed++; }
	if (Json->TryGetStringField(TEXT("projectVersion"), Val)) { Settings->ProjectVersion = Val; Changed++; }

	Settings->TryUpdateDefaultConfigFile();

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetNumberField(TEXT("fieldsChanged"), Changed);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}

// --- settingsSetEditor ---
// Set editor preference fields.
// Body: { "autoSaveEnabled": true, "autoSaveInterval": 300 }
FString FAgenticMCPServer::HandleSettingsSetEditor(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	int32 Changed = 0;
	bool BoolVal;
	double NumVal;

	// Auto-save settings via CVar
	if (Json->TryGetBoolField(TEXT("autoSaveEnabled"), BoolVal))
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("EditorAutoSave.Enabled"));
		if (CVar) { CVar->Set(BoolVal ? 1 : 0); Changed++; }
	}

	if (Json->TryGetNumberField(TEXT("autoSaveInterval"), NumVal))
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("EditorAutoSave.IntervalSeconds"));
		if (CVar) { CVar->Set((float)NumVal); Changed++; }
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetNumberField(TEXT("fieldsChanged"), Changed);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}

// --- settingsSetRendering ---
// Set rendering settings.
// Body: { "defaultRHI": "Vulkan"|"DX12"|"DX11", "rayTracing": true, "nanite": true, "lumen": true, "vsm": true }
FString FAgenticMCPServer::HandleSettingsSetRendering(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	int32 Changed = 0;
	bool BoolVal;

	if (Json->TryGetBoolField(TEXT("rayTracing"), BoolVal))
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing"));
		if (CVar) { CVar->Set(BoolVal ? 1 : 0); Changed++; }
	}

	if (Json->TryGetBoolField(TEXT("nanite"), BoolVal))
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Nanite"));
		if (CVar) { CVar->Set(BoolVal ? 1 : 0); Changed++; }
	}

	if (Json->TryGetBoolField(TEXT("lumen"), BoolVal))
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.Supported"));
		if (CVar) { CVar->Set(BoolVal ? 1 : 0); Changed++; }
	}

	if (Json->TryGetBoolField(TEXT("vsm"), BoolVal))
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.Enable"));
		if (CVar) { CVar->Set(BoolVal ? 1 : 0); Changed++; }
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetNumberField(TEXT("fieldsChanged"), Changed);
	OutJson->SetStringField(TEXT("note"), TEXT("Some settings require editor restart to take effect"));
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}
