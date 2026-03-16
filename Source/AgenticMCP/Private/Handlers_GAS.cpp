// Handlers_GAS.cpp
// Gameplay Ability System (GAS) handlers for AgenticMCP.
// UE 5.6 target. Abilities, Effects, Attribute Sets.
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

DEFINE_LOG_CATEGORY_STATIC(LogMCPGAS, Log, All);

// ============================================================
// gasList
// List all GAS-related assets (Abilities, Effects, Attribute Sets)
// ============================================================
FString FAgenticMCPServer::HandleGASList(const FString& Body)
{
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

    auto ListByClass = [&](const TCHAR* Module, const TCHAR* ClassName) -> TArray<FAssetData>
    {
        TArray<FAssetData> Assets;
        FTopLevelAssetPath ClassPath(Module, ClassName);
        ARM.Get().GetAssetsByClass(ClassPath, Assets, true);
        return Assets;
    };

    // GAS classes are in GameplayAbilities module
    TArray<FAssetData> Abilities = ListByClass(TEXT("/Script/GameplayAbilities"), TEXT("GameplayAbility"));
    TArray<FAssetData> Effects = ListByClass(TEXT("/Script/GameplayAbilities"), TEXT("GameplayEffect"));

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);

    auto WriteArray = [&](const TCHAR* Name, const TArray<FAssetData>& Arr)
    {
        Writer->WriteArrayStart(Name);
        for (const FAssetData& A : Arr)
        {
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("name"), A.AssetName.ToString());
            Writer->WriteValue(TEXT("path"), A.GetObjectPathString());
            Writer->WriteObjectEnd();
        }
        Writer->WriteArrayEnd();
    };

    WriteArray(TEXT("abilities"), Abilities);
    WriteArray(TEXT("effects"), Effects);
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// gasCreateAbility
// Create a Gameplay Ability Blueprint
// Params: name (string), path (string, default /Game/GAS)
//         parentClass (string, optional - parent ability class name)
// ============================================================
FString FAgenticMCPServer::HandleGASCreateAbility(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString Name = Json->GetStringField(TEXT("name"));
    FString Path = Json->HasField(TEXT("path")) ? Json->GetStringField(TEXT("path")) : TEXT("/Game/GAS");

    if (Name.IsEmpty())
    {
        return MakeErrorJson(TEXT("name is required"));
    }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "asset_tools = unreal.AssetToolsHelpers.get_asset_tools(); "
             "factory = unreal.BlueprintFactory(); "
             "factory.set_editor_property('parent_class', unreal.GameplayAbility); "
             "bp = asset_tools.create_asset('%s', '%s', unreal.Blueprint, factory); "
             "if bp: unreal.EditorAssetLibrary.save_loaded_asset(bp); print('SUCCESS')"),
        *Name, *Path);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        {
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("name"), Name);
            Writer->WriteValue(TEXT("path"), FString::Printf(TEXT("%s/%s"), *Path, *Name));
            Writer->WriteValue(TEXT("type"), TEXT("GameplayAbility"));
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded or GameplayAbilities not available."));
}

// ============================================================
// gasCreateEffect
// Create a Gameplay Effect Blueprint
// Params: name (string), path (string, default /Game/GAS)
//         durationType (string: Instant/Infinite/HasDuration, default Instant)
// ============================================================
FString FAgenticMCPServer::HandleGASCreateEffect(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString Name = Json->GetStringField(TEXT("name"));
    FString Path = Json->HasField(TEXT("path")) ? Json->GetStringField(TEXT("path")) : TEXT("/Game/GAS");
    FString DurationType = Json->HasField(TEXT("durationType")) ? Json->GetStringField(TEXT("durationType")) : TEXT("Instant");

    if (Name.IsEmpty())
    {
        return MakeErrorJson(TEXT("name is required"));
    }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "asset_tools = unreal.AssetToolsHelpers.get_asset_tools(); "
             "factory = unreal.BlueprintFactory(); "
             "factory.set_editor_property('parent_class', unreal.GameplayEffect); "
             "bp = asset_tools.create_asset('%s', '%s', unreal.Blueprint, factory); "
             "if bp: "
             "  cdo = unreal.get_default_object(bp.generated_class()); "
             "  cdo.set_editor_property('duration_policy', unreal.GameplayEffectDurationType.%s); "
             "  unreal.EditorAssetLibrary.save_loaded_asset(bp); print('SUCCESS')"),
        *Name, *Path, *DurationType);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        {
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("name"), Name);
            Writer->WriteValue(TEXT("path"), FString::Printf(TEXT("%s/%s"), *Path, *Name));
            Writer->WriteValue(TEXT("durationType"), DurationType);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}

// ============================================================
// gasAddModifier
// Add an attribute modifier to a Gameplay Effect
// Params: effectPath (string), attribute (string), operation (string: Add/Multiply/Override)
//         magnitude (float)
// ============================================================
FString FAgenticMCPServer::HandleGASAddModifier(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString EffectPath = Json->GetStringField(TEXT("effectPath"));
    FString Attribute = Json->GetStringField(TEXT("attribute"));
    FString Operation = Json->HasField(TEXT("operation")) ? Json->GetStringField(TEXT("operation")) : TEXT("Add");
    float Magnitude = Json->HasField(TEXT("magnitude")) ? Json->GetNumberField(TEXT("magnitude")) : 0.0f;

    if (EffectPath.IsEmpty() || Attribute.IsEmpty())
    {
        return MakeErrorJson(TEXT("effectPath and attribute are required"));
    }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "effect = unreal.load_asset('%s'); "
             "cdo = unreal.get_default_object(effect.generated_class()); "
             "mod = unreal.GameplayModifierInfo(); "
             "mod.attribute = unreal.GameplayAttribute(attribute_name='%s'); "
             "mod.modifier_op = unreal.GameplayModOp.%s; "
             "mod.modifier_magnitude = unreal.GameplayEffectModifierMagnitude(scalable_float_magnitude=%f); "
             "mods = list(cdo.get_editor_property('modifiers')); "
             "mods.append(mod); "
             "cdo.set_editor_property('modifiers', mods); "
             "unreal.EditorAssetLibrary.save_loaded_asset(effect)"),
        *EffectPath, *Attribute, *Operation, Magnitude);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        {
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("effectPath"), EffectPath);
            Writer->WriteValue(TEXT("attribute"), Attribute);
            Writer->WriteValue(TEXT("operation"), Operation);
            Writer->WriteValue(TEXT("magnitude"), Magnitude);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}
