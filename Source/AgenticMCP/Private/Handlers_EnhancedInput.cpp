// Handlers_EnhancedInput.cpp
// Enhanced Input system handlers for AgenticMCP.
// UE 5.6 target. Input Actions, Mapping Contexts, Modifiers, Triggers.
// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPEnhancedInput, Log, All);

// ============================================================
// inputListActions
// List all Input Action assets
// ============================================================
FString FAgenticMCPServer::HandleInputListActions(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    TArray<FAssetData> Assets;
    FTopLevelAssetPath ClassPath(TEXT("/Script/EnhancedInput"), TEXT("InputAction"));
    ARM.Get().GetAssetsByClass(ClassPath, Assets, true);

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteArrayStart(TEXT("inputActions"));
    for (const FAssetData& A : Assets)
    {
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("name"), A.AssetName.ToString());
        Writer->WriteValue(TEXT("path"), A.GetObjectPathString());
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();
    Writer->WriteValue(TEXT("count"), Assets.Num());
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// inputListMappingContexts
// List all Input Mapping Context assets
// ============================================================
FString FAgenticMCPServer::HandleInputListMappingContexts(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    TArray<FAssetData> Assets;
    FTopLevelAssetPath ClassPath(TEXT("/Script/EnhancedInput"), TEXT("InputMappingContext"));
    ARM.Get().GetAssetsByClass(ClassPath, Assets, true);

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteArrayStart(TEXT("mappingContexts"));
    for (const FAssetData& A : Assets)
    {
        Writer->WriteObjectStart();
        Writer->WriteValue(TEXT("name"), A.AssetName.ToString());
        Writer->WriteValue(TEXT("path"), A.GetObjectPathString());
        Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();
    Writer->WriteValue(TEXT("count"), Assets.Num());
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// inputCreateAction
// Create a new Input Action asset
// Params: name (string), path (string, default /Game/Input)
//         valueType (string: Bool/Float1D/Vector2D/Vector3D, default Bool)
// ============================================================
FString FAgenticMCPServer::HandleInputCreateAction(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString Name = Json->GetStringField(TEXT("name"));
    FString Path = Json->HasField(TEXT("path")) ? Json->GetStringField(TEXT("path")) : TEXT("/Game/Input");
    FString ValueType = Json->HasField(TEXT("valueType")) ? Json->GetStringField(TEXT("valueType")) : TEXT("Bool");

    if (Name.IsEmpty())
    {
        return MakeErrorJson(TEXT("name is required"));
    }

    UClass* FactoryClass = FindFirstObject<UClass>(TEXT("InputActionFactory"), EFindFirstObjectOptions::NativeFirst);
    if (!FactoryClass)
    {
        return MakeErrorJson(TEXT("EnhancedInput plugin is not loaded."));
    }

    UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UObject* NewAsset = AssetTools.CreateAsset(Name, Path, nullptr, Factory);
    if (!NewAsset)
    {
        return MakeErrorJson(TEXT("Failed to create Input Action"));
    }

    // Set the value type via reflection
    FProperty* ValueTypeProp = NewAsset->GetClass()->FindPropertyByName(TEXT("ValueType"));
    if (ValueTypeProp)
    {
        // Map string to enum value
        FString EnumVal;
        if (ValueType == TEXT("Bool")) EnumVal = TEXT("Boolean");
        else if (ValueType == TEXT("Float1D")) EnumVal = TEXT("Axis1D");
        else if (ValueType == TEXT("Vector2D")) EnumVal = TEXT("Axis2D");
        else if (ValueType == TEXT("Vector3D")) EnumVal = TEXT("Axis3D");
        else EnumVal = TEXT("Boolean");

        // Use Python for enum setting which is more reliable
        FString PythonCmd = FString::Printf(
            TEXT("import unreal; ia = unreal.load_asset('%s/%s'); "
                 "ia.set_editor_property('value_type', unreal.InputActionValueType.%s)"),
            *Path, *Name, *EnumVal);

        if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
        {
            FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
            GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        }
    }

    NewAsset->MarkPackageDirty();

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteValue(TEXT("name"), Name);
    Writer->WriteValue(TEXT("path"), NewAsset->GetPathName());
    Writer->WriteValue(TEXT("valueType"), ValueType);
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// inputCreateMappingContext
// Create a new Input Mapping Context asset
// Params: name (string), path (string, default /Game/Input)
// ============================================================
FString FAgenticMCPServer::HandleInputCreateMappingContext(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString Name = Json->GetStringField(TEXT("name"));
    FString Path = Json->HasField(TEXT("path")) ? Json->GetStringField(TEXT("path")) : TEXT("/Game/Input");

    if (Name.IsEmpty())
    {
        return MakeErrorJson(TEXT("name is required"));
    }

    UClass* FactoryClass = FindFirstObject<UClass>(TEXT("InputMappingContextFactory"), EFindFirstObjectOptions::NativeFirst);
    if (!FactoryClass)
    {
        return MakeErrorJson(TEXT("EnhancedInput plugin is not loaded."));
    }

    UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UObject* NewAsset = AssetTools.CreateAsset(Name, Path, nullptr, Factory);
    if (!NewAsset)
    {
        return MakeErrorJson(TEXT("Failed to create Input Mapping Context"));
    }

    NewAsset->MarkPackageDirty();

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteValue(TEXT("name"), Name);
    Writer->WriteValue(TEXT("path"), NewAsset->GetPathName());
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// inputAddMapping
// Add a key mapping to a Mapping Context
// Params: contextPath (string), actionPath (string), key (string, e.g. "W", "SpaceBar", "Gamepad_FaceButton_Bottom")
//         modifiers (array of strings, optional: Negate/DeadZone/Scalar/Swizzle)
//         triggers (array of strings, optional: Down/Pressed/Released/Hold/Tap)
// ============================================================
FString FAgenticMCPServer::HandleInputAddMapping(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString ContextPath = Json->GetStringField(TEXT("contextPath"));
    FString ActionPath = Json->GetStringField(TEXT("actionPath"));
    FString Key = Json->GetStringField(TEXT("key"));

    if (ContextPath.IsEmpty() || ActionPath.IsEmpty() || Key.IsEmpty())
    {
        return MakeErrorJson(TEXT("contextPath, actionPath, and key are required"));
    }

    // Build modifier and trigger strings for Python
    FString ModifiersStr = TEXT("[]");
    if (Json->HasField(TEXT("modifiers")))
    {
        TArray<TSharedPtr<FJsonValue>> Mods = Json->GetArrayField(TEXT("modifiers"));
        ModifiersStr = TEXT("[");
        for (int32 i = 0; i < Mods.Num(); i++)
        {
            if (i > 0) ModifiersStr += TEXT(",");
            ModifiersStr += FString::Printf(TEXT("unreal.InputModifier%s()"), *Mods[i]->AsString());
        }
        ModifiersStr += TEXT("]");
    }

    FString TriggersStr = TEXT("[]");
    if (Json->HasField(TEXT("triggers")))
    {
        TArray<TSharedPtr<FJsonValue>> Trigs = Json->GetArrayField(TEXT("triggers"));
        TriggersStr = TEXT("[");
        for (int32 i = 0; i < Trigs.Num(); i++)
        {
            if (i > 0) TriggersStr += TEXT(",");
            TriggersStr += FString::Printf(TEXT("unreal.InputTrigger%s()"), *Trigs[i]->AsString());
        }
        TriggersStr += TEXT("]");
    }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "ctx = unreal.load_asset('%s'); "
             "action = unreal.load_asset('%s'); "
             "key = unreal.Key('%s'); "
             "mapping = unreal.EnhancedActionKeyMapping(); "
             "mapping.action = action; "
             "mapping.key = key; "
             "mapping.modifiers = %s; "
             "mapping.triggers = %s; "
             "ctx.mappings.append(mapping); "
             "unreal.EditorAssetLibrary.save_loaded_asset(ctx)"),
        *ContextPath, *ActionPath, *Key, *ModifiersStr, *TriggersStr);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        {
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("contextPath"), ContextPath);
            Writer->WriteValue(TEXT("actionPath"), ActionPath);
            Writer->WriteValue(TEXT("key"), Key);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}
