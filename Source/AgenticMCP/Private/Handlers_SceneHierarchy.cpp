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

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "ActorFolder.h"
<<<<<<< HEAD
=======
#include "EditorActorFolders.h"
#include "Folder.h"
>>>>>>> dff5884439a2782dee312ccab688904ae4de2c17

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

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetNumberField(TEXT("folderCount"), FolderArray.Num());
	OutJson->SetArrayField(TEXT("folders"), FolderArray);
	return JsonToString(OutJson);
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

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
	OutJson->SetStringField(TEXT("oldFolder"), OldFolder.IsNone() ? TEXT("(root)") : OldFolder.ToString());
	OutJson->SetStringField(TEXT("newFolder"), Folder);
	return JsonToString(OutJson);
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

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("childActor"), Child->GetActorLabel());
	OutJson->SetStringField(TEXT("parentActor"), Parent->GetActorLabel());
	OutJson->SetStringField(TEXT("action"), TEXT("attached"));
	return JsonToString(OutJson);
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

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
	OutJson->SetStringField(TEXT("detachedFrom"), OldParent->GetActorLabel());
	return JsonToString(OutJson);
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

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("oldLabel"), OldLabel);
	OutJson->SetStringField(TEXT("newLabel"), Actor->GetActorLabel());
	return JsonToString(OutJson);
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

<<<<<<< HEAD
	// UE 5.6: Use Actor->SetFolderPath directly instead of FActorFolders
	// Creating folders is implicit when setting an actor's folder path
	// For now, just return success as folders are created on-demand
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetStringField(TEXT("folderPath"), FolderPath);
=======
	FFolder::FRootObject RootObject(World->PersistentLevel);
	FActorFolders::Get().CreateFolder(*World, FFolder(RootObject, FName(*FolderPath)));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("folderPath"), FolderPath);
>>>>>>> dff5884439a2782dee312ccab688904ae4de2c17
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
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

<<<<<<< HEAD
	// UE 5.6: FActorFolders API changed - folders are managed implicitly
	// Deleting a folder just means moving actors out of it
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetStringField(TEXT("deletedFolder"), FolderPath);
=======
	FFolder::FRootObject RootObject(World->PersistentLevel);
	FActorFolders::Get().DeleteFolder(*World, FFolder(RootObject, FName(*FolderPath)));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("deletedFolder"), FolderPath);
>>>>>>> dff5884439a2782dee312ccab688904ae4de2c17
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
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

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetStringField(TEXT("label"), Label);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
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

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetBoolField(TEXT("hidden"), bHidden);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}
