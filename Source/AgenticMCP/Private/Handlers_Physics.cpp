// Handlers_Physics.cpp
// Physics inspection and control handlers for AgenticMCP.
// Provides visibility into physics bodies, constraints, and simulation state.
//
// Endpoints:
//   physicsGetBodyInfo    - Get physics body info for an actor (mass, velocity, simulation state)
//   physicsSetSimulate    - Enable/disable physics simulation on an actor
//   physicsApplyForce     - Apply a force or impulse to an actor
//   physicsListConstraints - List all physics constraint actors in the level
//   physicsGetOverlaps    - Get overlapping actors for a given actor

#include "AgenticMCPServer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsConstraintActor.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

static UWorld* GetEditorWorld_Phys()
{
	if (GEditor) return GEditor->GetEditorWorldContext().World();
	return nullptr;
}

static AActor* FindActorByLabel_Phys(UWorld* World, const FString& Label)
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == Label || (*It)->GetName() == Label)
			return *It;
	}
	return nullptr;
}

// ============================================================
// physicsGetBodyInfo - Get physics body info for an actor
// ============================================================
FString FAgenticMCPServer::HandlePhysicsGetBodyInfo(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName"));

	UWorld* World = GetEditorWorld_Phys();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	AActor* Actor = FindActorByLabel_Phys(World, ActorName);
	if (!Actor) return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	// Find the root primitive component
	UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
	if (!PrimComp)
	{
		// Try to find any primitive component
		PrimComp = Actor->FindComponentByClass<UPrimitiveComponent>();
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("actorName"), Actor->GetActorLabel());

	if (!PrimComp)
	{
		Result->SetBoolField(TEXT("hasPhysicsBody"), false);
		Result->SetStringField(TEXT("note"), TEXT("Actor has no PrimitiveComponent"));
		return JsonToString(Result);
	}

	Result->SetBoolField(TEXT("hasPhysicsBody"), true);
	Result->SetStringField(TEXT("componentName"), PrimComp->GetName());
	Result->SetStringField(TEXT("componentClass"), PrimComp->GetClass()->GetName());
	Result->SetBoolField(TEXT("simulatePhysics"), PrimComp->IsSimulatingPhysics());
	Result->SetBoolField(TEXT("gravityEnabled"), PrimComp->IsGravityEnabled());
	Result->SetNumberField(TEXT("mass"), PrimComp->GetMass());

	// Collision
	Result->SetBoolField(TEXT("collisionEnabled"), PrimComp->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
	Result->SetStringField(TEXT("collisionProfile"), PrimComp->GetCollisionProfileName().ToString());

	// Linear/angular damping
	Result->SetNumberField(TEXT("linearDamping"), PrimComp->GetLinearDamping());
	Result->SetNumberField(TEXT("angularDamping"), PrimComp->GetAngularDamping());

	// Velocity (only meaningful during PIE)
	FVector LinVel = PrimComp->GetPhysicsLinearVelocity();
	FVector AngVel = PrimComp->GetPhysicsAngularVelocityInDegrees();
	Result->SetNumberField(TEXT("linearVelocityX"), LinVel.X);
	Result->SetNumberField(TEXT("linearVelocityY"), LinVel.Y);
	Result->SetNumberField(TEXT("linearVelocityZ"), LinVel.Z);
	Result->SetNumberField(TEXT("angularVelocityX"), AngVel.X);
	Result->SetNumberField(TEXT("angularVelocityY"), AngVel.Y);
	Result->SetNumberField(TEXT("angularVelocityZ"), AngVel.Z);

	return JsonToString(Result);
}

// ============================================================
// physicsSetSimulate - Enable/disable physics simulation
// ============================================================
FString FAgenticMCPServer::HandlePhysicsSetSimulate(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName"));
	if (!Json->HasField(TEXT("simulate")))
		return MakeErrorJson(TEXT("Missing required field: simulate (bool)"));
	bool bSimulate = Json->GetBoolField(TEXT("simulate"));

	UWorld* World = GetEditorWorld_Phys();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	AActor* Actor = FindActorByLabel_Phys(World, ActorName);
	if (!Actor) return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
	if (!PrimComp)
		PrimComp = Actor->FindComponentByClass<UPrimitiveComponent>();
	if (!PrimComp)
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' has no PrimitiveComponent"), *ActorName));

	bool bOldState = PrimComp->IsSimulatingPhysics();
	PrimComp->SetSimulatePhysics(bSimulate);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
	Result->SetBoolField(TEXT("previousState"), bOldState);
	Result->SetBoolField(TEXT("newState"), bSimulate);
	return JsonToString(Result);
}

// ============================================================
// physicsApplyForce - Apply force or impulse to an actor
// ============================================================
FString FAgenticMCPServer::HandlePhysicsApplyForce(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName"));

	FString Mode = Json->HasField(TEXT("mode")) ? Json->GetStringField(TEXT("mode")) : TEXT("impulse");
	double ForceX = Json->HasField(TEXT("forceX")) ? Json->GetNumberField(TEXT("forceX")) : 0.0;
	double ForceY = Json->HasField(TEXT("forceY")) ? Json->GetNumberField(TEXT("forceY")) : 0.0;
	double ForceZ = Json->HasField(TEXT("forceZ")) ? Json->GetNumberField(TEXT("forceZ")) : 0.0;

	UWorld* World = GetEditorWorld_Phys();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	AActor* Actor = FindActorByLabel_Phys(World, ActorName);
	if (!Actor) return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
	if (!PrimComp)
		PrimComp = Actor->FindComponentByClass<UPrimitiveComponent>();
	if (!PrimComp)
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' has no PrimitiveComponent"), *ActorName));

	if (!PrimComp->IsSimulatingPhysics())
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' is not simulating physics. Enable simulation first."), *ActorName));

	FVector Force(ForceX, ForceY, ForceZ);

	if (Mode == TEXT("impulse"))
	{
		PrimComp->AddImpulse(Force);
	}
	else if (Mode == TEXT("force"))
	{
		PrimComp->AddForce(Force);
	}
	else if (Mode == TEXT("velocity"))
	{
		PrimComp->SetPhysicsLinearVelocity(Force);
	}
	else
	{
		return MakeErrorJson(FString::Printf(TEXT("Unknown mode: %s. Supported: impulse, force, velocity"), *Mode));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetNumberField(TEXT("forceX"), ForceX);
	Result->SetNumberField(TEXT("forceY"), ForceY);
	Result->SetNumberField(TEXT("forceZ"), ForceZ);
	return JsonToString(Result);
}

// ============================================================
// physicsListConstraints - List all physics constraint actors
// ============================================================
FString FAgenticMCPServer::HandlePhysicsListConstraints(const FString& Body)
{
	UWorld* World = GetEditorWorld_Phys();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	TArray<TSharedPtr<FJsonValue>> ConstraintArr;
	for (TActorIterator<APhysicsConstraintActor> It(World); It; ++It)
	{
		APhysicsConstraintActor* ConstraintActor = *It;
		UPhysicsConstraintComponent* Comp = ConstraintActor->GetConstraintComp();
		if (!Comp) continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), ConstraintActor->GetActorLabel());
		Entry->SetStringField(TEXT("constraintActor1"), Comp->ConstraintActor1 ? Comp->ConstraintActor1->GetActorLabel() : TEXT("(none)"));
		Entry->SetStringField(TEXT("constraintActor2"), Comp->ConstraintActor2 ? Comp->ConstraintActor2->GetActorLabel() : TEXT("(none)"));
		Entry->SetBoolField(TEXT("constraintBroken"), Comp->IsBroken());

		ConstraintArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), ConstraintArr.Num());
	Result->SetArrayField(TEXT("constraints"), ConstraintArr);
	return JsonToString(Result);
}

// ============================================================
// physicsGetOverlaps - Get overlapping actors
// ============================================================
FString FAgenticMCPServer::HandlePhysicsGetOverlaps(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName"));

	UWorld* World = GetEditorWorld_Phys();
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	AActor* Actor = FindActorByLabel_Phys(World, ActorName);
	if (!Actor) return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	TArray<AActor*> Overlapping;
	Actor->GetOverlappingActors(Overlapping);

	TArray<TSharedPtr<FJsonValue>> OverlapArr;
	for (AActor* Other : Overlapping)
	{
		if (!Other) continue;
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Other->GetActorLabel());
		Entry->SetStringField(TEXT("class"), Other->GetClass()->GetName());
		OverlapArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("actorName"), Actor->GetActorLabel());
	Result->SetNumberField(TEXT("overlapCount"), OverlapArr.Num());
	Result->SetArrayField(TEXT("overlappingActors"), OverlapArr);
	return JsonToString(Result);
}
