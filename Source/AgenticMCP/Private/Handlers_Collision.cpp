// Handlers_Collision.cpp
// Collision/physics trace handlers for AgenticMCP
// Performs line traces and collision queries

#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "Editor.h"
#include "DrawDebugHelpers.h"

FString FAgenticMCPServer::HandleCollisionTrace(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body. Expected: {\"start\": [x,y,z], \"end\": [x,y,z]}"));
	}

	// Parse start position
	const TArray<TSharedPtr<FJsonValue>>* StartArray = nullptr;
	if (!BodyJson->TryGetArrayField(TEXT("start"), StartArray) || StartArray->Num() < 3)
	{
		return MakeErrorJson(TEXT("Missing or invalid 'start' array [x, y, z]"));
	}
	FVector Start(
		(*StartArray)[0]->AsNumber(),
		(*StartArray)[1]->AsNumber(),
		(*StartArray)[2]->AsNumber()
	);

	// Parse end position
	const TArray<TSharedPtr<FJsonValue>>* EndArray = nullptr;
	if (!BodyJson->TryGetArrayField(TEXT("end"), EndArray) || EndArray->Num() < 3)
	{
		return MakeErrorJson(TEXT("Missing or invalid 'end' array [x, y, z]"));
	}
	FVector End(
		(*EndArray)[0]->AsNumber(),
		(*EndArray)[1]->AsNumber(),
		(*EndArray)[2]->AsNumber()
	);

	// Optional parameters
	bool bTraceComplex = BodyJson->HasField(TEXT("traceComplex")) ? BodyJson->GetBoolField(TEXT("traceComplex")) : false;
	bool bDrawDebug = BodyJson->HasField(TEXT("drawDebug")) ? BodyJson->GetBoolField(TEXT("drawDebug")) : false;
	float DebugDuration = BodyJson->HasField(TEXT("debugDuration")) ? BodyJson->GetNumberField(TEXT("debugDuration")) : 5.0f;
	FString TraceChannel = BodyJson->HasField(TEXT("channel")) ? BodyJson->GetStringField(TEXT("channel")) : TEXT("Visibility");

	UWorld* World = nullptr;
	if (GEditor && GEditor->PlayWorld)
	{
		World = GEditor->PlayWorld;
	}
	else if (GEditor)
	{
		World = GEditor->GetEditorWorldContext().World();
	}

	if (!World)
	{
		return MakeErrorJson(TEXT("No world available"));
	}

	// Determine trace channel
	ECollisionChannel Channel = ECC_Visibility;
	if (TraceChannel.Equals(TEXT("Camera"), ESearchCase::IgnoreCase))
	{
		Channel = ECC_Camera;
	}
	else if (TraceChannel.Equals(TEXT("WorldStatic"), ESearchCase::IgnoreCase))
	{
		Channel = ECC_WorldStatic;
	}
	else if (TraceChannel.Equals(TEXT("WorldDynamic"), ESearchCase::IgnoreCase))
	{
		Channel = ECC_WorldDynamic;
	}
	else if (TraceChannel.Equals(TEXT("Pawn"), ESearchCase::IgnoreCase))
	{
		Channel = ECC_Pawn;
	}
	else if (TraceChannel.Equals(TEXT("PhysicsBody"), ESearchCase::IgnoreCase))
	{
		Channel = ECC_PhysicsBody;
	}

	// Set up collision query params
	FCollisionQueryParams QueryParams;
	QueryParams.bTraceComplex = bTraceComplex;
	QueryParams.bReturnPhysicalMaterial = true;

	// Perform the trace
	FHitResult HitResult;
	bool bHit = World->LineTraceSingleByChannel(HitResult, Start, End, Channel, QueryParams);

	// Draw debug visualization if requested
	if (bDrawDebug)
	{
		FColor LineColor = bHit ? FColor::Red : FColor::Green;
		DrawDebugLine(World, Start, bHit ? HitResult.ImpactPoint : End, LineColor, false, DebugDuration, 0, 2.0f);
		if (bHit)
		{
			DrawDebugPoint(World, HitResult.ImpactPoint, 10.0f, FColor::Yellow, false, DebugDuration);
			DrawDebugLine(World, HitResult.ImpactPoint, HitResult.ImpactPoint + HitResult.ImpactNormal * 50.0f, FColor::Blue, false, DebugDuration, 0, 1.0f);
		}
	}

	// Build result
	Result->SetBoolField(TEXT("hit"), bHit);

	TSharedRef<FJsonObject> StartObj = MakeShared<FJsonObject>();
	StartObj->SetNumberField(TEXT("x"), Start.X);
	StartObj->SetNumberField(TEXT("y"), Start.Y);
	StartObj->SetNumberField(TEXT("z"), Start.Z);
	Result->SetObjectField(TEXT("traceStart"), StartObj);

	TSharedRef<FJsonObject> EndObj = MakeShared<FJsonObject>();
	EndObj->SetNumberField(TEXT("x"), End.X);
	EndObj->SetNumberField(TEXT("y"), End.Y);
	EndObj->SetNumberField(TEXT("z"), End.Z);
	Result->SetObjectField(TEXT("traceEnd"), EndObj);

	Result->SetStringField(TEXT("channel"), TraceChannel);
	Result->SetNumberField(TEXT("traceDistance"), FVector::Dist(Start, End));

	if (bHit)
	{
		TSharedRef<FJsonObject> HitObj = MakeShared<FJsonObject>();

		// Impact point
		HitObj->SetNumberField(TEXT("impactX"), HitResult.ImpactPoint.X);
		HitObj->SetNumberField(TEXT("impactY"), HitResult.ImpactPoint.Y);
		HitObj->SetNumberField(TEXT("impactZ"), HitResult.ImpactPoint.Z);

		// Impact normal
		HitObj->SetNumberField(TEXT("normalX"), HitResult.ImpactNormal.X);
		HitObj->SetNumberField(TEXT("normalY"), HitResult.ImpactNormal.Y);
		HitObj->SetNumberField(TEXT("normalZ"), HitResult.ImpactNormal.Z);

		// Distance
		HitObj->SetNumberField(TEXT("distance"), HitResult.Distance);

		// Hit actor info
		if (HitResult.GetActor())
		{
			HitObj->SetStringField(TEXT("actorName"), HitResult.GetActor()->GetName());
			HitObj->SetStringField(TEXT("actorClass"), HitResult.GetActor()->GetClass()->GetName());
		}

		// Hit component info
		if (HitResult.GetComponent())
		{
			HitObj->SetStringField(TEXT("componentName"), HitResult.GetComponent()->GetName());
			HitObj->SetStringField(TEXT("componentClass"), HitResult.GetComponent()->GetClass()->GetName());
		}

		// Bone name (for skeletal meshes)
		if (HitResult.BoneName != NAME_None)
		{
			HitObj->SetStringField(TEXT("boneName"), HitResult.BoneName.ToString());
		}

		// Physical material
		if (HitResult.PhysMaterial.IsValid())
		{
			HitObj->SetStringField(TEXT("physicalMaterial"), HitResult.PhysMaterial->GetName());
		}

		// Face index (for complex traces)
		if (bTraceComplex)
		{
			HitObj->SetNumberField(TEXT("faceIndex"), HitResult.FaceIndex);
		}

		Result->SetObjectField(TEXT("hitResult"), HitObj);
	}

	return JsonToString(Result);
}
