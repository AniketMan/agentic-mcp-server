// Handlers_ComponentEdit.cpp
// Component manipulation handlers for AgenticMCP.
// Target: UE 5.4 - 5.6
//
// Endpoints:
//   componentList             - List all components on an actor
//   componentRemove           - Remove a component from an actor
//   componentSetProperty      - Set a property on a component (generic)
//   componentSetTransform     - Set relative transform on a component
//   componentSetVisibility    - Set visibility on a component
//   componentSetCollision     - Set collision profile/response on a component

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/DecalComponent.h"
#include "Components/ChildActorComponent.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// Helper: get editor world
static UWorld* GetEditorWorld_Comp()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

// Helper: find actor by name
static AActor* FindActorByName_Comp(const FString& Name)
{
	UWorld* World = GetEditorWorld_Comp();
	if (!World) return nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == Name || (*It)->GetName() == Name)
		{
			return *It;
		}
	}
	return nullptr;
}

// Helper: find component by name on an actor
static UActorComponent* FindComponentByName(AActor* Actor, const FString& CompName)
{
	if (!Actor) return nullptr;

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);

	for (UActorComponent* Comp : Components)
	{
		if (Comp->GetName() == CompName || Comp->GetFName().ToString() == CompName)
		{
			return Comp;
		}
	}
	return nullptr;
}

// ============================================================
// componentList - List all components on an actor
// Params: actorName
// ============================================================
FString FAgenticMCPServer::HandleComponentList(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName"));

	AActor* Actor = FindActorByName_Comp(ActorName);
	if (!Actor) return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);

	TArray<TSharedPtr<FJsonValue>> CompArr;
	for (int32 i = 0; i < Components.Num(); ++i)
	{
		UActorComponent* Comp = Components[i];
		if (!Comp) continue;

		TSharedRef<FJsonObject> CompJson = MakeShared<FJsonObject>();
		CompJson->SetNumberField(TEXT("index"), i);
		CompJson->SetStringField(TEXT("name"), Comp->GetName());
		CompJson->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
		CompJson->SetBoolField(TEXT("isSceneComponent"), Comp->IsA<USceneComponent>());

		if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
		{
			FVector Loc = SceneComp->GetRelativeLocation();
			FRotator Rot = SceneComp->GetRelativeRotation();
			FVector Scale = SceneComp->GetRelativeScale3D();

			TSharedRef<FJsonObject> TransJson = MakeShared<FJsonObject>();
			TransJson->SetNumberField(TEXT("lx"), Loc.X);
			TransJson->SetNumberField(TEXT("ly"), Loc.Y);
			TransJson->SetNumberField(TEXT("lz"), Loc.Z);
			TransJson->SetNumberField(TEXT("rx"), Rot.Roll);
			TransJson->SetNumberField(TEXT("ry"), Rot.Pitch);
			TransJson->SetNumberField(TEXT("rz"), Rot.Yaw);
			TransJson->SetNumberField(TEXT("sx"), Scale.X);
			TransJson->SetNumberField(TEXT("sy"), Scale.Y);
			TransJson->SetNumberField(TEXT("sz"), Scale.Z);
			CompJson->SetObjectField(TEXT("relativeTransform"), TransJson);

			CompJson->SetBoolField(TEXT("visible"), SceneComp->IsVisible());

			// Parent component
			if (SceneComp->GetAttachParent())
			{
				CompJson->SetStringField(TEXT("attachedTo"), SceneComp->GetAttachParent()->GetName());
			}
		}

		if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Comp))
		{
			CompJson->SetStringField(TEXT("collisionProfile"), PrimComp->GetCollisionProfileName().ToString());
			CompJson->SetBoolField(TEXT("simulatePhysics"), PrimComp->IsSimulatingPhysics());
		}

		if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Comp))
		{
			if (SMC->GetStaticMesh())
				CompJson->SetStringField(TEXT("staticMesh"), SMC->GetStaticMesh()->GetName());
			CompJson->SetNumberField(TEXT("materialCount"), SMC->GetNumMaterials());
		}

		CompArr.Add(MakeShared<FJsonValueObject>(CompJson));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("actorName"), ActorName);
	OutJson->SetNumberField(TEXT("componentCount"), CompArr.Num());
	OutJson->SetArrayField(TEXT("components"), CompArr);
	return JsonToString(OutJson);
}

// ============================================================
// componentRemove - Remove a component from an actor
// Params: actorName, componentName
// ============================================================
FString FAgenticMCPServer::HandleComponentRemove(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString CompName = Json->GetStringField(TEXT("componentName"));

	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName"));
	if (CompName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: componentName"));

	AActor* Actor = FindActorByName_Comp(ActorName);
	if (!Actor) return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	UActorComponent* Comp = FindComponentByName(Actor, CompName);
	if (!Comp) return MakeErrorJson(FString::Printf(TEXT("Component not found: %s"), *CompName));

	// Don't remove root component
	if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
	{
		if (SceneComp == Actor->GetRootComponent())
			return MakeErrorJson(TEXT("Cannot remove root component"));
	}

	FString ClassName = Comp->GetClass()->GetName();
	Comp->DestroyComponent();
	Actor->MarkPackageDirty();

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("actorName"), ActorName);
	OutJson->SetStringField(TEXT("removedComponent"), CompName);
	OutJson->SetStringField(TEXT("removedClass"), ClassName);
	return JsonToString(OutJson);
}

// ============================================================
// componentSetProperty - Set a generic property on a component
// Params: actorName, componentName, propertyName, value
// Uses UE property system (FProperty) for generic access
// ============================================================
FString FAgenticMCPServer::HandleComponentSetProperty(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString CompName = Json->GetStringField(TEXT("componentName"));
	FString PropName = Json->GetStringField(TEXT("propertyName"));

	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName"));
	if (CompName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: componentName"));
	if (PropName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: propertyName"));
	if (!Json->HasField(TEXT("value"))) return MakeErrorJson(TEXT("Missing required field: value"));

	AActor* Actor = FindActorByName_Comp(ActorName);
	if (!Actor) return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	UActorComponent* Comp = FindComponentByName(Actor, CompName);
	if (!Comp) return MakeErrorJson(FString::Printf(TEXT("Component not found: %s"), *CompName));

	// Use UE property system for generic property access
	FProperty* Prop = Comp->GetClass()->FindPropertyByName(FName(*PropName));
	if (!Prop)
		return MakeErrorJson(FString::Printf(TEXT("Property '%s' not found on component class '%s'"), *PropName, *Comp->GetClass()->GetName()));

	// Get the value as string and import it
	FString ValueStr;
	if (Json->TryGetStringField(TEXT("value"), ValueStr))
	{
		// String value - import directly
	}
	else
	{
		// Number or bool - convert to string for import
		double NumVal;
		if (Json->TryGetNumberField(TEXT("value"), NumVal))
		{
			ValueStr = FString::SanitizeFloat(NumVal);
		}
		else
		{
			bool BoolVal;
			if (Json->TryGetBoolField(TEXT("value"), BoolVal))
			{
				ValueStr = BoolVal ? TEXT("true") : TEXT("false");
			}
			else
			{
				// Try as JSON object string
				TSharedPtr<FJsonValue> Val = Json->TryGetField(TEXT("value"));
				if (Val.IsValid())
				{
					TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ValueStr);
					FJsonSerializer::Serialize(Val.ToSharedRef(), TEXT(""), Writer);
				}
			}
		}
	}

	Comp->PreEditChange(Prop);
	void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Comp);
	Prop->ImportText_Direct(*ValueStr, PropAddr, Comp, PPF_None);
	FPropertyChangedEvent PropertyChangedEvent(Prop); Comp->PostEditChangeProperty(PropertyChangedEvent);

	Actor->MarkPackageDirty();

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("actorName"), ActorName);
	OutJson->SetStringField(TEXT("componentName"), CompName);
	OutJson->SetStringField(TEXT("propertyName"), PropName);
	OutJson->SetStringField(TEXT("setValue"), ValueStr);
	OutJson->SetStringField(TEXT("propertyType"), Prop->GetCPPType());
	return JsonToString(OutJson);
}

// ============================================================
// componentSetTransform - Set relative transform on a component
// Params: actorName, componentName,
//         lx, ly, lz (location), rx, ry, rz (rotation),
//         sx, sy, sz (scale, default 1.0)
// ============================================================
FString FAgenticMCPServer::HandleComponentSetTransform(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString CompName = Json->GetStringField(TEXT("componentName"));

	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName"));
	if (CompName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: componentName"));

	AActor* Actor = FindActorByName_Comp(ActorName);
	if (!Actor) return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	UActorComponent* Comp = FindComponentByName(Actor, CompName);
	if (!Comp) return MakeErrorJson(FString::Printf(TEXT("Component not found: %s"), *CompName));

	USceneComponent* SceneComp = Cast<USceneComponent>(Comp);
	if (!SceneComp) return MakeErrorJson(TEXT("Component is not a SceneComponent, cannot set transform"));

	// Get current values as defaults
	FVector CurLoc = SceneComp->GetRelativeLocation();
	FRotator CurRot = SceneComp->GetRelativeRotation();
	FVector CurScale = SceneComp->GetRelativeScale3D();

	float LX = Json->HasField(TEXT("lx")) ? (float)Json->GetNumberField(TEXT("lx")) : CurLoc.X;
	float LY = Json->HasField(TEXT("ly")) ? (float)Json->GetNumberField(TEXT("ly")) : CurLoc.Y;
	float LZ = Json->HasField(TEXT("lz")) ? (float)Json->GetNumberField(TEXT("lz")) : CurLoc.Z;

	float RX = Json->HasField(TEXT("rx")) ? (float)Json->GetNumberField(TEXT("rx")) : CurRot.Roll;
	float RY = Json->HasField(TEXT("ry")) ? (float)Json->GetNumberField(TEXT("ry")) : CurRot.Pitch;
	float RZ = Json->HasField(TEXT("rz")) ? (float)Json->GetNumberField(TEXT("rz")) : CurRot.Yaw;

	float SX = Json->HasField(TEXT("sx")) ? (float)Json->GetNumberField(TEXT("sx")) : CurScale.X;
	float SY = Json->HasField(TEXT("sy")) ? (float)Json->GetNumberField(TEXT("sy")) : CurScale.Y;
	float SZ = Json->HasField(TEXT("sz")) ? (float)Json->GetNumberField(TEXT("sz")) : CurScale.Z;

	SceneComp->SetRelativeLocation(FVector(LX, LY, LZ));
	SceneComp->SetRelativeRotation(FRotator(RY, RZ, RX)); // Pitch=RY, Yaw=RZ, Roll=RX
	SceneComp->SetRelativeScale3D(FVector(SX, SY, SZ));

	Actor->MarkPackageDirty();

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("actorName"), ActorName);
	OutJson->SetStringField(TEXT("componentName"), CompName);
	OutJson->SetNumberField(TEXT("lx"), LX);
	OutJson->SetNumberField(TEXT("ly"), LY);
	OutJson->SetNumberField(TEXT("lz"), LZ);
	OutJson->SetNumberField(TEXT("rx"), RX);
	OutJson->SetNumberField(TEXT("ry"), RY);
	OutJson->SetNumberField(TEXT("rz"), RZ);
	OutJson->SetNumberField(TEXT("sx"), SX);
	OutJson->SetNumberField(TEXT("sy"), SY);
	OutJson->SetNumberField(TEXT("sz"), SZ);
	return JsonToString(OutJson);
}

// ============================================================
// componentSetVisibility - Set visibility on a component
// Params: actorName, componentName, visible (bool),
//         propagateToChildren (bool, default true)
// ============================================================
FString FAgenticMCPServer::HandleComponentSetVisibility(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString CompName = Json->GetStringField(TEXT("componentName"));

	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName"));
	if (CompName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: componentName"));
	if (!Json->HasField(TEXT("visible"))) return MakeErrorJson(TEXT("Missing required field: visible"));

	bool bVisible = Json->GetBoolField(TEXT("visible"));
	bool bPropagate = Json->HasField(TEXT("propagateToChildren")) ? Json->GetBoolField(TEXT("propagateToChildren")) : true;

	AActor* Actor = FindActorByName_Comp(ActorName);
	if (!Actor) return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	UActorComponent* Comp = FindComponentByName(Actor, CompName);
	if (!Comp) return MakeErrorJson(FString::Printf(TEXT("Component not found: %s"), *CompName));

	USceneComponent* SceneComp = Cast<USceneComponent>(Comp);
	if (!SceneComp) return MakeErrorJson(TEXT("Component is not a SceneComponent, cannot set visibility"));

	SceneComp->SetVisibility(bVisible, bPropagate);
	Actor->MarkPackageDirty();

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("actorName"), ActorName);
	OutJson->SetStringField(TEXT("componentName"), CompName);
	OutJson->SetBoolField(TEXT("visible"), bVisible);
	OutJson->SetBoolField(TEXT("propagateToChildren"), bPropagate);
	return JsonToString(OutJson);
}

// ============================================================
// componentSetCollision - Set collision profile/response
// Params: actorName, componentName,
//         collisionProfile (opt, e.g. "BlockAll", "OverlapAll", "NoCollision"),
//         simulatePhysics (opt, bool),
//         generateOverlapEvents (opt, bool)
// ============================================================
FString FAgenticMCPServer::HandleComponentSetCollision(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString CompName = Json->GetStringField(TEXT("componentName"));

	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName"));
	if (CompName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: componentName"));

	AActor* Actor = FindActorByName_Comp(ActorName);
	if (!Actor) return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	UActorComponent* Comp = FindComponentByName(Actor, CompName);
	if (!Comp) return MakeErrorJson(FString::Printf(TEXT("Component not found: %s"), *CompName));

	UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Comp);
	if (!PrimComp) return MakeErrorJson(TEXT("Component is not a PrimitiveComponent, cannot set collision"));

	FString CollisionProfile;
	if (Json->TryGetStringField(TEXT("collisionProfile"), CollisionProfile))
	{
		PrimComp->SetCollisionProfileName(FName(*CollisionProfile));
	}

	if (Json->HasField(TEXT("simulatePhysics")))
	{
		bool bSimPhys = Json->GetBoolField(TEXT("simulatePhysics"));
		PrimComp->SetSimulatePhysics(bSimPhys);
	}

	if (Json->HasField(TEXT("generateOverlapEvents")))
	{
		bool bOverlap = Json->GetBoolField(TEXT("generateOverlapEvents"));
		PrimComp->SetGenerateOverlapEvents(bOverlap);
	}

	Actor->MarkPackageDirty();

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("actorName"), ActorName);
	OutJson->SetStringField(TEXT("componentName"), CompName);
	OutJson->SetStringField(TEXT("collisionProfile"), PrimComp->GetCollisionProfileName().ToString());
	OutJson->SetBoolField(TEXT("simulatePhysics"), PrimComp->IsSimulatingPhysics());
	OutJson->SetBoolField(TEXT("generateOverlapEvents"), PrimComp->GetGenerateOverlapEvents());
	return JsonToString(OutJson);
}
