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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), Count);
	Result->SetStringField(TEXT("worldName"), World->GetName());
	Result->SetArrayField(TEXT("actors"), ActorArray);
	return JsonToString(Result);
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
	TSharedRef<FJsonObject> Result = SerializeActorBasic(FoundActor);

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
	Result->SetArrayField(TEXT("components"), CompArray);

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
	Result->SetArrayField(TEXT("properties"), PropArray);

	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = SerializeActorBasic(NewActor);
	Result->SetBoolField(TEXT("success"), true);
	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bDestroyed);
	Result->SetStringField(TEXT("deletedActor"), DeletedName);
	Result->SetStringField(TEXT("deletedClass"), DeletedClass);
	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor"), ActorName);
	Result->SetStringField(TEXT("property"), PropertyName);
	Result->SetStringField(TEXT("newValue"), ReadBack);
	return JsonToString(Result);
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

	TSharedRef<FJsonObject> Result = SerializeActorBasic(FoundActor);
	Result->SetBoolField(TEXT("success"), true);
	return JsonToString(Result);
}
