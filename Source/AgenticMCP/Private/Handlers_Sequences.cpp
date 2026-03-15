// Handlers_Sequences.cpp
// Level Sequence operations for AgenticMCP
// Handles reading sequence tracks, audio cues, and timing data

#include "AgenticMCPServer.h"

// UE5 Engine includes
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Sections/MovieSceneAudioSection.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Sections/MovieSceneSubSection.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Sound/SoundBase.h"

// JSON
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// Asset Registry
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

// ============================================================
// HandleListSequences - List all level sequences in loaded levels
// ============================================================
FString FAgenticMCPServer::HandleListSequences(const FString& Body)
{
    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> SequencesArray;

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        Response->SetStringField(TEXT("error"), TEXT("No world loaded"));
        return JsonToString(Response);
    }

    // Find all LevelSequenceActors in loaded levels
    for (TActorIterator<ALevelSequenceActor> It(World); It; ++It)
    {
        ALevelSequenceActor* SeqActor = *It;
        if (!SeqActor) continue;

        TSharedRef<FJsonObject> SeqObj = MakeShared<FJsonObject>();
        SeqObj->SetStringField(TEXT("actorName"), SeqActor->GetName());
        SeqObj->SetStringField(TEXT("actorLabel"), SeqActor->GetActorLabel());

        // Get the level this actor belongs to
        ULevel* Level = SeqActor->GetLevel();
        if (Level && Level->GetOuter())
        {
            SeqObj->SetStringField(TEXT("level"), FPackageName::GetShortName(Level->GetOuter()->GetName()));
        }

        // Get the LevelSequence asset
        ULevelSequence* Sequence = SeqActor->GetSequence();
        if (Sequence)
        {
            SeqObj->SetStringField(TEXT("sequenceName"), Sequence->GetName());
            SeqObj->SetStringField(TEXT("sequencePath"), Sequence->GetPathName());

            // Get movie scene data
            UMovieScene* MovieScene = Sequence->GetMovieScene();
            if (MovieScene)
            {
                // Get playback range
                TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
                FFrameRate TickResolution = MovieScene->GetTickResolution();
                FFrameRate DisplayRate = MovieScene->GetDisplayRate();

                double StartSeconds = TickResolution.AsSeconds(PlaybackRange.GetLowerBoundValue());
                double EndSeconds = TickResolution.AsSeconds(PlaybackRange.GetUpperBoundValue());

                SeqObj->SetNumberField(TEXT("startTime"), StartSeconds);
                SeqObj->SetNumberField(TEXT("endTime"), EndSeconds);
                SeqObj->SetNumberField(TEXT("duration"), EndSeconds - StartSeconds);
                SeqObj->SetNumberField(TEXT("frameRate"), DisplayRate.AsDecimal());
                SeqObj->SetNumberField(TEXT("trackCount"), MovieScene->GetTracks().Num());
            }
        }
        else
        {
            SeqObj->SetStringField(TEXT("sequenceName"), TEXT("(none)"));
        }

        SequencesArray.Add(MakeShared<FJsonValueObject>(SeqObj));
    }

    Response->SetBoolField(TEXT("success"), true);
    Response->SetNumberField(TEXT("count"), SequencesArray.Num());
    Response->SetArrayField(TEXT("sequences"), SequencesArray);

    return JsonToString(Response);
}

// ============================================================
// HandleReadSequence - Read detailed track data from a sequence
// ============================================================
FString FAgenticMCPServer::HandleReadSequence(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();

    // Parse JSON body
    TSharedPtr<FJsonObject> JsonBody;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, JsonBody) || !JsonBody.IsValid())
    {
        Response->SetStringField(TEXT("error"), TEXT("Invalid JSON body"));
        return JsonToString(Response);
    }

    // Get sequence path or actor name
    FString SequencePath = JsonBody->GetStringField(TEXT("sequencePath"));
    FString ActorName = JsonBody->GetStringField(TEXT("actorName"));

    ULevelSequence* Sequence = nullptr;

    // Try to find by actor name first
    if (!ActorName.IsEmpty())
    {
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (World)
        {
            for (TActorIterator<ALevelSequenceActor> It(World); It; ++It)
            {
                ALevelSequenceActor* SeqActor = *It;
                if (SeqActor && (SeqActor->GetName() == ActorName || SeqActor->GetActorLabel() == ActorName))
                {
                    Sequence = SeqActor->GetSequence();
                    break;
                }
            }
        }
    }

    // Try to load by path
    if (!Sequence && !SequencePath.IsEmpty())
    {
        Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath);
    }

    if (!Sequence)
    {
        Response->SetStringField(TEXT("error"), TEXT("Sequence not found. Provide 'sequencePath' or 'actorName'."));
        return JsonToString(Response);
    }

    UMovieScene* MovieScene = Sequence->GetMovieScene();
    if (!MovieScene)
    {
        Response->SetStringField(TEXT("error"), TEXT("No MovieScene in sequence"));
        return JsonToString(Response);
    }

    Response->SetBoolField(TEXT("success"), true);
    Response->SetStringField(TEXT("sequenceName"), Sequence->GetName());
    Response->SetStringField(TEXT("sequencePath"), Sequence->GetPathName());

    // Playback info
    TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
    FFrameRate TickResolution = MovieScene->GetTickResolution();
    FFrameRate DisplayRate = MovieScene->GetDisplayRate();

    double StartSeconds = TickResolution.AsSeconds(PlaybackRange.GetLowerBoundValue());
    double EndSeconds = TickResolution.AsSeconds(PlaybackRange.GetUpperBoundValue());

    Response->SetNumberField(TEXT("startTime"), StartSeconds);
    Response->SetNumberField(TEXT("endTime"), EndSeconds);
    Response->SetNumberField(TEXT("duration"), EndSeconds - StartSeconds);
    Response->SetNumberField(TEXT("frameRate"), DisplayRate.AsDecimal());

    // ---- Tracks (was GetMasterTracks, now GetTracks in UE5.6) ----
    TArray<TSharedPtr<FJsonValue>> TracksArray;

    for (UMovieSceneTrack* Track : MovieScene->GetTracks())
    {
        if (!Track) continue;

        TSharedRef<FJsonObject> TrackObj = MakeShared<FJsonObject>();
        TrackObj->SetStringField(TEXT("trackName"), Track->GetDisplayName().ToString());
        TrackObj->SetStringField(TEXT("trackClass"), Track->GetClass()->GetName());

        // Get sections
        TArray<TSharedPtr<FJsonValue>> SectionsArray;

        for (UMovieSceneSection* Section : Track->GetAllSections())
        {
            if (!Section) continue;

            TSharedRef<FJsonObject> SectionObj = MakeShared<FJsonObject>();

            // Section timing
            TRange<FFrameNumber> SectionRange = Section->GetRange();
            if (SectionRange.HasLowerBound())
            {
                double SectionStart = TickResolution.AsSeconds(SectionRange.GetLowerBoundValue());
                SectionObj->SetNumberField(TEXT("startTime"), SectionStart);
            }
            if (SectionRange.HasUpperBound())
            {
                double SectionEnd = TickResolution.AsSeconds(SectionRange.GetUpperBoundValue());
                SectionObj->SetNumberField(TEXT("endTime"), SectionEnd);
            }

            // Check if this is an Audio Section
            if (UMovieSceneAudioSection* AudioSection = Cast<UMovieSceneAudioSection>(Section))
            {
                SectionObj->SetStringField(TEXT("type"), TEXT("Audio"));

                USoundBase* Sound = AudioSection->GetSound();
                if (Sound)
                {
                    SectionObj->SetStringField(TEXT("soundName"), Sound->GetName());
                    SectionObj->SetStringField(TEXT("soundPath"), Sound->GetPathName());
                    SectionObj->SetNumberField(TEXT("soundDuration"), Sound->GetDuration());
                }
            }
            // Check if this is a Sub Section (nested sequence)
            else if (UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(Section))
            {
                SectionObj->SetStringField(TEXT("type"), TEXT("SubSequence"));

                UMovieSceneSequence* SubSequence = SubSection->GetSequence();
                if (SubSequence)
                {
                    SectionObj->SetStringField(TEXT("subSequenceName"), SubSequence->GetName());
                    SectionObj->SetStringField(TEXT("subSequencePath"), SubSequence->GetPathName());
                }
            }
            // Check for Skeletal Animation
            else if (UMovieSceneSkeletalAnimationSection* AnimSection = Cast<UMovieSceneSkeletalAnimationSection>(Section))
            {
                SectionObj->SetStringField(TEXT("type"), TEXT("SkeletalAnimation"));
            }
            else
            {
                SectionObj->SetStringField(TEXT("type"), Section->GetClass()->GetName());
            }

            SectionsArray.Add(MakeShared<FJsonValueObject>(SectionObj));
        }

        TrackObj->SetArrayField(TEXT("sections"), SectionsArray);
        TracksArray.Add(MakeShared<FJsonValueObject>(TrackObj));
    }

    Response->SetArrayField(TEXT("tracks"), TracksArray);

    // ---- Object Bindings (for character/object tracks) ----
    TArray<TSharedPtr<FJsonValue>> BindingsArray;

    for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
    {
        TSharedRef<FJsonObject> BindingObj = MakeShared<FJsonObject>();
        BindingObj->SetStringField(TEXT("name"), Binding.GetName());
        BindingObj->SetStringField(TEXT("guid"), Binding.GetObjectGuid().ToString());
        BindingObj->SetNumberField(TEXT("trackCount"), Binding.GetTracks().Num());

        // Get tracks for this binding
        TArray<TSharedPtr<FJsonValue>> BindingTracksArray;
        for (UMovieSceneTrack* Track : Binding.GetTracks())
        {
            if (!Track) continue;

            TSharedRef<FJsonObject> BTrackObj = MakeShared<FJsonObject>();
            BTrackObj->SetStringField(TEXT("trackName"), Track->GetDisplayName().ToString());
            BTrackObj->SetStringField(TEXT("trackClass"), Track->GetClass()->GetName());
            BTrackObj->SetNumberField(TEXT("sectionCount"), Track->GetAllSections().Num());

            BindingTracksArray.Add(MakeShared<FJsonValueObject>(BTrackObj));
        }
        BindingObj->SetArrayField(TEXT("tracks"), BindingTracksArray);

        BindingsArray.Add(MakeShared<FJsonValueObject>(BindingObj));
    }

    Response->SetArrayField(TEXT("objectBindings"), BindingsArray);

    return JsonToString(Response);
}

// ============================================================
// HandleRemoveAudioTracks - Remove audio tracks from sequences
// ============================================================
FString FAgenticMCPServer::HandleRemoveAudioTracks(const FString& Body)
{
	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

    TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();

    // Parse JSON body
    TSharedPtr<FJsonObject> JsonBody;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, JsonBody) || !JsonBody.IsValid())
    {
        Response->SetStringField(TEXT("error"), TEXT("Invalid JSON body"));
        return JsonToString(Response);
    }

    // Get sequence path or "all" flag
    FString SequencePath = JsonBody->GetStringField(TEXT("sequencePath"));
    bool bRemoveAll = JsonBody->GetBoolField(TEXT("all"));
    bool bMusicOnly = JsonBody->GetBoolField(TEXT("musicOnly"));

    TArray<ULevelSequence*> SequencesToProcess;

    if (bRemoveAll)
    {
        // Find all sequences in the project
        IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
        TArray<FAssetData> SequenceAssets;
        AssetRegistry.GetAssetsByClass(ULevelSequence::StaticClass()->GetClassPathName(), SequenceAssets);

        for (const FAssetData& Asset : SequenceAssets)
        {
            ULevelSequence* Seq = Cast<ULevelSequence>(Asset.GetAsset());
            if (Seq)
            {
                SequencesToProcess.Add(Seq);
            }
        }
    }
    else if (!SequencePath.IsEmpty())
    {
        ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath);
        if (Sequence)
        {
            SequencesToProcess.Add(Sequence);
        }
    }

    if (SequencesToProcess.Num() == 0)
    {
        Response->SetStringField(TEXT("error"), TEXT("No sequences found. Provide 'sequencePath' or set 'all':true"));
        return JsonToString(Response);
    }

    int32 TotalTracksRemoved = 0;
    TArray<TSharedPtr<FJsonValue>> ModifiedSequences;

    for (ULevelSequence* Sequence : SequencesToProcess)
    {
        UMovieScene* MovieScene = Sequence->GetMovieScene();
        if (!MovieScene) continue;

        TArray<UMovieSceneTrack*> TracksToRemove;

        // Find audio tracks (using GetTracks instead of GetMasterTracks)
        for (UMovieSceneTrack* Track : MovieScene->GetTracks())
        {
            if (UMovieSceneAudioTrack* AudioTrack = Cast<UMovieSceneAudioTrack>(Track))
            {
                if (bMusicOnly)
                {
                    // Check if this is music (heuristic: check name or asset path)
                    FString TrackName = AudioTrack->GetDisplayName().ToString().ToLower();
                    bool bIsMusic = TrackName.Contains(TEXT("music")) ||
                                    TrackName.Contains(TEXT("bgm")) ||
                                    TrackName.Contains(TEXT("ambient")) ||
                                    TrackName.Contains(TEXT("loop"));

                    // Also check section sound names
                    for (UMovieSceneSection* Section : AudioTrack->GetAllSections())
                    {
                        if (UMovieSceneAudioSection* AudioSection = Cast<UMovieSceneAudioSection>(Section))
                        {
                            USoundBase* Sound = AudioSection->GetSound();
                            if (Sound)
                            {
                                FString SoundName = Sound->GetName().ToLower();
                                if (SoundName.Contains(TEXT("music")) ||
                                    SoundName.Contains(TEXT("bgm")) ||
                                    SoundName.Contains(TEXT("loop")))
                                {
                                    bIsMusic = true;
                                }
                            }
                        }
                    }

                    if (bIsMusic)
                    {
                        TracksToRemove.Add(AudioTrack);
                    }
                }
                else
                {
                    // Remove all audio tracks
                    TracksToRemove.Add(AudioTrack);
                }
            }
        }

        // Remove the tracks
        for (UMovieSceneTrack* Track : TracksToRemove)
        {
            MovieScene->RemoveTrack(*Track);
            TotalTracksRemoved++;
        }

        if (TracksToRemove.Num() > 0)
        {
            // Mark package dirty
            Sequence->MarkPackageDirty();

            TSharedRef<FJsonObject> ModSeq = MakeShared<FJsonObject>();
            ModSeq->SetStringField(TEXT("name"), Sequence->GetName());
            ModSeq->SetStringField(TEXT("path"), Sequence->GetPathName());
            ModSeq->SetNumberField(TEXT("tracksRemoved"), TracksToRemove.Num());
            ModifiedSequences.Add(MakeShared<FJsonValueObject>(ModSeq));
        }
    }

    Response->SetBoolField(TEXT("success"), true);
    Response->SetNumberField(TEXT("totalTracksRemoved"), TotalTracksRemoved);
    Response->SetNumberField(TEXT("sequencesModified"), ModifiedSequences.Num());
    Response->SetArrayField(TEXT("modifiedSequences"), ModifiedSequences);
    Response->SetStringField(TEXT("note"), TEXT("Changes are in memory. Save modified sequences to persist."));

    return JsonToString(Response);
}
