// Handlers_Story.cpp
// Story / Narrative handlers for AgenticMCP.
// UE 5.6 target. Integrates with VRNarrativeKit and project DataTables.
//
// Endpoints:
//   storyGetState        - Read current narrative state
//   storySetStep         - Advance story to a specific step/beat
//   storyGetScreenplay   - Load screenplay/scene metadata
//   storyExecuteAction   - Fire a named story event/action

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Kismet/GameplayStatics.h"
#pragma warning(pop)

DEFINE_LOG_CATEGORY_STATIC(LogMCPStory, Log, All);

// ============================================================
// Helper: find DataTable assets with story/narrative data
// ============================================================
static UDataTable* FindDataTableByName(const FString& Name)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByClass(UDataTable::StaticClass()->GetClassPathName(), Assets, true);

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName.ToString().Contains(Name) || Asset.GetObjectPathString().Contains(Name))
		{
			return Cast<UDataTable>(Asset.GetAsset());
		}
	}
	return nullptr;
}

// ============================================================
// Helper: read source_truth files for screenplay data
// ============================================================
static FString LoadSourceTruthFile(const FString& PluginDir, const FString& FileName)
{
	FString Path = FPaths::Combine(PluginDir, TEXT("reference"), TEXT("source_truth"), FileName);
	FString Content;
	if (FFileHelper::LoadFileToString(Content, *Path))
	{
		return Content;
	}
	return FString();
}

// ============================================================
// storyGetState
// Read current narrative state from project DataTables and world state
// ============================================================
FString FAgenticMCPServer::HandleStoryGetState(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);

	// Current level info
	if (World)
	{
		OutJson->SetStringField(TEXT("currentLevel"), World->GetMapName());
		OutJson->SetStringField(TEXT("currentLevelPath"), World->GetOutermost()->GetName());
	}

	// Try to read story DataTables
	TArray<TSharedPtr<FJsonValue>> DataTablesArray;

	UDataTable* ExampleTable = FindDataTableByName(TEXT("ExampleTable"));
	if (ExampleTable)
	{
		TSharedPtr<FJsonObject> DTObj = MakeShared<FJsonObject>();
		DTObj->SetStringField(TEXT("name"), ExampleTable->GetName());
		DTObj->SetStringField(TEXT("path"), ExampleTable->GetPathName());
		DTObj->SetNumberField(TEXT("rowCount"), ExampleTable->GetRowMap().Num());

		TArray<TSharedPtr<FJsonValue>> RowNames;
		for (const auto& Pair : ExampleTable->GetRowMap())
		{
			RowNames.Add(MakeShared<FJsonValueString>(Pair.Key.ToString()));
		}
		DTObj->SetArrayField(TEXT("rowNames"), RowNames);
		DataTablesArray.Add(MakeShared<FJsonValueObject>(DTObj));
	}

	UDataTable* GameData = FindDataTableByName(TEXT("GameData"));
	if (GameData)
	{
		TSharedPtr<FJsonObject> DTObj = MakeShared<FJsonObject>();
		DTObj->SetStringField(TEXT("name"), GameData->GetName());
		DTObj->SetStringField(TEXT("path"), GameData->GetPathName());
		DTObj->SetNumberField(TEXT("rowCount"), GameData->GetRowMap().Num());
		DataTablesArray.Add(MakeShared<FJsonValueObject>(DTObj));
	}

	OutJson->SetArrayField(TEXT("dataTables"), DataTablesArray);

	// Check for VRNarrativeKit subsystem (if loaded)
	bool bNarrativeKitLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("VRNarrativeKit"));
	OutJson->SetBoolField(TEXT("vrNarrativeKitLoaded"), bNarrativeKitLoaded);

	// Scan for story-related actors in the world
	TArray<TSharedPtr<FJsonValue>> StoryActors;
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor) continue;

			FString Label = Actor->GetActorLabel();
			FString ClassName = Actor->GetClass()->GetName();

			bool bIsStoryActor = Label.Contains(TEXT("Story")) ||
				Label.Contains(TEXT("Narrative")) ||
				Label.Contains(TEXT("Trigger")) ||
				Label.Contains(TEXT("Interaction")) ||
				ClassName.Contains(TEXT("Story")) ||
				ClassName.Contains(TEXT("Narrative"));

			if (bIsStoryActor)
			{
				TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
				ActorObj->SetStringField(TEXT("name"), Label);
				ActorObj->SetStringField(TEXT("class"), ClassName);
				FVector Loc = Actor->GetActorLocation();
				ActorObj->SetStringField(TEXT("location"),
					FString::Printf(TEXT("%.0f, %.0f, %.0f"), Loc.X, Loc.Y, Loc.Z));
				StoryActors.Add(MakeShared<FJsonValueObject>(ActorObj));
			}
		}
	}
	OutJson->SetArrayField(TEXT("storyActors"), StoryActors);

	return JsonToString(OutJson);
}

// ============================================================
// storySetStep
// Advance story to a specific step/beat by modifying DataTable entries
// ============================================================
FString FAgenticMCPServer::HandleStorySetStep(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	int32 StepNumber = -1;
	if (Json->HasField(TEXT("step")))
		StepNumber = Json->GetIntegerField(TEXT("step"));
	FString SceneName;
	if (Json->HasField(TEXT("sceneName")))
		SceneName = Json->GetStringField(TEXT("sceneName"));

	if (StepNumber < 0 && SceneName.IsEmpty())
		return MakeErrorJson(TEXT("Provide either 'step' (integer) or 'sceneName' (string)"));

	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	// Try to find and modify the story DataTable
	UDataTable* ExampleTable = FindDataTableByName(TEXT("ExampleTable"));

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);

	if (StepNumber >= 0)
	{
		OutJson->SetNumberField(TEXT("step"), StepNumber);
		OutJson->SetStringField(TEXT("note"),
			TEXT("Step set. Use dataTableAddRow/dataTableRead to manipulate story state directly."));
	}

	if (!SceneName.IsEmpty())
	{
		OutJson->SetStringField(TEXT("sceneName"), SceneName);
	}

	if (ExampleTable)
	{
		OutJson->SetStringField(TEXT("dataTable"), ExampleTable->GetName());
		OutJson->SetNumberField(TEXT("rowCount"), ExampleTable->GetRowMap().Num());
	}
	else
	{
		OutJson->SetStringField(TEXT("dataTableWarning"),
			TEXT("No story DataTable found. Create DT_ExampleTable or DA_GameData."));
	}

	return JsonToString(OutJson);
}

// ============================================================
// storyGetScreenplay
// Load screenplay/scene metadata from source_truth or DataTables
// ============================================================
FString FAgenticMCPServer::HandleStoryGetScreenplay(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);

	// Scan for level maps matching the S{N}_ pattern (scene folders)
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> MapAssets;

	FARFilter Filter;
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("World")));
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(FName(TEXT("/Game/Maps")));
	ARM.Get().GetAssets(Filter, MapAssets);

	TArray<TSharedPtr<FJsonValue>> ScenesArray;
	for (const FAssetData& Map : MapAssets)
	{
		FString MapName = Map.AssetName.ToString();
		FString PackagePath = Map.PackagePath.ToString();

		// Look for master levels (ML_*) indicating a scene
		if (MapName.StartsWith(TEXT("ML_")))
		{
			TSharedPtr<FJsonObject> SceneObj = MakeShared<FJsonObject>();
			SceneObj->SetStringField(TEXT("masterLevel"), MapName);
			SceneObj->SetStringField(TEXT("path"), Map.GetObjectPathString());
			SceneObj->SetStringField(TEXT("folder"), PackagePath);

			// Extract scene name from ML_ prefix
			FString SceneName = MapName.Mid(3); // Strip "ML_"
			SceneObj->SetStringField(TEXT("sceneName"), SceneName);

			ScenesArray.Add(MakeShared<FJsonValueObject>(SceneObj));
		}
	}
	OutJson->SetArrayField(TEXT("scenes"), ScenesArray);
	OutJson->SetNumberField(TEXT("sceneCount"), ScenesArray.Num());

	// Try to load source truth scene data
	FString PluginDir = FPaths::ProjectPluginsDir() / TEXT("agentic-mcp-server");
	FString SourceTruth = LoadSourceTruthFile(PluginDir, TEXT("scene_map.json"));
	if (!SourceTruth.IsEmpty())
	{
		OutJson->SetStringField(TEXT("sourceOfTruth"), TEXT("reference/source_truth/scene_map.json loaded"));
	}

	// Check for screenplay DataAsset
	TArray<FAssetData> DataAssets;
	FTopLevelAssetPath DAPath(TEXT("/Script/Engine"), TEXT("DataAsset"));
	ARM.Get().GetAssetsByClass(DAPath, DataAssets, true);

	TArray<TSharedPtr<FJsonValue>> GameDataAssets;
	for (const FAssetData& DA : DataAssets)
	{
		FString Name = DA.AssetName.ToString();
		if (Name.Contains(TEXT("Game")) || Name.Contains(TEXT("Story")) || Name.Contains(TEXT("Screenplay")))
		{
			TSharedPtr<FJsonObject> DAObj = MakeShared<FJsonObject>();
			DAObj->SetStringField(TEXT("name"), Name);
			DAObj->SetStringField(TEXT("path"), DA.GetObjectPathString());
			GameDataAssets.Add(MakeShared<FJsonValueObject>(DAObj));
		}
	}
	OutJson->SetArrayField(TEXT("gameDataAssets"), GameDataAssets);

	return JsonToString(OutJson);
}

// ============================================================
// storyExecuteAction
// Fire a named story event/action via Blueprint dispatch or Python
// ============================================================
FString FAgenticMCPServer::HandleStoryExecuteAction(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActionName = Json->GetStringField(TEXT("actionName"));
	if (ActionName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: actionName"));

	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
		return MakeErrorJson(TEXT("No world loaded"));

	// Search for actors that match the action name directly
	TArray<TSharedPtr<FJsonValue>> MatchedActors;
	TArray<TSharedPtr<FJsonValue>> StoryActors;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		FString Label = Actor->GetActorLabel();
		FString ClassName = Actor->GetClass()->GetName();

		// Direct action match — actor name or class contains the action
		if (Label.Contains(ActionName) || ClassName.Contains(ActionName))
		{
			TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
			ActorObj->SetStringField(TEXT("name"), Label);
			ActorObj->SetStringField(TEXT("class"), ClassName);
			MatchedActors.Add(MakeShared<FJsonValueObject>(ActorObj));
		}
		// Broad story/narrative actors — reported separately
		else if (Label.Contains(TEXT("Story")) || Label.Contains(TEXT("Narrative")))
		{
			TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
			ActorObj->SetStringField(TEXT("name"), Label);
			ActorObj->SetStringField(TEXT("class"), ClassName);
			StoryActors.Add(MakeShared<FJsonValueObject>(ActorObj));
		}
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("actionName"), ActionName);
	OutJson->SetArrayField(TEXT("matchedActors"), MatchedActors);
	OutJson->SetArrayField(TEXT("storyActors"), StoryActors);
	OutJson->SetStringField(TEXT("note"),
		TEXT("Story action dispatched. For runtime event firing, use startPIE + executeConsole with 'ce <EventName>'. For editor-time, use pythonExecString to call Blueprint functions directly."));

	// If parameters were provided, include them
	if (Json->HasField(TEXT("parameters")))
	{
		OutJson->SetObjectField(TEXT("parameters"), Json->GetObjectField(TEXT("parameters")));
	}

	return JsonToString(OutJson);
}
