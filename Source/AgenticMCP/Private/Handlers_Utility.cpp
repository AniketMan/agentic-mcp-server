// Handlers_Utility.cpp
// Utility/discovery handlers for AgenticMCP.
// UE 5.6 target.
//
// Fixes (MCP_REQUESTS.md):
//   #3:  listEnumValues — Discover Python enum names from C++ enum names
//   #10: listEditableProperties — List all settable properties on a UClass
//   #1:  createDataAsset — Create a DataAsset by concrete subclass name
//   #8:  createTransitionPresetRegistry — Create a TransitionPresetRegistry DataAsset
//
// Also adds:
//   addComponentToActor — Add component to a PLACED actor (not Blueprint SCS)

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Engine/DataAsset.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#include "Misc/OutputDeviceRedirector.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Factories/DataAssetFactory.h"
#include "UObject/SavePackage.h"
#pragma warning(pop)

DEFINE_LOG_CATEGORY_STATIC(LogMCPUtility, Log, All);

// Helper: find actor by name in editor world
static AActor* FindActorByName_Util(const FString& Name)
{
	if (!GEditor) return nullptr;
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return nullptr;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == Name || (*It)->GetName() == Name)
			return *It;
	}
	return nullptr;
}

// ============================================================
// listEnumValues — FIX #3
// Given a C++ enum name, return all its values with both C++ and Python names.
// Body: { "enumName": "EVRInteractionType" }
// ============================================================
FString FAgenticMCPServer::HandleListEnumValues(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString EnumName = Json->GetStringField(TEXT("enumName"));
	if (EnumName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: enumName"));

	// Try to find the enum with several name variations
	UEnum* FoundEnum = nullptr;

	// Try exact name
	FoundEnum = FindFirstObject<UEnum>(*EnumName, EFindFirstObjectOptions::NativeFirst);

	// Try with E prefix added
	if (!FoundEnum && !EnumName.StartsWith(TEXT("E")))
		FoundEnum = FindFirstObject<UEnum>(*(TEXT("E") + EnumName), EFindFirstObjectOptions::NativeFirst);

	// Try with E prefix stripped
	if (!FoundEnum && EnumName.StartsWith(TEXT("E")))
		FoundEnum = FindFirstObject<UEnum>(*EnumName.Mid(1), EFindFirstObjectOptions::NativeFirst);

	// Broad search across all packages
	if (!FoundEnum)
	{
		for (TObjectIterator<UEnum> It; It; ++It)
		{
			FString ItName = It->GetName();
			if (ItName.Equals(EnumName, ESearchCase::IgnoreCase) ||
				ItName.Equals(TEXT("E") + EnumName, ESearchCase::IgnoreCase) ||
				(EnumName.StartsWith(TEXT("E")) && ItName.Equals(EnumName.Mid(1), ESearchCase::IgnoreCase)))
			{
				FoundEnum = *It;
				break;
			}
		}
	}

	if (!FoundEnum)
	{
		// Return available enums that partially match for discovery
		TArray<TSharedPtr<FJsonValue>> Suggestions;
		FString SearchLower = EnumName.ToLower();
		for (TObjectIterator<UEnum> It; It; ++It)
		{
			FString ItName = It->GetName();
			if (ItName.ToLower().Contains(SearchLower))
			{
				Suggestions.Add(MakeShared<FJsonValueString>(ItName));
				if (Suggestions.Num() >= 20) break;
			}
		}

		TSharedRef<FJsonObject> ErrJson = MakeShared<FJsonObject>();
		ErrJson->SetBoolField(TEXT("success"), false);
		ErrJson->SetStringField(TEXT("error"), FString::Printf(TEXT("Enum not found: %s"), *EnumName));
		ErrJson->SetArrayField(TEXT("suggestions"), Suggestions);
		return JsonToString(ErrJson);
	}

	// Build value list
	TArray<TSharedPtr<FJsonValue>> ValuesArray;
	int32 NumValues = FoundEnum->NumEnums();

	for (int32 i = 0; i < NumValues; i++)
	{
		// Skip the _MAX sentinel
		if (FoundEnum->HasMetaData(TEXT("Hidden"), i))
			continue;

		FString FullName = FoundEnum->GetNameStringByIndex(i);
		int64 Value = FoundEnum->GetValueByIndex(i);
		FString DisplayName = FoundEnum->GetDisplayNameTextByIndex(i).ToString();

		// Python name: strip the enum class prefix (e.g., "EVRInteractionType::Grab" -> "Grab")
		FString ShortName = FullName;
		int32 ColonPos;
		if (FullName.FindChar(TEXT(':'), ColonPos))
		{
			ShortName = FullName.Mid(ColonPos + 2); // skip ::
		}

		// Python enum access: unreal.EnumName.VALUE (without E prefix typically)
		FString PythonEnumName = FoundEnum->GetName();
		if (PythonEnumName.StartsWith(TEXT("E")))
			PythonEnumName = PythonEnumName.Mid(1);

		TSharedPtr<FJsonObject> ValueObj = MakeShared<FJsonObject>();
		ValueObj->SetStringField(TEXT("name"), ShortName);
		ValueObj->SetStringField(TEXT("fullName"), FullName);
		ValueObj->SetStringField(TEXT("displayName"), DisplayName);
		ValueObj->SetNumberField(TEXT("value"), (double)Value);
		ValueObj->SetStringField(TEXT("pythonAccess"),
			FString::Printf(TEXT("unreal.%s.%s"), *PythonEnumName, *ShortName));

		ValuesArray.Add(MakeShared<FJsonValueObject>(ValueObj));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("enumName"), FoundEnum->GetName());
	OutJson->SetStringField(TEXT("cppName"), FoundEnum->GetPathName());
	OutJson->SetNumberField(TEXT("valueCount"), ValuesArray.Num());
	OutJson->SetArrayField(TEXT("values"), ValuesArray);
	return JsonToString(OutJson);
}

// ============================================================
// createTransitionPresetRegistry — FIX #8
// Convenience wrapper: creates a UTransitionPresetRegistry DataAsset.
// UTransitionPresetRegistry lives in VRNarrativeKit and is a UDataAsset subclass.
// Before this handler existed, Python scripts tried unreal.TransitionPresetRegistryFactory()
// which doesn't exist — the class has no dedicated factory. This delegates to the
// same UDataAssetFactory logic used by createDataAsset (FIX #1).
//
// Body: { "assetName": "DA_TransitionPresetRegistry", "assetPath": "/Game/DataAssets" }
// Both fields are optional — defaults to the canonical name + path above.
// ============================================================
FString FAgenticMCPServer::HandleCreateTransitionPresetRegistry(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);

	FString AssetName = TEXT("DA_TransitionPresetRegistry");
	FString AssetPath = TEXT("/Game/DataAssets");

	if (Json.IsValid())
	{
		if (Json->HasField(TEXT("assetName")))
			AssetName = Json->GetStringField(TEXT("assetName"));
		if (Json->HasField(TEXT("assetPath")))
			AssetPath = Json->GetStringField(TEXT("assetPath"));
	}

	// Delegate to createDataAsset with the concrete class name
	TSharedRef<FJsonObject> Inner = MakeShared<FJsonObject>();
	Inner->SetStringField(TEXT("className"), TEXT("TransitionPresetRegistry"));
	Inner->SetStringField(TEXT("assetName"), AssetName);
	Inner->SetStringField(TEXT("assetPath"), AssetPath);

	return HandleCreateDataAsset(JsonToString(Inner));
}

// ============================================================
// listEditableProperties — FIX #10
// Given a UClass name, return all properties that can be set via set_editor_property().
// Body: { "className": "PointLightComponent" }
// ============================================================
FString FAgenticMCPServer::HandleListEditableProperties(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ClassName = Json->GetStringField(TEXT("className"));
	if (ClassName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: className"));

	// Find the class
	UClass* FoundClass = FindClassByName(ClassName);
	if (!FoundClass)
		FoundClass = FindClassByName(TEXT("U") + ClassName);
	if (!FoundClass)
		FoundClass = FindClassByName(TEXT("A") + ClassName);

	if (!FoundClass)
	{
		// Search by partial match
		TArray<TSharedPtr<FJsonValue>> Suggestions;
		FString SearchLower = ClassName.ToLower();
		for (TObjectIterator<UClass> It; It; ++It)
		{
			FString ItName = It->GetName();
			if (ItName.ToLower().Contains(SearchLower))
			{
				Suggestions.Add(MakeShared<FJsonValueString>(ItName));
				if (Suggestions.Num() >= 20) break;
			}
		}

		TSharedRef<FJsonObject> ErrJson = MakeShared<FJsonObject>();
		ErrJson->SetBoolField(TEXT("success"), false);
		ErrJson->SetStringField(TEXT("error"), FString::Printf(TEXT("Class not found: %s"), *ClassName));
		ErrJson->SetArrayField(TEXT("suggestions"), Suggestions);
		return JsonToString(ErrJson);
	}

	TArray<TSharedPtr<FJsonValue>> PropsArray;

	for (TFieldIterator<FProperty> PropIt(FoundClass); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		if (!Prop) continue;

		// Skip deprecated and hidden properties
		if (Prop->HasAnyPropertyFlags(CPF_Deprecated))
			continue;

		// Only include properties that are editable
		bool bEditable = Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible);
		if (!bEditable)
			continue;

		TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
		PropObj->SetStringField(TEXT("name"), Prop->GetName());
		PropObj->SetStringField(TEXT("type"), Prop->GetCPPType());
		PropObj->SetStringField(TEXT("category"), Prop->GetMetaData(TEXT("Category")));
		PropObj->SetBoolField(TEXT("readOnly"), Prop->HasAnyPropertyFlags(CPF_EditConst));
		PropObj->SetBoolField(TEXT("blueprintVisible"), Prop->HasAnyPropertyFlags(CPF_BlueprintVisible));
		PropObj->SetBoolField(TEXT("editAnywhere"), Prop->HasAnyPropertyFlags(CPF_Edit));

		// Python-style property name (lowercase first letter)
		FString PythonName = Prop->GetName();
		if (PythonName.Len() > 0 && FChar::IsUpper(PythonName[0]))
		{
			// UE Python uses snake_case for some, PascalCase for others
			// Report both the raw name and a snake_case guess
			PropObj->SetStringField(TEXT("pythonName"), PythonName);
		}

		// Include the owning class so callers know if it's inherited
		PropObj->SetStringField(TEXT("declaredIn"), Prop->GetOwnerClass()->GetName());

		PropsArray.Add(MakeShared<FJsonValueObject>(PropObj));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("className"), FoundClass->GetName());
	OutJson->SetStringField(TEXT("parentClass"), FoundClass->GetSuperClass() ? FoundClass->GetSuperClass()->GetName() : TEXT("None"));
	OutJson->SetNumberField(TEXT("propertyCount"), PropsArray.Num());
	OutJson->SetArrayField(TEXT("properties"), PropsArray);
	return JsonToString(OutJson);
}

// ============================================================
// createDataAsset — FIX #1
// Create a DataAsset by concrete subclass name.
// Body: { "className": "TransitionEffectConfig", "assetName": "MyConfig", "assetPath": "/Game/Data" }
// ============================================================
FString FAgenticMCPServer::HandleCreateDataAsset(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ClassName = Json->GetStringField(TEXT("className"));
	FString AssetName = Json->GetStringField(TEXT("assetName"));
	FString AssetPath = Json->GetStringField(TEXT("assetPath"));

	if (ClassName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: className"));
	if (AssetName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: assetName"));
	if (AssetPath.IsEmpty())
		AssetPath = TEXT("/Game/Data");

	// Find the concrete DataAsset subclass
	UClass* DataAssetClass = FindClassByName(ClassName);
	if (!DataAssetClass)
		DataAssetClass = FindClassByName(TEXT("U") + ClassName);

	if (!DataAssetClass)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Class '%s' not found. Provide the exact C++ class name (e.g., 'TransitionEffectConfig' or 'UTransitionEffectConfig')."),
			*ClassName));
	}

	// Verify it's a DataAsset subclass
	if (!DataAssetClass->IsChildOf(UDataAsset::StaticClass()))
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Class '%s' is not a UDataAsset subclass. It inherits from '%s'."),
			*ClassName, *DataAssetClass->GetSuperClass()->GetName()));
	}

	// Create using UDataAssetFactory with the concrete class
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
	Factory->DataAssetClass = DataAssetClass;

	FString FullPath = AssetPath / AssetName;
	UObject* NewAsset = AssetTools.CreateAsset(AssetName, AssetPath, DataAssetClass, Factory);

	if (!NewAsset)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Failed to create DataAsset '%s' of class '%s' at '%s'"),
			*AssetName, *ClassName, *AssetPath));
	}

	// Save the package
	UPackage* Package = NewAsset->GetOutermost();
	FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(),
		FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Standalone;
	bool bSaved = UPackage::SavePackage(Package, NewAsset, *PackageFilename, SaveArgs);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("assetName"), AssetName);
	OutJson->SetStringField(TEXT("assetPath"), NewAsset->GetPathName());
	OutJson->SetStringField(TEXT("className"), DataAssetClass->GetName());
	OutJson->SetBoolField(TEXT("saved"), bSaved);
	return JsonToString(OutJson);
}

// ============================================================
// addComponentToActor — FIX #2
// Add a component to a PLACED actor in the world (not a Blueprint SCS).
// Uses SubobjectDataSubsystem for reliable component addition.
// Body: { "actorName": "BP_Phone_C_0", "componentClass": "GrabbableComponent", "componentName": "MyGrab" }
// ============================================================
FString FAgenticMCPServer::HandleAddComponentToActor(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString ComponentClassName = Json->GetStringField(TEXT("componentClass"));
	FString ComponentName;
	if (Json->HasField(TEXT("componentName")))
		ComponentName = Json->GetStringField(TEXT("componentName"));

	if (ActorName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: actorName"));
	if (ComponentClassName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: componentClass"));

	AActor* Actor = FindActorByName_Util(ActorName);
	if (!Actor)
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	// Find the component class
	UClass* CompClass = FindClassByName(ComponentClassName);
	if (!CompClass)
		CompClass = FindClassByName(TEXT("U") + ComponentClassName);
	if (!CompClass)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName().Equals(ComponentClassName, ESearchCase::IgnoreCase) ||
				It->GetName().Equals(TEXT("U") + ComponentClassName, ESearchCase::IgnoreCase))
			{
				CompClass = *It;
				break;
			}
		}
	}
	if (!CompClass)
		return MakeErrorJson(FString::Printf(TEXT("Component class not found: %s"), *ComponentClassName));

	if (!CompClass->IsChildOf(UActorComponent::StaticClass()))
		return MakeErrorJson(FString::Printf(TEXT("'%s' is not an ActorComponent subclass"), *ComponentClassName));

	// Create the component via NewObject + AddInstanceComponent + RegisterComponent
	FName CompFName = ComponentName.IsEmpty() ? CompClass->GetFName() : FName(*ComponentName);
	UActorComponent* NewComp = NewObject<UActorComponent>(Actor, CompClass, CompFName);
	if (!NewComp)
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to create component of class '%s'"), *ComponentClassName));
	}

	// If it's a SceneComponent, attach to root
	if (USceneComponent* SceneComp = Cast<USceneComponent>(NewComp))
	{
		USceneComponent* Root = Actor->GetRootComponent();
		if (Root)
		{
			SceneComp->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
		}
		else
		{
			Actor->SetRootComponent(SceneComp);
		}
	}

	Actor->AddInstanceComponent(NewComp);
	NewComp->RegisterComponent();

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("actorName"), ActorName);
	OutJson->SetStringField(TEXT("componentClass"), NewComp->GetClass()->GetName());
	OutJson->SetStringField(TEXT("componentName"), NewComp->GetName());
	return JsonToString(OutJson);
}

// ============================================================
// setMaterialOnActor — FIX #7
// Assign a material to an actor's mesh. Detects Text3D actors and uses
// set_front_material() / set_extrude_material() methods instead of
// set_editor_property('front_material') which fails on Text3D.
// Body: { "actorName": "...", "materialPath": "/Game/...", "slotIndex": 0, "text3dFace": "front" }
// text3dFace: "front" | "extrude" | "bevel" | "back" (only for Text3D actors)
// ============================================================
FString FAgenticMCPServer::HandleSetMaterialOnActor(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString MaterialPath = Json->GetStringField(TEXT("materialPath"));
	int32 SlotIndex = 0;
	if (Json->HasField(TEXT("slotIndex")))
		SlotIndex = Json->GetIntegerField(TEXT("slotIndex"));
	FString Text3DFace;
	if (Json->HasField(TEXT("text3dFace")))
		Text3DFace = Json->GetStringField(TEXT("text3dFace"));

	if (ActorName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: actorName"));
	if (MaterialPath.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: materialPath"));

	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	AActor* Actor = FindActorByName_Util(ActorName);
	if (!Actor)
		return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	// Load the material
	UMaterialInterface* Material = Cast<UMaterialInterface>(
		StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MaterialPath));
	if (!Material)
		return MakeErrorJson(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));

	FString ActorClassName = Actor->GetClass()->GetName();
	bool bIsText3D = ActorClassName.Contains(TEXT("Text3D")) || ActorClassName.Contains(TEXT("TextRender"));

	if (bIsText3D && !Text3DFace.IsEmpty())
	{
		// Text3D uses methods, not properties. Route through Python.
		FString Face = Text3DFace.ToLower();
		FString MethodName;
		if (Face == TEXT("front")) MethodName = TEXT("set_front_material");
		else if (Face == TEXT("extrude")) MethodName = TEXT("set_extrude_material");
		else if (Face == TEXT("bevel")) MethodName = TEXT("set_bevel_material");
		else if (Face == TEXT("back")) MethodName = TEXT("set_back_material");
		else return MakeErrorJson(FString::Printf(TEXT("Invalid text3dFace: '%s'. Use front/extrude/bevel/back."), *Text3DFace));

		// Build and execute Python script for Text3D material assignment
		FString Script = FString::Printf(TEXT(
			"import unreal\n"
			"actor = None\n"
			"for a in unreal.EditorActorSubsystem().get_all_level_actors():\n"
			"    if a.get_actor_label() == '%s' or a.get_name() == '%s':\n"
			"        actor = a\n"
			"        break\n"
			"if actor:\n"
			"    mat = unreal.load_asset('%s')\n"
			"    if mat:\n"
			"        comp = actor.get_component_by_class(unreal.Text3DComponent)\n"
			"        if comp:\n"
			"            comp.%s(mat)\n"
			"            print('SUCCESS: Material set on Text3D %s face')\n"
			"        else:\n"
			"            print('ERROR: No Text3DComponent found')\n"
			"    else:\n"
			"        print('ERROR: Material not found')\n"
			"else:\n"
			"    print('ERROR: Actor not found')\n"
		), *ActorName, *ActorName, *MaterialPath, *MethodName, *Text3DFace);

		FString TempPath = FPaths::ProjectSavedDir() / FString::Printf(
			TEXT("MCP_Text3D_%s.py"), *FGuid::NewGuid().ToString());
		FFileHelper::SaveStringToFile(Script, *TempPath);
		FString Command = FString::Printf(TEXT("py \"%s\""), *TempPath);
		bool bSuccess = GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Command);
		IFileManager::Get().Delete(*TempPath);

		TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
		OutJson->SetBoolField(TEXT("success"), bSuccess);
		OutJson->SetStringField(TEXT("actorName"), ActorName);
		OutJson->SetStringField(TEXT("materialPath"), MaterialPath);
		OutJson->SetStringField(TEXT("method"), MethodName);
		OutJson->SetStringField(TEXT("note"), TEXT("Text3D material set via Python method call"));
		return JsonToString(OutJson);
	}

	// Standard mesh material assignment
	TArray<UMeshComponent*> MeshComponents;
	Actor->GetComponents<UMeshComponent>(MeshComponents);

	if (MeshComponents.Num() == 0)
		return MakeErrorJson(FString::Printf(TEXT("Actor '%s' has no mesh components"), *ActorName));

	UMeshComponent* MeshComp = MeshComponents[0];
	int32 NumSlots = MeshComp->GetNumMaterials();

	if (SlotIndex < 0 || SlotIndex >= NumSlots)
		return MakeErrorJson(FString::Printf(TEXT("Slot index %d out of range (0-%d)"), SlotIndex, NumSlots - 1));

	MeshComp->SetMaterial(SlotIndex, Material);

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), true);
	OutJson->SetStringField(TEXT("actorName"), ActorName);
	OutJson->SetStringField(TEXT("materialPath"), MaterialPath);
	OutJson->SetNumberField(TEXT("slotIndex"), SlotIndex);
	OutJson->SetStringField(TEXT("meshComponent"), MeshComp->GetName());
	OutJson->SetBoolField(TEXT("isText3D"), false);
	return JsonToString(OutJson);
}

// ============================================================
// spawnActorBatch — FIX #11
// Spawn multiple actors in one call.
// Body: { "actors": [ { "className": "StaticMeshActor", "label": "Orb_01",
//          "locationX": 0, "locationY": 0, "locationZ": 100 }, ... ] }
// ============================================================
FString FAgenticMCPServer::HandleSpawnActorBatch(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
		return MakeErrorJson(TEXT("No editor world available"));

	const TArray<TSharedPtr<FJsonValue>>* ActorsArray;
	if (!Json->TryGetArrayField(TEXT("actors"), ActorsArray) || ActorsArray->Num() == 0)
		return MakeErrorJson(TEXT("Missing or empty required field: actors (array)"));

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 Spawned = 0;
	int32 Failed = 0;

	for (const TSharedPtr<FJsonValue>& Entry : *ActorsArray)
	{
		const TSharedPtr<FJsonObject>* ActorJson;
		if (!Entry->TryGetObject(ActorJson))
		{
			Failed++;
			continue;
		}

		FString ClassName = (*ActorJson)->GetStringField(TEXT("className"));
		FString Label = (*ActorJson)->HasField(TEXT("label")) ? (*ActorJson)->GetStringField(TEXT("label")) : TEXT("");

		if (ClassName.IsEmpty())
		{
			TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
			ErrObj->SetBoolField(TEXT("success"), false);
			ErrObj->SetStringField(TEXT("error"), TEXT("Missing className"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(ErrObj));
			Failed++;
			continue;
		}

		// Find class (Blueprint or native)
		UClass* ActorClass = FindClassByName(ClassName);
		if (!ActorClass || !ActorClass->IsChildOf(AActor::StaticClass()))
		{
			// Try Blueprint
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
		}

		if (!ActorClass)
		{
			TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
			ErrObj->SetBoolField(TEXT("success"), false);
			ErrObj->SetStringField(TEXT("className"), ClassName);
			ErrObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Class not found: %s"), *ClassName));
			ResultsArray.Add(MakeShared<FJsonValueObject>(ErrObj));
			Failed++;
			continue;
		}

		// Parse transform
		FVector Location(0, 0, 0);
		FRotator Rotation(0, 0, 0);
		FVector Scale(1, 1, 1);

		if ((*ActorJson)->HasField(TEXT("locationX"))) Location.X = (*ActorJson)->GetNumberField(TEXT("locationX"));
		if ((*ActorJson)->HasField(TEXT("locationY"))) Location.Y = (*ActorJson)->GetNumberField(TEXT("locationY"));
		if ((*ActorJson)->HasField(TEXT("locationZ"))) Location.Z = (*ActorJson)->GetNumberField(TEXT("locationZ"));
		if ((*ActorJson)->HasField(TEXT("rotationPitch"))) Rotation.Pitch = (*ActorJson)->GetNumberField(TEXT("rotationPitch"));
		if ((*ActorJson)->HasField(TEXT("rotationYaw"))) Rotation.Yaw = (*ActorJson)->GetNumberField(TEXT("rotationYaw"));
		if ((*ActorJson)->HasField(TEXT("rotationRoll"))) Rotation.Roll = (*ActorJson)->GetNumberField(TEXT("rotationRoll"));
		if ((*ActorJson)->HasField(TEXT("scaleX"))) Scale.X = (*ActorJson)->GetNumberField(TEXT("scaleX"));
		if ((*ActorJson)->HasField(TEXT("scaleY"))) Scale.Y = (*ActorJson)->GetNumberField(TEXT("scaleY"));
		if ((*ActorJson)->HasField(TEXT("scaleZ"))) Scale.Z = (*ActorJson)->GetNumberField(TEXT("scaleZ"));

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		FTransform SpawnTransform(Rotation.Quaternion(), Location, Scale);

		AActor* NewActor = World->SpawnActor<AActor>(ActorClass, SpawnTransform, SpawnParams);
		if (NewActor)
		{
			if (!Label.IsEmpty()) NewActor->SetActorLabel(Label);

			TSharedPtr<FJsonObject> SuccessObj = MakeShared<FJsonObject>();
			SuccessObj->SetBoolField(TEXT("success"), true);
			SuccessObj->SetStringField(TEXT("name"), NewActor->GetName());
			SuccessObj->SetStringField(TEXT("label"), NewActor->GetActorLabel());
			SuccessObj->SetStringField(TEXT("class"), NewActor->GetClass()->GetName());
			ResultsArray.Add(MakeShared<FJsonValueObject>(SuccessObj));
			Spawned++;
		}
		else
		{
			TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
			ErrObj->SetBoolField(TEXT("success"), false);
			ErrObj->SetStringField(TEXT("className"), ClassName);
			ErrObj->SetStringField(TEXT("error"), TEXT("SpawnActor returned null"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(ErrObj));
			Failed++;
		}
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), Failed == 0);
	OutJson->SetNumberField(TEXT("spawned"), Spawned);
	OutJson->SetNumberField(TEXT("failed"), Failed);
	OutJson->SetNumberField(TEXT("total"), ActorsArray->Num());
	OutJson->SetArrayField(TEXT("results"), ResultsArray);
	return JsonToString(OutJson);
}

// ============================================================
// setWorldSetting — FIX #12
// Set world settings properties (DefaultGameMode, KillZ, GlobalGravity, etc.)
// Body: { "property": "DefaultGameMode", "value": "/Game/Blueprints/BP_MyGameMode.BP_MyGameMode_C" }
// Or: { "properties": { "KillZ": -10000, "bEnableWorldBoundsChecks": true } }
// ============================================================
FString FAgenticMCPServer::HandleSetWorldSetting(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	if (!GEditor)
		return MakeErrorJson(TEXT("Editor not available"));

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
		return MakeErrorJson(TEXT("No editor world available"));

	AWorldSettings* WorldSettings = World->GetWorldSettings();
	if (!WorldSettings)
		return MakeErrorJson(TEXT("No world settings available"));

	TArray<TSharedPtr<FJsonValue>> ChangedArray;

	// Single property mode
	if (Json->HasField(TEXT("property")))
	{
		FString PropName = Json->GetStringField(TEXT("property"));
		FString PropValue = Json->GetStringField(TEXT("value"));

		if (PropName.IsEmpty())
			return MakeErrorJson(TEXT("Missing required field: property"));

		// Special handling for DefaultGameMode (class reference)
		if (PropName == TEXT("DefaultGameMode") || PropName == TEXT("GameMode"))
		{
			UClass* GMClass = FindClassByName(PropValue);
			if (!GMClass)
			{
				GMClass = LoadClass<AGameModeBase>(nullptr, *PropValue);
			}
			if (GMClass)
			{
				WorldSettings->DefaultGameMode = GMClass;
				TSharedPtr<FJsonObject> Changed = MakeShared<FJsonObject>();
				Changed->SetStringField(TEXT("property"), TEXT("DefaultGameMode"));
				Changed->SetStringField(TEXT("value"), GMClass->GetPathName());
				ChangedArray.Add(MakeShared<FJsonValueObject>(Changed));
			}
			else
			{
				return MakeErrorJson(FString::Printf(TEXT("GameMode class not found: %s"), *PropValue));
			}
		}
		else
		{
			// Generic property set via FProperty
			FProperty* Prop = WorldSettings->GetClass()->FindPropertyByName(FName(*PropName));
			if (!Prop)
				return MakeErrorJson(FString::Printf(TEXT("Property '%s' not found on AWorldSettings"), *PropName));

			// Try setting from string
			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(WorldSettings);
			if (Prop->ImportText_Direct(*PropValue, ValuePtr, WorldSettings, PPF_None))
			{
				TSharedPtr<FJsonObject> Changed = MakeShared<FJsonObject>();
				Changed->SetStringField(TEXT("property"), PropName);
				Changed->SetStringField(TEXT("value"), PropValue);
				ChangedArray.Add(MakeShared<FJsonValueObject>(Changed));
			}
			else
			{
				return MakeErrorJson(FString::Printf(TEXT("Failed to set '%s' to '%s'"), *PropName, *PropValue));
			}
		}
	}

	// Multi-property mode
	if (Json->HasField(TEXT("properties")))
	{
		const TSharedPtr<FJsonObject>* PropsObj;
		if (Json->TryGetObjectField(TEXT("properties"), PropsObj))
		{
			for (const auto& Pair : (*PropsObj)->Values)
			{
				FString PropName = Pair.Key;
				FString PropValue;

				if (Pair.Value->Type == EJson::String)
					PropValue = Pair.Value->AsString();
				else if (Pair.Value->Type == EJson::Number)
					PropValue = FString::Printf(TEXT("%g"), Pair.Value->AsNumber());
				else if (Pair.Value->Type == EJson::Boolean)
					PropValue = Pair.Value->AsBool() ? TEXT("True") : TEXT("False");
				else
					continue;

				FProperty* Prop = WorldSettings->GetClass()->FindPropertyByName(FName(*PropName));
				if (Prop)
				{
					void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(WorldSettings);
					if (Prop->ImportText_Direct(*PropValue, ValuePtr, WorldSettings, PPF_None))
					{
						TSharedPtr<FJsonObject> Changed = MakeShared<FJsonObject>();
						Changed->SetStringField(TEXT("property"), PropName);
						Changed->SetStringField(TEXT("value"), PropValue);
						ChangedArray.Add(MakeShared<FJsonValueObject>(Changed));
					}
				}
			}
		}
	}

	// Read back current state
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetBoolField(TEXT("success"), ChangedArray.Num() > 0);
	OutJson->SetStringField(TEXT("worldName"), World->GetMapName());
	OutJson->SetNumberField(TEXT("changedCount"), ChangedArray.Num());
	OutJson->SetArrayField(TEXT("changed"), ChangedArray);

	// Include current key settings for reference
	OutJson->SetStringField(TEXT("currentGameMode"),
		WorldSettings->DefaultGameMode ? WorldSettings->DefaultGameMode->GetPathName() : TEXT("None"));
	OutJson->SetNumberField(TEXT("killZ"), WorldSettings->KillZ);
	OutJson->SetBoolField(TEXT("enableWorldBoundsChecks"), WorldSettings->bEnableWorldBoundsChecks);

	return JsonToString(OutJson);
}

// ============================================================
