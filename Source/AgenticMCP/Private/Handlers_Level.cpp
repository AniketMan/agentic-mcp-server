// Handlers_Level.cpp
// Level management handlers for AgenticMCP.
// These handlers manage sublevels and access level blueprints.
//
// Endpoints implemented:
//   /api/list-levels          - List all loaded levels and streaming sublevels
//   /api/load-level           - Load a sublevel into the current world
//   /api/remove-sublevel      - Remove a streaming sublevel from the world
//   /api/get-level-blueprint  - Get the level blueprint for a specific level

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Editor.h"
#include "EditorLevelUtils.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "FileHelpers.h"
#include "NavigationSystem.h"

// ============================================================
// Helper: Get the current editor world
// ============================================================

static UWorld* GetEditorWorldForLevel()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

// ============================================================
// HandleListLevels
// POST /api/list-levels {}
// ============================================================

FString FAgenticMCPServer::HandleListLevels(const FString& Body)
{
	UWorld* World = GetEditorWorldForLevel();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world available. Is a level open in the editor?"));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("worldName"), World->GetName());
	OutJson->SetStringField(TEXT("worldPath"), World->GetPathName());

	// Persistent level
	TSharedRef<FJsonObject> PersistentJson = MakeShared<FJsonObject>();
	PersistentJson->SetStringField(TEXT("name"), World->PersistentLevel->GetOuter()->GetName());
	PersistentJson->SetBoolField(TEXT("isPersistent"), true);

	// Check if persistent level has a level blueprint
	ULevelScriptBlueprint* PersistentBP = World->PersistentLevel->GetLevelScriptBlueprint(false);
	PersistentJson->SetBoolField(TEXT("hasLevelBlueprint"), PersistentBP != nullptr);
	if (PersistentBP)
	{
		TArray<UEdGraph*> Graphs;
		PersistentBP->GetAllGraphs(Graphs);
		int32 TotalNodes = 0;
		for (UEdGraph* G : Graphs) { if (G) TotalNodes += G->Nodes.Num(); }
		PersistentJson->SetNumberField(TEXT("graphCount"), Graphs.Num());
		PersistentJson->SetNumberField(TEXT("totalNodes"), TotalNodes);
	}

	// Actor count in persistent level
	int32 PersistentActorCount = 0;
	for (AActor* Actor : World->PersistentLevel->Actors)
	{
		if (Actor) PersistentActorCount++;
	}
	PersistentJson->SetNumberField(TEXT("actorCount"), PersistentActorCount);

	OutJson->SetObjectField(TEXT("persistentLevel"), PersistentJson);

	// Streaming levels (sublevels)
	TArray<TSharedPtr<FJsonValue>> SubLevelArray;
	for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		if (!StreamingLevel) continue;

		TSharedRef<FJsonObject> SubJson = MakeShared<FJsonObject>();
		SubJson->SetStringField(TEXT("name"), StreamingLevel->GetWorldAssetPackageName());
		SubJson->SetBoolField(TEXT("isLoaded"), StreamingLevel->HasLoadedLevel());
		SubJson->SetBoolField(TEXT("isVisible"), StreamingLevel->GetShouldBeVisibleFlag());

		// If loaded, get actor count and level blueprint info
		if (StreamingLevel->HasLoadedLevel())
		{
			ULevel* LoadedLevel = StreamingLevel->GetLoadedLevel();
			if (LoadedLevel)
			{
				int32 ActorCount = 0;
				for (AActor* Actor : LoadedLevel->Actors)
				{
					if (Actor) ActorCount++;
				}
				SubJson->SetNumberField(TEXT("actorCount"), ActorCount);

				ULevelScriptBlueprint* SubBP = LoadedLevel->GetLevelScriptBlueprint(false);
				SubJson->SetBoolField(TEXT("hasLevelBlueprint"), SubBP != nullptr);
				if (SubBP)
				{
					TArray<UEdGraph*> Graphs;
					SubBP->GetAllGraphs(Graphs);
					int32 TotalNodes = 0;
					for (UEdGraph* G : Graphs) { if (G) TotalNodes += G->Nodes.Num(); }
					SubJson->SetNumberField(TEXT("graphCount"), Graphs.Num());
					SubJson->SetNumberField(TEXT("totalNodes"), TotalNodes);
				}
			}
		}

		SubLevelArray.Add(MakeShared<FJsonValueObject>(SubJson));
	}
	OutJson->SetArrayField(TEXT("streamingLevels"), SubLevelArray);

	return JsonToString(OutJson);
}

// ============================================================
// HandleLoadLevel
// POST /api/load-level { "levelPath": "/Game/Maps/MyLevel" }
// ============================================================

FString FAgenticMCPServer::HandleLoadLevel(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString LevelPath = Json->GetStringField(TEXT("levelPath"));
	if (LevelPath.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: levelPath"));

	UWorld* World = GetEditorWorldForLevel();
	if (!World)
		return MakeErrorJson(TEXT("No editor world available"));

	// Check if already loaded
	for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		if (StreamingLevel && StreamingLevel->GetWorldAssetPackageName() == LevelPath)
		{
			if (StreamingLevel->HasLoadedLevel())
			{
				TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
				OutJson->SetBoolField(TEXT("success"), true);
				OutJson->SetBoolField(TEXT("alreadyLoaded"), true);
				OutJson->SetStringField(TEXT("levelPath"), LevelPath);
				return JsonToString(OutJson);
			}
		}
	}

	// Add the sublevel via editor utilities
	// Note: SEH exception handling removed for UE 5.6 compatibility
	// UE 5.6 does not allow __try in functions with C++ objects requiring unwinding
	ULevelStreaming* NewLevel = nullptr;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		// UE 5.6+ uses struct-based API with constructor params
		UEditorLevelUtils::FAddLevelToWorldParams AddParams(ULevelStreamingDynamic::StaticClass(), FName(*LevelPath));
		NewLevel = UEditorLevelUtils::AddLevelToWorld(World, AddParams);
#else
	// UE 5.4-5.5 uses the 3-param overload
	NewLevel = EditorLevelUtils::AddLevelToWorld(
		World, *LevelPath, ULevelStreamingDynamic::StaticClass());
#endif

	if (!NewLevel)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Failed to load level '%s'. Verify the path exists."), *LevelPath));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("levelPath"), LevelPath);
	OutJson->SetBoolField(TEXT("isLoaded"), NewLevel->HasLoadedLevel());
	return JsonToString(OutJson);
}

// ============================================================
// HandleRemoveSublevel
// POST /api/remove-sublevel { "levelPath": "/Game/Maps/MyLevel" }
// Removes a streaming sublevel from the persistent level.
// ============================================================

FString FAgenticMCPServer::HandleRemoveSublevel(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString LevelPath = Json->GetStringField(TEXT("levelPath"));
	if (LevelPath.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: levelPath"));

	UWorld* World = GetEditorWorldForLevel();
	if (!World)
		return MakeErrorJson(TEXT("No editor world available"));

	// Find the streaming level by path
	ULevelStreaming* FoundLevel = nullptr;
	for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		if (!StreamingLevel) continue;

		FString PackageName = StreamingLevel->GetWorldAssetPackageName();
		// Match by full path or partial name
		if (PackageName == LevelPath || PackageName.Contains(LevelPath) || LevelPath.Contains(PackageName))
		{
			FoundLevel = StreamingLevel;
			break;
		}
	}

	if (!FoundLevel)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Streaming level '%s' not found. Use list-levels to see available streaming levels."),
			*LevelPath));
	}

	// Get the actual package name before removal for reporting
	FString RemovedPackageName = FoundLevel->GetWorldAssetPackageName();

	// Remove the level from the world using editor utilities
	bool bSuccess = false;
	ULevel* LoadedLevel = FoundLevel->GetLoadedLevel();
	if (LoadedLevel)
	{
		bSuccess = UEditorLevelUtils::RemoveLevelFromWorld(LoadedLevel);
	}

	if (!bSuccess)
	{
		// Try alternative method - directly remove from streaming levels array
		World->RemoveStreamingLevel(FoundLevel);
		bSuccess = true;
	}

	// Mark the world package as dirty so it can be saved
	if (bSuccess)
	{
		World->MarkPackageDirty();
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), bSuccess);
	OutJson->SetStringField(TEXT("removedLevel"), RemovedPackageName);
	OutJson->SetNumberField(TEXT("remainingSublevels"), World->GetStreamingLevels().Num());

	return JsonToString(OutJson);
}

// ============================================================
// HandleSetStreamingLevelVisibility
// POST /api/streaming-level-visibility { "levelPath": "...", "visible": true/false }
// Makes a streaming level visible or hidden.
// ============================================================

FString FAgenticMCPServer::HandleSetStreamingLevelVisibility(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString LevelPath = Json->GetStringField(TEXT("levelPath"));
	if (LevelPath.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: levelPath"));

	bool bVisible = Json->GetBoolField(TEXT("visible"));

	UWorld* World = GetEditorWorldForLevel();
	if (!World)
		return MakeErrorJson(TEXT("No editor world available"));

	// Find the streaming level
	ULevelStreaming* FoundLevel = nullptr;
	for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		if (!StreamingLevel) continue;

		FString PackageName = StreamingLevel->GetWorldAssetPackageName();
		if (PackageName == LevelPath || PackageName.Contains(LevelPath) || LevelPath.Contains(PackageName))
		{
			FoundLevel = StreamingLevel;
			break;
		}
	}

	if (!FoundLevel)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Streaming level '%s' not found. Use list-levels to see available streaming levels."),
			*LevelPath));
	}

	// Set visibility
	bool bWasVisible = FoundLevel->GetShouldBeVisibleFlag();
	FoundLevel->SetShouldBeVisible(bVisible);

	// Force update streaming
	// Note: SEH exception handling removed for UE 5.6 compatibility
	World->FlushLevelStreaming(EFlushLevelStreamingType::Full);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("levelPath"), FoundLevel->GetWorldAssetPackageName());
	OutJson->SetBoolField(TEXT("wasVisible"), bWasVisible);
	OutJson->SetBoolField(TEXT("isNowVisible"), bVisible);
	OutJson->SetBoolField(TEXT("isLoaded"), FoundLevel->HasLoadedLevel());

	return JsonToString(OutJson);
}

// ============================================================
// HandleGetOutputLog
// POST /api/output-log { "lines": 50, "filter": "optional" }
// Returns recent output log entries.
// ============================================================

FString FAgenticMCPServer::HandleGetOutputLog(const FString& Body)
{
	if (!GLog)
	{
		return MakeErrorJson(TEXT("Log system not available"));
	}
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);

	int32 NumLines = 50;
	FString Filter;

	if (Json.IsValid())
	{
		if (Json->HasField(TEXT("lines")))
		{
			NumLines = static_cast<int32>(Json->GetNumberField(TEXT("lines")));
		}
		if (Json->HasField(TEXT("filter")))
		{
			Filter = Json->GetStringField(TEXT("filter"));
		}
	}

	NumLines = FMath::Clamp(NumLines, 1, 500);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> LogArray;

	// Access the output log via GLog
	if (GLog)
	{
		// Use output devices to capture log
		// Note: This is a simplified implementation - full log access
		// would require implementing a custom FOutputDevice

		// For now, read from the log file directly
		FString LogFilePath = FPaths::ProjectLogDir() / TEXT("*.log");
		TArray<FString> LogFiles;
		IFileManager::Get().FindFiles(LogFiles, *LogFilePath, true, false);

		if (LogFiles.Num() > 0)
		{
			// Get most recent log file
			FString LatestLog = FPaths::ProjectLogDir() / LogFiles.Last();

			TArray<FString> Lines;
			if (FFileHelper::LoadFileToStringArray(Lines, *LatestLog))
			{
				int32 StartIndex = FMath::Max(0, Lines.Num() - NumLines);
				for (int32 i = StartIndex; i < Lines.Num(); ++i)
				{
					const FString& Line = Lines[i];
					if (Filter.IsEmpty() || Line.Contains(Filter))
					{
						LogArray.Add(MakeShared<FJsonValueString>(Line));
					}
				}
			}

			OutJson->SetStringField(TEXT("logFile"), LatestLog);
		}
	}

	OutJson->SetArrayField(TEXT("lines"), LogArray);
	OutJson->SetNumberField(TEXT("count"), LogArray.Num());
	OutJson->SetNumberField(TEXT("requested"), NumLines);
	if (!Filter.IsEmpty())
	{
		OutJson->SetStringField(TEXT("filter"), Filter);
	}

	return JsonToString(OutJson);
}

// ============================================================
// HandleGetLevelBlueprint
// POST /api/get-level-blueprint { "level": "MyLevel" }
// ============================================================

FString FAgenticMCPServer::HandleGetLevelBlueprint(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString LevelName = Json->GetStringField(TEXT("level"));
	if (LevelName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: level"));

	// Try loading as a map asset first (this works for both persistent and streaming levels)
	FString LoadError;
	UBlueprint* LevelBP = LoadBlueprintByName(LevelName, LoadError);
	if (LevelBP)
	{
		TSharedRef<FJsonObject> OutJson = SerializeBlueprint(LevelBP);
		OutJson->SetStringField(TEXT("level"), LevelName);
		OutJson->SetStringField(TEXT("type"), TEXT("LevelScriptBlueprint"));
		return JsonToString(OutJson);
	}

	// Try finding in the current world's streaming levels
	UWorld* World = GetEditorWorldForLevel();
	if (World)
	{
		// Check persistent level
		if (World->GetName().Contains(LevelName) || LevelName == TEXT("persistent"))
		{
			ULevelScriptBlueprint* PBP = World->PersistentLevel->GetLevelScriptBlueprint(true);
			if (PBP)
			{
				TSharedRef<FJsonObject> OutJson = SerializeBlueprint(PBP);
				OutJson->SetStringField(TEXT("level"), World->GetName());
				OutJson->SetStringField(TEXT("type"), TEXT("LevelScriptBlueprint"));
				return JsonToString(OutJson);
			}
		}

		// Check streaming levels
		for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
		{
			if (!StreamingLevel || !StreamingLevel->HasLoadedLevel()) continue;

			FString StreamName = StreamingLevel->GetWorldAssetPackageName();
			if (StreamName.Contains(LevelName))
			{
				ULevel* LoadedLevel = StreamingLevel->GetLoadedLevel();
				if (LoadedLevel)
				{
					ULevelScriptBlueprint* SubBP = LoadedLevel->GetLevelScriptBlueprint(true);
					if (SubBP)
					{
						TSharedRef<FJsonObject> OutJson = SerializeBlueprint(SubBP);
						OutJson->SetStringField(TEXT("level"), StreamName);
						OutJson->SetStringField(TEXT("type"), TEXT("LevelScriptBlueprint"));
						return JsonToString(OutJson);
					}
				}
			}
		}
	}

	return MakeErrorJson(FString::Printf(
		TEXT("Level blueprint for '%s' not found. The level may not be loaded. "
			 "Use list-levels to see available levels, or load-level to add it."),
		*LevelName));
}

// ============================================================================
// LEVEL MUTATION HANDLERS
// ============================================================================

// --- levelCreate ---
// Create a new map asset.
// Body: { "path": "/Game/Maps/NewLevel", "template": "Default" (optional) }
FString FAgenticMCPServer::HandleLevelCreate(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString MapPath = Json->GetStringField(TEXT("path"));
	if (MapPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'path'"));
	}

	FString TemplateName;
	Json->TryGetStringField(TEXT("template"), TemplateName);

	// Create the new level
	UWorld* NewWorld = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!NewWorld)
	{
		return MakeErrorJson(TEXT("Failed to create new world"));
	}

	FString PackageName = MapPath;
	if (!PackageName.StartsWith(TEXT("/")))
	{
		PackageName = TEXT("/Game/") + PackageName;
	}

	// Save the world to the package
	bool bSaved = FEditorFileUtils::SaveLevel(NewWorld->PersistentLevel, *PackageName);
	if (!bSaved)
	{
		return MakeErrorJson(TEXT("Failed to save new level"));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetStringField(TEXT("path"), PackageName);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}

// --- levelSave ---
// Save the current level.
// Body: {} or { "all": true }
FString FAgenticMCPServer::HandleLevelSave(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

	bool bSaveAll = false;
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
	{
		Json->TryGetBoolField(TEXT("all"), bSaveAll);
	}

	bool bSuccess = false;
	if (bSaveAll)
	{
		bSuccess = FEditorFileUtils::SaveDirtyPackages(false, true, true);
	}
	else
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			return MakeErrorJson(TEXT("No editor world"));
		}
		FString MapName = World->GetMapName();
		bSuccess = FEditorFileUtils::SaveLevel(World->PersistentLevel);
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), bSuccess ? TEXT("ok") : TEXT("failed"));
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}

// --- levelAddSublevel ---
// Add a streaming sublevel to the current level.
// Body: { "path": "/Game/Maps/SubLevel_01" }
FString FAgenticMCPServer::HandleLevelAddSublevel(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString SublevelPath = Json->GetStringField(TEXT("path"));
	if (SublevelPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'path'"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world"));
	}

	ULevelStreaming* StreamingLevel = NewObject<ULevelStreamingDynamic>(World, ULevelStreamingDynamic::StaticClass());
	if (!StreamingLevel)
	{
		return MakeErrorJson(TEXT("Failed to create streaming level"));
	}

	StreamingLevel->SetWorldAssetByPackageName(FName(*SublevelPath));
	StreamingLevel->SetShouldBeLoaded(true);
	StreamingLevel->SetShouldBeVisible(true);
	World->AddStreamingLevel(StreamingLevel);
	World->MarkPackageDirty();

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetStringField(TEXT("sublevel"), SublevelPath);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}

// --- levelSetCurrentLevel ---
// Set the active level for editing (in multi-level setups).
// Body: { "levelName": "SubLevel_01" }
FString FAgenticMCPServer::HandleLevelSetCurrentLevel(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString LevelName = Json->GetStringField(TEXT("levelName"));
	if (LevelName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'levelName'"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world"));
	}

	// Check persistent level
	if (World->PersistentLevel && World->PersistentLevel->GetOuter()->GetName().Contains(LevelName))
	{
		World->SetCurrentLevel(World->PersistentLevel);
		TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
		OutJson->SetStringField(TEXT("status"), TEXT("ok"));
		OutJson->SetStringField(TEXT("currentLevel"), LevelName);
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(OutJson, Writer);
		return Out;
	}

	// Check streaming levels
	for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		if (StreamingLevel && StreamingLevel->GetWorldAssetPackageName().Contains(LevelName))
		{
			ULevel* Level = StreamingLevel->GetLoadedLevel();
			if (Level)
			{
				World->SetCurrentLevel(Level);
				TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
				OutJson->SetStringField(TEXT("status"), TEXT("ok"));
				OutJson->SetStringField(TEXT("currentLevel"), LevelName);
				FString Out;
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
				FJsonSerializer::Serialize(OutJson, Writer);
				return Out;
			}
			else
			{
				return MakeErrorJson(FString::Printf(TEXT("Level '%s' found but not loaded"), *LevelName));
			}
		}
	}

	return MakeErrorJson(FString::Printf(TEXT("Level not found: %s"), *LevelName));
}

// --- levelBuildLighting ---
// Build lighting for the current level.
// Body: { "quality": "Preview"|"Medium"|"High"|"Production" }
FString FAgenticMCPServer::HandleLevelBuildLighting(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

	FString Quality = TEXT("Preview");
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
	{
		Json->TryGetStringField(TEXT("quality"), Quality);
	}

	// Set quality level
	int32 QualityLevel = 0; // Preview
	if (Quality == TEXT("Medium")) QualityLevel = 1;
	else if (Quality == TEXT("High")) QualityLevel = 2;
	else if (Quality == TEXT("Production")) QualityLevel = 3;

	GEditor->Exec(GEditor->GetEditorWorldContext().World(), *FString::Printf(TEXT("BUILD LIGHTING QUALITY=%d"), QualityLevel));

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetStringField(TEXT("quality"), Quality);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}

// --- levelBuildNavigation ---
// Build navigation mesh for the current level.
// Body: {}
FString FAgenticMCPServer::HandleLevelBuildNavigation(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world"));
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		return MakeErrorJson(TEXT("No navigation system in world"));
	}

	NavSys->Build();

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}
