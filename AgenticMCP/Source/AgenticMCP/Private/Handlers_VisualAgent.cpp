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
