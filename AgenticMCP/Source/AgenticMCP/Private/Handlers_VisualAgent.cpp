// Handlers_VisualAgent.cpp
// VisualAgent automation handlers for AgenticMCP.
// These handlers provide scene snapshots, viewport screenshots, camera control,
// and selection management for visual automation of Unreal Engine 5.
//
// Endpoints implemented:
//   /api/scene-snapshot   - Get hierarchical scene tree with short refs (like accessibility tree)
//   /api/screenshot       - Capture viewport screenshot using UE's built-in system
//   /api/focus-actor      - Move editor camera to focus on an actor
//   /api/select-actor     - Select actor(s) in the editor
//   /api/set-viewport     - Set camera position and rotation
//   /api/wait-ready       - Wait for assets/compile/render to complete
//   /api/resolve-ref      - Resolve a short ref (a0, a1.c0) to actor/component name

#include "AgenticMCPServer.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "LevelEditor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/Base64.h"
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UnrealClient.h"
#include "Slate/SceneViewport.h"
#include "HighResScreenshot.h"
#include "DrawDebugHelpers.h"

// ============================================================
// Ref Registry - Maps short refs to actors/components
// Stored as static to persist across requests within session
// ============================================================

static TMap<FString, TWeakObjectPtr<UObject>> RefRegistry;
static int32 RefCounter = 0;
static double LastSnapshotTime = 0.0;

// Rate limiting for screenshots
static double LastScreenshotTime = 0.0;
static const double ScreenshotCooldownSeconds = 0.5;

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
// Helper: Clear and rebuild ref registry
// ============================================================

static void ClearRefRegistry()
{
	RefRegistry.Empty();
	RefCounter = 0;
}

// ============================================================
// Helper: Register an object and return its short ref
// ============================================================

static FString RegisterRef(UObject* Obj, const FString& ParentRef = TEXT(""))
{
	if (!Obj) return TEXT("");

	FString Ref;
	if (ParentRef.IsEmpty())
	{
		Ref = FString::Printf(TEXT("a%d"), RefCounter++);
	}
	else
	{
		static TMap<FString, int32> ComponentCounters;
		int32& Counter = ComponentCounters.FindOrAdd(ParentRef);
		Ref = FString::Printf(TEXT("%s.c%d"), *ParentRef, Counter++);
	}

	RefRegistry.Add(Ref, Obj);
	return Ref;
}

// ============================================================
// Helper: Resolve ref to object
// ============================================================

static UObject* ResolveRef(const FString& Ref)
{
	TWeakObjectPtr<UObject>* Found = RefRegistry.Find(Ref);
	if (Found && Found->IsValid())
	{
		return Found->Get();
	}
	return nullptr;
}

// ============================================================
// Helper: Find actor by name OR ref
// ============================================================

static AActor* FindActorByNameOrRef(UWorld* World, const FString& NameOrRef)
{
	if (!World || NameOrRef.IsEmpty()) return nullptr;

	// First try ref registry
	UObject* Obj = ResolveRef(NameOrRef);
	if (AActor* Actor = Cast<AActor>(Obj))
	{
		return Actor;
	}

	// Fall back to name/label search
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->GetName() == NameOrRef || Actor->GetActorLabel() == NameOrRef)
		{
			return Actor;
		}
	}

	return nullptr;
}

// ============================================================
// Helper: Get active viewport client
// ============================================================

static FLevelEditorViewportClient* GetActiveViewportClient()
{
	if (!GEditor) return nullptr;

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<ILevelViewport> ActiveViewport = LevelEditorModule.GetFirstActiveViewport();

	if (ActiveViewport.IsValid())
	{
		return &ActiveViewport->GetLevelViewportClient();
	}

	return nullptr;
}

// ============================================================
// HandleSceneSnapshot
// POST /api/scene-snapshot { "classFilter": "", "includeComponents": true }
// Returns hierarchical scene tree with short refs
// ============================================================

FString FAgenticMCPServer::HandleSceneSnapshot(const FString& Body)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world available."));
	}

	// Parse options
	bool bIncludeComponents = true;
	FString ClassFilter;

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (Json.IsValid())
	{
		if (Json->HasField(TEXT("includeComponents")))
		{
			bIncludeComponents = Json->GetBoolField(TEXT("includeComponents"));
		}
		ClassFilter = Json->GetStringField(TEXT("classFilter"));
	}

	// Clear and rebuild ref registry
	ClearRefRegistry();

	// Build parent -> children map for hierarchy
	TMap<AActor*, TArray<AActor*>> ParentChildMap;
	TArray<AActor*> RootActors;

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

		AActor* Parent = Actor->GetAttachParentActor();
		if (Parent)
		{
			ParentChildMap.FindOrAdd(Parent).Add(Actor);
		}
		else
		{
			RootActors.Add(Actor);
		}
	}

	// YAML-like snapshot builder
	FString YamlSnapshot;

	// Recursive lambda to serialize actor and children
	TFunction<TSharedRef<FJsonObject>(AActor*, int32)> SerializeActorHierarchy;
	SerializeActorHierarchy = [&](AActor* Actor, int32 Depth) -> TSharedRef<FJsonObject>
	{
		FString Ref = RegisterRef(Actor);
		FString Indent = FString::ChrN(Depth * 2, ' ');

		TSharedRef<FJsonObject> ActorJson = MakeShared<FJsonObject>();
		ActorJson->SetStringField(TEXT("ref"), Ref);
		ActorJson->SetStringField(TEXT("name"), Actor->GetName());
		ActorJson->SetStringField(TEXT("label"), Actor->GetActorLabel());
		ActorJson->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

		// Transform
		FVector Loc = Actor->GetActorLocation();
		FRotator Rot = Actor->GetActorRotation();
		ActorJson->SetNumberField(TEXT("x"), Loc.X);
		ActorJson->SetNumberField(TEXT("y"), Loc.Y);
		ActorJson->SetNumberField(TEXT("z"), Loc.Z);
		ActorJson->SetNumberField(TEXT("pitch"), Rot.Pitch);
		ActorJson->SetNumberField(TEXT("yaw"), Rot.Yaw);
		ActorJson->SetNumberField(TEXT("roll"), Rot.Roll);

		// Build YAML line
		YamlSnapshot += FString::Printf(TEXT("%s- %s [%s] (%s) @ (%.0f, %.0f, %.0f)\n"),
			*Indent, *Actor->GetActorLabel(), *Ref, *Actor->GetClass()->GetName(),
			Loc.X, Loc.Y, Loc.Z);

		// Components
		if (bIncludeComponents)
		{
			TArray<TSharedPtr<FJsonValue>> ComponentsArray;
			TArray<UActorComponent*> Components;
			Actor->GetComponents(Components);

			for (UActorComponent* Comp : Components)
			{
				if (!Comp) continue;

				FString CompRef = RegisterRef(Comp, Ref);

				TSharedRef<FJsonObject> CompJson = MakeShared<FJsonObject>();
				CompJson->SetStringField(TEXT("ref"), CompRef);
				CompJson->SetStringField(TEXT("name"), Comp->GetName());
				CompJson->SetStringField(TEXT("class"), Comp->GetClass()->GetName());

				// Scene component transform
				if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
				{
					FVector CompLoc = SceneComp->GetRelativeLocation();
					CompJson->SetNumberField(TEXT("relX"), CompLoc.X);
					CompJson->SetNumberField(TEXT("relY"), CompLoc.Y);
					CompJson->SetNumberField(TEXT("relZ"), CompLoc.Z);
				}

				ComponentsArray.Add(MakeShared<FJsonValueObject>(CompJson));

				YamlSnapshot += FString::Printf(TEXT("%s    - %s [%s] (%s)\n"),
					*Indent, *Comp->GetName(), *CompRef, *Comp->GetClass()->GetName());
			}

			if (ComponentsArray.Num() > 0)
			{
				ActorJson->SetArrayField(TEXT("components"), ComponentsArray);
			}
		}

		// Child actors
		TArray<AActor*>* Children = ParentChildMap.Find(Actor);
		if (Children && Children->Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> ChildrenArray;
			for (AActor* Child : *Children)
			{
				ChildrenArray.Add(MakeShared<FJsonValueObject>(SerializeActorHierarchy(Child, Depth + 1)));
			}
			ActorJson->SetArrayField(TEXT("children"), ChildrenArray);
		}

		return ActorJson;
	};

	// Build actors array from roots
	TArray<TSharedPtr<FJsonValue>> ActorsArray;
	for (AActor* RootActor : RootActors)
	{
		ActorsArray.Add(MakeShared<FJsonValueObject>(SerializeActorHierarchy(RootActor, 0)));
	}

	LastSnapshotTime = FPlatformTime::Seconds();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("worldName"), World->GetName());
	Result->SetNumberField(TEXT("actorCount"), RefCounter);
	Result->SetArrayField(TEXT("actors"), ActorsArray);
	Result->SetStringField(TEXT("yamlSnapshot"), YamlSnapshot);

	return JsonToString(Result);
}

// ============================================================
// HandleScreenshot
// POST /api/screenshot { "width": 1280, "height": 720, "format": "png", "quality": 85 }
// Uses Unreal's built-in ScreenshotTools for GPU-efficient capture
// ============================================================

FString FAgenticMCPServer::HandleScreenshot(const FString& Body)
{
	// Rate limiting
	double CurrentTime = FPlatformTime::Seconds();
	if (CurrentTime - LastScreenshotTime < ScreenshotCooldownSeconds)
	{
		return MakeErrorJson(FString::Printf(TEXT("Screenshot rate limited. Wait %.1f seconds."),
			ScreenshotCooldownSeconds - (CurrentTime - LastScreenshotTime)));
	}

	// Parse options
	int32 TargetWidth = 1280;
	int32 TargetHeight = 720;
	FString Format = TEXT("jpeg");
	int32 Quality = 75;

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (Json.IsValid())
	{
		if (Json->HasField(TEXT("width"))) TargetWidth = Json->GetIntegerField(TEXT("width"));
		if (Json->HasField(TEXT("height"))) TargetHeight = Json->GetIntegerField(TEXT("height"));
		if (Json->HasField(TEXT("format"))) Format = Json->GetStringField(TEXT("format")).ToLower();
		if (Json->HasField(TEXT("quality"))) Quality = FMath::Clamp(Json->GetIntegerField(TEXT("quality")), 1, 100);
	}

	// Get viewport
	FLevelEditorViewportClient* ViewportClient = GetActiveViewportClient();
	if (!ViewportClient)
	{
		return MakeErrorJson(TEXT("No active viewport available."));
	}

	FViewport* Viewport = ViewportClient->Viewport;
	if (!Viewport)
	{
		return MakeErrorJson(TEXT("Viewport is null."));
	}

	// Check if viewport is valid
	FIntPoint ViewportSize = Viewport->GetSizeXY();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return MakeErrorJson(TEXT("Viewport has invalid size. Is the editor minimized?"));
	}

	// Use Unreal's built-in screenshot utility via FHighResScreenshotConfig
	// This uses the proper render pipeline and doesn't directly read the framebuffer
	FHighResScreenshotConfig& Config = GetHighResScreenshotConfig();

	// Calculate resolution multiplier to match target size while maintaining proper rendering
	float ResolutionMultiplier = FMath::Min(
		(float)TargetWidth / (float)ViewportSize.X,
		(float)TargetHeight / (float)ViewportSize.Y
	);
	ResolutionMultiplier = FMath::Max(0.1f, FMath::Min(1.0f, ResolutionMultiplier));

	// Store original config
	float OriginalMultiplier = Config.ResolutionMultiplier;
	bool bOriginalHDR = Config.bCaptureHDR;

	// Configure for our capture
	Config.ResolutionMultiplier = ResolutionMultiplier;
	Config.bCaptureHDR = false;

	// Force viewport redraw with proper frame sync
	ViewportClient->Invalidate();

	// Use FRenderTarget::ReadPixels through the scene capture - this is GPU-efficient
	// because it reads after the render pipeline completes, not directly from framebuffer
	FlushRenderingCommands();

	TArray<FColor> Bitmap;
	FIntRect CaptureRect(0, 0, ViewportSize.X, ViewportSize.Y);

	// Use GetRawData for proper GPU synchronization - reads from back buffer after present
	bool bSuccess = Viewport->ReadPixelsPtr(Bitmap.GetData(), FReadSurfaceDataFlags(), CaptureRect);

	// Fallback to standard ReadPixels if ReadPixelsPtr failed
	if (!bSuccess)
	{
		Bitmap.Reset();
		bSuccess = Viewport->ReadPixels(Bitmap, FReadSurfaceDataFlags(), CaptureRect);
	}

	// Restore config
	Config.ResolutionMultiplier = OriginalMultiplier;
	Config.bCaptureHDR = bOriginalHDR;

	if (!bSuccess || Bitmap.Num() == 0)
	{
		return MakeErrorJson(TEXT("Failed to capture screenshot. Viewport may not be ready."));
	}

	int32 Width = ViewportSize.X;
	int32 Height = ViewportSize.Y;

	// Downscale using FImageUtils for GPU-optimized resizing
	TArray<FColor> FinalBitmap;
	int32 FinalWidth = Width;
	int32 FinalHeight = Height;

	if (TargetWidth < Width || TargetHeight < Height)
	{
		FinalWidth = TargetWidth;
		FinalHeight = TargetHeight;
		FinalBitmap.SetNumUninitialized(FinalWidth * FinalHeight);

		// Use simple box filter for efficient downscaling
		float ScaleX = (float)Width / (float)FinalWidth;
		float ScaleY = (float)Height / (float)FinalHeight;

		for (int32 Y = 0; Y < FinalHeight; Y++)
		{
			for (int32 X = 0; X < FinalWidth; X++)
			{
				int32 SrcX = FMath::Clamp((int32)(X * ScaleX), 0, Width - 1);
				int32 SrcY = FMath::Clamp((int32)(Y * ScaleY), 0, Height - 1);
				FinalBitmap[Y * FinalWidth + X] = Bitmap[SrcY * Width + SrcX];
			}
		}
	}
	else
	{
		FinalBitmap = MoveTemp(Bitmap);
	}

	// Compress using ImageWrapper
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	EImageFormat ImageFormat = Format == TEXT("png") ? EImageFormat::PNG : EImageFormat::JPEG;
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);

	if (!ImageWrapper.IsValid())
	{
		return MakeErrorJson(TEXT("Failed to create image wrapper."));
	}

	// UE viewports are already in correct orientation when using ReadPixels properly
	if (!ImageWrapper->SetRaw(FinalBitmap.GetData(), FinalBitmap.Num() * sizeof(FColor), FinalWidth, FinalHeight, ERGBFormat::BGRA, 8))
	{
		return MakeErrorJson(TEXT("Failed to set raw image data."));
	}

	TArray64<uint8> CompressedData;
	if (ImageFormat == EImageFormat::JPEG)
	{
		CompressedData = ImageWrapper->GetCompressed(Quality);
	}
	else
	{
		CompressedData = ImageWrapper->GetCompressed();
	}

	if (CompressedData.Num() == 0)
	{
		return MakeErrorJson(TEXT("Failed to compress image."));
	}

	// Base64 encode
	FString Base64Data = FBase64::Encode(CompressedData.GetData(), CompressedData.Num());

	LastScreenshotTime = CurrentTime;

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("width"), FinalWidth);
	Result->SetNumberField(TEXT("height"), FinalHeight);
	Result->SetStringField(TEXT("format"), Format);
	Result->SetStringField(TEXT("mimeType"), Format == TEXT("png") ? TEXT("image/png") : TEXT("image/jpeg"));
	Result->SetNumberField(TEXT("sizeBytes"), CompressedData.Num());
	Result->SetStringField(TEXT("data"), Base64Data);

	return JsonToString(Result);
}

// ============================================================
// HandleFocusActor
// POST /api/focus-actor { "name": "BP_Character_1" } or { "ref": "a3" }
// ============================================================

FString FAgenticMCPServer::HandleFocusActor(const FString& Body)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world available."));
	}

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body."));
	}

	FString NameOrRef = Json->GetStringField(TEXT("name"));
	if (NameOrRef.IsEmpty())
	{
		NameOrRef = Json->GetStringField(TEXT("ref"));
	}

	if (NameOrRef.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'name' or 'ref' parameter."));
	}

	AActor* Actor = FindActorByNameOrRef(World, NameOrRef);
	if (!Actor)
	{
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *NameOrRef));
	}

	// Focus camera on actor
	GEditor->MoveViewportCamerasToActor(*Actor, false);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Camera focused on %s"), *Actor->GetActorLabel()));
	Result->SetStringField(TEXT("actorName"), Actor->GetName());
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());

	return JsonToString(Result);
}

// ============================================================
// HandleSelectActor
// POST /api/select-actor { "name": "BP_Character_1", "addToSelection": false }
// ============================================================

FString FAgenticMCPServer::HandleSelectActor(const FString& Body)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world available."));
	}

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body."));
	}

	FString NameOrRef = Json->GetStringField(TEXT("name"));
	if (NameOrRef.IsEmpty())
	{
		NameOrRef = Json->GetStringField(TEXT("ref"));
	}

	bool bAddToSelection = Json->GetBoolField(TEXT("addToSelection"));

	if (NameOrRef.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'name' or 'ref' parameter."));
	}

	AActor* Actor = FindActorByNameOrRef(World, NameOrRef);
	if (!Actor)
	{
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *NameOrRef));
	}

	// Select the actor
	if (!bAddToSelection)
	{
		GEditor->SelectNone(true, true);
	}
	GEditor->SelectActor(Actor, true, true);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Selected %s"), *Actor->GetActorLabel()));
	Result->SetStringField(TEXT("actorName"), Actor->GetName());
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());

	return JsonToString(Result);
}

// ============================================================
// HandleSetViewport
// POST /api/set-viewport { "locationX": 0, "locationY": 0, "locationZ": 500, "pitch": -45, "yaw": 0, "roll": 0 }
// ============================================================

FString FAgenticMCPServer::HandleSetViewport(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body."));
	}

	FLevelEditorViewportClient* ViewportClient = GetActiveViewportClient();
	if (!ViewportClient)
	{
		return MakeErrorJson(TEXT("No active viewport available."));
	}

	// Get current values as defaults
	FVector CurrentLoc = ViewportClient->GetViewLocation();
	FRotator CurrentRot = ViewportClient->GetViewRotation();

	FVector NewLoc = CurrentLoc;
	FRotator NewRot = CurrentRot;

	if (Json->HasField(TEXT("locationX"))) NewLoc.X = Json->GetNumberField(TEXT("locationX"));
	if (Json->HasField(TEXT("locationY"))) NewLoc.Y = Json->GetNumberField(TEXT("locationY"));
	if (Json->HasField(TEXT("locationZ"))) NewLoc.Z = Json->GetNumberField(TEXT("locationZ"));
	if (Json->HasField(TEXT("pitch"))) NewRot.Pitch = Json->GetNumberField(TEXT("pitch"));
	if (Json->HasField(TEXT("yaw"))) NewRot.Yaw = Json->GetNumberField(TEXT("yaw"));
	if (Json->HasField(TEXT("roll"))) NewRot.Roll = Json->GetNumberField(TEXT("roll"));

	ViewportClient->SetViewLocation(NewLoc);
	ViewportClient->SetViewRotation(NewRot);
	ViewportClient->Invalidate();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("Viewport updated"));
	Result->SetNumberField(TEXT("locationX"), NewLoc.X);
	Result->SetNumberField(TEXT("locationY"), NewLoc.Y);
	Result->SetNumberField(TEXT("locationZ"), NewLoc.Z);
	Result->SetNumberField(TEXT("pitch"), NewRot.Pitch);
	Result->SetNumberField(TEXT("yaw"), NewRot.Yaw);
	Result->SetNumberField(TEXT("roll"), NewRot.Roll);

	return JsonToString(Result);
}

// ============================================================
// HandleWaitReady
// POST /api/wait-ready { "condition": "assets" | "compile" | "render" }
// ============================================================

FString FAgenticMCPServer::HandleWaitReady(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body."));
	}

	FString Condition = Json->GetStringField(TEXT("condition")).ToLower();
	bool bReady = true;
	FString StatusMessage;

	if (Condition == TEXT("assets"))
	{
		// Check if asset registry is still loading
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		bReady = !AssetRegistryModule.Get().IsLoadingAssets();
		StatusMessage = bReady ? TEXT("Asset registry scan complete") : TEXT("Asset registry still scanning");
	}
	else if (Condition == TEXT("compile"))
	{
		// Check if any Blueprints need compilation
		bReady = true;
		int32 DirtyCount = 0;

		for (TObjectIterator<UBlueprint> It; It; ++It)
		{
			UBlueprint* BP = *It;
			if (BP && BP->Status == BS_Dirty)
			{
				DirtyCount++;
				bReady = false;
			}
		}

		StatusMessage = bReady ? TEXT("No dirty Blueprints") : FString::Printf(TEXT("%d Blueprint(s) need compilation"), DirtyCount);
	}
	else if (Condition == TEXT("render"))
	{
		// Flush rendering commands
		FlushRenderingCommands();
		bReady = true;
		StatusMessage = TEXT("Render commands flushed");
	}
	else
	{
		return MakeErrorJson(FString::Printf(TEXT("Unknown condition: %s. Use 'assets', 'compile', or 'render'."), *Condition));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("ready"), bReady);
	Result->SetStringField(TEXT("condition"), Condition);
	Result->SetStringField(TEXT("message"), StatusMessage);

	return JsonToString(Result);
}

// ============================================================
// HandleResolveRef
// POST /api/resolve-ref { "ref": "a3" }
// ============================================================

FString FAgenticMCPServer::HandleResolveRef(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body."));
	}

	FString Ref = Json->GetStringField(TEXT("ref"));
	if (Ref.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'ref' parameter."));
	}

	UObject* Obj = ResolveRef(Ref);
	if (!Obj)
	{
		return MakeErrorJson(FString::Printf(TEXT("Ref not found: %s. Run sceneSnapshot first to populate refs."), *Ref));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("ref"), Ref);
	Result->SetStringField(TEXT("name"), Obj->GetName());
	Result->SetStringField(TEXT("class"), Obj->GetClass()->GetName());
	Result->SetStringField(TEXT("pathName"), Obj->GetPathName());

	if (AActor* Actor = Cast<AActor>(Obj))
	{
		Result->SetStringField(TEXT("type"), TEXT("Actor"));
		Result->SetStringField(TEXT("label"), Actor->GetActorLabel());
	}
	else if (UActorComponent* Comp = Cast<UActorComponent>(Obj))
	{
		Result->SetStringField(TEXT("type"), TEXT("Component"));
		if (AActor* Owner = Comp->GetOwner())
		{
			Result->SetStringField(TEXT("ownerName"), Owner->GetName());
		}
	}

	return JsonToString(Result);
}

// ============================================================
// DEBUG VISUALIZATION
// ============================================================

// Storage for debug draw requests - processed on game thread
struct FDebugDrawRequest
{
	enum class EType { Sphere, Line, Text, Box, Clear };
	EType Type;
	FVector Location;
	FVector EndLocation;  // For lines
	float Radius;
	FColor Color;
	float Duration;
	FString Text;
	FString Id;  // For tracking/clearing specific draws
};

static TArray<FDebugDrawRequest> PendingDebugDraws;
static TMap<FString, FDebugDrawRequest> PersistentDraws;
static int32 DebugDrawCounter = 0;

// Helper: Parse color from string
static FColor ParseColor(const FString& ColorStr)
{
	FString Lower = ColorStr.ToLower();
	if (Lower == TEXT("red")) return FColor::Red;
	if (Lower == TEXT("green")) return FColor::Green;
	if (Lower == TEXT("blue")) return FColor::Blue;
	if (Lower == TEXT("yellow")) return FColor::Yellow;
	if (Lower == TEXT("cyan")) return FColor::Cyan;
	if (Lower == TEXT("magenta")) return FColor::Magenta;
	if (Lower == TEXT("white")) return FColor::White;
	if (Lower == TEXT("black")) return FColor::Black;
	if (Lower == TEXT("orange")) return FColor::Orange;
	if (Lower == TEXT("purple")) return FColor(128, 0, 128);

	// Try hex color #RRGGBB
	if (Lower.StartsWith(TEXT("#")) && Lower.Len() >= 7)
	{
		FString Hex = Lower.Mid(1);
		int32 R = FParse::HexDigit(Hex[0]) * 16 + FParse::HexDigit(Hex[1]);
		int32 G = FParse::HexDigit(Hex[2]) * 16 + FParse::HexDigit(Hex[3]);
		int32 B = FParse::HexDigit(Hex[4]) * 16 + FParse::HexDigit(Hex[5]);
		return FColor(R, G, B);
	}

	return FColor::White;
}

// ============================================================
// HandleDrawDebug
// POST /api/draw-debug { "type": "sphere", "target": "a0", "radius": 100, "color": "red", "duration": 5.0 }
// ============================================================

FString FAgenticMCPServer::HandleDrawDebug(const FString& Body)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world available."));
	}

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body."));
	}

	FString Type = Json->GetStringField(TEXT("type")).ToLower();
	FString ColorStr = Json->HasField(TEXT("color")) ? Json->GetStringField(TEXT("color")) : TEXT("white");
	float Duration = Json->HasField(TEXT("duration")) ? Json->GetNumberField(TEXT("duration")) : 5.0f;
	FColor Color = ParseColor(ColorStr);

	FString DrawId = FString::Printf(TEXT("draw_%d"), DebugDrawCounter++);

	if (Type == TEXT("sphere"))
	{
		FVector Location;

		// Get location from target actor or explicit coordinates
		if (Json->HasField(TEXT("target")))
		{
			FString Target = Json->GetStringField(TEXT("target"));
			AActor* Actor = FindActorByNameOrRef(World, Target);
			if (!Actor)
			{
				return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *Target));
			}
			Location = Actor->GetActorLocation();
		}
		else
		{
			Location.X = Json->GetNumberField(TEXT("x"));
			Location.Y = Json->GetNumberField(TEXT("y"));
			Location.Z = Json->GetNumberField(TEXT("z"));
		}

		float Radius = Json->HasField(TEXT("radius")) ? Json->GetNumberField(TEXT("radius")) : 50.0f;

		DrawDebugSphere(World, Location, Radius, 16, Color, false, Duration, 0, 2.0f);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("id"), DrawId);
		Result->SetStringField(TEXT("type"), TEXT("sphere"));
		Result->SetNumberField(TEXT("x"), Location.X);
		Result->SetNumberField(TEXT("y"), Location.Y);
		Result->SetNumberField(TEXT("z"), Location.Z);
		Result->SetNumberField(TEXT("radius"), Radius);
		Result->SetNumberField(TEXT("duration"), Duration);
		return JsonToString(Result);
	}
	else if (Type == TEXT("line"))
	{
		FVector Start, End;

		// Get start/end from actors or coordinates
		if (Json->HasField(TEXT("startTarget")))
		{
			AActor* Actor = FindActorByNameOrRef(World, Json->GetStringField(TEXT("startTarget")));
			if (!Actor) return MakeErrorJson(TEXT("Start actor not found."));
			Start = Actor->GetActorLocation();
		}
		else
		{
			Start.X = Json->GetNumberField(TEXT("startX"));
			Start.Y = Json->GetNumberField(TEXT("startY"));
			Start.Z = Json->GetNumberField(TEXT("startZ"));
		}

		if (Json->HasField(TEXT("endTarget")))
		{
			AActor* Actor = FindActorByNameOrRef(World, Json->GetStringField(TEXT("endTarget")));
			if (!Actor) return MakeErrorJson(TEXT("End actor not found."));
			End = Actor->GetActorLocation();
		}
		else
		{
			End.X = Json->GetNumberField(TEXT("endX"));
			End.Y = Json->GetNumberField(TEXT("endY"));
			End.Z = Json->GetNumberField(TEXT("endZ"));
		}

		float Thickness = Json->HasField(TEXT("thickness")) ? Json->GetNumberField(TEXT("thickness")) : 2.0f;
		DrawDebugLine(World, Start, End, Color, false, Duration, 0, Thickness);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("id"), DrawId);
		Result->SetStringField(TEXT("type"), TEXT("line"));
		return JsonToString(Result);
	}
	else if (Type == TEXT("text"))
	{
		FVector Location;
		FString Text = Json->GetStringField(TEXT("text"));

		if (Json->HasField(TEXT("target")))
		{
			AActor* Actor = FindActorByNameOrRef(World, Json->GetStringField(TEXT("target")));
			if (!Actor) return MakeErrorJson(TEXT("Target actor not found."));
			Location = Actor->GetActorLocation();
			Location.Z += 100.0f;  // Offset above actor
		}
		else
		{
			Location.X = Json->GetNumberField(TEXT("x"));
			Location.Y = Json->GetNumberField(TEXT("y"));
			Location.Z = Json->GetNumberField(TEXT("z"));
		}

		float Scale = Json->HasField(TEXT("scale")) ? Json->GetNumberField(TEXT("scale")) : 1.5f;
		DrawDebugString(World, Location, Text, nullptr, Color, Duration, false, Scale);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("id"), DrawId);
		Result->SetStringField(TEXT("type"), TEXT("text"));
		Result->SetStringField(TEXT("text"), Text);
		return JsonToString(Result);
	}
	else if (Type == TEXT("box"))
	{
		FVector Location;
		FVector Extent;

		if (Json->HasField(TEXT("target")))
		{
			AActor* Actor = FindActorByNameOrRef(World, Json->GetStringField(TEXT("target")));
			if (!Actor) return MakeErrorJson(TEXT("Target actor not found."));

			FVector Origin;
			Actor->GetActorBounds(false, Origin, Extent);
			Location = Origin;
		}
		else
		{
			Location.X = Json->GetNumberField(TEXT("x"));
			Location.Y = Json->GetNumberField(TEXT("y"));
			Location.Z = Json->GetNumberField(TEXT("z"));
			Extent.X = Json->HasField(TEXT("extentX")) ? Json->GetNumberField(TEXT("extentX")) : 50.0f;
			Extent.Y = Json->HasField(TEXT("extentY")) ? Json->GetNumberField(TEXT("extentY")) : 50.0f;
			Extent.Z = Json->HasField(TEXT("extentZ")) ? Json->GetNumberField(TEXT("extentZ")) : 50.0f;
		}

		DrawDebugBox(World, Location, Extent, Color, false, Duration, 0, 2.0f);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("id"), DrawId);
		Result->SetStringField(TEXT("type"), TEXT("box"));
		return JsonToString(Result);
	}

	return MakeErrorJson(FString::Printf(TEXT("Unknown draw type: %s. Use sphere, line, text, or box."), *Type));
}

// ============================================================
// HandleClearDebug
// POST /api/clear-debug { }
// ============================================================

FString FAgenticMCPServer::HandleClearDebug(const FString& Body)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world available."));
	}

	FlushPersistentDebugLines(World);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("Debug draws cleared."));
	return JsonToString(Result);
}

// ============================================================
// BLUEPRINT GRAPH SNAPSHOT
// ============================================================

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "AssetRegistry/AssetData.h"

// ============================================================
// HandleBlueprintSnapshot
// POST /api/blueprint-snapshot { "asset": "/Game/Blueprints/BP_Character" }
// ============================================================

FString FAgenticMCPServer::HandleBlueprintSnapshot(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body."));
	}

	FString AssetPath = Json->GetStringField(TEXT("asset"));
	if (AssetPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'asset' parameter."));
	}

	// Load the Blueprint asset
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
	if (!Blueprint)
	{
		return MakeErrorJson(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	TArray<TSharedPtr<FJsonValue>> GraphsArray;
	FString YamlSnapshot;

	// Iterate all graphs in the Blueprint
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph) continue;

		TSharedRef<FJsonObject> GraphJson = MakeShared<FJsonObject>();
		GraphJson->SetStringField(TEXT("name"), Graph->GetName());
		GraphJson->SetStringField(TEXT("type"), TEXT("EventGraph"));

		TArray<TSharedPtr<FJsonValue>> NodesArray;
		YamlSnapshot += FString::Printf(TEXT("Graph: %s\n"), *Graph->GetName());

		int32 NodeIndex = 0;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;

			FString NodeRef = FString::Printf(TEXT("n%d"), NodeIndex++);

			TSharedRef<FJsonObject> NodeJson = MakeShared<FJsonObject>();
			NodeJson->SetStringField(TEXT("ref"), NodeRef);
			NodeJson->SetStringField(TEXT("class"), Node->GetClass()->GetName());
			NodeJson->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			NodeJson->SetNumberField(TEXT("posX"), Node->NodePosX);
			NodeJson->SetNumberField(TEXT("posY"), Node->NodePosY);

			// Get node-specific info
			FString NodeType = TEXT("Unknown");
			FString NodeDetails;

			if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
			{
				NodeType = TEXT("Event");
				NodeDetails = EventNode->GetFunctionName().ToString();
			}
			else if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
			{
				NodeType = TEXT("Function");
				NodeDetails = CallNode->GetFunctionName().ToString();
			}
			else if (UK2Node_VariableGet* GetNode = Cast<UK2Node_VariableGet>(Node))
			{
				NodeType = TEXT("GetVariable");
				NodeDetails = GetNode->GetVarName().ToString();
			}
			else if (UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(Node))
			{
				NodeType = TEXT("SetVariable");
				NodeDetails = SetNode->GetVarName().ToString();
			}

			NodeJson->SetStringField(TEXT("nodeType"), NodeType);
			if (!NodeDetails.IsEmpty())
			{
				NodeJson->SetStringField(TEXT("details"), NodeDetails);
			}

			// Get pins
			TArray<TSharedPtr<FJsonValue>> PinsArray;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin) continue;

				TSharedRef<FJsonObject> PinJson = MakeShared<FJsonObject>();
				PinJson->SetStringField(TEXT("name"), Pin->PinName.ToString());
				PinJson->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("In") : TEXT("Out"));
				PinJson->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());

				// Get connections
				TArray<TSharedPtr<FJsonValue>> ConnectionsArray;
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;

					TSharedRef<FJsonObject> ConnJson = MakeShared<FJsonObject>();
					ConnJson->SetStringField(TEXT("node"), LinkedPin->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString());
					ConnJson->SetStringField(TEXT("pin"), LinkedPin->PinName.ToString());
					ConnectionsArray.Add(MakeShared<FJsonValueObject>(ConnJson));
				}
				if (ConnectionsArray.Num() > 0)
				{
					PinJson->SetArrayField(TEXT("connections"), ConnectionsArray);
				}

				PinsArray.Add(MakeShared<FJsonValueObject>(PinJson));
			}
			NodeJson->SetArrayField(TEXT("pins"), PinsArray);

			NodesArray.Add(MakeShared<FJsonValueObject>(NodeJson));

			// YAML line
			YamlSnapshot += FString::Printf(TEXT("  - [%s] %s: %s\n"),
				*NodeRef, *NodeType, *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		}

		GraphJson->SetArrayField(TEXT("nodes"), NodesArray);
		GraphsArray.Add(MakeShared<FJsonValueObject>(GraphJson));
	}

	// Also include function graphs
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph) continue;

		TSharedRef<FJsonObject> GraphJson = MakeShared<FJsonObject>();
		GraphJson->SetStringField(TEXT("name"), Graph->GetName());
		GraphJson->SetStringField(TEXT("type"), TEXT("Function"));
		GraphJson->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());

		GraphsArray.Add(MakeShared<FJsonValueObject>(GraphJson));
		YamlSnapshot += FString::Printf(TEXT("Function: %s (%d nodes)\n"), *Graph->GetName(), Graph->Nodes.Num());
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
	Result->SetStringField(TEXT("blueprintClass"), Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetName() : TEXT("None"));
	Result->SetStringField(TEXT("parentClass"), Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None"));
	Result->SetArrayField(TEXT("graphs"), GraphsArray);
	Result->SetStringField(TEXT("yamlSnapshot"), YamlSnapshot);

	return JsonToString(Result);
}

// ============================================================
// UNDO/REDO TRANSACTIONS
// ============================================================

static bool bInTransaction = false;
static FString CurrentTransactionName;

// ============================================================
// HandleBeginTransaction
// POST /api/begin-transaction { "name": "Move furniture" }
// ============================================================

FString FAgenticMCPServer::HandleBeginTransaction(const FString& Body)
{
	if (bInTransaction)
	{
		return MakeErrorJson(FString::Printf(TEXT("Already in transaction: %s. End it first."), *CurrentTransactionName));
	}

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	FString Name = TEXT("AgenticMCP Operation");
	if (Json.IsValid() && Json->HasField(TEXT("name")))
	{
		Name = Json->GetStringField(TEXT("name"));
	}

	GEditor->BeginTransaction(FText::FromString(Name));
	bInTransaction = true;
	CurrentTransactionName = Name;

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Transaction started: %s"), *Name));
	Result->SetStringField(TEXT("transactionName"), Name);
	return JsonToString(Result);
}

// ============================================================
// HandleEndTransaction
// POST /api/end-transaction { }
// ============================================================

FString FAgenticMCPServer::HandleEndTransaction(const FString& Body)
{
	if (!bInTransaction)
	{
		return MakeErrorJson(TEXT("No active transaction to end."));
	}

	GEditor->EndTransaction();
	FString EndedName = CurrentTransactionName;
	bInTransaction = false;
	CurrentTransactionName.Empty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Transaction ended: %s"), *EndedName));
	return JsonToString(Result);
}

// ============================================================
// HandleUndo
// POST /api/undo { "count": 1 }
// ============================================================

FString FAgenticMCPServer::HandleUndo(const FString& Body)
{
	if (bInTransaction)
	{
		return MakeErrorJson(TEXT("Cannot undo while in a transaction. End the transaction first."));
	}

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	int32 Count = 1;
	if (Json.IsValid() && Json->HasField(TEXT("count")))
	{
		Count = FMath::Max(1, Json->GetIntegerField(TEXT("count")));
	}

	int32 UndoneCount = 0;
	for (int32 i = 0; i < Count; i++)
	{
		if (GEditor->UndoTransaction())
		{
			UndoneCount++;
		}
		else
		{
			break;
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), UndoneCount > 0);
	Result->SetNumberField(TEXT("undoneCount"), UndoneCount);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Undone %d operation(s)"), UndoneCount));
	return JsonToString(Result);
}

// ============================================================
// HandleRedo
// POST /api/redo { "count": 1 }
// ============================================================

FString FAgenticMCPServer::HandleRedo(const FString& Body)
{
	if (bInTransaction)
	{
		return MakeErrorJson(TEXT("Cannot redo while in a transaction. End the transaction first."));
	}

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	int32 Count = 1;
	if (Json.IsValid() && Json->HasField(TEXT("count")))
	{
		Count = FMath::Max(1, Json->GetIntegerField(TEXT("count")));
	}

	int32 RedoneCount = 0;
	for (int32 i = 0; i < Count; i++)
	{
		if (GEditor->RedoTransaction())
		{
			RedoneCount++;
		}
		else
		{
			break;
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), RedoneCount > 0);
	Result->SetNumberField(TEXT("redoneCount"), RedoneCount);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Redone %d operation(s)"), RedoneCount));
	return JsonToString(Result);
}

// ============================================================
// DIFF/COMPARE MODE
// ============================================================

// State snapshot storage
struct FSceneStateSnapshot
{
	FString Name;
	FDateTime Timestamp;
	TMap<FString, FTransform> ActorTransforms;
	TMap<FString, FString> ActorClasses;
	TSet<FString> ActorNames;
};

static TMap<FString, FSceneStateSnapshot> SavedSnapshots;

// ============================================================
// HandleSaveState
// POST /api/save-state { "name": "before" }
// ============================================================

FString FAgenticMCPServer::HandleSaveState(const FString& Body)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world available."));
	}

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	FString Name = TEXT("default");
	if (Json.IsValid() && Json->HasField(TEXT("name")))
	{
		Name = Json->GetStringField(TEXT("name"));
	}

	FSceneStateSnapshot Snapshot;
	Snapshot.Name = Name;
	Snapshot.Timestamp = FDateTime::Now();

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		FString ActorName = Actor->GetName();
		Snapshot.ActorNames.Add(ActorName);
		Snapshot.ActorTransforms.Add(ActorName, Actor->GetActorTransform());
		Snapshot.ActorClasses.Add(ActorName, Actor->GetClass()->GetName());
	}

	SavedSnapshots.Add(Name, Snapshot);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetNumberField(TEXT("actorCount"), Snapshot.ActorNames.Num());
	Result->SetStringField(TEXT("timestamp"), Snapshot.Timestamp.ToString());
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Saved state '%s' with %d actors"), *Name, Snapshot.ActorNames.Num()));
	return JsonToString(Result);
}

// ============================================================
// HandleDiffState
// POST /api/diff-state { "name": "before" }
// ============================================================

FString FAgenticMCPServer::HandleDiffState(const FString& Body)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world available."));
	}

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	FString Name = TEXT("default");
	if (Json.IsValid() && Json->HasField(TEXT("name")))
	{
		Name = Json->GetStringField(TEXT("name"));
	}

	FSceneStateSnapshot* Snapshot = SavedSnapshots.Find(Name);
	if (!Snapshot)
	{
		return MakeErrorJson(FString::Printf(TEXT("No saved state found: %s"), *Name));
	}

	// Get current state
	TSet<FString> CurrentActors;
	TMap<FString, FTransform> CurrentTransforms;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		FString ActorName = Actor->GetName();
		CurrentActors.Add(ActorName);
		CurrentTransforms.Add(ActorName, Actor->GetActorTransform());
	}

	// Compute diff
	TArray<TSharedPtr<FJsonValue>> AddedArray;
	TArray<TSharedPtr<FJsonValue>> RemovedArray;
	TArray<TSharedPtr<FJsonValue>> ModifiedArray;

	FString DiffYaml;

	// Find added actors
	for (const FString& Name : CurrentActors)
	{
		if (!Snapshot->ActorNames.Contains(Name))
		{
			TSharedRef<FJsonObject> AddedJson = MakeShared<FJsonObject>();
			AddedJson->SetStringField(TEXT("name"), Name);
			AddedArray.Add(MakeShared<FJsonValueObject>(AddedJson));
			DiffYaml += FString::Printf(TEXT("+ ADDED: %s\n"), *Name);
		}
	}

	// Find removed actors
	for (const FString& Name : Snapshot->ActorNames)
	{
		if (!CurrentActors.Contains(Name))
		{
			TSharedRef<FJsonObject> RemovedJson = MakeShared<FJsonObject>();
			RemovedJson->SetStringField(TEXT("name"), Name);
			RemovedJson->SetStringField(TEXT("class"), Snapshot->ActorClasses.FindRef(Name));
			RemovedArray.Add(MakeShared<FJsonValueObject>(RemovedJson));
			DiffYaml += FString::Printf(TEXT("- REMOVED: %s\n"), *Name);
		}
	}

	// Find modified actors
	for (const FString& Name : CurrentActors)
	{
		if (!Snapshot->ActorNames.Contains(Name)) continue;

		FTransform* OldTransform = Snapshot->ActorTransforms.Find(Name);
		FTransform* NewTransform = CurrentTransforms.Find(Name);

		if (OldTransform && NewTransform && !OldTransform->Equals(*NewTransform, 0.1f))
		{
			TSharedRef<FJsonObject> ModJson = MakeShared<FJsonObject>();
			ModJson->SetStringField(TEXT("name"), Name);

			FVector OldLoc = OldTransform->GetLocation();
			FVector NewLoc = NewTransform->GetLocation();
			FRotator OldRot = OldTransform->Rotator();
			FRotator NewRot = NewTransform->Rotator();

			bool bLocationChanged = !OldLoc.Equals(NewLoc, 0.1f);
			bool bRotationChanged = !OldRot.Equals(NewRot, 0.1f);

			if (bLocationChanged)
			{
				TSharedRef<FJsonObject> LocChange = MakeShared<FJsonObject>();
				LocChange->SetNumberField(TEXT("oldX"), OldLoc.X);
				LocChange->SetNumberField(TEXT("oldY"), OldLoc.Y);
				LocChange->SetNumberField(TEXT("oldZ"), OldLoc.Z);
				LocChange->SetNumberField(TEXT("newX"), NewLoc.X);
				LocChange->SetNumberField(TEXT("newY"), NewLoc.Y);
				LocChange->SetNumberField(TEXT("newZ"), NewLoc.Z);
				ModJson->SetObjectField(TEXT("location"), LocChange);
			}

			if (bRotationChanged)
			{
				TSharedRef<FJsonObject> RotChange = MakeShared<FJsonObject>();
				RotChange->SetNumberField(TEXT("oldPitch"), OldRot.Pitch);
				RotChange->SetNumberField(TEXT("oldYaw"), OldRot.Yaw);
				RotChange->SetNumberField(TEXT("oldRoll"), OldRot.Roll);
				RotChange->SetNumberField(TEXT("newPitch"), NewRot.Pitch);
				RotChange->SetNumberField(TEXT("newYaw"), NewRot.Yaw);
				RotChange->SetNumberField(TEXT("newRoll"), NewRot.Roll);
				ModJson->SetObjectField(TEXT("rotation"), RotChange);
			}

			ModifiedArray.Add(MakeShared<FJsonValueObject>(ModJson));

			if (bLocationChanged)
			{
				DiffYaml += FString::Printf(TEXT("~ MOVED: %s (%.0f,%.0f,%.0f) -> (%.0f,%.0f,%.0f)\n"),
					*Name, OldLoc.X, OldLoc.Y, OldLoc.Z, NewLoc.X, NewLoc.Y, NewLoc.Z);
			}
			if (bRotationChanged)
			{
				DiffYaml += FString::Printf(TEXT("~ ROTATED: %s (%.0f,%.0f,%.0f) -> (%.0f,%.0f,%.0f)\n"),
					*Name, OldRot.Pitch, OldRot.Yaw, OldRot.Roll, NewRot.Pitch, NewRot.Yaw, NewRot.Roll);
			}
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("comparedTo"), Name);
	Result->SetNumberField(TEXT("addedCount"), AddedArray.Num());
	Result->SetNumberField(TEXT("removedCount"), RemovedArray.Num());
	Result->SetNumberField(TEXT("modifiedCount"), ModifiedArray.Num());
	Result->SetArrayField(TEXT("added"), AddedArray);
	Result->SetArrayField(TEXT("removed"), RemovedArray);
	Result->SetArrayField(TEXT("modified"), ModifiedArray);
	Result->SetStringField(TEXT("diffYaml"), DiffYaml);
	Result->SetBoolField(TEXT("hasChanges"), AddedArray.Num() > 0 || RemovedArray.Num() > 0 || ModifiedArray.Num() > 0);

	return JsonToString(Result);
}

// ============================================================
// HandleRestoreState
// POST /api/restore-state { "name": "before" }
// ============================================================

FString FAgenticMCPServer::HandleRestoreState(const FString& Body)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world available."));
	}

	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	FString Name = TEXT("default");
	if (Json.IsValid() && Json->HasField(TEXT("name")))
	{
		Name = Json->GetStringField(TEXT("name"));
	}

	FSceneStateSnapshot* Snapshot = SavedSnapshots.Find(Name);
	if (!Snapshot)
	{
		return MakeErrorJson(FString::Printf(TEXT("No saved state found: %s"), *Name));
	}

	// Begin transaction for restore
	GEditor->BeginTransaction(FText::FromString(FString::Printf(TEXT("Restore State: %s"), *Name)));

	int32 RestoredCount = 0;

	// Restore actor transforms
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		FString ActorName = Actor->GetName();
		FTransform* SavedTransform = Snapshot->ActorTransforms.Find(ActorName);

		if (SavedTransform)
		{
			Actor->Modify();
			Actor->SetActorTransform(*SavedTransform);
			RestoredCount++;
		}
	}

	GEditor->EndTransaction();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), Name);
	Result->SetNumberField(TEXT("restoredCount"), RestoredCount);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Restored %d actors to state '%s'"), RestoredCount, *Name));
	return JsonToString(Result);
}

// ============================================================
// HandleListStates
// POST /api/list-states { }
// ============================================================

FString FAgenticMCPServer::HandleListStates(const FString& Body)
{
	TArray<TSharedPtr<FJsonValue>> StatesArray;

	for (const auto& Pair : SavedSnapshots)
	{
		TSharedRef<FJsonObject> StateJson = MakeShared<FJsonObject>();
		StateJson->SetStringField(TEXT("name"), Pair.Key);
		StateJson->SetNumberField(TEXT("actorCount"), Pair.Value.ActorNames.Num());
		StateJson->SetStringField(TEXT("timestamp"), Pair.Value.Timestamp.ToString());
		StatesArray.Add(MakeShared<FJsonValueObject>(StateJson));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), StatesArray.Num());
	Result->SetArrayField(TEXT("states"), StatesArray);
	return JsonToString(Result);
}
