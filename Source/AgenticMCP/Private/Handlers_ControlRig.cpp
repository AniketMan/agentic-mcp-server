// Handlers_ControlRig.cpp
// Control Rig creation and editing handlers for AgenticMCP.
// UE 5.6 target. Uses dynamic module loading -- ControlRig plugin must be enabled.
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
#include "FileHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPControlRig, Log, All);

// ============================================================
// controlRigList
// List all Control Rig assets in the project
// ============================================================
FString FAgenticMCPServer::HandleControlRigList(const FString& Body)
{
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    TArray<FAssetData> Assets;

    // ControlRigBlueprint is in /Script/ControlRig
    FTopLevelAssetPath ClassPath(TEXT("/Script/ControlRig"), TEXT("ControlRigBlueprint"));
    ARM.Get().GetAssetsByClass(ClassPath, Assets, true);

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteArrayStart(TEXT("controlRigs"));
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
// controlRigCreate
// Create a new Control Rig Blueprint asset
// Params: name (string), path (string, default /Game/ControlRigs)
// ============================================================
FString FAgenticMCPServer::HandleControlRigCreate(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString Name = Json->GetStringField(TEXT("name"));
    FString Path = Json->HasField(TEXT("path")) ? Json->GetStringField(TEXT("path")) : TEXT("/Game/ControlRigs");
    if (Name.IsEmpty())
    {
        return MakeErrorJson(TEXT("name is required"));
    }

    // Use the factory system to create a ControlRigBlueprint
    UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("ControlRigBlueprintFactory"));
    if (!FactoryClass)
    {
        return MakeErrorJson(TEXT("ControlRig plugin is not loaded. Enable the ControlRig plugin first."));
    }

    UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
    if (!Factory)
    {
        return MakeErrorJson(TEXT("Failed to create ControlRigBlueprintFactory"));
    }

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    UObject* NewAsset = AssetTools.CreateAsset(Name, Path, nullptr, Factory);
    if (!NewAsset)
    {
        return MakeErrorJson(TEXT("Failed to create Control Rig asset"));
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
// controlRigGetGraph
// Get the rig graph structure (controls, bones, chains)
// Params: path (string) - asset path to the Control Rig
// ============================================================
FString FAgenticMCPServer::HandleControlRigGetGraph(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString AssetPath = Json->GetStringField(TEXT("path"));
    if (AssetPath.IsEmpty())
    {
        return MakeErrorJson(TEXT("path is required"));
    }

    UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
    if (!Asset)
    {
        return MakeErrorJson(FString::Printf(TEXT("Control Rig not found: %s"), *AssetPath));
    }

    // Use reflection to get the rig model hierarchy
    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteValue(TEXT("path"), AssetPath);
    Writer->WriteValue(TEXT("class"), Asset->GetClass()->GetName());

    // Enumerate all UEdGraph objects in the blueprint
    Writer->WriteArrayStart(TEXT("graphs"));
    TArray<UObject*> SubObjects;
    GetObjectsWithOuter(Asset, SubObjects, false);
    for (UObject* Sub : SubObjects)
    {
        if (UEdGraph* Graph = Cast<UEdGraph>(Sub))
        {
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("name"), Graph->GetName());
            Writer->WriteValue(TEXT("nodeCount"), Graph->Nodes.Num());
            Writer->WriteArrayStart(TEXT("nodes"));
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                Writer->WriteObjectStart();
                Writer->WriteValue(TEXT("name"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                Writer->WriteValue(TEXT("class"), Node->GetClass()->GetName());
                Writer->WriteValue(TEXT("id"), Node->NodeGuid.ToString());
                Writer->WriteValue(TEXT("posX"), Node->NodePosX);
                Writer->WriteValue(TEXT("posY"), Node->NodePosY);
                Writer->WriteObjectEnd();
            }
            Writer->WriteArrayEnd();
            Writer->WriteObjectEnd();
        }
    }
    Writer->WriteArrayEnd();
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// controlRigAddControl
// Add a control to a Control Rig
// Params: path (string), controlName (string), controlType (string: Float/Vector/Transform/Bool/Integer)
//         parentBone (string, optional)
// ============================================================
FString FAgenticMCPServer::HandleControlRigAddControl(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString AssetPath = Json->GetStringField(TEXT("path"));
    FString ControlName = Json->GetStringField(TEXT("controlName"));
    FString ControlType = Json->HasField(TEXT("controlType")) ? Json->GetStringField(TEXT("controlType")) : TEXT("Transform");

    if (AssetPath.IsEmpty() || ControlName.IsEmpty())
    {
        return MakeErrorJson(TEXT("path and controlName are required"));
    }

    // Load the rig blueprint and use Python bridge for complex rig operations
    // Control Rig editing API is heavily Python-based in UE 5.6
    UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
    if (!Asset)
    {
        return MakeErrorJson(FString::Printf(TEXT("Control Rig not found: %s"), *AssetPath));
    }

    // Execute via the unreal Python module for rig manipulation
    FString PythonCmd = FString::Printf(
        TEXT("import unreal; rig = unreal.load_object(name='%s', outer=None); "
             "lib = unreal.ControlRigBlueprintLibrary; "
             "lib.add_control(rig, '%s', unreal.RigControlType.%s)"),
        *AssetPath, *ControlName, *ControlType);

    // Execute Python command via GEditor->Exec()
    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
            FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
            bool bSuccess = GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        if (bSuccess)
        {
            Asset->MarkPackageDirty();
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("controlName"), ControlName);
            Writer->WriteValue(TEXT("controlType"), ControlType);
            Writer->WriteValue(TEXT("rigPath"), AssetPath);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded. Enable Python Editor Script Plugin to use Control Rig editing."));
}

// ============================================================
// controlRigAddBone
// Add a bone/element to the rig hierarchy
// Params: path (string), boneName (string), parentBone (string, optional)
//         position (object: x,y,z), rotation (object: x,y,z)
// ============================================================
FString FAgenticMCPServer::HandleControlRigAddBone(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString AssetPath = Json->GetStringField(TEXT("path"));
    FString BoneName = Json->GetStringField(TEXT("boneName"));
    FString ParentBone = Json->HasField(TEXT("parentBone")) ? Json->GetStringField(TEXT("parentBone")) : TEXT("");

    if (AssetPath.IsEmpty() || BoneName.IsEmpty())
    {
        return MakeErrorJson(TEXT("path and boneName are required"));
    }

    float PosX = 0, PosY = 0, PosZ = 0;
    if (Json->HasField(TEXT("position")))
    {
        auto Pos = Json->GetObjectField(TEXT("position"));
        PosX = Pos->GetNumberField(TEXT("x"));
        PosY = Pos->GetNumberField(TEXT("y"));
        PosZ = Pos->GetNumberField(TEXT("z"));
    }

    UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
    if (!Asset)
    {
        return MakeErrorJson(FString::Printf(TEXT("Control Rig not found: %s"), *AssetPath));
    }

    FString ParentArg = ParentBone.IsEmpty() ? TEXT("None") : FString::Printf(TEXT("'%s'"), *ParentBone);
    FString PythonCmd = FString::Printf(
        TEXT("import unreal; rig = unreal.load_object(name='%s', outer=None); "
             "lib = unreal.ControlRigBlueprintLibrary; "
             "t = unreal.Transform(location=unreal.Vector(%f,%f,%f)); "
             "lib.add_bone(rig, '%s', parent_name=%s, transform=t)"),
        *AssetPath, PosX, PosY, PosZ, *BoneName, *ParentArg);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        {
            Asset->MarkPackageDirty();
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("boneName"), BoneName);
            Writer->WriteValue(TEXT("parentBone"), ParentBone);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded. Enable Python Editor Script Plugin."));
}

// ============================================================
// controlRigSetupIK
// Set up an IK chain on the rig
// Params: path (string), chainName (string), startBone (string), endBone (string)
//         ikType (string: FBIK/CCDIK/TwoBoneIK, default TwoBoneIK)
// ============================================================
FString FAgenticMCPServer::HandleControlRigSetupIK(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString AssetPath = Json->GetStringField(TEXT("path"));
    FString ChainName = Json->GetStringField(TEXT("chainName"));
    FString StartBone = Json->GetStringField(TEXT("startBone"));
    FString EndBone = Json->GetStringField(TEXT("endBone"));
    FString IKType = Json->HasField(TEXT("ikType")) ? Json->GetStringField(TEXT("ikType")) : TEXT("TwoBoneIK");

    if (AssetPath.IsEmpty() || ChainName.IsEmpty() || StartBone.IsEmpty() || EndBone.IsEmpty())
    {
        return MakeErrorJson(TEXT("path, chainName, startBone, and endBone are required"));
    }

    UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
    if (!Asset)
    {
        return MakeErrorJson(FString::Printf(TEXT("Control Rig not found: %s"), *AssetPath));
    }

    // IK setup via Python -- the Control Rig solver nodes are best added via script
    FString PythonCmd = FString::Printf(
        TEXT("import unreal; rig = unreal.load_object(name='%s', outer=None); "
             "ctrl = rig.get_controller(); "
             "solver = ctrl.add_unit_node('%s', method_name='Execute', position=unreal.Vector2D(400,200)); "
             "ctrl.set_pin_default_value(solver + '.StartBone', '%s'); "
             "ctrl.set_pin_default_value(solver + '.EndBone', '%s')"),
        *AssetPath, *IKType, *StartBone, *EndBone);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        {
            Asset->MarkPackageDirty();
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("chainName"), ChainName);
            Writer->WriteValue(TEXT("ikType"), IKType);
            Writer->WriteValue(TEXT("startBone"), StartBone);
            Writer->WriteValue(TEXT("endBone"), EndBone);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded. Enable Python Editor Script Plugin."));
}
