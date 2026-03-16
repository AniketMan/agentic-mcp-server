// Handlers_SceneHierarchy.cpp
// Scene hierarchy / World Outliner management handlers for AgenticMCP.
// Provides reparenting, grouping, folder organization, and hierarchy inspection.
//
// Endpoints:
//   sceneGetHierarchy     - Get the full actor hierarchy tree (parent-child + folders)
//   sceneSetActorFolder   - Move an actor to a specific World Outliner folder
//   sceneAttachActor      - Attach one actor to another (parent-child)
//   sceneDetachActor      - Detach an actor from its parent
//   sceneRenameActor      - Rename an actor's label
//   sceneGroupActors      - Group selected actors into a GroupActor

#include "AgenticMCPServer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GroupActor.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "ActorFolder.h"
#include "EditorActorFolders.h"
#include "Folder.h"

static UWorld* GetEditorWorld_Scene()
{
	if (GEditor) return GEditor->GetEditorWorldContext().World();
	return nullptr;
}

static AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == Label || (*It)->GetName() == Label)
			return *It;
	}
	return nullptr;
}

// ============================================================
// sceneGetHierarchy - Full actor hierarchy tree
// ============================================================
FString FAgenticMCPServer::HandleSceneGetHierarchy(const FString& Body)
{
	UWorld* World = GetEditorWorld_Scene();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	TSharedPtr<FJsonObject> BodyJson = ParseBodyJson(Body);
	FString FolderFilter;
	if (BodyJson.IsValid() && BodyJson->HasField(TEXT("folder")))
		FolderFilter = BodyJson->GetStringField(TEXT("folder"));

	// Build folder -> actors map
	TMap<FString, TArray<AActor*>> FolderMap;
	TArray<AActor*> RootActors;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		FName FolderPath = Actor->GetFolderPath();
		FString Folder = FolderPath.IsNone() ? TEXT("(root)") : FolderPath.ToString();

		if (!FolderFilter.IsEmpty() && !Folder.Contains(FolderFilter))
			continue;

		FolderMap.FindOrAdd(Folder).Add(Actor);
	}

	TArray<TSharedPtr<FJsonValue>> FolderArray;
	for (auto& Pair : FolderMap)
	{
		TSharedRef<FJsonObject> FolderJson = MakeShared<FJsonObject>();
		FolderJson->SetStringField(TEXT("folder"), Pair.Key);

		TArray<TSharedPtr<FJsonValue>> ActorArr;
		for (AActor* Actor : Pair.Value)
		{
			TSharedRef<FJsonObject> ActorJson = MakeShared<FJsonObject>();
			ActorJson->SetStringField(TEXT("name"), Actor->GetName());
			ActorJson->SetStringField(TEXT("label"), Actor->GetActorLabel());
			ActorJson->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

			// Parent info
			AActor* Parent = Actor->GetAttachParentActor();
			if (Parent)
			{
				ActorJson->SetStringField(TEXT("parentActor"), Parent->GetActorLabel());
			}

			// Children
			TArray<AActor*> Children;
			Actor->GetAttachedActors(Children);
			if (Children.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> ChildArr;
				for (AActor* Child : Children)
				{
					ChildArr.Add(MakeShared<FJsonValueString>(Child->GetActorLabel()));
				}
				ActorJson->SetArrayField(TEXT("children"), ChildArr);
			}

			ActorArr.Add(MakeShared<FJsonValueObject>(ActorJson));
		}
		FolderJson->SetNumberField(TEXT("actorCount"), ActorArr.Num());
		FolderJson->SetArrayField(TEXT("actors"), ActorArr);
		FolderArray.Add(MakeShared<FJsonValueObject>(FolderJson));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("folderCount"), FolderArray.Num());
	Result->SetArrayField(TEXT("folders"), FolderArray);
	return JsonToString(Result);
}

// ============================================================
// sceneSetActorFolder - Move actor to a World Outliner folder
// ============================================================
FString FAgenticMCPServer::HandleSceneSetActorFolder(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString Folder = Json->GetStringField(TEXT("folder"));
	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName"));
	if (!Json->HasField(TEXT("folder"))) return MakeErrorJson(TEXT("Missing required field: folder"));

	UWorld* World = GetEditorWorld_Scene();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	AActor* Actor = FindActorByLabel(World, ActorName);
	if (!Actor) return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	FName OldFolder = Actor->GetFolderPath();
	Actor->SetFolderPath(FName(*Folder));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("oldFolder"), OldFolder.IsNone() ? TEXT("(root)") : OldFolder.ToString());
	Result->SetStringField(TEXT("newFolder"), Folder);
	return JsonToString(Result);
}

// ============================================================
// sceneAttachActor - Attach one actor to another
// ============================================================
FString FAgenticMCPServer::HandleSceneAttachActor(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ChildName = Json->GetStringField(TEXT("childActor"));
	FString ParentName = Json->GetStringField(TEXT("parentActor"));
	if (ChildName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: childActor"));
	if (ParentName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: parentActor"));

	UWorld* World = GetEditorWorld_Scene();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	AActor* Child = FindActorByLabel(World, ChildName);
	AActor* Parent = FindActorByLabel(World, ParentName);
	if (!Child) return MakeErrorJson(FString::Printf(TEXT("Child actor not found: %s"), *ChildName));
	if (!Parent) return MakeErrorJson(FString::Printf(TEXT("Parent actor not found: %s"), *ParentName));

	if (Child == Parent)
		return MakeErrorJson(TEXT("Cannot attach an actor to itself"));

	// Attach
	Child->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("childActor"), Child->GetActorLabel());
	Result->SetStringField(TEXT("parentActor"), Parent->GetActorLabel());
	Result->SetStringField(TEXT("action"), TEXT("attached"));
	return JsonToString(Result);
}

// ============================================================
// sceneDetachActor - Detach an actor from its parent
// ============================================================
FString FAgenticMCPServer::HandleSceneDetachActor(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName"));

	UWorld* World = GetEditorWorld_Scene();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	AActor* Actor = FindActorByLabel(World, ActorName);
	if (!Actor) return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	AActor* OldParent = Actor->GetAttachParentActor();
	if (!OldParent)
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' is not attached to any parent"), *ActorName));

	Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("detachedFrom"), OldParent->GetActorLabel());
	return JsonToString(Result);
}

// ============================================================
// sceneRenameActor - Rename an actor's label
// ============================================================
FString FAgenticMCPServer::HandleSceneRenameActor(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString NewLabel = Json->GetStringField(TEXT("newLabel"));
	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName"));
	if (NewLabel.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: newLabel"));

	UWorld* World = GetEditorWorld_Scene();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	AActor* Actor = FindActorByLabel(World, ActorName);
	if (!Actor) return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	FString OldLabel = Actor->GetActorLabel();
	Actor->SetActorLabel(NewLabel);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("oldLabel"), OldLabel);
	Result->SetStringField(TEXT("newLabel"), Actor->GetActorLabel());
	return JsonToString(Result);
}

// ============================================================================
// SCENE HIERARCHY MUTATION HANDLERS
// ============================================================================

// --- sceneCreateFolder ---
// Create an actor folder in the outliner.
// Body: { "folderPath": "Environment/Trees" }
FString FAgenticMCPServer::HandleSceneCreateFolder(const FString& Body)
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

	FString FolderPath = Json->GetStringField(TEXT("folderPath"));
	if (FolderPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'folderPath'"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world"));
	}

	FFolder::FRootObject RootObject(World->PersistentLevel);
	FActorFolders::Get().CreateFolder(*World, FFolder(RootObject, FName(*FolderPath)));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("folderPath"), FolderPath);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, Writer);
	return Out;
}

// --- sceneDeleteFolder ---
// Delete an actor folder from the outliner.
// Body: { "folderPath": "Environment/Trees" }
FString FAgenticMCPServer::HandleSceneDeleteFolder(const FString& Body)
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

	FString FolderPath = Json->GetStringField(TEXT("folderPath"));
	if (FolderPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'folderPath'"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world"));
	}

	FFolder::FRootObject RootObject(World->PersistentLevel);
	FActorFolders::Get().DeleteFolder(*World, FFolder(RootObject, FName(*FolderPath)));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("deletedFolder"), FolderPath);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, Writer);
	return Out;
}

// --- sceneSetActorLabel ---
// Set the display name (label) of an actor.
// Body: { "actorName": "StaticMeshActor_0", "label": "Hero_Platform" }
FString FAgenticMCPServer::HandleSceneSetActorLabel(const FString& Body)
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

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString Label = Json->GetStringField(TEXT("label"));

	if (ActorName.IsEmpty() || Label.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'actorName' or 'label'"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world"));
	}

	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorName || It->GetName() == ActorName)
		{
			FoundActor = *It;
			break;
		}
	}
	if (!FoundActor)
	{
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
	}

	FoundActor->SetActorLabel(Label);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("label"), Label);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, Writer);
	return Out;
}

// --- sceneHideActor ---
// Set editor visibility of an actor.
// Body: { "actorName": "Cube_01", "hidden": true }
FString FAgenticMCPServer::HandleSceneHideActor(const FString& Body)
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

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	bool bHidden = true;
	Json->TryGetBoolField(TEXT("hidden"), bHidden);

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world"));
	}

	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorName || It->GetName() == ActorName)
		{
			FoundActor = *It;
			break;
		}
	}
	if (!FoundActor)
	{
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
	}

	FoundActor->SetIsTemporarilyHiddenInEditor(bHidden);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetBoolField(TEXT("hidden"), bHidden);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result, Writer);
	return Out;
}
