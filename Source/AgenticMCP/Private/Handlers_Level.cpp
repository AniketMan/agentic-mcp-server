// Handlers_Level.cpp
// Level management handlers for AgenticMCP.
// These handlers manage sublevels and access level blueprints.
//
// Endpoints implemented:
//   /api/list-levels          - List all loaded levels and streaming sublevels
//   /api/load-level           - Load a sublevel into the current world
//   /api/remove-sublevel      - Remove a streaming sublevel from the world
//   /api/get-level-blueprint  - Get the level blueprint for a specific level

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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("worldName"), World->GetName());
	Result->SetStringField(TEXT("worldPath"), World->GetPathName());

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

	Result->SetObjectField(TEXT("persistentLevel"), PersistentJson);

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
	Result->SetArrayField(TEXT("streamingLevels"), SubLevelArray);

	return JsonToString(Result);
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
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetBoolField(TEXT("success"), true);
				Result->SetBoolField(TEXT("alreadyLoaded"), true);
				Result->SetStringField(TEXT("levelPath"), LevelPath);
				return JsonToString(Result);
			}
		}
	}

	// Add the sublevel via editor utilities
	// Wrapped in SEH to catch FSlowTask crashes during level loading
	ULevelStreaming* NewLevel = nullptr;

#if PLATFORM_WINDOWS
	__try
	{
#endif

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		// UE 5.6+ uses struct-based API with constructor
		UEditorLevelUtils::FAddLevelToWorldParams AddParams(
			ULevelStreamingDynamic::StaticClass(),
			FName(*LevelPath)
		);
		NewLevel = UEditorLevelUtils::AddLevelToWorld(World, AddParams);
#else
		// UE 5.4-5.5 uses the 3-param overload
		NewLevel = EditorLevelUtils::AddLevelToWorld(
			World, *LevelPath, ULevelStreamingDynamic::StaticClass());
#endif

#if PLATFORM_WINDOWS
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		UE_LOG(LogTemp, Error,
			TEXT("AgenticMCP: AddLevelToWorld crashed (SEH caught) for '%s' - "
				 "FSlowTask/FText issue during level streaming. Level may still be partially loaded."),
			*LevelPath);
		NewLevel = nullptr;
	}
#endif

	if (!NewLevel)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Failed to load level '%s'. Verify the path exists."), *LevelPath));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("levelPath"), LevelPath);
	Result->SetBoolField(TEXT("isLoaded"), NewLevel->HasLoadedLevel());
	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("removedLevel"), RemovedPackageName);
	Result->SetNumberField(TEXT("remainingSublevels"), World->GetStreamingLevels().Num());

	return JsonToString(Result);
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
	// Wrapped in SEH - FlushLevelStreaming can trigger FSlowTask crashes
#if PLATFORM_WINDOWS
	__try
	{
#endif
		World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
#if PLATFORM_WINDOWS
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("AgenticMCP: FlushLevelStreaming crashed (SEH caught) - "
				 "visibility change may not be fully applied"));
	}
#endif

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("levelPath"), FoundLevel->GetWorldAssetPackageName());
	Result->SetBoolField(TEXT("wasVisible"), bWasVisible);
	Result->SetBoolField(TEXT("isNowVisible"), bVisible);
	Result->SetBoolField(TEXT("isLoaded"), FoundLevel->HasLoadedLevel());

	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
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

			Result->SetStringField(TEXT("logFile"), LatestLog);
		}
	}

	Result->SetArrayField(TEXT("lines"), LogArray);
	Result->SetNumberField(TEXT("count"), LogArray.Num());
	Result->SetNumberField(TEXT("requested"), NumLines);
	if (!Filter.IsEmpty())
	{
		Result->SetStringField(TEXT("filter"), Filter);
	}

	return JsonToString(Result);
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
		TSharedRef<FJsonObject> Result = SerializeBlueprint(LevelBP);
		Result->SetStringField(TEXT("level"), LevelName);
		Result->SetStringField(TEXT("type"), TEXT("LevelScriptBlueprint"));
		return JsonToString(Result);
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
				TSharedRef<FJsonObject> Result = SerializeBlueprint(PBP);
				Result->SetStringField(TEXT("level"), World->GetName());
				Result->SetStringField(TEXT("type"), TEXT("LevelScriptBlueprint"));
				return JsonToString(Result);
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
						TSharedRef<FJsonObject> Result = SerializeBlueprint(SubBP);
						Result->SetStringField(TEXT("level"), StreamName);
						Result->SetStringField(TEXT("type"), TEXT("LevelScriptBlueprint"));
						return JsonToString(Result);
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
