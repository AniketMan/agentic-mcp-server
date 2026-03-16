// Handlers_Actors.cpp
// Actor management handlers for AgenticMCP.
// These handlers list, inspect, spawn, delete, and modify actors in the current world.
//
// Endpoints implemented:
//   /api/list-actors         - List actors in the current world (with optional class/name filter)
//   /api/get-actor           - Get detailed info about a specific actor
//   /api/spawn-actor         - Spawn a new actor in the world
//   /api/delete-actor        - Delete an actor from the world
//   /api/set-actor-property  - Set a property value on an actor
//   /api/set-actor-transform - Set an actor's transform (location, rotation, scale)

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "EditorLevelUtils.h"
#include "Selection.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "UObject/PropertyAccessUtil.h"

// ============================================================
// Helper: Get the current editor world
// ============================================================

static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

// ============================================================
// Helper: Serialize an actor's basic info to JSON
// ============================================================

static TSharedRef<FJsonObject> SerializeActorBasic(AActor* Actor)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("name"), Actor->GetName());
	Json->SetStringField(TEXT("label"), Actor->GetActorLabel());
	Json->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

	// Transform
	FVector Loc = Actor->GetActorLocation();
	FRotator Rot = Actor->GetActorRotation();
	FVector Scale = Actor->GetActorScale3D();

	TSharedRef<FJsonObject> Transform = MakeShared<FJsonObject>();
	Transform->SetNumberField(TEXT("locationX"), Loc.X);
	Transform->SetNumberField(TEXT("locationY"), Loc.Y);
	Transform->SetNumberField(TEXT("locationZ"), Loc.Z);
	Transform->SetNumberField(TEXT("rotationPitch"), Rot.Pitch);
	Transform->SetNumberField(TEXT("rotationYaw"), Rot.Yaw);
	Transform->SetNumberField(TEXT("rotationRoll"), Rot.Roll);
	Transform->SetNumberField(TEXT("scaleX"), Scale.X);
	Transform->SetNumberField(TEXT("scaleY"), Scale.Y);
	Transform->SetNumberField(TEXT("scaleZ"), Scale.Z);
	Json->SetObjectField(TEXT("transform"), Transform);

	// Level
	if (Actor->GetLevel())
	{
		UWorld* OwnerWorld = Actor->GetLevel()->GetTypedOuter<UWorld>();
		if (OwnerWorld)
		{
			Json->SetStringField(TEXT("level"), OwnerWorld->GetName());
		}
	}

	return Json;
}

// ============================================================
// HandleListActors
// POST /api/list-actors { "classFilter": "StaticMeshActor", "nameFilter": "", "level": "" }
// ============================================================

FString FAgenticMCPServer::HandleListActors(const FString& Body)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world available. Is a level open in the editor?"));
	}

	// Parse optional filters
	FString ClassFilter;
	FString NameFilter;
	FString LevelFilter;

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (Json.IsValid())
	{
		ClassFilter = Json->GetStringField(TEXT("classFilter"));
		NameFilter = Json->GetStringField(TEXT("nameFilter"));
		LevelFilter = Json->GetStringField(TEXT("level"));
	}

	TArray<TSharedPtr<FJsonValue>> ActorArray;
	int32 Count = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		// Apply class filter
		if (!ClassFilter.IsEmpty())
		{
			FString ClassName = Actor->GetClass()->GetName();
			if (!ClassName.Contains(ClassFilter)) continue;
		}

		// Apply name filter
		if (!NameFilter.IsEmpty())
		{
			FString ActorName = Actor->GetName();
			FString ActorLabel = Actor->GetActorLabel();
			if (!ActorName.Contains(NameFilter) && !ActorLabel.Contains(NameFilter)) continue;
		}

		// Apply level filter
		if (!LevelFilter.IsEmpty() && Actor->GetLevel())
		{
			UWorld* OwnerWorld = Actor->GetLevel()->GetTypedOuter<UWorld>();
			if (OwnerWorld && !OwnerWorld->GetName().Contains(LevelFilter)) continue;
		}

		ActorArray.Add(MakeShared<FJsonValueObject>(SerializeActorBasic(Actor)));
		Count++;
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetNumberField(TEXT("count"), Count);
	OutJson->SetStringField(TEXT("worldName"), World->GetName());
	OutJson->SetArrayField(TEXT("actors"), ActorArray);
	return JsonToString(OutJson);
}

// ============================================================
// HandleGetActor
// POST /api/get-actor { "name": "StaticMeshActor_C_0" }
// ============================================================

FString FAgenticMCPServer::HandleGetActor(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("name"));
	if (ActorName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: name"));

	UWorld* World = GetEditorWorld();
	if (!World)
		return MakeErrorJson(TEXT("No editor world available"));

	// Find actor by name or label
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && (Actor->GetName() == ActorName || Actor->GetActorLabel() == ActorName))
		{
			FoundActor = Actor;
			break;
		}
	}

	if (!FoundActor)
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' not found"), *ActorName));

	// Build detailed response
	TSharedRef<FJsonObject> OutJson = SerializeActorBasic(FoundActor);

	// Components
	TArray<TSharedPtr<FJsonValue>> CompArray;
	TInlineComponentArray<UActorComponent*> Components;
	FoundActor->GetComponents(Components);
	for (UActorComponent* Comp : Components)
	{
		if (!Comp) continue;
		TSharedRef<FJsonObject> CompJson = MakeShared<FJsonObject>();
		CompJson->SetStringField(TEXT("name"), Comp->GetName());
		CompJson->SetStringField(TEXT("class"), Comp->GetClass()->GetName());

		// If it's a scene component, include relative transform
		USceneComponent* SceneComp = Cast<USceneComponent>(Comp);
		if (SceneComp)
		{
			FVector RelLoc = SceneComp->GetRelativeLocation();
			CompJson->SetStringField(TEXT("relativeLocation"),
				FString::Printf(TEXT("%.1f, %.1f, %.1f"), RelLoc.X, RelLoc.Y, RelLoc.Z));
		}

		CompArray.Add(MakeShared<FJsonValueObject>(CompJson));
	}
	OutJson->SetArrayField(TEXT("components"), CompArray);

	// Blueprint-visible properties (EditAnywhere or BlueprintReadWrite)
	TArray<TSharedPtr<FJsonValue>> PropArray;
	for (TFieldIterator<FProperty> PropIt(FoundActor->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		if (!Prop) continue;

		// Only include editable/blueprint-visible properties
		if (!Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible)) continue;

		TSharedRef<FJsonObject> PropJson = MakeShared<FJsonObject>();
		PropJson->SetStringField(TEXT("name"), Prop->GetName());
		PropJson->SetStringField(TEXT("type"), Prop->GetCPPType());

		// Try to get the value as a string
		FString ValueStr;
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(FoundActor);
		if (ValuePtr)
		{
			Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, FoundActor, PPF_None);
		}
		if (!ValueStr.IsEmpty())
		{
			PropJson->SetStringField(TEXT("value"), ValueStr);
		}

		PropArray.Add(MakeShared<FJsonValueObject>(PropJson));
	}
	OutJson->SetArrayField(TEXT("properties"), PropArray);

	return JsonToString(OutJson);
}

// ============================================================
// HandleSpawnActor
// POST /api/spawn-actor { "className": "StaticMeshActor", "label": "MyActor_01",
//                          "locationX": 0, "locationY": 0, "locationZ": 0 }
// ============================================================

FString FAgenticMCPServer::HandleSpawnActor(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ClassName = Json->GetStringField(TEXT("className"));
	FString Label = Json->GetStringField(TEXT("label"));

	if (ClassName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: className"));

	UWorld* World = GetEditorWorld();
	if (!World)
		return MakeErrorJson(TEXT("No editor world available"));

	// Find the class
	UClass* ActorClass = nullptr;

	// Try as a Blueprint class first (append _C if needed)
	for (const FAssetData& Asset : AllBlueprintAssets)
	{
		if (Asset.AssetName.ToString() == ClassName)
		{
			UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset());
			if (BP && BP->GeneratedClass && BP->GeneratedClass->IsChildOf(AActor::StaticClass()))
			{
				ActorClass = BP->GeneratedClass;
				break;
			}
		}
	}

	// Try as a native class
	if (!ActorClass)
	{
		ActorClass = FindClassByName(ClassName);
		if (ActorClass && !ActorClass->IsChildOf(AActor::StaticClass()))
		{
			ActorClass = nullptr;
		}
	}

	if (!ActorClass)
		return MakeErrorJson(FString::Printf(TEXT("Actor class '%s' not found"), *ClassName));

	// Parse transform
	FVector Location(0.0, 0.0, 0.0);
	FRotator Rotation(0.0, 0.0, 0.0);
	FVector Scale(1.0, 1.0, 1.0);

	if (Json->HasField(TEXT("locationX"))) Location.X = Json->GetNumberField(TEXT("locationX"));
	if (Json->HasField(TEXT("locationY"))) Location.Y = Json->GetNumberField(TEXT("locationY"));
	if (Json->HasField(TEXT("locationZ"))) Location.Z = Json->GetNumberField(TEXT("locationZ"));
	if (Json->HasField(TEXT("rotationPitch"))) Rotation.Pitch = Json->GetNumberField(TEXT("rotationPitch"));
	if (Json->HasField(TEXT("rotationYaw"))) Rotation.Yaw = Json->GetNumberField(TEXT("rotationYaw"));
	if (Json->HasField(TEXT("rotationRoll"))) Rotation.Roll = Json->GetNumberField(TEXT("rotationRoll"));
	if (Json->HasField(TEXT("scaleX"))) Scale.X = Json->GetNumberField(TEXT("scaleX"));
	if (Json->HasField(TEXT("scaleY"))) Scale.Y = Json->GetNumberField(TEXT("scaleY"));
	if (Json->HasField(TEXT("scaleZ"))) Scale.Z = Json->GetNumberField(TEXT("scaleZ"));

	// Spawn the actor
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Use FTransform overload -- works on UE 5.4 through 5.7+
	FTransform SpawnTransform(FRotator(Rotation.Pitch, Rotation.Yaw, Rotation.Roll).Quaternion(), Location, Scale);
	AActor* NewActor = World->SpawnActor<AActor>(ActorClass, SpawnTransform, SpawnParams);
	if (!NewActor)
		return MakeErrorJson(TEXT("Failed to spawn actor"));

	NewActor->SetActorScale3D(Scale);

	if (!Label.IsEmpty())
	{
		NewActor->SetActorLabel(Label);
	}

	UE_LOG(LogTemp, Display, TEXT("AgenticMCP: Spawned '%s' (%s) at (%.0f, %.0f, %.0f)"),
		*NewActor->GetName(), *ClassName, Location.X, Location.Y, Location.Z);

	TSharedRef<FJsonObject> OutJson = SerializeActorBasic(NewActor);
	OutJson->SetBoolField(TEXT("success"), true);
	return JsonToString(OutJson);
}

// ============================================================
// HandleDeleteActor
// POST /api/delete-actor { "name": "StaticMeshActor_C_0" }
// ============================================================

FString FAgenticMCPServer::HandleDeleteActor(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("name"));
	if (ActorName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: name"));

	UWorld* World = GetEditorWorld();
	if (!World)
		return MakeErrorJson(TEXT("No editor world available"));

	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetName() == ActorName || (*It)->GetActorLabel() == ActorName)
		{
			FoundActor = *It;
			break;
		}
	}

	if (!FoundActor)
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' not found"), *ActorName));

	FString DeletedName = FoundActor->GetName();
	FString DeletedClass = FoundActor->GetClass()->GetName();

	bool bDestroyed = World->DestroyActor(FoundActor);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), bDestroyed);
	OutJson->SetStringField(TEXT("deletedActor"), DeletedName);
	OutJson->SetStringField(TEXT("deletedClass"), DeletedClass);
	return JsonToString(OutJson);
}

// ============================================================
// HandleSetActorProperty
// POST /api/set-actor-property { "name": "StaticMeshActor_C_0", "property": "MyProperty", "value": "3" }
// ============================================================

FString FAgenticMCPServer::HandleSetActorProperty(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("name"));
	FString PropertyName = Json->GetStringField(TEXT("property"));
	FString Value = Json->GetStringField(TEXT("value"));

	if (ActorName.IsEmpty() || PropertyName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required fields: name, property"));

	UWorld* World = GetEditorWorld();
	if (!World)
		return MakeErrorJson(TEXT("No editor world available"));

	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetName() == ActorName || (*It)->GetActorLabel() == ActorName)
		{
			FoundActor = *It;
			break;
		}
	}

	if (!FoundActor)
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' not found"), *ActorName));

	// Find the property
	FProperty* Prop = FoundActor->GetClass()->FindPropertyByName(FName(*PropertyName));

	// If not found on actor, search components
	UActorComponent* TargetComp = nullptr;
	if (!Prop)
	{
		TInlineComponentArray<UActorComponent*> Components;
		FoundActor->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			Prop = Comp->GetClass()->FindPropertyByName(FName(*PropertyName));
			if (Prop)
			{
				TargetComp = Comp;
				break;
			}
		}
	}

	if (!Prop)
		return MakeErrorJson(FString::Printf(
			TEXT("Property '%s' not found on actor '%s' or its components"),
			*PropertyName, *ActorName));

	// Set the value
	UObject* TargetObj = TargetComp ? (UObject*)TargetComp : (UObject*)FoundActor;
	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(TargetObj);

	// Use ImportText for type-safe value setting
	const TCHAR* ImportResult = Prop->ImportText_Direct(*Value, ValuePtr, TargetObj, PPF_None);
	if (!ImportResult)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Failed to set property '%s' to '%s' (type: %s)"),
			*PropertyName, *Value, *Prop->GetCPPType()));
	}

	// Mark modified
	FoundActor->MarkPackageDirty();

	// Read back the value to confirm
	FString ReadBack;
	Prop->ExportTextItem_Direct(ReadBack, ValuePtr, nullptr, TargetObj, PPF_None);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("actor"), ActorName);
	OutJson->SetStringField(TEXT("property"), PropertyName);
	OutJson->SetStringField(TEXT("newValue"), ReadBack);
	return JsonToString(OutJson);
}

// ============================================================
// HandleSetActorTransform
// POST /api/set-actor-transform { "name": "...", "locationX": 0, ... }
// ============================================================

FString FAgenticMCPServer::HandleSetActorTransform(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("name"));
	if (ActorName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: name"));

	UWorld* World = GetEditorWorld();
	if (!World)
		return MakeErrorJson(TEXT("No editor world available"));

	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetName() == ActorName || (*It)->GetActorLabel() == ActorName)
		{
			FoundActor = *It;
			break;
		}
	}

	if (!FoundActor)
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' not found"), *ActorName));

	// Get current values as defaults
	FVector Location = FoundActor->GetActorLocation();
	FRotator Rotation = FoundActor->GetActorRotation();
	FVector Scale = FoundActor->GetActorScale3D();

	// Override with provided values
	if (Json->HasField(TEXT("locationX"))) Location.X = Json->GetNumberField(TEXT("locationX"));
	if (Json->HasField(TEXT("locationY"))) Location.Y = Json->GetNumberField(TEXT("locationY"));
	if (Json->HasField(TEXT("locationZ"))) Location.Z = Json->GetNumberField(TEXT("locationZ"));
	if (Json->HasField(TEXT("rotationPitch"))) Rotation.Pitch = Json->GetNumberField(TEXT("rotationPitch"));
	if (Json->HasField(TEXT("rotationYaw"))) Rotation.Yaw = Json->GetNumberField(TEXT("rotationYaw"));
	if (Json->HasField(TEXT("rotationRoll"))) Rotation.Roll = Json->GetNumberField(TEXT("rotationRoll"));
	if (Json->HasField(TEXT("scaleX"))) Scale.X = Json->GetNumberField(TEXT("scaleX"));
	if (Json->HasField(TEXT("scaleY"))) Scale.Y = Json->GetNumberField(TEXT("scaleY"));
	if (Json->HasField(TEXT("scaleZ"))) Scale.Z = Json->GetNumberField(TEXT("scaleZ"));

	FoundActor->SetActorLocation(Location);
	FoundActor->SetActorRotation(Rotation);
	FoundActor->SetActorScale3D(Scale);
	FoundActor->MarkPackageDirty();

	TSharedRef<FJsonObject> OutJson = SerializeActorBasic(FoundActor);
	OutJson->SetBoolField(TEXT("success"), true);
	return JsonToString(OutJson);
}

// ============================================================================
// ACTOR MUTATION HANDLERS
// ============================================================================

// --- actorDuplicate ---
// Duplicate an actor in the level.
// Body: { "actorName": "Cube_01", "newName": "Cube_02" (optional), "offset": [100,0,0] (optional) }
FString FAgenticMCPServer::HandleActorDuplicate(const FString& Body)
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
	if (ActorName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'actorName'"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world"));
	}

	AActor* SourceActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorName || It->GetName() == ActorName)
		{
			SourceActor = *It;
			break;
		}
	}
	if (!SourceActor)
	{
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
	}

	// Select the actor and duplicate via editor
	GEditor->SelectNone(false, true, false);
	GEditor->SelectActor(SourceActor, true, false, true);
	GEditor->edactDuplicateSelected(World->PersistentLevel, false);

	// Find the newly created actor (last selected)
	AActor* NewActor = nullptr;
	USelection* SelectedActors = GEditor->GetSelectedActors();
	if (SelectedActors)
	{
		for (int32 i = 0; i < SelectedActors->Num(); ++i)
		{
			NewActor = Cast<AActor>(SelectedActors->GetSelectedObject(i));
		}
	}

	if (!NewActor)
	{
		return MakeErrorJson(TEXT("Duplication failed"));
	}

	// Apply offset if provided
	const TArray<TSharedPtr<FJsonValue>>* OffsetArray = nullptr;
	if (Json->TryGetArrayField(TEXT("offset"), OffsetArray) && OffsetArray->Num() >= 3)
	{
		FVector Offset((*OffsetArray)[0]->AsNumber(), (*OffsetArray)[1]->AsNumber(), (*OffsetArray)[2]->AsNumber());
		NewActor->SetActorLocation(SourceActor->GetActorLocation() + Offset);
	}

	// Rename if requested
	FString NewName;
	if (Json->TryGetStringField(TEXT("newName"), NewName) && !NewName.IsEmpty())
	{
		NewActor->SetActorLabel(NewName);
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetStringField(TEXT("newActorName"), NewActor->GetActorLabel());
	OutJson->SetStringField(TEXT("newActorPath"), NewActor->GetPathName());
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}

// --- actorSetMobility ---
// Set actor mobility (Static, Stationary, Movable).
// Body: { "actorName": "Cube_01", "mobility": "Movable" }
FString FAgenticMCPServer::HandleActorSetMobility(const FString& Body)
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
	FString Mobility = Json->GetStringField(TEXT("mobility"));

	if (ActorName.IsEmpty() || Mobility.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'actorName' or 'mobility'"));
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

	USceneComponent* RootComp = FoundActor->GetRootComponent();
	if (!RootComp)
	{
		return MakeErrorJson(TEXT("Actor has no root component"));
	}

	EComponentMobility::Type MobilityType;
	if (Mobility == TEXT("Static")) MobilityType = EComponentMobility::Static;
	else if (Mobility == TEXT("Stationary")) MobilityType = EComponentMobility::Stationary;
	else if (Mobility == TEXT("Movable")) MobilityType = EComponentMobility::Movable;
	else
	{
		return MakeErrorJson(TEXT("Invalid mobility. Use: Static, Stationary, Movable"));
	}

	RootComp->SetMobility(MobilityType);
	FoundActor->MarkPackageDirty();

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetStringField(TEXT("mobility"), Mobility);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}

// --- actorSetTags ---
// Set, add, or remove actor tags.
// Body: { "actorName": "Cube_01", "tags": ["Interactive", "Destructible"], "mode": "set"|"add"|"remove" }
FString FAgenticMCPServer::HandleActorSetTags(const FString& Body)
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
	FString Mode = TEXT("set");
	Json->TryGetStringField(TEXT("mode"), Mode);

	const TArray<TSharedPtr<FJsonValue>>* TagsArray = nullptr;
	if (!Json->TryGetArrayField(TEXT("tags"), TagsArray))
	{
		return MakeErrorJson(TEXT("Missing 'tags' array"));
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

	TArray<FName> NewTags;
	for (const TSharedPtr<FJsonValue>& Val : *TagsArray)
	{
		NewTags.Add(FName(*Val->AsString()));
	}

	if (Mode == TEXT("set"))
	{
		FoundActor->Tags = NewTags;
	}
	else if (Mode == TEXT("add"))
	{
		for (const FName& Tag : NewTags)
		{
			FoundActor->Tags.AddUnique(Tag);
		}
	}
	else if (Mode == TEXT("remove"))
	{
		for (const FName& Tag : NewTags)
		{
			FoundActor->Tags.Remove(Tag);
		}
	}
	else
	{
		return MakeErrorJson(TEXT("Invalid mode. Use: set, add, remove"));
	}

	FoundActor->MarkPackageDirty();

	TArray<TSharedPtr<FJsonValue>> TagsOut;
	for (const FName& Tag : FoundActor->Tags)
	{
		TagsOut.Add(MakeShared<FJsonValueString>(Tag.ToString()));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetArrayField(TEXT("tags"), TagsOut);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}

// --- actorSetLayer ---
// Set the editor layer for an actor.
// Body: { "actorName": "Cube_01", "layer": "Environment" }
FString FAgenticMCPServer::HandleActorSetLayer(const FString& Body)
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
	FString Layer = Json->GetStringField(TEXT("layer"));

	if (ActorName.IsEmpty() || Layer.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'actorName' or 'layer'"));
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

	FoundActor->Layers.Empty();
	FoundActor->Layers.Add(FName(*Layer));
	FoundActor->MarkPackageDirty();

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetStringField(TEXT("layer"), Layer);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}
