// Handlers_SequencerEdit.cpp
// Level Sequence editing handlers for AgenticMCP.
// Extends the existing read-only Sequences handlers with creation and editing.
//
// Endpoints:
//   sequencerCreate       - Create a new LevelSequence asset
//   sequencerAddTrack     - Add a binding track (actor) to a sequence
//   sequencerAddSection   - Add a section (transform, visibility, etc.) to a track
//   sequencerSetKeyframe  - Set a keyframe value at a specific time
//   sequencerGetTracks    - Get all tracks and sections in a sequence
//   sequencerSetPlayRange - Set the playback range of a sequence

#include "AgenticMCPServer.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "MovieSceneFolder.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneVisibilityTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/LevelSequenceFactoryNew.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

static UWorld* GetEditorWorld_Seq()
{
	if (GEditor) return GEditor->GetEditorWorldContext().World();
	return nullptr;
}

static ULevelSequence* FindSequenceByName(const FString& Name)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> SeqAssets;
	AR.GetAssetsByClass(ULevelSequence::StaticClass()->GetClassPathName(), SeqAssets, true);

	for (const FAssetData& Asset : SeqAssets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			return Cast<ULevelSequence>(Asset.GetAsset());
		}
	}
	return nullptr;
}

// ============================================================
// sequencerCreate - Create a new LevelSequence asset
// ============================================================
FString FAgenticMCPServer::HandleSequencerCreate(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	if (Name.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: name"));

	FString Path = Json->HasField(TEXT("path")) ? Json->GetStringField(TEXT("path")) : TEXT("/Game/Cinematics");

	FString FullPath = FString::Printf(TEXT("%s/%s"), *Path, *Name);

	// Check if it already exists
	if (FindSequenceByName(Name))
		return MakeErrorJson(FString::Printf(TEXT("LevelSequence '%s' already exists"), *Name));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	ULevelSequenceFactoryNew* Factory = NewObject<ULevelSequenceFactoryNew>();
	UObject* NewAsset = AssetTools.CreateAsset(Name, Path, ULevelSequence::StaticClass(), Factory);

	if (!NewAsset)
		return MakeErrorJson(TEXT("Failed to create LevelSequence asset"));

	ULevelSequence* Seq = Cast<ULevelSequence>(NewAsset);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), Seq->GetName());
	Result->SetStringField(TEXT("path"), Seq->GetPathName());
	return JsonToString(Result);
}

// ============================================================
// sequencerAddTrack - Add an actor binding track
// ============================================================
FString FAgenticMCPServer::HandleSequencerAddTrack(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SeqName = Json->GetStringField(TEXT("sequenceName"));
	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString TrackType = Json->HasField(TEXT("trackType")) ? Json->GetStringField(TEXT("trackType")) : TEXT("Transform");

	if (SeqName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: sequenceName"));
	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName"));

	ULevelSequence* Seq = FindSequenceByName(SeqName);
	if (!Seq) return MakeErrorJson(FString::Printf(TEXT("LevelSequence not found: %s"), *SeqName));

	UWorld* World = GetEditorWorld_Seq();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	// Find the actor
	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorName || (*It)->GetName() == ActorName)
		{
			Actor = *It;
			break;
		}
	}
	if (!Actor) return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	UMovieScene* MovieScene = Seq->GetMovieScene();
	if (!MovieScene) return MakeErrorJson(TEXT("Sequence has no MovieScene"));

	// Create or find possessable binding
	FGuid BindingGuid;
	bool bFound = false;
	for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
	{
		const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(i);
		if (Possessable.GetName() == Actor->GetActorLabel() || Possessable.GetName() == Actor->GetName())
		{
			BindingGuid = Possessable.GetGuid();
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		BindingGuid = MovieScene->AddPossessable(Actor->GetActorLabel(), Actor->GetClass());
		Seq->BindPossessableObject(BindingGuid, *Actor, World);
	}

	// Add the track
	UMovieSceneTrack* NewTrack = nullptr;
	if (TrackType == TEXT("Transform") || TrackType == TEXT("3DTransform"))
	{
		NewTrack = MovieScene->AddTrack<UMovieScene3DTransformTrack>(BindingGuid);
	}
	else if (TrackType == TEXT("Bool") || TrackType == TEXT("Visibility"))
	{
		NewTrack = MovieScene->AddTrack<UMovieSceneVisibilityTrack>(BindingGuid);
	}
	else if (TrackType == TEXT("Float"))
	{
		NewTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(BindingGuid);
	}
	else
	{
		return MakeErrorJson(FString::Printf(TEXT("Unsupported track type: %s. Supported: Transform, Bool, Visibility, Float"), *TrackType));
	}

	if (!NewTrack)
		return MakeErrorJson(TEXT("Failed to add track (may already exist)"));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("sequenceName"), SeqName);
	Result->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("trackType"), TrackType);
	Result->SetStringField(TEXT("bindingGuid"), BindingGuid.ToString());
	return JsonToString(Result);
}

// ============================================================
// sequencerGetTracks - Get all tracks and sections in a sequence
// ============================================================
FString FAgenticMCPServer::HandleSequencerGetTracks(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SeqName = Json->GetStringField(TEXT("sequenceName"));
	if (SeqName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: sequenceName"));

	ULevelSequence* Seq = FindSequenceByName(SeqName);
	if (!Seq) return MakeErrorJson(FString::Printf(TEXT("LevelSequence not found: %s"), *SeqName));

	UMovieScene* MovieScene = Seq->GetMovieScene();
	if (!MovieScene) return MakeErrorJson(TEXT("Sequence has no MovieScene"));

	TArray<TSharedPtr<FJsonValue>> BindingArray;

	// Possessable bindings
	for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
	{
		const FMovieScenePossessable& Poss = MovieScene->GetPossessable(i);
		FMovieSceneBinding* Binding = MovieScene->FindBinding(Poss.GetGuid());
		if (!Binding) continue;

		TSharedRef<FJsonObject> BindJson = MakeShared<FJsonObject>();
		BindJson->SetStringField(TEXT("name"), Poss.GetName());
		BindJson->SetStringField(TEXT("guid"), Poss.GetGuid().ToString());
		BindJson->SetStringField(TEXT("type"), TEXT("Possessable"));

		TArray<TSharedPtr<FJsonValue>> TrackArr;
		for (UMovieSceneTrack* Track : Binding->GetTracks())
		{
			if (!Track) continue;
			TSharedRef<FJsonObject> TrackJson = MakeShared<FJsonObject>();
			TrackJson->SetStringField(TEXT("name"), Track->GetTrackName().ToString());
			TrackJson->SetStringField(TEXT("class"), Track->GetClass()->GetName());
			TrackJson->SetNumberField(TEXT("sectionCount"), Track->GetAllSections().Num());

			TArray<TSharedPtr<FJsonValue>> SecArr;
			for (UMovieSceneSection* Section : Track->GetAllSections())
			{
				if (!Section) continue;
				TSharedRef<FJsonObject> SecJson = MakeShared<FJsonObject>();
				SecJson->SetStringField(TEXT("class"), Section->GetClass()->GetName());

				TRange<FFrameNumber> Range = Section->GetRange();
				if (Range.HasLowerBound())
					SecJson->SetNumberField(TEXT("startFrame"), Range.GetLowerBoundValue().Value);
				if (Range.HasUpperBound())
					SecJson->SetNumberField(TEXT("endFrame"), Range.GetUpperBoundValue().Value);

				SecArr.Add(MakeShared<FJsonValueObject>(SecJson));
			}
			TrackJson->SetArrayField(TEXT("sections"), SecArr);
			TrackArr.Add(MakeShared<FJsonValueObject>(TrackJson));
		}
		BindJson->SetArrayField(TEXT("tracks"), TrackArr);
		BindingArray.Add(MakeShared<FJsonValueObject>(BindJson));
	}

	// Master tracks
	TArray<TSharedPtr<FJsonValue>> MasterTrackArr;
	for (UMovieSceneTrack* Track : MovieScene->GetMasterTracks())
	{
		if (!Track) continue;
		TSharedRef<FJsonObject> TrackJson = MakeShared<FJsonObject>();
		TrackJson->SetStringField(TEXT("name"), Track->GetTrackName().ToString());
		TrackJson->SetStringField(TEXT("class"), Track->GetClass()->GetName());
		TrackJson->SetNumberField(TEXT("sectionCount"), Track->GetAllSections().Num());
		MasterTrackArr.Add(MakeShared<FJsonValueObject>(TrackJson));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("sequenceName"), SeqName);
	Result->SetNumberField(TEXT("bindingCount"), BindingArray.Num());
	Result->SetArrayField(TEXT("bindings"), BindingArray);
	Result->SetNumberField(TEXT("masterTrackCount"), MasterTrackArr.Num());
	Result->SetArrayField(TEXT("masterTracks"), MasterTrackArr);

	// Playback range
	TRange<FFrameNumber> PlayRange = MovieScene->GetPlaybackRange();
	if (PlayRange.HasLowerBound())
		Result->SetNumberField(TEXT("playbackStartFrame"), PlayRange.GetLowerBoundValue().Value);
	if (PlayRange.HasUpperBound())
		Result->SetNumberField(TEXT("playbackEndFrame"), PlayRange.GetUpperBoundValue().Value);
	Result->SetNumberField(TEXT("tickResolution"), MovieScene->GetTickResolution().Numerator);

	return JsonToString(Result);
}

// ============================================================
// sequencerSetPlayRange - Set the playback range
// ============================================================
FString FAgenticMCPServer::HandleSequencerSetPlayRange(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SeqName = Json->GetStringField(TEXT("sequenceName"));
	if (SeqName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: sequenceName"));
	if (!Json->HasField(TEXT("startFrame"))) return MakeErrorJson(TEXT("Missing required field: startFrame"));
	if (!Json->HasField(TEXT("endFrame"))) return MakeErrorJson(TEXT("Missing required field: endFrame"));

	int32 StartFrame = (int32)Json->GetNumberField(TEXT("startFrame"));
	int32 EndFrame = (int32)Json->GetNumberField(TEXT("endFrame"));

	ULevelSequence* Seq = FindSequenceByName(SeqName);
	if (!Seq) return MakeErrorJson(FString::Printf(TEXT("LevelSequence not found: %s"), *SeqName));

	UMovieScene* MovieScene = Seq->GetMovieScene();
	if (!MovieScene) return MakeErrorJson(TEXT("Sequence has no MovieScene"));

	MovieScene->SetPlaybackRange(FFrameNumber(StartFrame), (EndFrame - StartFrame));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("sequenceName"), SeqName);
	Result->SetNumberField(TEXT("startFrame"), StartFrame);
	Result->SetNumberField(TEXT("endFrame"), EndFrame);
	return JsonToString(Result);
}
