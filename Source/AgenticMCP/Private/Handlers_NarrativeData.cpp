// Handlers_NarrativeData.cpp
// Narrative DataAsset authoring tools for AgenticMCP.
// OPTIONAL: Only compiles when VRNarrativeKit plugin is present.
// Allows an LLM to read/write UNarrativeData directly via MCP tool calls.
//
// Endpoints: narrativeRead, narrativeAddChapter, narrativeAddScene,
// narrativeAddInteraction, narrativeAddNarrationCue, narrativeRemoveScene,
// narrativeRemoveChapter, narrativeClear, narrativeReorderScenes,
// narrativeUpdateScene

#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#pragma warning(pop)

#if WITH_VRNARRATIVEKIT

#include "Core/NarrativeData.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPNarrative, Log, All);

// ============================================================
// Enum parsing helpers — string to enum via StaticEnum<>
// ============================================================

static EStreamingStrategy ParseStreamingStrategy(const FString& Str)
{
	const UEnum* Enum = StaticEnum<EStreamingStrategy>();
	int64 Val = Enum->GetValueByNameString(Str);
	if (Val == INDEX_NONE)
	{
		UE_LOG(LogMCPNarrative, Warning, TEXT("Unknown StreamingStrategy '%s', defaulting to Auto"), *Str);
		return EStreamingStrategy::Auto;
	}
	return static_cast<EStreamingStrategy>(Val);
}

static ETransitionPreset ParseTransitionPreset(const FString& Str)
{
	const UEnum* Enum = StaticEnum<ETransitionPreset>();
	int64 Val = Enum->GetValueByNameString(Str);
	if (Val == INDEX_NONE)
	{
		UE_LOG(LogMCPNarrative, Warning, TEXT("Unknown TransitionPreset '%s', defaulting to FadeBlack"), *Str);
		return ETransitionPreset::FadeBlack;
	}
	return static_cast<ETransitionPreset>(Val);
}

static EVRInteractionType ParseInteractionType(const FString& Str)
{
	const UEnum* Enum = StaticEnum<EVRInteractionType>();
	int64 Val = Enum->GetValueByNameString(Str);
	if (Val == INDEX_NONE)
	{
		UE_LOG(LogMCPNarrative, Warning, TEXT("Unknown InteractionType '%s', defaulting to Gaze"), *Str);
		return EVRInteractionType::Gaze;
	}
	return static_cast<EVRInteractionType>(Val);
}

static ENarratorVoice ParseNarratorVoice(const FString& Str)
{
	const UEnum* Enum = StaticEnum<ENarratorVoice>();
	int64 Val = Enum->GetValueByNameString(Str);
	if (Val == INDEX_NONE)
	{
		UE_LOG(LogMCPNarrative, Warning, TEXT("Unknown NarratorVoice '%s', defaulting to Primary"), *Str);
		return ENarratorVoice::Primary;
	}
	return static_cast<ENarratorVoice>(Val);
}

static FString EnumToString(const UEnum* Enum, int64 Val)
{
	FString Name = Enum->GetNameStringByValue(Val);
	// Strip the enum class prefix if present (e.g. "EStreamingStrategy::")
	int32 ColonIdx;
	if (Name.FindLastChar(TEXT(':'), ColonIdx))
	{
		Name = Name.Mid(ColonIdx + 1);
	}
	return Name;
}

// ============================================================
// Asset load / save helpers
// ============================================================

static UNarrativeData* LoadNarrativeAsset(const FString& Body)
{
	FString AssetPath = TEXT("/Game/OrdinaryCourage/Data/DA_NarrativeData");

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
	{
		if (Json->HasField(TEXT("path")))
		{
			AssetPath = Json->GetStringField(TEXT("path"));
		}
	}

	UNarrativeData* Asset = LoadObject<UNarrativeData>(nullptr, *AssetPath);
	if (!Asset)
	{
		FString FullPath = AssetPath + TEXT(".") + FPaths::GetBaseFilename(AssetPath);
		Asset = LoadObject<UNarrativeData>(nullptr, *FullPath);
	}
	return Asset;
}

static bool SaveNarrativeAsset(UNarrativeData* Asset)
{
	if (!Asset) return false;

	Asset->Modify();
	Asset->MarkPackageDirty();

	UPackage* Package = Asset->GetOutermost();
	FString PackageFileName;
	if (FPackageName::DoesPackageExist(Package->GetName(), &PackageFileName))
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);
	}
	return false;
}

// ============================================================
// JSON serialization helpers — struct to JSON
// ============================================================

static TSharedRef<FJsonObject> InteractionToJson(const FInteractionRequirement& IR)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("interactionID"), IR.InteractionID.ToString());
	J->SetStringField(TEXT("type"), EnumToString(StaticEnum<EVRInteractionType>(), (int64)IR.Type));
	J->SetStringField(TEXT("description"), IR.Description.ToString());
	J->SetBoolField(TEXT("bRequired"), IR.bRequired);
	J->SetStringField(TEXT("targetActorTag"), IR.TargetActorTag.ToString());
	return J;
}

static TSharedRef<FJsonObject> NarrationCueToJson(const FNarrationCue& Cue)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("voice"), EnumToString(StaticEnum<ENarratorVoice>(), (int64)Cue.Voice));
	J->SetStringField(TEXT("characterName"), Cue.CharacterName.ToString());
	J->SetStringField(TEXT("subtitleText"), Cue.SubtitleText.ToString());
	J->SetStringField(TEXT("voiceOverAsset"), Cue.VoiceOverAsset.IsNull() ? TEXT("") : Cue.VoiceOverAsset.ToSoftObjectPath().ToString());
	J->SetNumberField(TEXT("delayAfterPrevious"), Cue.DelayAfterPrevious);
	J->SetStringField(TEXT("waitForInteractionID"), Cue.WaitForInteractionID.ToString());
	return J;
}

static TSharedRef<FJsonObject> TransitionToJson(const FSceneTransition& T)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("preset"), EnumToString(StaticEnum<ETransitionPreset>(), (int64)T.Preset));
	J->SetNumberField(TEXT("duration"), T.Duration);
	return J;
}

static TSharedRef<FJsonObject> SceneToJson(const FNarrativeSceneData& S)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("sceneID"), S.SceneID.ToString());
	J->SetStringField(TEXT("sceneTitle"), S.SceneTitle.ToString());
	J->SetStringField(TEXT("level"), S.Level.IsNull() ? TEXT("") : S.Level.ToSoftObjectPath().ToString());
	J->SetStringField(TEXT("baseEnvironmentTag"), S.BaseEnvironmentTag.ToString());
	J->SetStringField(TEXT("streamingStrategy"), EnumToString(StaticEnum<EStreamingStrategy>(), (int64)S.StreamingStrategy));
	J->SetBoolField(TEXT("bPreloadDuringPrevious"), S.bPreloadDuringPrevious);
	J->SetBoolField(TEXT("bAdditiveLoad"), S.bAdditiveLoad);
	J->SetNumberField(TEXT("AllowedLocomotionModes"), S.AllowedLocomotionModes);
	J->SetNumberField(TEXT("AllowedTeleportModes"), S.AllowedTeleportModes);
	J->SetBoolField(TEXT("bForceLocomotionMode"), S.bForceLocomotionMode);

	TSharedRef<FJsonObject> Loc = MakeShared<FJsonObject>();
	FVector L = S.PlayerStartTransform.GetLocation();
	Loc->SetNumberField(TEXT("x"), L.X); Loc->SetNumberField(TEXT("y"), L.Y); Loc->SetNumberField(TEXT("z"), L.Z);
	J->SetObjectField(TEXT("playerStartLocation"), Loc);

	FRotator R = S.PlayerStartTransform.Rotator();
	TSharedRef<FJsonObject> Rot = MakeShared<FJsonObject>();
	Rot->SetNumberField(TEXT("pitch"), R.Pitch); Rot->SetNumberField(TEXT("yaw"), R.Yaw); Rot->SetNumberField(TEXT("roll"), R.Roll);
	J->SetObjectField(TEXT("playerStartRotation"), Rot);

	J->SetObjectField(TEXT("transitionIn"), TransitionToJson(S.TransitionIn));
	J->SetObjectField(TEXT("transitionOut"), TransitionToJson(S.TransitionOut));

	TArray<TSharedPtr<FJsonValue>> InteractionsArr;
	for (const FInteractionRequirement& IR : S.Interactions)
	{
		InteractionsArr.Add(MakeShared<FJsonValueObject>(InteractionToJson(IR)));
	}
	J->SetArrayField(TEXT("interactions"), InteractionsArr);

	TArray<TSharedPtr<FJsonValue>> CuesArr;
	for (const FNarrationCue& Cue : S.NarrationCues)
	{
		CuesArr.Add(MakeShared<FJsonValueObject>(NarrationCueToJson(Cue)));
	}
	J->SetArrayField(TEXT("narrationCues"), CuesArr);

	return J;
}

static TSharedRef<FJsonObject> ChapterToJson(const FNarrativeChapterData& Ch)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("chapterID"), Ch.ChapterID.ToString());
	J->SetStringField(TEXT("chapterTitle"), Ch.ChapterTitle.ToString());

	TArray<TSharedPtr<FJsonValue>> ScenesArr;
	for (const FNarrativeSceneData& S : Ch.Scenes)
	{
		ScenesArr.Add(MakeShared<FJsonValueObject>(SceneToJson(S)));
	}
	J->SetArrayField(TEXT("scenes"), ScenesArr);
	return J;
}

// ============================================================
// Helper: find a scene by ID across all chapters (returns pointers)
// ============================================================

static FNarrativeSceneData* FindSceneMutable(UNarrativeData* Asset, const FName& SceneID, FNarrativeChapterData** OutChapter = nullptr)
{
	for (FNarrativeChapterData& Ch : Asset->Chapters)
	{
		for (FNarrativeSceneData& S : Ch.Scenes)
		{
			if (S.SceneID == SceneID)
			{
				if (OutChapter) *OutChapter = &Ch;
				return &S;
			}
		}
	}
	return nullptr;
}

// ============================================================
// Handler Implementations
// ============================================================

FString FAgenticMCPServer::HandleNarrativeRead(const FString& Body)
{
	UNarrativeData* Asset = LoadNarrativeAsset(Body);
	if (!Asset)
	{
		return MakeErrorJson(TEXT("NarrativeData asset not found. Create it first at /Game/OrdinaryCourage/Data/DA_NarrativeData or specify a custom 'path'."));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("experienceTitle"), Asset->ExperienceTitle.ToString());

	TArray<TSharedPtr<FJsonValue>> ChaptersArr;
	for (const FNarrativeChapterData& Ch : Asset->Chapters)
	{
		ChaptersArr.Add(MakeShared<FJsonValueObject>(ChapterToJson(Ch)));
	}
	Result->SetArrayField(TEXT("chapters"), ChaptersArr);

	int32 TotalScenes = 0;
	for (const FNarrativeChapterData& Ch : Asset->Chapters)
	{
		TotalScenes += Ch.Scenes.Num();
	}
	Result->SetNumberField(TEXT("totalScenes"), TotalScenes);
	Result->SetNumberField(TEXT("totalChapters"), Asset->Chapters.Num());

	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleNarrativeAddChapter(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString ChapterID = Json->GetStringField(TEXT("chapterID"));
	FString ChapterTitle = Json->GetStringField(TEXT("chapterTitle"));

	if (ChapterID.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: 'chapterID'"));
	}

	UNarrativeData* Asset = LoadNarrativeAsset(Body);
	if (!Asset)
	{
		return MakeErrorJson(TEXT("NarrativeData asset not found."));
	}

	FName ChapterName = FName(*ChapterID);
	for (const FNarrativeChapterData& Ch : Asset->Chapters)
	{
		if (Ch.ChapterID == ChapterName)
		{
			return MakeErrorJson(FString::Printf(TEXT("Chapter '%s' already exists."), *ChapterID));
		}
	}

	FNarrativeChapterData NewChapter;
	NewChapter.ChapterID = ChapterName;
	NewChapter.ChapterTitle = FText::FromString(ChapterTitle);
	Asset->Chapters.Add(NewChapter);

	bool bSaved = SaveNarrativeAsset(Asset);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("success"));
	Result->SetStringField(TEXT("chapterID"), ChapterID);
	Result->SetNumberField(TEXT("totalChapters"), Asset->Chapters.Num());
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleNarrativeAddScene(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString ChapterID = Json->GetStringField(TEXT("chapterID"));
	FString SceneID = Json->GetStringField(TEXT("sceneID"));

	if (ChapterID.IsEmpty() || SceneID.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required fields: 'chapterID' and 'sceneID'"));
	}

	UNarrativeData* Asset = LoadNarrativeAsset(Body);
	if (!Asset) return MakeErrorJson(TEXT("NarrativeData asset not found."));

	// Check for duplicate scene ID
	if (FindSceneMutable(Asset, FName(*SceneID)))
	{
		return MakeErrorJson(FString::Printf(TEXT("Scene '%s' already exists."), *SceneID));
	}

	// Find target chapter
	FNarrativeChapterData* TargetChapter = nullptr;
	for (FNarrativeChapterData& Ch : Asset->Chapters)
	{
		if (Ch.ChapterID == FName(*ChapterID))
		{
			TargetChapter = &Ch;
			break;
		}
	}
	if (!TargetChapter)
	{
		return MakeErrorJson(FString::Printf(TEXT("Chapter '%s' not found."), *ChapterID));
	}

	FNarrativeSceneData Scene;
	Scene.SceneID = FName(*SceneID);
	Scene.SceneTitle = FText::FromString(Json->GetStringField(TEXT("sceneTitle")));

	FString LevelPath = Json->GetStringField(TEXT("level"));
	if (!LevelPath.IsEmpty())
	{
		FString FullLevelPath = LevelPath + TEXT(".") + FPaths::GetBaseFilename(LevelPath);
		Scene.Level = TSoftObjectPtr<UWorld>(FSoftObjectPath(FullLevelPath));
	}

	Scene.BaseEnvironmentTag = FName(*Json->GetStringField(TEXT("baseEnvironmentTag")));
	if (Json->HasField(TEXT("streamingStrategy")))
		Scene.StreamingStrategy = ParseStreamingStrategy(Json->GetStringField(TEXT("streamingStrategy")));

	if (Json->HasField(TEXT("bPreloadDuringPrevious")))
		Scene.bPreloadDuringPrevious = Json->GetBoolField(TEXT("bPreloadDuringPrevious"));
	if (Json->HasField(TEXT("bAdditiveLoad")))
		Scene.bAdditiveLoad = Json->GetBoolField(TEXT("bAdditiveLoad"));
	if (Json->HasField(TEXT("AllowedLocomotionModes")))
		Scene.AllowedLocomotionModes = Json->GetIntegerField(TEXT("AllowedLocomotionModes"));
	if (Json->HasField(TEXT("AllowedTeleportModes")))
		Scene.AllowedTeleportModes = Json->GetIntegerField(TEXT("AllowedTeleportModes"));
	if (Json->HasField(TEXT("bForceLocomotionMode")))
		Scene.bForceLocomotionMode = Json->GetBoolField(TEXT("bForceLocomotionMode"));

	// Player start transform
	const TSharedPtr<FJsonObject>* LocObj = nullptr;
	if (Json->TryGetObjectField(TEXT("playerStartLocation"), LocObj))
	{
		FVector Loc((*LocObj)->GetNumberField(TEXT("x")), (*LocObj)->GetNumberField(TEXT("y")), (*LocObj)->GetNumberField(TEXT("z")));
		Scene.PlayerStartTransform.SetLocation(Loc);
	}
	const TSharedPtr<FJsonObject>* RotObj = nullptr;
	if (Json->TryGetObjectField(TEXT("playerStartRotation"), RotObj))
	{
		FRotator Rot((*RotObj)->GetNumberField(TEXT("pitch")), (*RotObj)->GetNumberField(TEXT("yaw")), (*RotObj)->GetNumberField(TEXT("roll")));
		Scene.PlayerStartTransform.SetRotation(Rot.Quaternion());
	}

	// Transitions
	if (Json->HasField(TEXT("transitionInPreset")))
		Scene.TransitionIn.Preset = ParseTransitionPreset(Json->GetStringField(TEXT("transitionInPreset")));
	if (Json->HasField(TEXT("transitionInDuration")))
		Scene.TransitionIn.Duration = Json->GetNumberField(TEXT("transitionInDuration"));
	if (Json->HasField(TEXT("transitionOutPreset")))
		Scene.TransitionOut.Preset = ParseTransitionPreset(Json->GetStringField(TEXT("transitionOutPreset")));
	if (Json->HasField(TEXT("transitionOutDuration")))
		Scene.TransitionOut.Duration = Json->GetNumberField(TEXT("transitionOutDuration"));

	TargetChapter->Scenes.Add(Scene);
	bool bSaved = SaveNarrativeAsset(Asset);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("success"));
	Result->SetStringField(TEXT("sceneID"), SceneID);
	Result->SetStringField(TEXT("chapterID"), ChapterID);
	Result->SetNumberField(TEXT("scenesInChapter"), TargetChapter->Scenes.Num());
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleNarrativeAddInteraction(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SceneID = Json->GetStringField(TEXT("sceneID"));
	FString InteractionID = Json->GetStringField(TEXT("interactionID"));

	if (SceneID.IsEmpty() || InteractionID.IsEmpty())
		return MakeErrorJson(TEXT("Missing required fields: 'sceneID' and 'interactionID'"));

	UNarrativeData* Asset = LoadNarrativeAsset(Body);
	if (!Asset) return MakeErrorJson(TEXT("NarrativeData asset not found."));

	FNarrativeSceneData* Scene = FindSceneMutable(Asset, FName(*SceneID));
	if (!Scene)
		return MakeErrorJson(FString::Printf(TEXT("Scene '%s' not found."), *SceneID));

	// Check for duplicate interaction ID
	FName InteractionName = FName(*InteractionID);
	for (const FInteractionRequirement& Existing : Scene->Interactions)
	{
		if (Existing.InteractionID == InteractionName)
		{
			return MakeErrorJson(FString::Printf(TEXT("Interaction '%s' already exists in scene '%s'."), *InteractionID, *SceneID));
		}
	}

	FInteractionRequirement IR;
	IR.InteractionID = FName(*InteractionID);
	IR.Type = ParseInteractionType(Json->GetStringField(TEXT("type")));
	IR.Description = FText::FromString(Json->GetStringField(TEXT("description")));
	if (Json->HasField(TEXT("bRequired")))
		IR.bRequired = Json->GetBoolField(TEXT("bRequired"));
	IR.TargetActorTag = FName(*Json->GetStringField(TEXT("targetActorTag")));

	Scene->Interactions.Add(IR);
	bool bSaved = SaveNarrativeAsset(Asset);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("success"));
	Result->SetStringField(TEXT("interactionID"), InteractionID);
	Result->SetStringField(TEXT("sceneID"), SceneID);
	Result->SetNumberField(TEXT("totalInteractions"), Scene->Interactions.Num());
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleNarrativeAddNarrationCue(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SceneID = Json->GetStringField(TEXT("sceneID"));
	if (SceneID.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: 'sceneID'"));

	UNarrativeData* Asset = LoadNarrativeAsset(Body);
	if (!Asset) return MakeErrorJson(TEXT("NarrativeData asset not found."));

	FNarrativeSceneData* Scene = FindSceneMutable(Asset, FName(*SceneID));
	if (!Scene)
		return MakeErrorJson(FString::Printf(TEXT("Scene '%s' not found."), *SceneID));

	FNarrationCue Cue;
	Cue.Voice = ParseNarratorVoice(Json->GetStringField(TEXT("voice")));

	FString CharName = Json->GetStringField(TEXT("characterName"));
	if (!CharName.IsEmpty()) Cue.CharacterName = FName(*CharName);

	Cue.SubtitleText = FText::FromString(Json->GetStringField(TEXT("subtitleText")));

	FString VOPath = Json->GetStringField(TEXT("voiceOverAsset"));
	if (!VOPath.IsEmpty())
	{
		FString FullVOPath = VOPath + TEXT(".") + FPaths::GetBaseFilename(VOPath);
		Cue.VoiceOverAsset = TSoftObjectPtr<USoundBase>(FSoftObjectPath(FullVOPath));
	}

	if (Json->HasField(TEXT("delayAfterPrevious")))
		Cue.DelayAfterPrevious = Json->GetNumberField(TEXT("delayAfterPrevious"));

	FString WaitID = Json->GetStringField(TEXT("waitForInteractionID"));
	if (!WaitID.IsEmpty()) Cue.WaitForInteractionID = FName(*WaitID);

	Scene->NarrationCues.Add(Cue);
	bool bSaved = SaveNarrativeAsset(Asset);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("success"));
	Result->SetStringField(TEXT("sceneID"), SceneID);
	Result->SetNumberField(TEXT("totalCues"), Scene->NarrationCues.Num());
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleNarrativeRemoveScene(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SceneID = Json->GetStringField(TEXT("sceneID"));
	if (SceneID.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: 'sceneID'"));

	UNarrativeData* Asset = LoadNarrativeAsset(Body);
	if (!Asset) return MakeErrorJson(TEXT("NarrativeData asset not found."));

	FName SceneName = FName(*SceneID);
	bool bFound = false;
	for (FNarrativeChapterData& Ch : Asset->Chapters)
	{
		int32 Idx = Ch.Scenes.IndexOfByPredicate([SceneName](const FNarrativeSceneData& S) { return S.SceneID == SceneName; });
		if (Idx != INDEX_NONE)
		{
			Ch.Scenes.RemoveAt(Idx);
			bFound = true;
			break;
		}
	}

	if (!bFound)
		return MakeErrorJson(FString::Printf(TEXT("Scene '%s' not found in any chapter."), *SceneID));

	bool bSaved = SaveNarrativeAsset(Asset);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("success"));
	Result->SetStringField(TEXT("removedSceneID"), SceneID);
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleNarrativeRemoveChapter(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ChapterID = Json->GetStringField(TEXT("chapterID"));
	if (ChapterID.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: 'chapterID'"));

	UNarrativeData* Asset = LoadNarrativeAsset(Body);
	if (!Asset) return MakeErrorJson(TEXT("NarrativeData asset not found."));

	FName ChapterName = FName(*ChapterID);
	int32 Idx = Asset->Chapters.IndexOfByPredicate([ChapterName](const FNarrativeChapterData& Ch) { return Ch.ChapterID == ChapterName; });

	if (Idx == INDEX_NONE)
		return MakeErrorJson(FString::Printf(TEXT("Chapter '%s' not found."), *ChapterID));

	int32 RemovedScenes = Asset->Chapters[Idx].Scenes.Num();
	Asset->Chapters.RemoveAt(Idx);
	bool bSaved = SaveNarrativeAsset(Asset);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("success"));
	Result->SetStringField(TEXT("removedChapterID"), ChapterID);
	Result->SetNumberField(TEXT("removedScenes"), RemovedScenes);
	Result->SetNumberField(TEXT("remainingChapters"), Asset->Chapters.Num());
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleNarrativeClear(const FString& Body)
{
	UNarrativeData* Asset = LoadNarrativeAsset(Body);
	if (!Asset) return MakeErrorJson(TEXT("NarrativeData asset not found."));

	int32 OldChapters = Asset->Chapters.Num();
	int32 OldScenes = 0;
	for (const FNarrativeChapterData& Ch : Asset->Chapters)
		OldScenes += Ch.Scenes.Num();

	Asset->Chapters.Empty();
	bool bSaved = SaveNarrativeAsset(Asset);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("success"));
	Result->SetNumberField(TEXT("clearedChapters"), OldChapters);
	Result->SetNumberField(TEXT("clearedScenes"), OldScenes);
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleNarrativeReorderScenes(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ChapterID = Json->GetStringField(TEXT("chapterID"));
	if (ChapterID.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: 'chapterID'"));

	UNarrativeData* Asset = LoadNarrativeAsset(Body);
	if (!Asset) return MakeErrorJson(TEXT("NarrativeData asset not found."));

	FNarrativeChapterData* TargetChapter = nullptr;
	for (FNarrativeChapterData& Ch : Asset->Chapters)
	{
		if (Ch.ChapterID == FName(*ChapterID))
		{
			TargetChapter = &Ch;
			break;
		}
	}
	if (!TargetChapter)
		return MakeErrorJson(FString::Printf(TEXT("Chapter '%s' not found."), *ChapterID));

	const TArray<TSharedPtr<FJsonValue>>* OrderArray;
	if (!Json->TryGetArrayField(TEXT("sceneOrder"), OrderArray))
		return MakeErrorJson(TEXT("Missing required field: 'sceneOrder' (array of scene ID strings)"));

	// Validate: sceneOrder must list ALL scenes in the chapter (prevents data loss)
	if (OrderArray->Num() != TargetChapter->Scenes.Num())
	{
		return MakeErrorJson(FString::Printf(TEXT("sceneOrder has %d entries but chapter '%s' has %d scenes. All scenes must be listed to prevent data loss."),
			OrderArray->Num(), *ChapterID, TargetChapter->Scenes.Num()));
	}

	TSet<FName> SeenIDs;
	TArray<FNarrativeSceneData> Reordered;
	for (const TSharedPtr<FJsonValue>& Val : *OrderArray)
	{
		FString SID = Val->AsString();
		FName SName = FName(*SID);

		// Reject duplicate scene IDs in the order array
		if (SeenIDs.Contains(SName))
			return MakeErrorJson(FString::Printf(TEXT("Duplicate scene ID '%s' in sceneOrder array."), *SID));
		SeenIDs.Add(SName);

		int32 Found = TargetChapter->Scenes.IndexOfByPredicate([SName](const FNarrativeSceneData& S) { return S.SceneID == SName; });
		if (Found == INDEX_NONE)
			return MakeErrorJson(FString::Printf(TEXT("Scene '%s' not found in chapter '%s'."), *SID, *ChapterID));
		Reordered.Add(TargetChapter->Scenes[Found]);
	}

	TargetChapter->Scenes = MoveTemp(Reordered);
	bool bSaved = SaveNarrativeAsset(Asset);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("success"));
	Result->SetStringField(TEXT("chapterID"), ChapterID);
	Result->SetNumberField(TEXT("scenesReordered"), TargetChapter->Scenes.Num());
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

FString FAgenticMCPServer::HandleNarrativeUpdateScene(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SceneID = Json->GetStringField(TEXT("sceneID"));
	if (SceneID.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: 'sceneID'"));

	UNarrativeData* Asset = LoadNarrativeAsset(Body);
	if (!Asset) return MakeErrorJson(TEXT("NarrativeData asset not found."));

	FNarrativeSceneData* Scene = FindSceneMutable(Asset, FName(*SceneID));
	if (!Scene)
		return MakeErrorJson(FString::Printf(TEXT("Scene '%s' not found."), *SceneID));

	TArray<FString> UpdatedFields;

	if (Json->HasField(TEXT("sceneTitle")))
	{
		Scene->SceneTitle = FText::FromString(Json->GetStringField(TEXT("sceneTitle")));
		UpdatedFields.Add(TEXT("sceneTitle"));
	}
	if (Json->HasField(TEXT("level")))
	{
		FString LP = Json->GetStringField(TEXT("level"));
		if (!LP.IsEmpty())
		{
			FString FLP = LP + TEXT(".") + FPaths::GetBaseFilename(LP);
			Scene->Level = TSoftObjectPtr<UWorld>(FSoftObjectPath(FLP));
		}
		else
		{
			Scene->Level = TSoftObjectPtr<UWorld>();
		}
		UpdatedFields.Add(TEXT("level"));
	}
	if (Json->HasField(TEXT("baseEnvironmentTag")))
	{
		Scene->BaseEnvironmentTag = FName(*Json->GetStringField(TEXT("baseEnvironmentTag")));
		UpdatedFields.Add(TEXT("baseEnvironmentTag"));
	}
	if (Json->HasField(TEXT("streamingStrategy")))
	{
		Scene->StreamingStrategy = ParseStreamingStrategy(Json->GetStringField(TEXT("streamingStrategy")));
		UpdatedFields.Add(TEXT("streamingStrategy"));
	}
	if (Json->HasField(TEXT("bPreloadDuringPrevious")))
	{
		Scene->bPreloadDuringPrevious = Json->GetBoolField(TEXT("bPreloadDuringPrevious"));
		UpdatedFields.Add(TEXT("bPreloadDuringPrevious"));
	}
	if (Json->HasField(TEXT("AllowedLocomotionModes")))
	{
		Scene->AllowedLocomotionModes = Json->GetIntegerField(TEXT("AllowedLocomotionModes"));
		UpdatedFields.Add(TEXT("AllowedLocomotionModes"));
	}
	if (Json->HasField(TEXT("AllowedTeleportModes")))
	{
		Scene->AllowedTeleportModes = Json->GetIntegerField(TEXT("AllowedTeleportModes"));
		UpdatedFields.Add(TEXT("AllowedTeleportModes"));
	}
	if (Json->HasField(TEXT("bForceLocomotionMode")))
	{
		Scene->bForceLocomotionMode = Json->GetBoolField(TEXT("bForceLocomotionMode"));
		UpdatedFields.Add(TEXT("bForceLocomotionMode"));
	}
	if (Json->HasField(TEXT("transitionInPreset")))
	{
		Scene->TransitionIn.Preset = ParseTransitionPreset(Json->GetStringField(TEXT("transitionInPreset")));
		UpdatedFields.Add(TEXT("transitionInPreset"));
	}
	if (Json->HasField(TEXT("transitionInDuration")))
	{
		Scene->TransitionIn.Duration = Json->GetNumberField(TEXT("transitionInDuration"));
		UpdatedFields.Add(TEXT("transitionInDuration"));
	}
	if (Json->HasField(TEXT("transitionOutPreset")))
	{
		Scene->TransitionOut.Preset = ParseTransitionPreset(Json->GetStringField(TEXT("transitionOutPreset")));
		UpdatedFields.Add(TEXT("transitionOutPreset"));
	}
	if (Json->HasField(TEXT("transitionOutDuration")))
	{
		Scene->TransitionOut.Duration = Json->GetNumberField(TEXT("transitionOutDuration"));
		UpdatedFields.Add(TEXT("transitionOutDuration"));
	}

	const TSharedPtr<FJsonObject>* LocObj = nullptr;
	if (Json->TryGetObjectField(TEXT("playerStartLocation"), LocObj))
	{
		FVector Loc((*LocObj)->GetNumberField(TEXT("x")), (*LocObj)->GetNumberField(TEXT("y")), (*LocObj)->GetNumberField(TEXT("z")));
		Scene->PlayerStartTransform.SetLocation(Loc);
		UpdatedFields.Add(TEXT("playerStartLocation"));
	}
	const TSharedPtr<FJsonObject>* RotObj = nullptr;
	if (Json->TryGetObjectField(TEXT("playerStartRotation"), RotObj))
	{
		FRotator Rot((*RotObj)->GetNumberField(TEXT("pitch")), (*RotObj)->GetNumberField(TEXT("yaw")), (*RotObj)->GetNumberField(TEXT("roll")));
		Scene->PlayerStartTransform.SetRotation(Rot.Quaternion());
		UpdatedFields.Add(TEXT("playerStartRotation"));
	}

	bool bSaved = SaveNarrativeAsset(Asset);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("success"));
	Result->SetStringField(TEXT("sceneID"), SceneID);
	Result->SetNumberField(TEXT("fieldsUpdated"), UpdatedFields.Num());

	TArray<TSharedPtr<FJsonValue>> FieldsArr;
	for (const FString& F : UpdatedFields)
	{
		FieldsArr.Add(MakeShared<FJsonValueString>(F));
	}
	Result->SetArrayField(TEXT("updatedFields"), FieldsArr);
	Result->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(Result);
}

#endif // WITH_VRNARRATIVEKIT
