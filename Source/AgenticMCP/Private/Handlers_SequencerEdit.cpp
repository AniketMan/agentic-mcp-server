// Handlers_SequencerEdit.cpp
// Level Sequence editing handlers for AgenticMCP.
// Full mutation support: create, tracks, sections, keyframes, camera cuts, render.
//
// Endpoints:
//   sequencerCreate        - Create a new LevelSequence asset
//   sequencerAddTrack      - Add a binding track (actor) to a sequence
//   sequencerAddSection    - Add a section to a track
//   sequencerSetKeyframe   - Set a keyframe value at a specific frame
//   sequencerDeleteSection - Remove a section from a track
//   sequencerBindActor     - Bind an actor to a sequence as possessable
//   sequencerAddCameraCut  - Add a camera cut track with camera binding
//   sequencerGetTracks     - Get all tracks and sections in a sequence
//   sequencerSetPlayRange  - Set the playback range of a sequence
//   sequencerRender        - Render sequence to image sequence via Movie Pipeline

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
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneAudioSection.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Camera/CameraActor.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/LevelSequenceFactoryNew.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"

// Movie Render Pipeline (for sequencerRender)
#include "MoviePipelineQueue.h"
#include "MoviePipelineQueueSubsystem.h"
#include "MoviePipelinePrimaryConfig.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineDeferredPassBase.h"
#include "MoviePipelineImageSequenceOutput.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "MovieSceneObjectBindingID.h"

// Convert a display-rate frame number to tick-resolution frame number.
// Sequencer internally stores everything in tick resolution (typically 24000 tps),
// but the user thinks in display frames (e.g. 24, 30, 60 fps).
// Passing raw display frames to SetRange / AddKey will place keys at the wrong time.
static FFrameNumber DisplayFrameToTick(int32 DisplayFrame, const UMovieScene* MovieScene)
{
	FFrameRate TickRes = MovieScene->GetTickResolution();
	FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	return ConvertFrameTime(
		FFrameTime(FFrameNumber(DisplayFrame)),
		DisplayRate,
		TickRes
	).FloorToFrame();
}

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

// Helper: find a binding GUID by actor name in a MovieScene
static bool FindBindingByActorName(UMovieScene* MovieScene, const FString& ActorName, FGuid& OutGuid)
{
	for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
	{
		const FMovieScenePossessable& Poss = MovieScene->GetPossessable(i);
		if (Poss.GetName() == ActorName)
		{
			OutGuid = Poss.GetGuid();
			return true;
		}
	}
	for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
	{
		const FMovieSceneSpawnable& Spawn = MovieScene->GetSpawnable(i);
		if (Spawn.GetName() == ActorName)
		{
			OutGuid = Spawn.GetGuid();
			return true;
		}
	}
	return false;
}

// Helper: find a binding GUID by GUID string
static bool FindBindingByGuid(UMovieScene* MovieScene, const FString& GuidStr, FGuid& OutGuid)
{
	FGuid Parsed;
	if (FGuid::Parse(GuidStr, Parsed))
	{
		// Verify it exists
		if (MovieScene->FindBinding(Parsed))
		{
			OutGuid = Parsed;
			return true;
		}
	}
	return false;
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

	// Check if it already exists
	if (FindSequenceByName(Name))
		return MakeErrorJson(FString::Printf(TEXT("LevelSequence '%s' already exists"), *Name));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	ULevelSequenceFactoryNew* Factory = NewObject<ULevelSequenceFactoryNew>();
	UObject* NewAsset = AssetTools.CreateAsset(Name, Path, ULevelSequence::StaticClass(), Factory);

	if (!NewAsset)
		return MakeErrorJson(TEXT("Failed to create LevelSequence asset"));

	ULevelSequence* Seq = Cast<ULevelSequence>(NewAsset);

	// Set default playback range if provided
	if (Json->HasField(TEXT("startFrame")) && Json->HasField(TEXT("endFrame")))
	{
		UMovieScene* MovieScene = Seq->GetMovieScene();
		if (MovieScene)
		{
				int32 StartFrame = (int32)Json->GetNumberField(TEXT("startFrame"));
				int32 EndFrame = (int32)Json->GetNumberField(TEXT("endFrame"));
				FFrameNumber TickStart = DisplayFrameToTick(StartFrame, MovieScene);
				FFrameNumber TickEnd   = DisplayFrameToTick(EndFrame, MovieScene);
				MovieScene->SetPlaybackRange(TickStart, (TickEnd - TickStart).Value);
		}
	}

	// Set display rate if provided
	if (Json->HasField(TEXT("frameRate")))
	{
		UMovieScene* MovieScene = Seq->GetMovieScene();
		if (MovieScene)
		{
			int32 FPS = (int32)Json->GetNumberField(TEXT("frameRate"));
			MovieScene->SetDisplayRate(FFrameRate(FPS, 1));
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), Seq->GetName());
	Result->SetStringField(TEXT("path"), Seq->GetPathName());
	return JsonToString(Result);
}

// ============================================================
// sequencerAddTrack - Add an actor binding track
// Supports: Transform, Float, Bool, Visibility, Audio,
//           SkeletalAnimation, Event, CameraCut
// ============================================================
FString FAgenticMCPServer::HandleSequencerAddTrack(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SeqName = Json->GetStringField(TEXT("sequenceName"));
	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString TrackType = Json->HasField(TEXT("trackType")) ? Json->GetStringField(TEXT("trackType")) : TEXT("Transform");

	if (SeqName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: sequenceName"));

	ULevelSequence* Seq = FindSequenceByName(SeqName);
	if (!Seq) return MakeErrorJson(FString::Printf(TEXT("LevelSequence not found: %s"), *SeqName));

	UMovieScene* MovieScene = Seq->GetMovieScene();
	if (!MovieScene) return MakeErrorJson(TEXT("Sequence has no MovieScene"));

	// Master tracks (no actor binding needed)
	if (TrackType == TEXT("CameraCut"))
	{
		UMovieSceneTrack* NewTrack = MovieScene->AddTrack<UMovieSceneCameraCutTrack>();
		if (!NewTrack) return MakeErrorJson(TEXT("Failed to add CameraCut track (may already exist)"));

		Seq->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("sequenceName"), SeqName);
		Result->SetStringField(TEXT("trackType"), TEXT("CameraCut"));
		Result->SetStringField(TEXT("note"), TEXT("Master track added. Use sequencerAddCameraCut to add camera sections."));
		return JsonToString(Result);
	}

	if (TrackType == TEXT("Audio"))
	{
		UMovieSceneTrack* NewTrack = MovieScene->AddTrack<UMovieSceneAudioTrack>();
		if (!NewTrack) return MakeErrorJson(TEXT("Failed to add Audio master track"));

		Seq->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("sequenceName"), SeqName);
		Result->SetStringField(TEXT("trackType"), TEXT("Audio"));
		return JsonToString(Result);
	}

	if (TrackType == TEXT("Event"))
	{
		UMovieSceneTrack* NewTrack = MovieScene->AddTrack<UMovieSceneEventTrack>();
		if (!NewTrack) return MakeErrorJson(TEXT("Failed to add Event master track"));

		Seq->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("sequenceName"), SeqName);
		Result->SetStringField(TEXT("trackType"), TEXT("Event"));
		return JsonToString(Result);
	}

	// Actor-bound tracks require actorName
	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName (required for actor-bound tracks)"));

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
	else if (TrackType == TEXT("Visibility"))
	{
		NewTrack = MovieScene->AddTrack<UMovieSceneVisibilityTrack>(BindingGuid);
	}
	else if (TrackType == TEXT("Bool"))
	{
		NewTrack = MovieScene->AddTrack<UMovieSceneBoolTrack>(BindingGuid);
	}
	else if (TrackType == TEXT("Float"))
	{
		NewTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(BindingGuid);
	}
	else if (TrackType == TEXT("SkeletalAnimation"))
	{
		NewTrack = MovieScene->AddTrack<UMovieSceneSkeletalAnimationTrack>(BindingGuid);
	}
	else
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Unsupported track type: %s. Supported: Transform, Visibility, Bool, Float, SkeletalAnimation, CameraCut, Audio, Event"),
			*TrackType));
	}

	if (!NewTrack)
		return MakeErrorJson(TEXT("Failed to add track (may already exist for this binding)"));

	Seq->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("sequenceName"), SeqName);
	Result->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("trackType"), TrackType);
	Result->SetStringField(TEXT("bindingGuid"), BindingGuid.ToString());
	return JsonToString(Result);
}

// ============================================================
// sequencerAddSection - Add a section to a track
// Params: sequenceName, actorName or bindingGuid, trackType,
//         startFrame, endFrame
// ============================================================
FString FAgenticMCPServer::HandleSequencerAddSection(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SeqName = Json->GetStringField(TEXT("sequenceName"));
	if (SeqName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: sequenceName"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString BindingGuidStr = Json->GetStringField(TEXT("bindingGuid"));
	FString TrackType = Json->HasField(TEXT("trackType")) ? Json->GetStringField(TEXT("trackType")) : TEXT("Transform");

	if (!Json->HasField(TEXT("startFrame"))) return MakeErrorJson(TEXT("Missing required field: startFrame"));
	if (!Json->HasField(TEXT("endFrame"))) return MakeErrorJson(TEXT("Missing required field: endFrame"));

	int32 StartFrame = (int32)Json->GetNumberField(TEXT("startFrame"));
	int32 EndFrame = (int32)Json->GetNumberField(TEXT("endFrame"));

	ULevelSequence* Seq = FindSequenceByName(SeqName);
	if (!Seq) return MakeErrorJson(FString::Printf(TEXT("LevelSequence not found: %s"), *SeqName));

	UMovieScene* MovieScene = Seq->GetMovieScene();
	if (!MovieScene) return MakeErrorJson(TEXT("Sequence has no MovieScene"));

	// Find the binding
	FGuid BindingGuid;
	if (!BindingGuidStr.IsEmpty())
	{
		if (!FindBindingByGuid(MovieScene, BindingGuidStr, BindingGuid))
			return MakeErrorJson(FString::Printf(TEXT("Binding GUID not found: %s"), *BindingGuidStr));
	}
	else if (!ActorName.IsEmpty())
	{
		if (!FindBindingByActorName(MovieScene, ActorName, BindingGuid))
			return MakeErrorJson(FString::Printf(TEXT("No binding found for actor: %s. Use sequencerAddTrack first."), *ActorName));
	}
	else
	{
		return MakeErrorJson(TEXT("Provide 'actorName' or 'bindingGuid' to identify the binding"));
	}

	// Find the track
	FMovieSceneBinding* Binding = MovieScene->FindBinding(BindingGuid);
	if (!Binding) return MakeErrorJson(TEXT("Binding not found in MovieScene"));

	UMovieSceneTrack* TargetTrack = nullptr;
	for (UMovieSceneTrack* Track : Binding->GetTracks())
	{
		if (!Track) continue;

		bool bMatch = false;
		if (TrackType == TEXT("Transform") || TrackType == TEXT("3DTransform"))
			bMatch = Track->IsA<UMovieScene3DTransformTrack>();
		else if (TrackType == TEXT("Visibility"))
			bMatch = Track->IsA<UMovieSceneVisibilityTrack>();
		else if (TrackType == TEXT("Bool"))
			bMatch = Track->IsA<UMovieSceneBoolTrack>();
		else if (TrackType == TEXT("Float"))
			bMatch = Track->IsA<UMovieSceneFloatTrack>();
		else if (TrackType == TEXT("SkeletalAnimation"))
			bMatch = Track->IsA<UMovieSceneSkeletalAnimationTrack>();

		if (bMatch)
		{
			TargetTrack = Track;
			break;
		}
	}

	if (!TargetTrack)
		return MakeErrorJson(FString::Printf(TEXT("No %s track found for this binding. Use sequencerAddTrack first."), *TrackType));

	// Create the section
	UMovieSceneSection* NewSection = TargetTrack->CreateNewSection();
	if (!NewSection)
		return MakeErrorJson(TEXT("Failed to create section"));

	// Set range (convert display frames to tick resolution)
	FFrameNumber TickStart = DisplayFrameToTick(StartFrame, MovieScene);
	FFrameNumber TickEnd   = DisplayFrameToTick(EndFrame, MovieScene);
	NewSection->SetRange(TRange<FFrameNumber>(TickStart, TickEnd));

	// Add to track
	TargetTrack->AddSection(*NewSection);

	Seq->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("sequenceName"), SeqName);
	Result->SetStringField(TEXT("trackType"), TrackType);
	Result->SetNumberField(TEXT("startFrame"), StartFrame);
	Result->SetNumberField(TEXT("endFrame"), EndFrame);
	Result->SetStringField(TEXT("sectionClass"), NewSection->GetClass()->GetName());
	Result->SetNumberField(TEXT("sectionIndex"), TargetTrack->GetAllSections().Num() - 1);
	return JsonToString(Result);
}

// ============================================================
// sequencerSetKeyframe - Set a keyframe value at a specific frame
// Params: sequenceName, actorName or bindingGuid, trackType,
//         frame, value (or lx,ly,lz,rx,ry,rz,sx,sy,sz for transform)
//         sectionIndex (optional, defaults to 0)
//         interpolation (optional: linear, constant, cubic)
// ============================================================
FString FAgenticMCPServer::HandleSequencerSetKeyframe(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SeqName = Json->GetStringField(TEXT("sequenceName"));
	if (SeqName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: sequenceName"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString BindingGuidStr = Json->GetStringField(TEXT("bindingGuid"));
	FString TrackType = Json->HasField(TEXT("trackType")) ? Json->GetStringField(TEXT("trackType")) : TEXT("Transform");

	if (!Json->HasField(TEXT("frame"))) return MakeErrorJson(TEXT("Missing required field: frame"));
	int32 Frame = (int32)Json->GetNumberField(TEXT("frame"));
	int32 SectionIdx = Json->HasField(TEXT("sectionIndex")) ? (int32)Json->GetNumberField(TEXT("sectionIndex")) : 0;

	// Interpolation mode
	FString InterpStr = Json->HasField(TEXT("interpolation")) ? Json->GetStringField(TEXT("interpolation")) : TEXT("cubic");
	ERichCurveInterpMode InterpMode = RCIM_Cubic;
	if (InterpStr == TEXT("linear")) InterpMode = RCIM_Linear;
	else if (InterpStr == TEXT("constant")) InterpMode = RCIM_Constant;

	ULevelSequence* Seq = FindSequenceByName(SeqName);
	if (!Seq) return MakeErrorJson(FString::Printf(TEXT("LevelSequence not found: %s"), *SeqName));

	UMovieScene* MovieScene = Seq->GetMovieScene();
	if (!MovieScene) return MakeErrorJson(TEXT("Sequence has no MovieScene"));

	// Find binding
	FGuid BindingGuid;
	if (!BindingGuidStr.IsEmpty())
	{
		if (!FindBindingByGuid(MovieScene, BindingGuidStr, BindingGuid))
			return MakeErrorJson(FString::Printf(TEXT("Binding GUID not found: %s"), *BindingGuidStr));
	}
	else if (!ActorName.IsEmpty())
	{
		if (!FindBindingByActorName(MovieScene, ActorName, BindingGuid))
			return MakeErrorJson(FString::Printf(TEXT("No binding found for actor: %s"), *ActorName));
	}
	else
	{
		return MakeErrorJson(TEXT("Provide 'actorName' or 'bindingGuid'"));
	}

	// Find the track
	FMovieSceneBinding* Binding = MovieScene->FindBinding(BindingGuid);
	if (!Binding) return MakeErrorJson(TEXT("Binding not found"));

	UMovieSceneTrack* TargetTrack = nullptr;
	for (UMovieSceneTrack* Track : Binding->GetTracks())
	{
		if (!Track) continue;
		bool bMatch = false;
		if (TrackType == TEXT("Transform") || TrackType == TEXT("3DTransform"))
			bMatch = Track->IsA<UMovieScene3DTransformTrack>();
		else if (TrackType == TEXT("Visibility"))
			bMatch = Track->IsA<UMovieSceneVisibilityTrack>();
		else if (TrackType == TEXT("Bool"))
			bMatch = Track->IsA<UMovieSceneBoolTrack>();
		else if (TrackType == TEXT("Float"))
			bMatch = Track->IsA<UMovieSceneFloatTrack>();
		if (bMatch) { TargetTrack = Track; break; }
	}

	if (!TargetTrack)
		return MakeErrorJson(FString::Printf(TEXT("No %s track found for this binding"), *TrackType));

	// Get the section
	const TArray<UMovieSceneSection*>& Sections = TargetTrack->GetAllSections();
	if (SectionIdx < 0 || SectionIdx >= Sections.Num())
		return MakeErrorJson(FString::Printf(TEXT("Section index %d out of range (0-%d)"), SectionIdx, Sections.Num() - 1));

	UMovieSceneSection* Section = Sections[SectionIdx];
	// Convert display frame to tick resolution
	FFrameNumber FrameNum = DisplayFrameToTick(Frame, MovieScene);

	int32 KeysSet = 0;

	// ---- TRANSFORM keyframes ----
	if (TrackType == TEXT("Transform") || TrackType == TEXT("3DTransform"))
	{
		UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(Section);
		if (!TransformSection)
			return MakeErrorJson(TEXT("Section is not a 3DTransformSection"));

		// UE5.4+ uses double channels for transform
		TArrayView<FMovieSceneDoubleChannel*> DoubleChannels = TransformSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();

		// Channel order: LocationX, LocationY, LocationZ, RotationX, RotationY, RotationZ, ScaleX, ScaleY, ScaleZ
		struct { const TCHAR* Key; int32 ChannelIdx; } TransformChannels[] = {
			{ TEXT("lx"), 0 }, { TEXT("ly"), 1 }, { TEXT("lz"), 2 },
			{ TEXT("rx"), 3 }, { TEXT("ry"), 4 }, { TEXT("rz"), 5 },
			{ TEXT("sx"), 6 }, { TEXT("sy"), 7 }, { TEXT("sz"), 8 },
		};

		for (auto& TC : TransformChannels)
		{
			if (Json->HasField(TC.Key) && TC.ChannelIdx < DoubleChannels.Num())
			{
				double Val = Json->GetNumberField(TC.Key);
				FMovieSceneDoubleChannel* Channel = DoubleChannels[TC.ChannelIdx];

				// Remove existing key at this frame
				TArray<FFrameNumber> KeyTimes;
				TArray<FKeyHandle> KeyHandles;
				Channel->GetKeys(TRange<FFrameNumber>(FrameNum, FFrameNumber(FrameNum.Value + 1)), &KeyTimes, &KeyHandles);
				for (const FKeyHandle& Handle : KeyHandles)
				{
					Channel->DeleteKeys(TArrayView<const FKeyHandle>(&Handle, 1));
				}

					// Add new key (2-arg overload; tangent mode is auto by default)
					if (InterpMode == RCIM_Linear)
						Channel->AddLinearKey(FrameNum, Val);
					else if (InterpMode == RCIM_Constant)
						Channel->AddConstantKey(FrameNum, Val);
					else
						Channel->AddCubicKey(FrameNum, Val);
				KeysSet++;
			}
		}

		// Also support "location" and "rotation" and "scale" as FVector JSON
		if (Json->HasField(TEXT("location")))
		{
			const TSharedPtr<FJsonObject>* LocObj;
			if (Json->TryGetObjectField(TEXT("location"), LocObj))
			{
				double X = (*LocObj)->HasField(TEXT("x")) ? (*LocObj)->GetNumberField(TEXT("x")) : 0.0;
				double Y = (*LocObj)->HasField(TEXT("y")) ? (*LocObj)->GetNumberField(TEXT("y")) : 0.0;
				double Z = (*LocObj)->HasField(TEXT("z")) ? (*LocObj)->GetNumberField(TEXT("z")) : 0.0;
					if (0 < DoubleChannels.Num()) { DoubleChannels[0]->AddCubicKey(FrameNum, X); KeysSet++; }
					if (1 < DoubleChannels.Num()) { DoubleChannels[1]->AddCubicKey(FrameNum, Y); KeysSet++; }
					if (2 < DoubleChannels.Num()) { DoubleChannels[2]->AddCubicKey(FrameNum, Z); KeysSet++; }
			}
		}
		if (Json->HasField(TEXT("rotation")))
		{
			const TSharedPtr<FJsonObject>* RotObj;
			if (Json->TryGetObjectField(TEXT("rotation"), RotObj))
			{
				double X = (*RotObj)->HasField(TEXT("x")) ? (*RotObj)->GetNumberField(TEXT("x")) : 0.0;
				double Y = (*RotObj)->HasField(TEXT("y")) ? (*RotObj)->GetNumberField(TEXT("y")) : 0.0;
				double Z = (*RotObj)->HasField(TEXT("z")) ? (*RotObj)->GetNumberField(TEXT("z")) : 0.0;
					if (3 < DoubleChannels.Num()) { DoubleChannels[3]->AddCubicKey(FrameNum, X); KeysSet++; }
					if (4 < DoubleChannels.Num()) { DoubleChannels[4]->AddCubicKey(FrameNum, Y); KeysSet++; }
					if (5 < DoubleChannels.Num()) { DoubleChannels[5]->AddCubicKey(FrameNum, Z); KeysSet++; }
			}
		}
		if (Json->HasField(TEXT("scale")))
		{
			const TSharedPtr<FJsonObject>* ScaleObj;
			if (Json->TryGetObjectField(TEXT("scale"), ScaleObj))
			{
				double X = (*ScaleObj)->HasField(TEXT("x")) ? (*ScaleObj)->GetNumberField(TEXT("x")) : 1.0;
				double Y = (*ScaleObj)->HasField(TEXT("y")) ? (*ScaleObj)->GetNumberField(TEXT("y")) : 1.0;
				double Z = (*ScaleObj)->HasField(TEXT("z")) ? (*ScaleObj)->GetNumberField(TEXT("z")) : 1.0;
					if (6 < DoubleChannels.Num()) { DoubleChannels[6]->AddCubicKey(FrameNum, X); KeysSet++; }
					if (7 < DoubleChannels.Num()) { DoubleChannels[7]->AddCubicKey(FrameNum, Y); KeysSet++; }
					if (8 < DoubleChannels.Num()) { DoubleChannels[8]->AddCubicKey(FrameNum, Z); KeysSet++; }
			}
		}
	}
	// ---- FLOAT keyframes ----
	else if (TrackType == TEXT("Float"))
	{
		if (!Json->HasField(TEXT("value")))
			return MakeErrorJson(TEXT("Missing required field: value (for Float keyframe)"));

		double Value = Json->GetNumberField(TEXT("value"));

		TArrayView<FMovieSceneFloatChannel*> FloatChannels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
		if (FloatChannels.Num() == 0)
			return MakeErrorJson(TEXT("Section has no float channels"));

		int32 ChannelIdx = Json->HasField(TEXT("channelIndex")) ? (int32)Json->GetNumberField(TEXT("channelIndex")) : 0;
		if (ChannelIdx >= FloatChannels.Num())
			return MakeErrorJson(FString::Printf(TEXT("Channel index %d out of range (0-%d)"), ChannelIdx, FloatChannels.Num() - 1));

		FMovieSceneFloatChannel* Channel = FloatChannels[ChannelIdx];
		if (InterpMode == RCIM_Linear)
			Channel->AddLinearKey(FrameNum, (float)Value);
		else if (InterpMode == RCIM_Constant)
			Channel->AddConstantKey(FrameNum, (float)Value);
		else
			Channel->AddCubicKey(FrameNum, (float)Value);
		KeysSet = 1;
	}
	// ---- BOOL / VISIBILITY keyframes ----
	else if (TrackType == TEXT("Bool") || TrackType == TEXT("Visibility"))
	{
		if (!Json->HasField(TEXT("value")))
			return MakeErrorJson(TEXT("Missing required field: value (for Bool keyframe)"));

		bool Value = Json->GetBoolField(TEXT("value"));

		TArrayView<FMovieSceneBoolChannel*> BoolChannels = Section->GetChannelProxy().GetChannels<FMovieSceneBoolChannel>();
		if (BoolChannels.Num() == 0)
			return MakeErrorJson(TEXT("Section has no bool channels"));

		FMovieSceneBoolChannel* Channel = BoolChannels[0];

		// Add key
		TMovieSceneChannelData<bool> ChannelData = Channel->GetData();
		ChannelData.AddKey(FrameNum, Value);
		KeysSet = 1;
	}

	if (KeysSet == 0)
		return MakeErrorJson(TEXT("No keyframes were set. Check parameter names match trackType."));

	Seq->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("sequenceName"), SeqName);
	Result->SetStringField(TEXT("trackType"), TrackType);
	Result->SetNumberField(TEXT("frame"), Frame);
	Result->SetNumberField(TEXT("keysSet"), KeysSet);
	Result->SetStringField(TEXT("interpolation"), InterpStr);
	return JsonToString(Result);
}

// ============================================================
// sequencerDeleteSection - Remove a section from a track
// Params: sequenceName, actorName or bindingGuid, trackType,
//         sectionIndex
// ============================================================
FString FAgenticMCPServer::HandleSequencerDeleteSection(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SeqName = Json->GetStringField(TEXT("sequenceName"));
	if (SeqName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: sequenceName"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString BindingGuidStr = Json->GetStringField(TEXT("bindingGuid"));
	FString TrackType = Json->HasField(TEXT("trackType")) ? Json->GetStringField(TEXT("trackType")) : TEXT("Transform");

	if (!Json->HasField(TEXT("sectionIndex"))) return MakeErrorJson(TEXT("Missing required field: sectionIndex"));
	int32 SectionIdx = (int32)Json->GetNumberField(TEXT("sectionIndex"));

	ULevelSequence* Seq = FindSequenceByName(SeqName);
	if (!Seq) return MakeErrorJson(FString::Printf(TEXT("LevelSequence not found: %s"), *SeqName));

	UMovieScene* MovieScene = Seq->GetMovieScene();
	if (!MovieScene) return MakeErrorJson(TEXT("Sequence has no MovieScene"));

	// Find binding
	FGuid BindingGuid;
	if (!BindingGuidStr.IsEmpty())
	{
		if (!FindBindingByGuid(MovieScene, BindingGuidStr, BindingGuid))
			return MakeErrorJson(FString::Printf(TEXT("Binding GUID not found: %s"), *BindingGuidStr));
	}
	else if (!ActorName.IsEmpty())
	{
		if (!FindBindingByActorName(MovieScene, ActorName, BindingGuid))
			return MakeErrorJson(FString::Printf(TEXT("No binding found for actor: %s"), *ActorName));
	}
	else
	{
		return MakeErrorJson(TEXT("Provide 'actorName' or 'bindingGuid'"));
	}

	FMovieSceneBinding* Binding = MovieScene->FindBinding(BindingGuid);
	if (!Binding) return MakeErrorJson(TEXT("Binding not found"));

	// Find the track
	UMovieSceneTrack* TargetTrack = nullptr;
	for (UMovieSceneTrack* Track : Binding->GetTracks())
	{
		if (!Track) continue;
		bool bMatch = false;
		if (TrackType == TEXT("Transform") || TrackType == TEXT("3DTransform"))
			bMatch = Track->IsA<UMovieScene3DTransformTrack>();
		else if (TrackType == TEXT("Visibility"))
			bMatch = Track->IsA<UMovieSceneVisibilityTrack>();
		else if (TrackType == TEXT("Bool"))
			bMatch = Track->IsA<UMovieSceneBoolTrack>();
		else if (TrackType == TEXT("Float"))
			bMatch = Track->IsA<UMovieSceneFloatTrack>();
		if (bMatch) { TargetTrack = Track; break; }
	}

	if (!TargetTrack)
		return MakeErrorJson(FString::Printf(TEXT("No %s track found for this binding"), *TrackType));

	const TArray<UMovieSceneSection*>& Sections = TargetTrack->GetAllSections();
	if (SectionIdx < 0 || SectionIdx >= Sections.Num())
		return MakeErrorJson(FString::Printf(TEXT("Section index %d out of range (0-%d)"), SectionIdx, Sections.Num() - 1));

	UMovieSceneSection* SectionToRemove = Sections[SectionIdx];
	TargetTrack->RemoveSection(*SectionToRemove);

	Seq->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("sequenceName"), SeqName);
	Result->SetStringField(TEXT("trackType"), TrackType);
	Result->SetNumberField(TEXT("removedSectionIndex"), SectionIdx);
	Result->SetNumberField(TEXT("remainingSections"), TargetTrack->GetAllSections().Num());
	return JsonToString(Result);
}

// ============================================================
// sequencerBindActor - Bind an actor to a sequence as possessable
// Params: sequenceName, actorName
// Returns the binding GUID for use in other calls
// ============================================================
FString FAgenticMCPServer::HandleSequencerBindActor(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SeqName = Json->GetStringField(TEXT("sequenceName"));
	FString ActorName = Json->GetStringField(TEXT("actorName"));

	if (SeqName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: sequenceName"));
	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName"));

	ULevelSequence* Seq = FindSequenceByName(SeqName);
	if (!Seq) return MakeErrorJson(FString::Printf(TEXT("LevelSequence not found: %s"), *SeqName));

	UWorld* World = GetEditorWorld_Seq();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	UMovieScene* MovieScene = Seq->GetMovieScene();
	if (!MovieScene) return MakeErrorJson(TEXT("Sequence has no MovieScene"));

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

	// Check if already bound
	FGuid ExistingGuid;
	if (FindBindingByActorName(MovieScene, Actor->GetActorLabel(), ExistingGuid) ||
		FindBindingByActorName(MovieScene, Actor->GetName(), ExistingGuid))
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("alreadyBound"), true);
		Result->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("bindingGuid"), ExistingGuid.ToString());
		return JsonToString(Result);
	}

	// Create possessable binding
	FGuid BindingGuid = MovieScene->AddPossessable(Actor->GetActorLabel(), Actor->GetClass());
	Seq->BindPossessableObject(BindingGuid, *Actor, World);

	Seq->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("alreadyBound"), false);
	Result->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());
	Result->SetStringField(TEXT("bindingGuid"), BindingGuid.ToString());
	return JsonToString(Result);
}

// ============================================================
// sequencerAddCameraCut - Add a camera cut section
// Params: sequenceName, cameraActorName, startFrame, endFrame
// ============================================================
FString FAgenticMCPServer::HandleSequencerAddCameraCut(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SeqName = Json->GetStringField(TEXT("sequenceName"));
	FString CameraName = Json->GetStringField(TEXT("cameraActorName"));

	if (SeqName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: sequenceName"));
	if (CameraName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: cameraActorName"));
	if (!Json->HasField(TEXT("startFrame"))) return MakeErrorJson(TEXT("Missing required field: startFrame"));
	if (!Json->HasField(TEXT("endFrame"))) return MakeErrorJson(TEXT("Missing required field: endFrame"));

	int32 StartFrame = (int32)Json->GetNumberField(TEXT("startFrame"));
	int32 EndFrame = (int32)Json->GetNumberField(TEXT("endFrame"));

	ULevelSequence* Seq = FindSequenceByName(SeqName);
	if (!Seq) return MakeErrorJson(FString::Printf(TEXT("LevelSequence not found: %s"), *SeqName));

	UWorld* World = GetEditorWorld_Seq();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	UMovieScene* MovieScene = Seq->GetMovieScene();
	if (!MovieScene) return MakeErrorJson(TEXT("Sequence has no MovieScene"));

	// Find camera actor
	ACameraActor* CameraActor = nullptr;
	for (TActorIterator<ACameraActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == CameraName || (*It)->GetName() == CameraName)
		{
			CameraActor = *It;
			break;
		}
	}
	if (!CameraActor)
		return MakeErrorJson(FString::Printf(TEXT("Camera actor not found: %s. Must be a CameraActor."), *CameraName));

	// Ensure camera is bound as possessable
	FGuid CameraGuid;
	if (!FindBindingByActorName(MovieScene, CameraActor->GetActorLabel(), CameraGuid) &&
		!FindBindingByActorName(MovieScene, CameraActor->GetName(), CameraGuid))
	{
		CameraGuid = MovieScene->AddPossessable(CameraActor->GetActorLabel(), CameraActor->GetClass());
		Seq->BindPossessableObject(CameraGuid, *CameraActor, World);
	}

	// Find or create CameraCut track
	UMovieSceneCameraCutTrack* CameraCutTrack = nullptr;
	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		if (UMovieSceneCameraCutTrack* CCT = Cast<UMovieSceneCameraCutTrack>(Track))
		{
			CameraCutTrack = CCT;
			break;
		}
	}
	if (!CameraCutTrack)
	{
		CameraCutTrack = MovieScene->AddTrack<UMovieSceneCameraCutTrack>();
		if (!CameraCutTrack)
			return MakeErrorJson(TEXT("Failed to create CameraCut track"));
	}

	// Create camera cut section
	UMovieSceneCameraCutSection* CutSection = Cast<UMovieSceneCameraCutSection>(CameraCutTrack->CreateNewSection());
	if (!CutSection)
		return MakeErrorJson(TEXT("Failed to create CameraCut section"));

	// Convert display frames to tick resolution
	FFrameNumber TickStart = DisplayFrameToTick(StartFrame, MovieScene);
	FFrameNumber TickEnd   = DisplayFrameToTick(EndFrame, MovieScene);
	CutSection->SetRange(TRange<FFrameNumber>(TickStart, TickEnd));

	// Bind the camera to this cut section using SetGuid (safe across all UE5 versions)
	FMovieSceneObjectBindingID BindingID;
	BindingID.SetGuid(CameraGuid);
	CutSection->SetCameraBindingID(BindingID);

	CameraCutTrack->AddSection(*CutSection);

	Seq->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("sequenceName"), SeqName);
	Result->SetStringField(TEXT("cameraActor"), CameraActor->GetActorLabel());
	Result->SetStringField(TEXT("cameraBindingGuid"), CameraGuid.ToString());
	Result->SetNumberField(TEXT("startFrame"), StartFrame);
	Result->SetNumberField(TEXT("endFrame"), EndFrame);
	return JsonToString(Result);
}

// ============================================================
// sequencerGetTracks - Get all tracks and sections in a sequence
// (Fixed: GetMasterTracks -> GetTracks for UE5.4+)
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
			for (int32 si = 0; si < Track->GetAllSections().Num(); ++si)
			{
				UMovieSceneSection* Section = Track->GetAllSections()[si];
				if (!Section) continue;
				TSharedRef<FJsonObject> SecJson = MakeShared<FJsonObject>();
				SecJson->SetStringField(TEXT("class"), Section->GetClass()->GetName());
				SecJson->SetNumberField(TEXT("index"), si);

					TRange<FFrameNumber> Range = Section->GetRange();
					FFrameRate TickRes = MovieScene->GetTickResolution();
					FFrameRate DispRate = MovieScene->GetDisplayRate();
					if (Range.HasLowerBound())
					{
						int32 DisplayStart = ConvertFrameTime(FFrameTime(Range.GetLowerBoundValue()), TickRes, DispRate).FloorToFrame().Value;
						SecJson->SetNumberField(TEXT("startFrame"), DisplayStart);
					}
					if (Range.HasUpperBound())
					{
						int32 DisplayEnd = ConvertFrameTime(FFrameTime(Range.GetUpperBoundValue()), TickRes, DispRate).FloorToFrame().Value;
						SecJson->SetNumberField(TEXT("endFrame"), DisplayEnd);
					}

				// Report channel count for keyframe-able sections
				FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
				int32 TotalChannels = 0;
				TotalChannels += Proxy.GetChannels<FMovieSceneDoubleChannel>().Num();
				TotalChannels += Proxy.GetChannels<FMovieSceneFloatChannel>().Num();
				TotalChannels += Proxy.GetChannels<FMovieSceneBoolChannel>().Num();
				SecJson->SetNumberField(TEXT("channelCount"), TotalChannels);

				SecArr.Add(MakeShared<FJsonValueObject>(SecJson));
			}
			TrackJson->SetArrayField(TEXT("sections"), SecArr);
			TrackArr.Add(MakeShared<FJsonValueObject>(TrackJson));
		}
		BindJson->SetArrayField(TEXT("tracks"), TrackArr);
		BindingArray.Add(MakeShared<FJsonValueObject>(BindJson));
	}

	// Spawnable bindings
	for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
	{
		const FMovieSceneSpawnable& Spawn = MovieScene->GetSpawnable(i);
		FMovieSceneBinding* Binding = MovieScene->FindBinding(Spawn.GetGuid());
		if (!Binding) continue;

		TSharedRef<FJsonObject> BindJson = MakeShared<FJsonObject>();
		BindJson->SetStringField(TEXT("name"), Spawn.GetName());
		BindJson->SetStringField(TEXT("guid"), Spawn.GetGuid().ToString());
		BindJson->SetStringField(TEXT("type"), TEXT("Spawnable"));

		TArray<TSharedPtr<FJsonValue>> TrackArr;
		for (UMovieSceneTrack* Track : Binding->GetTracks())
		{
			if (!Track) continue;
			TSharedRef<FJsonObject> TrackJson = MakeShared<FJsonObject>();
			TrackJson->SetStringField(TEXT("name"), Track->GetTrackName().ToString());
			TrackJson->SetStringField(TEXT("class"), Track->GetClass()->GetName());
			TrackJson->SetNumberField(TEXT("sectionCount"), Track->GetAllSections().Num());
			TrackArr.Add(MakeShared<FJsonValueObject>(TrackJson));
		}
		BindJson->SetArrayField(TEXT("tracks"), TrackArr);
		BindingArray.Add(MakeShared<FJsonValueObject>(BindJson));
	}

	// Master/Global tracks (UE5.4+: GetTracks() replaces GetMasterTracks())
	TArray<TSharedPtr<FJsonValue>> MasterTrackArr;
	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		if (!Track) continue;
		TSharedRef<FJsonObject> TrackJson = MakeShared<FJsonObject>();
		TrackJson->SetStringField(TEXT("name"), Track->GetTrackName().ToString());
		TrackJson->SetStringField(TEXT("class"), Track->GetClass()->GetName());
		TrackJson->SetNumberField(TEXT("sectionCount"), Track->GetAllSections().Num());

		// For CameraCut tracks, report camera bindings
		if (UMovieSceneCameraCutTrack* CCT = Cast<UMovieSceneCameraCutTrack>(Track))
		{
			TArray<TSharedPtr<FJsonValue>> CutArr;
			for (UMovieSceneSection* Section : CCT->GetAllSections())
			{
				if (UMovieSceneCameraCutSection* CutSec = Cast<UMovieSceneCameraCutSection>(Section))
				{
						TSharedRef<FJsonObject> CutJson = MakeShared<FJsonObject>();
						TRange<FFrameNumber> Range = CutSec->GetRange();
						FFrameRate CutTickRes = MovieScene->GetTickResolution();
						FFrameRate CutDispRate = MovieScene->GetDisplayRate();
						if (Range.HasLowerBound())
						{
							int32 DS = ConvertFrameTime(FFrameTime(Range.GetLowerBoundValue()), CutTickRes, CutDispRate).FloorToFrame().Value;
							CutJson->SetNumberField(TEXT("startFrame"), DS);
						}
						if (Range.HasUpperBound())
						{
							int32 DE = ConvertFrameTime(FFrameTime(Range.GetUpperBoundValue()), CutTickRes, CutDispRate).FloorToFrame().Value;
							CutJson->SetNumberField(TEXT("endFrame"), DE);
						}
					CutJson->SetStringField(TEXT("cameraBindingGuid"), CutSec->GetCameraBindingID().GetGuid().ToString());
					CutArr.Add(MakeShared<FJsonValueObject>(CutJson));
				}
			}
			TrackJson->SetArrayField(TEXT("cameraCuts"), CutArr);
		}

		MasterTrackArr.Add(MakeShared<FJsonValueObject>(TrackJson));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("sequenceName"), SeqName);
	Result->SetNumberField(TEXT("bindingCount"), BindingArray.Num());
	Result->SetArrayField(TEXT("bindings"), BindingArray);
	Result->SetNumberField(TEXT("masterTrackCount"), MasterTrackArr.Num());
	Result->SetArrayField(TEXT("masterTracks"), MasterTrackArr);

	// Playback range (convert ticks back to display frames for consistency)
	TRange<FFrameNumber> PlayRange = MovieScene->GetPlaybackRange();
	FFrameRate TickRes = MovieScene->GetTickResolution();
	FFrameRate DispRate = MovieScene->GetDisplayRate();
	if (PlayRange.HasLowerBound())
	{
		int32 DisplayStart = ConvertFrameTime(FFrameTime(PlayRange.GetLowerBoundValue()), TickRes, DispRate).FloorToFrame().Value;
		Result->SetNumberField(TEXT("playbackStartFrame"), DisplayStart);
	}
	if (PlayRange.HasUpperBound())
	{
		int32 DisplayEnd = ConvertFrameTime(FFrameTime(PlayRange.GetUpperBoundValue()), TickRes, DispRate).FloorToFrame().Value;
		Result->SetNumberField(TEXT("playbackEndFrame"), DisplayEnd);
	}
	Result->SetNumberField(TEXT("tickResolution"), MovieScene->GetTickResolution().Numerator);
	Result->SetNumberField(TEXT("displayRate"), MovieScene->GetDisplayRate().Numerator);

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

	FFrameNumber TickStart = DisplayFrameToTick(StartFrame, MovieScene);
	FFrameNumber TickEnd   = DisplayFrameToTick(EndFrame, MovieScene);
	MovieScene->SetPlaybackRange(TickStart, (TickEnd - TickStart).Value);

	Seq->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("sequenceName"), SeqName);
	Result->SetNumberField(TEXT("startFrame"), StartFrame);
	Result->SetNumberField(TEXT("endFrame"), EndFrame);
	return JsonToString(Result);
}

// ============================================================
// sequencerRender - Render sequence to image sequence
// Uses Movie Render Pipeline (MoviePipelineQueueSubsystem)
// Params: sequenceName, outputDir (optional), format (png/exr/jpg),
//         resolution (e.g. "1920x1080"), antiAliasing (1-8)
// ============================================================
FString FAgenticMCPServer::HandleSequencerRender(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString SeqName = Json->GetStringField(TEXT("sequenceName"));
	if (SeqName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: sequenceName"));

	FString OutputDir = Json->HasField(TEXT("outputDir"))
		? Json->GetStringField(TEXT("outputDir"))
		: FPaths::ProjectSavedDir() / TEXT("MovieRenders");

	FString Format = Json->HasField(TEXT("format"))
		? Json->GetStringField(TEXT("format"))
		: TEXT("png");

	FString Resolution = Json->HasField(TEXT("resolution"))
		? Json->GetStringField(TEXT("resolution"))
		: TEXT("1920x1080");

	int32 AAOverride = Json->HasField(TEXT("antiAliasing"))
		? (int32)Json->GetNumberField(TEXT("antiAliasing"))
		: 1;

	ULevelSequence* Seq = FindSequenceByName(SeqName);
	if (!Seq) return MakeErrorJson(FString::Printf(TEXT("LevelSequence not found: %s"), *SeqName));

	// Get the Movie Pipeline Queue Subsystem
	UMoviePipelineQueueSubsystem* QueueSubsystem = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
	if (!QueueSubsystem)
		return MakeErrorJson(TEXT("MoviePipelineQueueSubsystem not available. Ensure MovieRenderPipeline plugin is enabled."));

	// Get or create the queue
	UMoviePipelineQueue* Queue = QueueSubsystem->GetQueue();
	if (!Queue)
		return MakeErrorJson(TEXT("Failed to get Movie Pipeline Queue"));

	// Create a new job
	UMoviePipelineExecutorJob* Job = Queue->AllocateNewJob(UMoviePipelineExecutorJob::StaticClass());
	if (!Job)
		return MakeErrorJson(TEXT("Failed to allocate render job"));

	Job->SetSequence(FSoftObjectPath(Seq->GetPathName()));
	Job->JobName = FString::Printf(TEXT("MCP_Render_%s"), *SeqName);

	// Find the current map
	UWorld* World = GetEditorWorld_Seq();
	if (World)
	{
		Job->Map = FSoftObjectPath(World->GetOutermost()->GetPathName());
	}

	// Create render config
	UMoviePipelinePrimaryConfig* Config = NewObject<UMoviePipelinePrimaryConfig>(Job);
	Job->SetConfiguration(Config);

	// Output settings
	UMoviePipelineOutputSetting* OutputSettings = Config->FindOrAddSettingByClass<UMoviePipelineOutputSetting>();
	if (OutputSettings)
	{
		OutputSettings->OutputDirectory.Path = OutputDir;
		OutputSettings->FileNameFormat = TEXT("{sequence_name}/{sequence_name}.{frame_number}");

		// Parse resolution
		FString ResX, ResY;
		if (Resolution.Split(TEXT("x"), &ResX, &ResY))
		{
			OutputSettings->OutputResolution = FIntPoint(FCString::Atoi(*ResX), FCString::Atoi(*ResY));
		}
	}

	// Image format
	if (Format == TEXT("png") || Format == TEXT("PNG"))
	{
		Config->FindOrAddSettingByClass<UMoviePipelineImageSequenceOutput_PNG>();
	}
	else if (Format == TEXT("exr") || Format == TEXT("EXR"))
	{
		Config->FindOrAddSettingByClass<UMoviePipelineImageSequenceOutput_EXR>();
	}
	else if (Format == TEXT("jpg") || Format == TEXT("jpeg") || Format == TEXT("JPG"))
	{
		Config->FindOrAddSettingByClass<UMoviePipelineImageSequenceOutput_JPG>();
	}

	// Anti-aliasing
	UMoviePipelineAntiAliasingSetting* AASetting = Config->FindOrAddSettingByClass<UMoviePipelineAntiAliasingSetting>();
	if (AASetting)
	{
		AASetting->SpatialSampleCount = FMath::Clamp(AAOverride, 1, 64);
		AASetting->TemporalSampleCount = 1;
	}

	// Deferred renderer pass (base class handles standard deferred rendering)
	// NOTE: UMoviePipelineDeferredPassBase is abstract in some UE versions.
	// If compilation fails, replace with UMoviePipelineDeferredPass_Unlit or remove this line.
	Config->FindOrAddSettingByClass<UMoviePipelineDeferredPassBase>();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("sequenceName"), SeqName);
	Result->SetStringField(TEXT("jobName"), Job->JobName);
	Result->SetStringField(TEXT("outputDir"), OutputDir);
	Result->SetStringField(TEXT("format"), Format);
	Result->SetStringField(TEXT("resolution"), Resolution);
	Result->SetNumberField(TEXT("antiAliasing"), AAOverride);
	Result->SetStringField(TEXT("note"),
		TEXT("Job queued. Use Movie Render Pipeline UI or call 'executeConsole' with "
			 "'MovieRenderPipeline.RenderQueuedJobs' to start rendering. "
			 "Or use executePython to call: "
			 "unreal.MoviePipelineQueueSubsystem().render_queue_with_executor(unreal.MoviePipelinePIEExecutor())"));
	return JsonToString(Result);
}
