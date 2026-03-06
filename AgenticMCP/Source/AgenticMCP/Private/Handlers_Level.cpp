// Handlers_Level.cpp
// Level management handlers for AgenticMCP.
// These handlers manage sublevels and access level blueprints.
//
// Endpoints implemented:
//   /api/list-levels          - List all loaded levels and streaming sublevels
//   /api/load-level           - Load a sublevel into the current world
//   /api/get-level-blueprint  - Get the level blueprint for a specific level

#include "AgenticMCPServer.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Editor.h"
#include "EditorLevelUtils.h"
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
	ULevelStreaming* NewLevel = EditorLevelUtils::AddLevelToWorld(
		World, *LevelPath, ULevelStreamingDynamic::StaticClass());

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
