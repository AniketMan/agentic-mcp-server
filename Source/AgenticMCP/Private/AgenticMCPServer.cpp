// AgenticMCPServer.cpp
// Core HTTP server implementation for AgenticMCP.
// Contains: Start/Stop/ProcessOneRequest, route registration, serialization,
// and all common helper functions used across handler files.
//
// Handler implementations are split across separate files:
//   Handlers_Read.cpp       - Blueprint read operations
//   Handlers_Mutation.cpp   - Blueprint write operations
//   Handlers_Actors.cpp     - Actor management
//   Handlers_Level.cpp      - Level/sublevel management
//   Handlers_Validation.cpp - Validation and snapshots

#include "AgenticMCPServer.h"

// UE5 Engine includes
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Knot.h"
#include "EdGraphNode_Comment.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

// JSON
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// HTTP Server
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpPath.h"

// Misc
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/GarbageCollection.h"

// ============================================================
// SEH wrappers for crash-safe compilation and saving (Windows only)
// These prevent a single bad Blueprint from crashing the entire server.
// ============================================================

#if PLATFORM_WINDOWS

// Inner functions with C++ objects (destructors allowed)
static void CompileBlueprintInner(UBlueprint* BP, EBlueprintCompileOptions Opts)
{
	FKismetEditorUtilities::CompileBlueprint(BP, Opts, nullptr);
}

static ESavePackageResult SavePackageInner(
	UPackage* Package, UObject* Asset, const TCHAR* Filename,
	FSavePackageArgs* SaveArgs)
{
	FSavePackageResultStruct Result = UPackage::Save(Package, Asset, Filename, *SaveArgs);
	return Result.Result;
}

// SEH wrappers - NO C++ objects with destructors allowed here
#pragma warning(push)
#pragma warning(disable: 4611)

int32 TryCompileBlueprintSEH(UBlueprint* BP, EBlueprintCompileOptions Opts)
{
	__try
	{
		CompileBlueprintInner(BP, Opts);
		return 0;
	}
	__except (1) // EXCEPTION_EXECUTE_HANDLER
	{
		return -1;
	}
}

static int32 TrySavePackageSEH(
	UPackage* Package, UObject* Asset, const TCHAR* Filename,
	FSavePackageArgs* SaveArgs, ESavePackageResult* OutResult)
{
	__try
	{
		*OutResult = SavePackageInner(Package, Asset, Filename, SaveArgs);
		return 0;
	}
	__except (1)
	{
		*OutResult = ESavePackageResult::Error;
		return -1;
	}
}

static void RefreshAllNodesInner(UBlueprint* BP)
{
	FBlueprintEditorUtils::RefreshAllNodes(BP);
}

int32 TryRefreshAllNodesSEH(UBlueprint* BP)
{
	__try
	{
		RefreshAllNodesInner(BP);
		return 0;
	}
	__except (1)
	{
		return -1;
	}
}

static void MarkStructurallyModifiedInner(UBlueprint* BP)
{
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
}

int32 TryMarkStructurallyModifiedSEH(UBlueprint* BP)
{
	__try
	{
		MarkStructurallyModifiedInner(BP);
		return 0;
	}
	__except (1)
	{
		return -1;
	}
}

static void MarkModifiedInner(UBlueprint* BP)
{
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
}

int32 TryMarkModifiedSEH(UBlueprint* BP)
{
	__try
	{
		MarkModifiedInner(BP);
		return 0;
	}
	__except (1)
	{
		return -1;
	}
}

#pragma warning(pop)
#endif // PLATFORM_WINDOWS

// ============================================================
// JSON Helpers
// ============================================================

FString FAgenticMCPServer::JsonToString(TSharedRef<FJsonObject> JsonObj)
{
	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(JsonObj, Writer);
	return Output;
}

TSharedPtr<FJsonObject> FAgenticMCPServer::ParseBodyJson(const FString& Body)
{
	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	FJsonSerializer::Deserialize(Reader, JsonObj);
	return JsonObj;
}

FString FAgenticMCPServer::MakeErrorJson(const FString& Message)
{
	TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
	E->SetStringField(TEXT("error"), Message);
	return JsonToString(E);
}

// ============================================================
// URL Decoding
// ============================================================

FString FAgenticMCPServer::UrlDecode(const FString& EncodedString)
{
	FString Result;
	Result.Reserve(EncodedString.Len());

	for (int32 i = 0; i < EncodedString.Len(); ++i)
	{
		TCHAR C = EncodedString[i];
		if (C == TEXT('+'))
		{
			Result += TEXT(' ');
		}
		else if (C == TEXT('%') && i + 2 < EncodedString.Len())
		{
			FString HexStr = EncodedString.Mid(i + 1, 2);
			int32 HexVal = 0;
			bool bValid = true;
			for (TCHAR H : HexStr)
			{
				HexVal <<= 4;
				if      (H >= TEXT('0') && H <= TEXT('9')) HexVal += H - TEXT('0');
				else if (H >= TEXT('a') && H <= TEXT('f')) HexVal += 10 + H - TEXT('a');
				else if (H >= TEXT('A') && H <= TEXT('F')) HexVal += 10 + H - TEXT('A');
				else { bValid = false; break; }
			}
			if (bValid) { Result += (TCHAR)HexVal; i += 2; }
			else        { Result += C; }
		}
		else
		{
			Result += C;
		}
	}
	return Result;
}

// ============================================================
// Asset Lookup Helpers
// ============================================================

FAssetData* FAgenticMCPServer::FindBlueprintAsset(const FString& NameOrPath)
{
	// Exact match first
	for (FAssetData& Asset : AllBlueprintAssets)
	{
		if (Asset.AssetName.ToString() == NameOrPath ||
			Asset.PackageName.ToString() == NameOrPath)
		{
			return &Asset;
		}
	}
	// Case-insensitive fallback
	for (FAssetData& Asset : AllBlueprintAssets)
	{
		if (Asset.AssetName.ToString().Equals(NameOrPath, ESearchCase::IgnoreCase) ||
			Asset.PackageName.ToString().Equals(NameOrPath, ESearchCase::IgnoreCase))
		{
			return &Asset;
		}
	}
	return nullptr;
}

FAssetData* FAgenticMCPServer::FindMapAsset(const FString& NameOrPath)
{
	// Exact match first
	for (FAssetData& Asset : AllMapAssets)
	{
		if (Asset.AssetName.ToString() == NameOrPath ||
			Asset.PackageName.ToString() == NameOrPath)
		{
			return &Asset;
		}
	}
	// Case-insensitive fallback
	for (FAssetData& Asset : AllMapAssets)
	{
		if (Asset.AssetName.ToString().Equals(NameOrPath, ESearchCase::IgnoreCase) ||
			Asset.PackageName.ToString().Equals(NameOrPath, ESearchCase::IgnoreCase))
		{
			return &Asset;
		}
	}
	return nullptr;
}

UBlueprint* FAgenticMCPServer::LoadBlueprintByName(const FString& NameOrPath, FString& OutError)
{
	// Strategy 1: Try as a regular Blueprint asset
	FAssetData* Asset = FindBlueprintAsset(NameOrPath);
	if (Asset)
	{
		UBlueprint* BP = Cast<UBlueprint>(Asset->GetAsset());
		if (BP) return BP;
	}

	// Strategy 2: Try as a level blueprint (from a .umap)
	FAssetData* MapAsset = FindMapAsset(NameOrPath);
	if (MapAsset)
	{
		UWorld* World = Cast<UWorld>(MapAsset->GetAsset());
		if (World && World->PersistentLevel)
		{
			ULevelScriptBlueprint* LevelBP = World->PersistentLevel->GetLevelScriptBlueprint(true);
			if (LevelBP)
			{
				UE_LOG(LogTemp, Display, TEXT("AgenticMCP: Loaded level blueprint from map '%s'"),
					*NameOrPath);
				return LevelBP;
			}
		}
		OutError = FString::Printf(
			TEXT("Map '%s' loaded but its level blueprint could not be retrieved."),
			*NameOrPath);
		return nullptr;
	}

	OutError = FString::Printf(
		TEXT("Blueprint or map '%s' not found. Use list_blueprints to see available assets. "
			 "Level blueprints are referenced by their map name."),
		*NameOrPath);
	return nullptr;
}

UEdGraphNode* FAgenticMCPServer::FindNodeByGuid(
	UBlueprint* BP, const FString& GuidString, UEdGraph** OutGraph)
{
	FGuid TargetGuid;
	FGuid::Parse(GuidString, TargetGuid);

	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == TargetGuid)
			{
				if (OutGraph) *OutGraph = Graph;
				return Node;
			}
		}
	}
	return nullptr;
}

UClass* FAgenticMCPServer::FindClassByName(const FString& ClassName)
{
	// Direct lookup
	UClass* Found = FindFirstObject<UClass>(*ClassName);
	if (Found) return Found;

	// Try with/without U prefix
	if (ClassName.StartsWith(TEXT("U")))
	{
		Found = FindFirstObject<UClass>(*ClassName.Mid(1));
		if (Found) return Found;
	}
	else
	{
		Found = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *ClassName));
		if (Found) return Found;
	}

	// Broad search across all modules
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->GetName() == ClassName ||
			It->GetName() == FString::Printf(TEXT("U%s"), *ClassName))
		{
			return *It;
		}
	}

	return nullptr;
}

bool FAgenticMCPServer::ResolveTypeFromString(
	const FString& TypeName, FEdGraphPinType& OutPinType, FString& OutError)
{
	// Built-in types
	if (TypeName == TEXT("bool"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		return true;
	}
	if (TypeName == TEXT("byte") || TypeName == TEXT("uint8"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		return true;
	}
	if (TypeName == TEXT("int") || TypeName == TEXT("int32"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		return true;
	}
	if (TypeName == TEXT("int64"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
		return true;
	}
	if (TypeName == TEXT("float") || TypeName == TEXT("double"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = TEXT("double");
		return true;
	}
	if (TypeName == TEXT("string") || TypeName == TEXT("FString"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
		return true;
	}
	if (TypeName == TEXT("name") || TypeName == TEXT("FName"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		return true;
	}
	if (TypeName == TEXT("text") || TypeName == TEXT("FText"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
		return true;
	}
	if (TypeName == TEXT("exec"))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
		return true;
	}

	// Try as a struct
	FString SearchName = TypeName;
	if (SearchName.StartsWith(TEXT("F")))
		SearchName = SearchName.Mid(1);

	UScriptStruct* FoundStruct = FindFirstObject<UScriptStruct>(*SearchName);
	if (!FoundStruct)
		FoundStruct = FindFirstObject<UScriptStruct>(*TypeName);
	if (FoundStruct)
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = FoundStruct;
		return true;
	}

	// Try as a class (object reference)
	UClass* FoundClass = FindClassByName(TypeName);
	if (FoundClass)
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		OutPinType.PinSubCategoryObject = FoundClass;
		return true;
	}

	OutError = FString::Printf(TEXT("Could not resolve type '%s'"), *TypeName);
	return false;
}

// ============================================================
// Blueprint Save Helper
// ============================================================

bool FAgenticMCPServer::SaveBlueprintPackage(UBlueprint* BP)
{
	if (!BP) return false;

	// Compile first
	EBlueprintCompileOptions CompileOpts = EBlueprintCompileOptions::None;

#if PLATFORM_WINDOWS
	int32 CompileResult = TryCompileBlueprintSEH(BP, CompileOpts);
	if (CompileResult != 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("AgenticMCP: Blueprint compilation crashed (SEH caught) for '%s'"),
			*BP->GetName());
		// Continue to save anyway -- the Blueprint may still be in a usable state
	}
#else
	FKismetEditorUtilities::CompileBlueprint(BP, CompileOpts, nullptr);
#endif

	// Save the package
	UPackage* Package = BP->GetOutermost();
	if (!Package) return false;

	FString PackageFilename;
	if (!FPackageName::DoesPackageExist(Package->GetName(), &PackageFilename))
	{
		// New package -- derive filename from package name
		PackageFilename = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Standalone;

#if PLATFORM_WINDOWS
	ESavePackageResult SaveResult;
	int32 SaveSEH = TrySavePackageSEH(Package, BP, *PackageFilename, &SaveArgs, &SaveResult);
	if (SaveSEH != 0)
	{
		UE_LOG(LogTemp, Error, TEXT("AgenticMCP: Package save crashed (SEH caught) for '%s'"),
			*BP->GetName());
		return false;
	}
	return SaveResult == ESavePackageResult::Success;
#else
	FSavePackageResultStruct Result = UPackage::Save(Package, BP, *PackageFilename, SaveArgs);
	return Result.Result == ESavePackageResult::Success;
#endif
}

// ============================================================
// Serialization
// ============================================================

TSharedRef<FJsonObject> FAgenticMCPServer::SerializeBlueprint(UBlueprint* BP)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("name"), BP->GetName());
	Json->SetStringField(TEXT("path"), BP->GetPathName());

	if (BP->ParentClass)
	{
		Json->SetStringField(TEXT("parentClass"), BP->ParentClass->GetName());
	}

	// Graphs
	TArray<TSharedPtr<FJsonValue>> GraphArray;
	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph)
		{
			TSharedRef<FJsonObject> GraphInfo = MakeShared<FJsonObject>();
			GraphInfo->SetStringField(TEXT("name"), Graph->GetName());
			GraphInfo->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
			GraphArray.Add(MakeShared<FJsonValueObject>(GraphInfo));
		}
	}
	Json->SetArrayField(TEXT("graphs"), GraphArray);

	// Variables
	TArray<TSharedPtr<FJsonValue>> VarArray;
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		TSharedRef<FJsonObject> VarInfo = MakeShared<FJsonObject>();
		VarInfo->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarInfo->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		if (Var.VarType.PinSubCategoryObject.IsValid())
		{
			VarInfo->SetStringField(TEXT("subtype"), Var.VarType.PinSubCategoryObject->GetName());
		}
		VarArray.Add(MakeShared<FJsonValueObject>(VarInfo));
	}
	Json->SetArrayField(TEXT("variables"), VarArray);

	return Json;
}

TSharedPtr<FJsonObject> FAgenticMCPServer::SerializeGraph(UEdGraph* Graph)
{
	if (!Graph) return nullptr;

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("name"), Graph->GetName());

	// Nodes
	TArray<TSharedPtr<FJsonValue>> NodeArray;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		TSharedPtr<FJsonObject> NodeJson = SerializeNode(Node);
		if (NodeJson.IsValid())
		{
			NodeArray.Add(MakeShared<FJsonValueObject>(NodeJson.ToSharedRef()));
		}
	}
	Json->SetArrayField(TEXT("nodes"), NodeArray);

	return Json;
}

TSharedPtr<FJsonObject> FAgenticMCPServer::SerializeNode(UEdGraphNode* Node)
{
	if (!Node) return nullptr;

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
	Json->SetStringField(TEXT("nodeClass"), Node->GetClass()->GetName());
	Json->SetStringField(TEXT("nodeTitle"),
		Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	Json->SetNumberField(TEXT("posX"), Node->NodePosX);
	Json->SetNumberField(TEXT("posY"), Node->NodePosY);

	// Node comment
	if (!Node->NodeComment.IsEmpty())
	{
		Json->SetStringField(TEXT("comment"), Node->NodeComment);
	}

	// Pins
	TArray<TSharedPtr<FJsonValue>> PinArray;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		TSharedPtr<FJsonObject> PinJson = SerializePin(Pin);
		if (PinJson.IsValid())
		{
			PinArray.Add(MakeShared<FJsonValueObject>(PinJson.ToSharedRef()));
		}
	}
	Json->SetArrayField(TEXT("pins"), PinArray);

	return Json;
}

TSharedPtr<FJsonObject> FAgenticMCPServer::SerializePin(UEdGraphPin* Pin)
{
	if (!Pin) return nullptr;

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("pinName"), Pin->PinName.ToString());
	Json->SetStringField(TEXT("direction"),
		Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
	Json->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());

	if (Pin->PinType.PinSubCategoryObject.IsValid())
	{
		Json->SetStringField(TEXT("subtype"), Pin->PinType.PinSubCategoryObject->GetName());
	}

	// Default value
	if (!Pin->DefaultValue.IsEmpty())
	{
		Json->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
	}
	if (Pin->DefaultObject)
	{
		Json->SetStringField(TEXT("defaultObject"), Pin->DefaultObject->GetPathName());
	}

	// Connections
	if (Pin->LinkedTo.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Links;
		for (UEdGraphPin* Linked : Pin->LinkedTo)
		{
			if (Linked && Linked->GetOwningNode())
			{
				TSharedRef<FJsonObject> LinkJson = MakeShared<FJsonObject>();
				LinkJson->SetStringField(TEXT("nodeId"),
					Linked->GetOwningNode()->NodeGuid.ToString());
				LinkJson->SetStringField(TEXT("pinName"), Linked->PinName.ToString());
				Links.Add(MakeShared<FJsonValueObject>(LinkJson));
			}
		}
		Json->SetArrayField(TEXT("linkedTo"), Links);
	}

	return Json;
}

// ============================================================
// Snapshot Helpers
// ============================================================

FString FAgenticMCPServer::GenerateSnapshotId(const FString& BlueprintName)
{
	return FString::Printf(TEXT("%s_%s"),
		*BlueprintName, *FGuid::NewGuid().ToString(EGuidFormats::Short));
}

FAgenticGraphSnapshotData FAgenticMCPServer::CaptureGraphSnapshot(UEdGraph* Graph)
{
	FAgenticGraphSnapshotData Data;
	if (!Graph) return Data;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		FAgenticNodeRecord Record;
		Record.NodeGuid = Node->NodeGuid.ToString();
		Record.NodeClass = Node->GetClass()->GetName();
		Record.NodeTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		Data.Nodes.Add(Record);

		// Record connections (output pins only to avoid duplicates)
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output)
			{
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (Linked && Linked->GetOwningNode())
					{
						FAgenticPinConnectionRecord Conn;
						Conn.SourceNodeGuid = Node->NodeGuid.ToString();
						Conn.SourcePinName = Pin->PinName.ToString();
						Conn.TargetNodeGuid = Linked->GetOwningNode()->NodeGuid.ToString();
						Conn.TargetPinName = Linked->PinName.ToString();
						Data.Connections.Add(Conn);
					}
				}
			}
		}
	}

	return Data;
}

void FAgenticMCPServer::PruneOldSnapshots()
{
	while (Snapshots.Num() > MaxSnapshots)
	{
		// Remove oldest snapshot
		FString OldestId;
		FDateTime OldestTime = FDateTime::MaxValue();
		for (const auto& Pair : Snapshots)
		{
			if (Pair.Value.CreatedAt < OldestTime)
			{
				OldestTime = Pair.Value.CreatedAt;
				OldestId = Pair.Key;
			}
		}
		if (!OldestId.IsEmpty())
		{
			Snapshots.Remove(OldestId);
		}
	}
}

// ============================================================
// Asset Registry Rescan
// ============================================================

FString FAgenticMCPServer::HandleRescan()
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	ARM.Get().SearchAllAssets(true);

	AllBlueprintAssets.Empty();
	ARM.Get().GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), AllBlueprintAssets, true);

	AllMapAssets.Empty();
	ARM.Get().GetAssetsByClass(UWorld::StaticClass()->GetClassPathName(), AllMapAssets, false);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("rescanned"));
	Result->SetNumberField(TEXT("blueprintCount"), AllBlueprintAssets.Num());
	Result->SetNumberField(TEXT("mapCount"), AllMapAssets.Num());

	UE_LOG(LogTemp, Display, TEXT("AgenticMCP: Rescanned - %d Blueprints, %d Maps"),
		AllBlueprintAssets.Num(), AllMapAssets.Num());

	return JsonToString(Result);
}

// ============================================================
// HandleOpenAsset
// POST /api/open-asset { "assetPath": "..." }
// Opens an asset in its appropriate editor (e.g., Sequencer for LevelSequence).
// ============================================================

FString FAgenticMCPServer::HandleOpenAsset(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString AssetPath = Json->GetStringField(TEXT("assetPath"));
	if (AssetPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: 'assetPath'"));
	}

	// Check if GEditor is available
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("GEditor not available"));
	}

	// Load the asset
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
	}

	// Open the asset in its editor using AssetEditorSubsystem
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return MakeErrorJson(TEXT("AssetEditorSubsystem not available"));
	}

	bool bOpened = AssetEditorSubsystem->OpenEditorForAsset(Asset);
	if (!bOpened)
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to open editor for asset: %s"), *AssetPath));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());

	return JsonToString(Result);
}

// ============================================================
// Route Registration
// ============================================================

void FAgenticMCPServer::RegisterHandlers()
{
	// Each entry maps an endpoint key to a lambda that dispatches to the correct handler.
	// GET handlers receive query params; POST handlers receive the body string.

	// ---- Lifecycle ----
	HandlerMap.Add(TEXT("rescan"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleRescan();
	});

	// ---- Blueprint Read ----
	HandlerMap.Add(TEXT("list"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleList(Params);
	});
	HandlerMap.Add(TEXT("blueprint"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleGetBlueprint(Params);
	});
	HandlerMap.Add(TEXT("graph"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleGetGraph(Params);
	});
	HandlerMap.Add(TEXT("search"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSearch(Params);
	});
	HandlerMap.Add(TEXT("references"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleFindReferences(Params);
	});
	HandlerMap.Add(TEXT("listClasses"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleListClasses(Body);
	});
	HandlerMap.Add(TEXT("listFunctions"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleListFunctions(Body);
	});
	HandlerMap.Add(TEXT("listProperties"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleListProperties(Body);
	});
	HandlerMap.Add(TEXT("getPinInfo"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleGetPinInfo(Body);
	});

	// ---- Blueprint Mutation ----
	HandlerMap.Add(TEXT("addNode"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAddNode(Body);
	});
	HandlerMap.Add(TEXT("deleteNode"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleDeleteNode(Body);
	});
	HandlerMap.Add(TEXT("connectPins"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleConnectPins(Body);
	});
	HandlerMap.Add(TEXT("disconnectPin"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleDisconnectPin(Body);
	});
	HandlerMap.Add(TEXT("setPinDefault"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSetPinDefault(Body);
	});
	HandlerMap.Add(TEXT("moveNode"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleMoveNode(Body);
	});
	HandlerMap.Add(TEXT("refreshAllNodes"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleRefreshAllNodes(Body);
	});
	HandlerMap.Add(TEXT("createBlueprint"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleCreateBlueprint(Body);
	});
	HandlerMap.Add(TEXT("createGraph"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleCreateGraph(Body);
	});
	HandlerMap.Add(TEXT("deleteGraph"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleDeleteGraph(Body);
	});
	HandlerMap.Add(TEXT("addVariable"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAddVariable(Body);
	});
	HandlerMap.Add(TEXT("removeVariable"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleRemoveVariable(Body);
	});
	HandlerMap.Add(TEXT("compileBlueprint"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleCompileBlueprint(Body);
	});
	HandlerMap.Add(TEXT("duplicateNodes"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleDuplicateNodes(Body);
	});
	HandlerMap.Add(TEXT("setNodeComment"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSetNodeComment(Body);
	});
	HandlerMap.Add(TEXT("addComponent"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAddComponent(Body);
	});

	// ---- Actor Management ----
	HandlerMap.Add(TEXT("listActors"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleListActors(Body);
	});
	HandlerMap.Add(TEXT("getActor"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleGetActor(Body);
	});
	HandlerMap.Add(TEXT("spawnActor"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSpawnActor(Body);
	});
	HandlerMap.Add(TEXT("deleteActor"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleDeleteActor(Body);
	});
	HandlerMap.Add(TEXT("setActorProperty"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSetActorProperty(Body);
	});
	HandlerMap.Add(TEXT("setActorTransform"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSetActorTransform(Body);
	});
	HandlerMap.Add(TEXT("moveActor"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSetActorTransform(Body);  // moveActor is alias for setActorTransform
	});

	// ---- Level Management ----
	HandlerMap.Add(TEXT("listLevels"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleListLevels(Body);
	});
	HandlerMap.Add(TEXT("loadLevel"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleLoadLevel(Body);
	});
	HandlerMap.Add(TEXT("removeSublevel"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleRemoveSublevel(Body);
	});
	HandlerMap.Add(TEXT("getLevelBlueprint"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleGetLevelBlueprint(Body);
	});
	HandlerMap.Add(TEXT("streamingLevelVisibility"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSetStreamingLevelVisibility(Body);
	});
	HandlerMap.Add(TEXT("outputLog"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleGetOutputLog(Body);
	});

	// ---- Validation and Safety ----
	HandlerMap.Add(TEXT("validateBlueprint"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleValidateBlueprint(Body);
	});
	HandlerMap.Add(TEXT("snapshotGraph"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSnapshotGraph(Body);
	});
	HandlerMap.Add(TEXT("restoreGraph"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleRestoreGraph(Body);
	});

	// ---- VisualAgent Automation ----
	HandlerMap.Add(TEXT("sceneSnapshot"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSceneSnapshot(Body);
	});
	HandlerMap.Add(TEXT("screenshot"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleScreenshot(Body);
	});
	HandlerMap.Add(TEXT("focusActor"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleFocusActor(Body);
	});
	HandlerMap.Add(TEXT("selectActor"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSelectActor(Body);
	});
	HandlerMap.Add(TEXT("setViewport"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSetViewport(Body);
	});
	HandlerMap.Add(TEXT("waitReady"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleWaitReady(Body);
	});
	HandlerMap.Add(TEXT("resolveRef"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleResolveRef(Body);
	});
	HandlerMap.Add(TEXT("getCamera"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleGetCamera(Body);
	});
	HandlerMap.Add(TEXT("listViewports"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleListViewports(Body);
	});
	HandlerMap.Add(TEXT("getSelection"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleGetSelection(Body);
	});

	// ---- Level Sequences ----
	HandlerMap.Add(TEXT("listSequences"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleListSequences(Body);
	});
	HandlerMap.Add(TEXT("readSequence"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleReadSequence(Body);
	});
	HandlerMap.Add(TEXT("removeAudioTracks"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleRemoveAudioTracks(Body);
	});

	// ---- Debug Visualization ----
	HandlerMap.Add(TEXT("drawDebug"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleDrawDebug(Body);
	});
	HandlerMap.Add(TEXT("clearDebug"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleClearDebug(Body);
	});

	// ---- Blueprint Graph Snapshot ----
	HandlerMap.Add(TEXT("blueprintSnapshot"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleBlueprintSnapshot(Body);
	});

	// ---- Undo/Redo Transactions ----
	HandlerMap.Add(TEXT("beginTransaction"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleBeginTransaction(Body);
	});
	HandlerMap.Add(TEXT("endTransaction"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleEndTransaction(Body);
	});
	HandlerMap.Add(TEXT("undo"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleUndo(Body);
	});
	HandlerMap.Add(TEXT("redo"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleRedo(Body);
	});

	// ---- Diff/Compare Mode ----
	HandlerMap.Add(TEXT("saveState"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSaveState(Body);
	});

	// ---- Python Execution ----
	HandlerMap.Add(TEXT("executePython"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleExecutePython(Body);
	});
	HandlerMap.Add(TEXT("diffState"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleDiffState(Body);
	});
	HandlerMap.Add(TEXT("restoreState"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleRestoreState(Body);
	});
	HandlerMap.Add(TEXT("listStates"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleListStates(Body);
	});

	// ---- Asset Editor ----
	HandlerMap.Add(TEXT("openAsset"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleOpenAsset(Body);
	});

	// ---- PIE Control ----
	HandlerMap.Add(TEXT("startPIE"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleStartPIE(Params, Body);
	});
	HandlerMap.Add(TEXT("stopPIE"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleStopPIE(Params, Body);
	});
	HandlerMap.Add(TEXT("pausePIE"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePausePIE(Params, Body);
	});
	HandlerMap.Add(TEXT("stepPIE"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleStepPIE(Params, Body);
	});
	HandlerMap.Add(TEXT("getPIEState"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleGetPIEState(Params, Body);
	});

	// ---- Console Commands ----
	HandlerMap.Add(TEXT("executeConsole"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleExecuteConsole(Params, Body);
	});
	HandlerMap.Add(TEXT("getCVar"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleGetCVar(Params, Body);
	});
	HandlerMap.Add(TEXT("setCVar"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSetCVar(Params, Body);
	});
	HandlerMap.Add(TEXT("listCVars"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleListCVars(Params, Body);
	});

	// ---- Input Simulation ----
	HandlerMap.Add(TEXT("simulateInput"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSimulateInput(Params, Body);
	});

	// ---- Meta XR / OculusXR ----
	HandlerMap.Add(TEXT("xrStatus"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleXRStatus(Params, Body);
	});
	HandlerMap.Add(TEXT("xrGuardian"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleXRGuardian(Params, Body);
	});
	HandlerMap.Add(TEXT("xrSetGuardianVisibility"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleXRSetGuardianVisibility(Params, Body);
	});
	HandlerMap.Add(TEXT("xrHandTracking"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleXRHandTracking(Params, Body);
	});
	HandlerMap.Add(TEXT("xrControllers"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleXRControllers(Params, Body);
	});
	HandlerMap.Add(TEXT("xrPassthrough"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleXRPassthrough(Params, Body);
	});
	HandlerMap.Add(TEXT("xrSetPassthrough"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleXRSetPassthrough(Params, Body);
	});
	HandlerMap.Add(TEXT("xrSetDisplayFrequency"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleXRSetDisplayFrequency(Params, Body);
	});
	HandlerMap.Add(TEXT("xrSetPerformanceLevels"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleXRSetPerformanceLevels(Params, Body);
	});
	HandlerMap.Add(TEXT("xrRecenter"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleXRRecenter(Params, Body);
	});

	// ---- MetaXR Audio/Haptics Handlers ----
	HandlerMap.Add(TEXT("xrPlayHapticEffect"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleXRPlayHapticEffect(Params, Body);
	});
	HandlerMap.Add(TEXT("xrStopHapticEffect"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleXRStopHapticEffect(Params, Body);
	});
	HandlerMap.Add(TEXT("xrGetHapticCapabilities"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleXRGetHapticCapabilities(Params, Body);
	});
	HandlerMap.Add(TEXT("xrSetSpatialAudioEnabled"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleXRSetSpatialAudioEnabled(Params, Body);
	});
	HandlerMap.Add(TEXT("xrGetSpatialAudioStatus"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleXRGetSpatialAudioStatus(Params, Body);
	});
	HandlerMap.Add(TEXT("xrConfigureAudioAttenuation"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleXRConfigureAudioAttenuation(Params, Body);
	});

	// ---- Story/Game Handlers ----
	HandlerMap.Add(TEXT("storyState"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleStoryState(Params, Body);
	});
	HandlerMap.Add(TEXT("storyAdvance"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleStoryAdvance(Params, Body);
	});
	HandlerMap.Add(TEXT("storyGoto"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleStoryGoto(Params, Body);
	});
	HandlerMap.Add(TEXT("storyPlay"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleStoryPlay(Params, Body);
	});

	// ---- DataTable Handlers ----
	HandlerMap.Add(TEXT("dataTableRead"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleDataTableRead(Params, Body);
	});
	HandlerMap.Add(TEXT("dataTableWrite"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleDataTableWrite(Params, Body);
	});

	// ---- Animation Handlers ----
	HandlerMap.Add(TEXT("animationPlay"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimationPlay(Params, Body);
	});
	HandlerMap.Add(TEXT("animationStop"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimationStop(Params, Body);
	});

	// ---- Material Handlers ----
	HandlerMap.Add(TEXT("materialSetParam"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleMaterialSetParam(Params, Body);
	});

	// ---- Collision Handlers ----
	HandlerMap.Add(TEXT("collisionTrace"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleCollisionTrace(Params, Body);
	});

	// ---- RenderDoc Handlers ----
	HandlerMap.Add(TEXT("renderDocCapture"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleRenderDocCapture(Params, Body);
	});

	// ---- Audio Handlers ----
	HandlerMap.Add(TEXT("audioGetStatus"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAudioGetStatus(Params, Body);
	});
	HandlerMap.Add(TEXT("audioListActiveSounds"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAudioListActiveSounds(Params, Body);
	});
	HandlerMap.Add(TEXT("audioGetDeviceInfo"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAudioGetDeviceInfo(Params, Body);
	});
	HandlerMap.Add(TEXT("audioListSoundClasses"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAudioListSoundClasses(Params, Body);
	});
	HandlerMap.Add(TEXT("audioSetVolume"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAudioSetVolume(Params, Body);
	});
	HandlerMap.Add(TEXT("audioGetStats"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAudioGetStats(Params, Body);
	});
	HandlerMap.Add(TEXT("audioPlaySound"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAudioPlaySound(Params, Body);
	});
	HandlerMap.Add(TEXT("audioStopSound"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAudioStopSound(Params, Body);
	});
	HandlerMap.Add(TEXT("audioSetListener"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAudioSetListener(Params, Body);
	});
	HandlerMap.Add(TEXT("audioDebugVisualize"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAudioDebugVisualize(Params, Body);
	});

	// ---- Niagara Handlers ----
	HandlerMap.Add(TEXT("niagaraGetStatus"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleNiagaraGetStatus(Params, Body);
	});
	HandlerMap.Add(TEXT("niagaraListSystems"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleNiagaraListSystems(Params, Body);
	});
	HandlerMap.Add(TEXT("niagaraGetSystemInfo"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleNiagaraGetSystemInfo(Params, Body);
	});
	HandlerMap.Add(TEXT("niagaraGetEmitters"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleNiagaraGetEmitters(Params, Body);
	});
	HandlerMap.Add(TEXT("niagaraSetParameter"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleNiagaraSetParameter(Params, Body);
	});
	HandlerMap.Add(TEXT("niagaraGetParameters"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleNiagaraGetParameters(Params, Body);
	});
	HandlerMap.Add(TEXT("niagaraActivateSystem"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleNiagaraActivateSystem(Params, Body);
	});
	HandlerMap.Add(TEXT("niagaraSetEmitterEnable"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleNiagaraSetEmitterEnable(Params, Body);
	});
	HandlerMap.Add(TEXT("niagaraResetSystem"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleNiagaraResetSystem(Params, Body);
	});
	HandlerMap.Add(TEXT("niagaraGetStats"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleNiagaraGetStats(Params, Body);
	});
	HandlerMap.Add(TEXT("niagaraDebugHUD"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleNiagaraDebugHUD(Params, Body);
	});

	// ---- PixelStreaming Handlers ----
	HandlerMap.Add(TEXT("pixelStreamingGetStatus"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePixelStreamingGetStatus(Params, Body);
	});
	HandlerMap.Add(TEXT("pixelStreamingStart"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePixelStreamingStart(Params, Body);
	});
	HandlerMap.Add(TEXT("pixelStreamingStop"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePixelStreamingStop(Params, Body);
	});
	HandlerMap.Add(TEXT("pixelStreamingListStreamers"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePixelStreamingListStreamers(Params, Body);
	});
	HandlerMap.Add(TEXT("pixelStreamingGetCodec"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePixelStreamingGetCodec(Params, Body);
	});
	HandlerMap.Add(TEXT("pixelStreamingSetCodec"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePixelStreamingSetCodec(Params, Body);
	});
	HandlerMap.Add(TEXT("pixelStreamingListPlayers"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePixelStreamingListPlayers(Params, Body);
	});

	// ---- PCG Handlers ----
	HandlerMap.Add(TEXT("pcgListGraphs"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePCGListGraphs(Body);
	});
	HandlerMap.Add(TEXT("pcgGetGraphInfo"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePCGGetGraphInfo(Body);
	});
	HandlerMap.Add(TEXT("pcgExecuteGraph"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePCGExecuteGraph(Body);
	});
	HandlerMap.Add(TEXT("pcgGetNodeSettings"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePCGGetNodeSettings(Body);
	});
	HandlerMap.Add(TEXT("pcgSetNodeSettings"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePCGSetNodeSettings(Body);
	});
	HandlerMap.Add(TEXT("pcgListComponents"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePCGListComponents(Body);
	});

	// ---- Animation Blueprint Handlers ----
	HandlerMap.Add(TEXT("animBPList"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimBPList(Body);
	});
	HandlerMap.Add(TEXT("animBPGetGraph"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimBPGetGraph(Body);
	});
	HandlerMap.Add(TEXT("animBPGetStates"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimBPGetStates(Body);
	});
	HandlerMap.Add(TEXT("animBPGetTransitions"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimBPGetTransitions(Body);
	});
	HandlerMap.Add(TEXT("animBPGetSlotGroups"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimBPGetSlotGroups(Body);
	});
	HandlerMap.Add(TEXT("animBPGetMontages"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimBPGetMontages(Body);
	});

	// ---- Scene Hierarchy Handlers ----
	HandlerMap.Add(TEXT("sceneGetHierarchy"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSceneGetHierarchy(Body);
	});
	HandlerMap.Add(TEXT("sceneSetActorFolder"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSceneSetActorFolder(Body);
	});
	HandlerMap.Add(TEXT("sceneAttachActor"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSceneAttachActor(Body);
	});
	HandlerMap.Add(TEXT("sceneDetachActor"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSceneDetachActor(Body);
	});
	HandlerMap.Add(TEXT("sceneRenameActor"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSceneRenameActor(Body);
	});

	// ---- Sequencer Editing Handlers ----
	HandlerMap.Add(TEXT("sequencerCreate"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerCreate(Body);
	});
	HandlerMap.Add(TEXT("sequencerAddTrack"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerAddTrack(Body);
	});
	HandlerMap.Add(TEXT("sequencerGetTracks"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerGetTracks(Body);
	});
	HandlerMap.Add(TEXT("sequencerSetPlayRange"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerSetPlayRange(Body);
	});
	HandlerMap.Add(TEXT("sequencerAddSection"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerAddSection(Body);
	});
	HandlerMap.Add(TEXT("sequencerSetKeyframe"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerSetKeyframe(Body);
	});
	HandlerMap.Add(TEXT("sequencerDeleteSection"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerDeleteSection(Body);
	});
	HandlerMap.Add(TEXT("sequencerBindActor"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerBindActor(Body);
	});
	HandlerMap.Add(TEXT("sequencerAddCameraCut"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerAddCameraCut(Body);
	});
	HandlerMap.Add(TEXT("sequencerRender"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerRender(Body);
	});
	HandlerMap.Add(TEXT("sequencerDeleteTrack"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerDeleteTrack(Body);
	});
	HandlerMap.Add(TEXT("sequencerGetKeyframes"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerGetKeyframes(Body);
	});
	HandlerMap.Add(TEXT("sequencerAddSpawnable"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerAddSpawnable(Body);
	});
	HandlerMap.Add(TEXT("sequencerMoveSection"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerMoveSection(Body);
	});
	HandlerMap.Add(TEXT("sequencerDuplicateSection"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerDuplicateSection(Body);
	});
	HandlerMap.Add(TEXT("sequencerSetTrackMute"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerSetTrackMute(Body);
	});
	HandlerMap.Add(TEXT("sequencerAddSubSequence"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerAddSubSequence(Body);
	});
	HandlerMap.Add(TEXT("sequencerAddFade"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerAddFade(Body);
	});
	HandlerMap.Add(TEXT("sequencerSetAudioSection"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerSetAudioSection(Body);
	});
	HandlerMap.Add(TEXT("sequencerSetEventPayload"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerSetEventPayload(Body);
	});
	HandlerMap.Add(TEXT("sequencerRenderStatus"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSequencerRenderStatus(Body);
	});

	// ---- Material Graph Mutation Handlers ----
	HandlerMap.Add(TEXT("materialAddNode"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleMaterialAddNode(Body);
	});
	HandlerMap.Add(TEXT("materialDeleteNode"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleMaterialDeleteNode(Body);
	});
	HandlerMap.Add(TEXT("materialConnectPins"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleMaterialConnectPins(Body);
	});
	HandlerMap.Add(TEXT("materialDisconnectPin"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleMaterialDisconnectPin(Body);
	});
	HandlerMap.Add(TEXT("materialSetTextureParam"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleMaterialSetTextureParam(Body);
	});
	HandlerMap.Add(TEXT("materialCreateInstance"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleMaterialCreateInstance(Body);
	});
	HandlerMap.Add(TEXT("materialAssignToActor"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleMaterialAssignToActor(Body);
	});

	// ---- UMG Widget Mutation Handlers ----
	HandlerMap.Add(TEXT("umgCreateWidget"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleUMGCreateWidget(Body);
	});
	HandlerMap.Add(TEXT("umgAddChild"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleUMGAddChild(Body);
	});
	HandlerMap.Add(TEXT("umgRemoveChild"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleUMGRemoveChild(Body);
	});
	HandlerMap.Add(TEXT("umgSetWidgetProperty"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleUMGSetWidgetProperty(Body);
	});
	HandlerMap.Add(TEXT("umgBindEvent"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleUMGBindEvent(Body);
	});
	HandlerMap.Add(TEXT("umgGetWidgetChildren"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleUMGGetWidgetChildren(Body);
	});

	// ---- Animation Blueprint Mutation Handlers ----
	HandlerMap.Add(TEXT("animBPAddState"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimBPAddState(Body);
	});
	HandlerMap.Add(TEXT("animBPRemoveState"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimBPRemoveState(Body);
	});
	HandlerMap.Add(TEXT("animBPAddTransition"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimBPAddTransition(Body);
	});
	HandlerMap.Add(TEXT("animBPSetTransitionRule"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimBPSetTransitionRule(Body);
	});
	HandlerMap.Add(TEXT("animBPSetStateAnimation"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimBPSetStateAnimation(Body);
	});
	HandlerMap.Add(TEXT("animBPGetStateMachine"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimBPGetStateMachine(Body);
	});
	HandlerMap.Add(TEXT("animBPAddBlendNode"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimBPAddBlendNode(Body);
	});

	// ---- AI / Behavior Tree Mutation Handlers ----
	HandlerMap.Add(TEXT("btAddTask"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleBTAddTask(Body);
	});
	HandlerMap.Add(TEXT("btAddComposite"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleBTAddComposite(Body);
	});
	HandlerMap.Add(TEXT("btRemoveNode"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleBTRemoveNode(Body);
	});
	HandlerMap.Add(TEXT("btAddDecorator"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleBTAddDecorator(Body);
	});
	HandlerMap.Add(TEXT("btAddService"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleBTAddService(Body);
	});
	HandlerMap.Add(TEXT("btSetBlackboardValue"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleBTSetBlackboardValue(Body);
	});
	HandlerMap.Add(TEXT("btWireNodes"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleBTWireNodes(Body);
	});
	HandlerMap.Add(TEXT("btGetTree"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleBTGetTree(Body);
	});

	// ---- Component Manipulation Handlers ----
	HandlerMap.Add(TEXT("componentList"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleComponentList(Body);
	});
	HandlerMap.Add(TEXT("componentRemove"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleComponentRemove(Body);
	});
	HandlerMap.Add(TEXT("componentSetProperty"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleComponentSetProperty(Body);
	});
	HandlerMap.Add(TEXT("componentSetTransform"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleComponentSetTransform(Body);
	});
	HandlerMap.Add(TEXT("componentSetVisibility"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleComponentSetVisibility(Body);
	});
	HandlerMap.Add(TEXT("componentSetCollision"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleComponentSetCollision(Body);
	});

	// ---- Landscape / Foliage Handlers ----
	HandlerMap.Add(TEXT("landscapeList"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleLandscapeList(Body);
	});
	HandlerMap.Add(TEXT("landscapeGetInfo"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleLandscapeGetInfo(Body);
	});
	HandlerMap.Add(TEXT("landscapeGetLayers"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleLandscapeGetLayers(Body);
	});
	HandlerMap.Add(TEXT("foliageList"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleFoliageList(Body);
	});
	HandlerMap.Add(TEXT("foliageGetStats"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleFoliageGetStats(Body);
	});

	// ---- Physics Handlers ----
	HandlerMap.Add(TEXT("physicsGetBodyInfo"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePhysicsGetBodyInfo(Body);
	});
	HandlerMap.Add(TEXT("physicsSetSimulate"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePhysicsSetSimulate(Body);
	});
	HandlerMap.Add(TEXT("physicsApplyForce"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePhysicsApplyForce(Body);
	});
	HandlerMap.Add(TEXT("physicsListConstraints"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePhysicsListConstraints(Body);
	});
	HandlerMap.Add(TEXT("physicsGetOverlaps"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePhysicsGetOverlaps(Body);
	});

	// ---- AI / Behavior Tree Handlers ----
	HandlerMap.Add(TEXT("aiListBehaviorTrees"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAIListBehaviorTrees(Body);
	});
	HandlerMap.Add(TEXT("aiGetBehaviorTree"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAIGetBehaviorTree(Body);
	});
	HandlerMap.Add(TEXT("aiListBlackboards"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAIListBlackboards(Body);
	});
	HandlerMap.Add(TEXT("aiGetBlackboard"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAIGetBlackboard(Body);
	});
	HandlerMap.Add(TEXT("aiListControllers"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAIListControllers(Body);
	});
	HandlerMap.Add(TEXT("aiGetEQSQueries"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAIGetEQSQueries(Body);
	});

	// ---- Material Editing Handlers ----
	HandlerMap.Add(TEXT("materialList"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleMaterialList(Body);
	});
	HandlerMap.Add(TEXT("materialGetInfo"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleMaterialGetInfo(Body);
	});
	HandlerMap.Add(TEXT("materialCreate"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleMaterialCreate(Body);
	});
	HandlerMap.Add(TEXT("materialListInstances"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleMaterialListInstances(Body);
	});

	// ---- Asset Import / Management Handlers ----
	HandlerMap.Add(TEXT("assetImport"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAssetImport(Body);
	});
	HandlerMap.Add(TEXT("assetGetInfo"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAssetGetInfo(Body);
	});
	HandlerMap.Add(TEXT("assetDuplicate"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAssetDuplicate(Body);
	});
	HandlerMap.Add(TEXT("assetRename"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAssetRename(Body);
	});
	HandlerMap.Add(TEXT("assetDelete"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAssetDelete(Body);
	});
	HandlerMap.Add(TEXT("assetListByType"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAssetListByType(Body);
	});

	// ---- Editor Settings Handlers ----
	HandlerMap.Add(TEXT("settingsGetProject"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSettingsGetProject(Body);
	});
	HandlerMap.Add(TEXT("settingsGetEditor"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSettingsGetEditor(Body);
	});
	HandlerMap.Add(TEXT("settingsGetRendering"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSettingsGetRendering(Body);
	});
	HandlerMap.Add(TEXT("settingsGetPlugins"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSettingsGetPlugins(Body);
	});

	// ---- UMG / Widget Handlers ----
	HandlerMap.Add(TEXT("umgListWidgets"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleUMGListWidgets(Body);
	});
	HandlerMap.Add(TEXT("umgGetWidgetTree"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleUMGGetWidgetTree(Body);
	});
	HandlerMap.Add(TEXT("umgGetWidgetProperties"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleUMGGetWidgetProperties(Body);
	});
	HandlerMap.Add(TEXT("umgListHUDs"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleUMGListHUDs(Body);
	});

	// ---- Skeletal Mesh Handlers ----
	HandlerMap.Add(TEXT("skelMeshList"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSkelMeshList(Body);
	});
	HandlerMap.Add(TEXT("skelMeshGetInfo"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSkelMeshGetInfo(Body);
	});
	HandlerMap.Add(TEXT("skelMeshGetBones"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSkelMeshGetBones(Body);
	});
	HandlerMap.Add(TEXT("skelMeshGetMorphTargets"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSkelMeshGetMorphTargets(Body);
	});
	HandlerMap.Add(TEXT("skelMeshGetSockets"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSkelMeshGetSockets(Body);
	});

	// ---- Build / Packaging Handlers ----
	HandlerMap.Add(TEXT("buildGetStatus"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleBuildGetStatus(Body);
	});
	HandlerMap.Add(TEXT("buildLighting"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleBuildLighting(Body);
	});
	HandlerMap.Add(TEXT("sourceControlGetStatus"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSourceControlGetStatus(Body);
	});
	HandlerMap.Add(TEXT("sourceControlCheckout"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleSourceControlCheckout(Body);
	});

	// ---- Python Execution with Output Capture ----
	HandlerMap.Add(TEXT("executePythonCapture"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleExecutePythonCapture(Body);
	});

	// ---- AnimBlueprint (auto-generated routes) ----
	HandlerMap.Add(TEXT("animBPGetBlendSpace"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimBPGetBlendSpace(Body);
	});
	HandlerMap.Add(TEXT("animBPListMontages"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleAnimBPListMontages(Body);
	});

	// ---- MaterialGraphEdit (auto-generated routes) ----
	HandlerMap.Add(TEXT("materialGetGraph"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleMaterialGetGraph(Body);
	});
	HandlerMap.Add(TEXT("materialSetScalar"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleMaterialSetScalar(Body);
	});
	HandlerMap.Add(TEXT("materialSetVector"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleMaterialSetVector(Body);
	});

	// ---- PCG (auto-generated routes) ----
	HandlerMap.Add(TEXT("pcgCleanup"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePCGCleanup(Body);
	});
	HandlerMap.Add(TEXT("pcgGenerate"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePCGGenerate(Body);
	});
	HandlerMap.Add(TEXT("pcgGetComponent"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePCGGetComponent(Body);
	});
	HandlerMap.Add(TEXT("pcgSetSeed"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandlePCGSetSeed(Body);
	});

	// ---- Read (auto-generated routes) ----
	HandlerMap.Add(TEXT("findReferences"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleFindReferences(Params);
	});
	HandlerMap.Add(TEXT("getBlueprint"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleGetBlueprint(Params);
	});
	HandlerMap.Add(TEXT("getGraph"), [this](const TMap<FString, FString>& Params, const FString& Body)
<<<<<<< HEAD
=======

	// --- New mutation routes ---
	HandlerMap.Add(TEXT("landscapeSculpt"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLandscapeSculpt(Body); });
	HandlerMap.Add(TEXT("landscapePaint"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLandscapePaint(Body); });
	HandlerMap.Add(TEXT("landscapeAddLayer"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLandscapeAddLayer(Body); });
	HandlerMap.Add(TEXT("landscapeRemoveLayer"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLandscapeRemoveLayer(Body); });
	HandlerMap.Add(TEXT("landscapeImportHeightmap"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLandscapeImportHeightmap(Body); });
	HandlerMap.Add(TEXT("landscapeExportHeightmap"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLandscapeExportHeightmap(Body); });
	HandlerMap.Add(TEXT("foliageAdd"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleFoliageAdd(Body); });
	HandlerMap.Add(TEXT("foliageRemove"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleFoliageRemove(Body); });
	HandlerMap.Add(TEXT("foliageSetDensity"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleFoliageSetDensity(Body); });
	HandlerMap.Add(TEXT("skelMeshSetMorphTarget"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSkelMeshSetMorphTarget(Body); });
	HandlerMap.Add(TEXT("skelMeshAddSocket"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSkelMeshAddSocket(Body); });
	HandlerMap.Add(TEXT("skelMeshRemoveSocket"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSkelMeshRemoveSocket(Body); });
	HandlerMap.Add(TEXT("skelMeshSetMaterial"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSkelMeshSetMaterial(Body); });
	HandlerMap.Add(TEXT("skelMeshSetPhysicsAsset"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSkelMeshSetPhysicsAsset(Body); });
	HandlerMap.Add(TEXT("dataTableAddRow"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleDataTableAddRow(Body); });
	HandlerMap.Add(TEXT("dataTableDeleteRow"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleDataTableDeleteRow(Body); });
	HandlerMap.Add(TEXT("dataTableGetSchema"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleDataTableGetSchema(Body); });
	HandlerMap.Add(TEXT("levelCreate"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLevelCreate(Body); });
	HandlerMap.Add(TEXT("levelSave"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLevelSave(Body); });
	HandlerMap.Add(TEXT("levelAddSublevel"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLevelAddSublevel(Body); });
	HandlerMap.Add(TEXT("levelSetCurrentLevel"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLevelSetCurrentLevel(Body); });
	HandlerMap.Add(TEXT("levelBuildLighting"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLevelBuildLighting(Body); });
	HandlerMap.Add(TEXT("levelBuildNavigation"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLevelBuildNavigation(Body); });
	HandlerMap.Add(TEXT("actorDuplicate"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleActorDuplicate(Body); });
	HandlerMap.Add(TEXT("actorSetMobility"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleActorSetMobility(Body); });
	HandlerMap.Add(TEXT("actorSetTags"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleActorSetTags(Body); });
	HandlerMap.Add(TEXT("actorSetLayer"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleActorSetLayer(Body); });
	HandlerMap.Add(TEXT("physicsAddConstraint"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePhysicsAddConstraint(Body); });
	HandlerMap.Add(TEXT("physicsRemoveConstraint"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePhysicsRemoveConstraint(Body); });
	HandlerMap.Add(TEXT("physicsSetMass"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePhysicsSetMass(Body); });
	HandlerMap.Add(TEXT("physicsSetDamping"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePhysicsSetDamping(Body); });
	HandlerMap.Add(TEXT("physicsSetGravity"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePhysicsSetGravity(Body); });
	HandlerMap.Add(TEXT("physicsApplyImpulse"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePhysicsApplyImpulse(Body); });
	HandlerMap.Add(TEXT("sceneCreateFolder"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSceneCreateFolder(Body); });
	HandlerMap.Add(TEXT("sceneDeleteFolder"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSceneDeleteFolder(Body); });
	HandlerMap.Add(TEXT("sceneSetActorLabel"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSceneSetActorLabel(Body); });
	HandlerMap.Add(TEXT("sceneHideActor"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSceneHideActor(Body); });
	HandlerMap.Add(TEXT("settingsSetProject"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSettingsSetProject(Body); });
	HandlerMap.Add(TEXT("settingsSetEditor"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSettingsSetEditor(Body); });
	HandlerMap.Add(TEXT("settingsSetRendering"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSettingsSetRendering(Body); });
	HandlerMap.Add(TEXT("assetMove"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleAssetMove(Body); });

	// --- Final gap routes ---
	HandlerMap.Add(TEXT("niagaraCreateSystem"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleNiagaraCreateSystem(Body); });
	HandlerMap.Add(TEXT("niagaraAddEmitter"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleNiagaraAddEmitter(Body); });
	HandlerMap.Add(TEXT("niagaraRemoveEmitter"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleNiagaraRemoveEmitter(Body); });
	HandlerMap.Add(TEXT("niagaraSetSystemProperty"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleNiagaraSetSystemProperty(Body); });
	HandlerMap.Add(TEXT("niagaraSpawnSystem"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleNiagaraSpawnSystem(Body); });
	HandlerMap.Add(TEXT("audioCreateSoundCue"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleAudioCreateSoundCue(Body); });
	HandlerMap.Add(TEXT("audioSetAttenuation"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleAudioSetAttenuation(Body); });
	HandlerMap.Add(TEXT("audioCreateAmbientSound"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleAudioCreateAmbientSound(Body); });
	HandlerMap.Add(TEXT("audioCreateAudioVolume"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleAudioCreateAudioVolume(Body); });
	HandlerMap.Add(TEXT("lightCreate"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLightCreate(Body); });
	HandlerMap.Add(TEXT("lightSetProperties"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLightSetProperties(Body); });
	HandlerMap.Add(TEXT("lightList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLightList(Body); });
	HandlerMap.Add(TEXT("bpCreateBlueprint"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleBPCreateBlueprint(Body); });
	HandlerMap.Add(TEXT("bpAddVariable"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleBPAddVariable(Body); });
	HandlerMap.Add(TEXT("bpAddFunction"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleBPAddFunction(Body); });
	HandlerMap.Add(TEXT("bpAddNode"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleBPAddNode(Body); });
	HandlerMap.Add(TEXT("bpConnectPins"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleBPConnectPins(Body); });
	HandlerMap.Add(TEXT("bpCompile"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleBPCompile(Body); });
	HandlerMap.Add(TEXT("bpGetGraph"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleBPGetGraph(Body); });
	HandlerMap.Add(TEXT("bpDeleteNode"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleBPDeleteNode(Body); });
	HandlerMap.Add(TEXT("pcgAddNode"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePCGAddNode(Body); });
	HandlerMap.Add(TEXT("pcgRemoveNode"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePCGRemoveNode(Body); });
	HandlerMap.Add(TEXT("pcgConnectNodes"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePCGConnectNodes(Body); });
	HandlerMap.Add(TEXT("pcgCreateGraph"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePCGCreateGraph(Body); });
	HandlerMap.Add(TEXT("wpGetInfo"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleWPGetInfo(Body); });
	HandlerMap.Add(TEXT("wpSetActorDataLayer"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleWPSetActorDataLayer(Body); });
	HandlerMap.Add(TEXT("wpSetActorRuntimeGrid"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleWPSetActorRuntimeGrid(Body); });
	HandlerMap.Add(TEXT("metahumanList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMetaHumanList(Body); });
	HandlerMap.Add(TEXT("metahumanSpawn"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMetaHumanSpawn(Body); });
	HandlerMap.Add(TEXT("groomList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleGroomList(Body); });
	HandlerMap.Add(TEXT("groomSetBinding"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleGroomSetBinding(Body); });
	HandlerMap.Add(TEXT("pythonExecFile"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePythonExecFile(Body); });
	HandlerMap.Add(TEXT("pythonExecString"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePythonExecString(Body); });


	// --- New subsystem routes ---
	HandlerMap.Add(TEXT("mRGGetStatus"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMRGGetStatus(Body); });
	HandlerMap.Add(TEXT("mRGCreateConfig"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMRGCreateConfig(Body); });
	HandlerMap.Add(TEXT("mRGRender"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMRGRender(Body); });
	HandlerMap.Add(TEXT("mRGListConfigs"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMRGListConfigs(Body); });
	HandlerMap.Add(TEXT("clothList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleClothList(Body); });
	HandlerMap.Add(TEXT("clothCreateAsset"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleClothCreateAsset(Body); });
	HandlerMap.Add(TEXT("clothSetConfig"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleClothSetConfig(Body); });
	HandlerMap.Add(TEXT("gASList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleGASList(Body); });
	HandlerMap.Add(TEXT("gASCreateAbility"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleGASCreateAbility(Body); });
	HandlerMap.Add(TEXT("gASCreateEffect"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleGASCreateEffect(Body); });
	HandlerMap.Add(TEXT("gASAddModifier"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleGASAddModifier(Body); });
	HandlerMap.Add(TEXT("massList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMassList(Body); });
	HandlerMap.Add(TEXT("massCreateConfig"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMassCreateConfig(Body); });
	HandlerMap.Add(TEXT("massAddTrait"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMassAddTrait(Body); });
	HandlerMap.Add(TEXT("interchangeGetStatus"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleInterchangeGetStatus(Body); });
	HandlerMap.Add(TEXT("interchangeImport"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleInterchangeImport(Body); });
	HandlerMap.Add(TEXT("interchangeExport"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleInterchangeExport(Body); });
	HandlerMap.Add(TEXT("vCamList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleVCamList(Body); });
	HandlerMap.Add(TEXT("vCamCreate"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleVCamCreate(Body); });
	HandlerMap.Add(TEXT("vCamAddModifier"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleVCamAddModifier(Body); });
	HandlerMap.Add(TEXT("variantList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleVariantList(Body); });
	HandlerMap.Add(TEXT("variantCreate"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleVariantCreate(Body); });
	HandlerMap.Add(TEXT("variantAddSet"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleVariantAddSet(Body); });
	HandlerMap.Add(TEXT("variantAddVariant"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleVariantAddVariant(Body); });
	HandlerMap.Add(TEXT("composureList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleComposureList(Body); });
	HandlerMap.Add(TEXT("composureCreateElement"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleComposureCreateElement(Body); });
	HandlerMap.Add(TEXT("composureAddPass"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleComposureAddPass(Body); });
	HandlerMap.Add(TEXT("waterList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleWaterList(Body); });
	HandlerMap.Add(TEXT("waterCreate"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleWaterCreate(Body); });
	HandlerMap.Add(TEXT("waterSetProperties"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleWaterSetProperties(Body); });
	HandlerMap.Add(TEXT("sCGetStatus"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSCGetStatus(Body); });
	HandlerMap.Add(TEXT("sCCheckout"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSCCheckout(Body); });
	HandlerMap.Add(TEXT("sCSubmit"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSCSubmit(Body); });
	HandlerMap.Add(TEXT("sCRevert"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSCRevert(Body); });
	HandlerMap.Add(TEXT("sCHistory"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSCHistory(Body); });
	HandlerMap.Add(TEXT("replicationGetSettings"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleReplicationGetSettings(Body); });
	HandlerMap.Add(TEXT("replicationSetSettings"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleReplicationSetSettings(Body); });
	HandlerMap.Add(TEXT("replicationList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleReplicationList(Body); });
	HandlerMap.Add(TEXT("controlRigList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleControlRigList(Body); });
	HandlerMap.Add(TEXT("controlRigCreate"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleControlRigCreate(Body); });
	HandlerMap.Add(TEXT("controlRigGetGraph"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleControlRigGetGraph(Body); });
	HandlerMap.Add(TEXT("controlRigAddControl"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleControlRigAddControl(Body); });
	HandlerMap.Add(TEXT("controlRigAddBone"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleControlRigAddBone(Body); });
	HandlerMap.Add(TEXT("controlRigSetupIK"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleControlRigSetupIK(Body); });
	HandlerMap.Add(TEXT("chaosList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleChaosList(Body); });
	HandlerMap.Add(TEXT("chaosCreateGeometryCollection"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleChaosCreateGeometryCollection(Body); });
	HandlerMap.Add(TEXT("chaosFracture"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleChaosFracture(Body); });
	HandlerMap.Add(TEXT("chaosSpawnField"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleChaosSpawnField(Body); });
	HandlerMap.Add(TEXT("inputListActions"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleInputListActions(Body); });
	HandlerMap.Add(TEXT("inputListMappingContexts"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleInputListMappingContexts(Body); });
	HandlerMap.Add(TEXT("inputCreateAction"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleInputCreateAction(Body); });
	HandlerMap.Add(TEXT("inputCreateMappingContext"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleInputCreateMappingContext(Body); });
	HandlerMap.Add(TEXT("inputAddMapping"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleInputAddMapping(Body); });
	HandlerMap.Add(TEXT("liveLinkGetStatus"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLiveLinkGetStatus(Body); });
	HandlerMap.Add(TEXT("liveLinkListSources"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLiveLinkListSources(Body); });
	HandlerMap.Add(TEXT("liveLinkAddSource"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLiveLinkAddSource(Body); });
	HandlerMap.Add(TEXT("liveLinkRemoveSource"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLiveLinkRemoveSource(Body); });
	HandlerMap.Add(TEXT("mediaList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMediaList(Body); });
	HandlerMap.Add(TEXT("mediaCreatePlayer"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMediaCreatePlayer(Body); });
	HandlerMap.Add(TEXT("mediaCreateSource"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMediaCreateSource(Body); });
	HandlerMap.Add(TEXT("mediaSetSource"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMediaSetSource(Body); });


>>>>>>> dff5884439a2782dee312ccab688904ae4de2c17
	{
		return HandleGetGraph(Params);
	});

	// --- New mutation routes ---
	HandlerMap.Add(TEXT("landscapeSculpt"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLandscapeSculpt(Body); });
	HandlerMap.Add(TEXT("landscapePaint"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLandscapePaint(Body); });
	HandlerMap.Add(TEXT("landscapeAddLayer"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLandscapeAddLayer(Body); });
	HandlerMap.Add(TEXT("landscapeRemoveLayer"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLandscapeRemoveLayer(Body); });
	HandlerMap.Add(TEXT("landscapeImportHeightmap"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLandscapeImportHeightmap(Body); });
	HandlerMap.Add(TEXT("landscapeExportHeightmap"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLandscapeExportHeightmap(Body); });
	HandlerMap.Add(TEXT("foliageAdd"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleFoliageAdd(Body); });
	HandlerMap.Add(TEXT("foliageRemove"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleFoliageRemove(Body); });
	HandlerMap.Add(TEXT("foliageSetDensity"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleFoliageSetDensity(Body); });
	HandlerMap.Add(TEXT("skelMeshSetMorphTarget"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSkelMeshSetMorphTarget(Body); });
	HandlerMap.Add(TEXT("skelMeshAddSocket"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSkelMeshAddSocket(Body); });
	HandlerMap.Add(TEXT("skelMeshRemoveSocket"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSkelMeshRemoveSocket(Body); });
	HandlerMap.Add(TEXT("skelMeshSetMaterial"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSkelMeshSetMaterial(Body); });
	HandlerMap.Add(TEXT("skelMeshSetPhysicsAsset"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSkelMeshSetPhysicsAsset(Body); });
	HandlerMap.Add(TEXT("dataTableAddRow"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleDataTableAddRow(Body); });
	HandlerMap.Add(TEXT("dataTableDeleteRow"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleDataTableDeleteRow(Body); });
	HandlerMap.Add(TEXT("dataTableGetSchema"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleDataTableGetSchema(Body); });
	HandlerMap.Add(TEXT("levelCreate"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLevelCreate(Body); });
	HandlerMap.Add(TEXT("levelSave"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLevelSave(Body); });
	HandlerMap.Add(TEXT("levelAddSublevel"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLevelAddSublevel(Body); });
	HandlerMap.Add(TEXT("levelSetCurrentLevel"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLevelSetCurrentLevel(Body); });
	HandlerMap.Add(TEXT("levelBuildLighting"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLevelBuildLighting(Body); });
	HandlerMap.Add(TEXT("levelBuildNavigation"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLevelBuildNavigation(Body); });
	HandlerMap.Add(TEXT("actorDuplicate"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleActorDuplicate(Body); });
	HandlerMap.Add(TEXT("actorSetMobility"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleActorSetMobility(Body); });
	HandlerMap.Add(TEXT("actorSetTags"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleActorSetTags(Body); });
	HandlerMap.Add(TEXT("actorSetLayer"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleActorSetLayer(Body); });
	HandlerMap.Add(TEXT("physicsAddConstraint"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePhysicsAddConstraint(Body); });
	HandlerMap.Add(TEXT("physicsRemoveConstraint"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePhysicsRemoveConstraint(Body); });
	HandlerMap.Add(TEXT("physicsSetMass"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePhysicsSetMass(Body); });
	HandlerMap.Add(TEXT("physicsSetDamping"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePhysicsSetDamping(Body); });
	HandlerMap.Add(TEXT("physicsSetGravity"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePhysicsSetGravity(Body); });
	HandlerMap.Add(TEXT("physicsApplyImpulse"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePhysicsApplyImpulse(Body); });
	HandlerMap.Add(TEXT("sceneCreateFolder"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSceneCreateFolder(Body); });
	HandlerMap.Add(TEXT("sceneDeleteFolder"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSceneDeleteFolder(Body); });
	HandlerMap.Add(TEXT("sceneSetActorLabel"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSceneSetActorLabel(Body); });
	HandlerMap.Add(TEXT("sceneHideActor"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSceneHideActor(Body); });
	HandlerMap.Add(TEXT("settingsSetProject"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSettingsSetProject(Body); });
	HandlerMap.Add(TEXT("settingsSetEditor"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSettingsSetEditor(Body); });
	HandlerMap.Add(TEXT("settingsSetRendering"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSettingsSetRendering(Body); });
	HandlerMap.Add(TEXT("assetMove"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleAssetMove(Body); });

	// --- Final gap routes ---
	HandlerMap.Add(TEXT("niagaraCreateSystem"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleNiagaraCreateSystem(Body); });
	HandlerMap.Add(TEXT("niagaraAddEmitter"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleNiagaraAddEmitter(Body); });
	HandlerMap.Add(TEXT("niagaraRemoveEmitter"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleNiagaraRemoveEmitter(Body); });
	HandlerMap.Add(TEXT("niagaraSetSystemProperty"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleNiagaraSetSystemProperty(Body); });
	HandlerMap.Add(TEXT("niagaraSpawnSystem"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleNiagaraSpawnSystem(Body); });
	HandlerMap.Add(TEXT("audioCreateSoundCue"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleAudioCreateSoundCue(Body); });
	HandlerMap.Add(TEXT("audioSetAttenuation"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleAudioSetAttenuation(Body); });
	HandlerMap.Add(TEXT("audioCreateAmbientSound"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleAudioCreateAmbientSound(Body); });
	HandlerMap.Add(TEXT("audioCreateAudioVolume"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleAudioCreateAudioVolume(Body); });
	HandlerMap.Add(TEXT("lightCreate"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLightCreate(Body); });
	HandlerMap.Add(TEXT("lightSetProperties"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLightSetProperties(Body); });
	HandlerMap.Add(TEXT("lightList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLightList(Body); });
	HandlerMap.Add(TEXT("bpCreateBlueprint"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleBPCreateBlueprint(Body); });
	HandlerMap.Add(TEXT("bpAddVariable"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleBPAddVariable(Body); });
	HandlerMap.Add(TEXT("bpAddFunction"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleBPAddFunction(Body); });
	HandlerMap.Add(TEXT("bpAddNode"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleBPAddNode(Body); });
	HandlerMap.Add(TEXT("bpConnectPins"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleBPConnectPins(Body); });
	HandlerMap.Add(TEXT("bpCompile"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleBPCompile(Body); });
	HandlerMap.Add(TEXT("bpGetGraph"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleBPGetGraph(Body); });
	HandlerMap.Add(TEXT("bpDeleteNode"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleBPDeleteNode(Body); });
	HandlerMap.Add(TEXT("pcgAddNode"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePCGAddNode(Body); });
	HandlerMap.Add(TEXT("pcgRemoveNode"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePCGRemoveNode(Body); });
	HandlerMap.Add(TEXT("pcgConnectNodes"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePCGConnectNodes(Body); });
	HandlerMap.Add(TEXT("pcgCreateGraph"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePCGCreateGraph(Body); });
	HandlerMap.Add(TEXT("wpGetInfo"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleWPGetInfo(Body); });
	HandlerMap.Add(TEXT("wpSetActorDataLayer"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleWPSetActorDataLayer(Body); });
	HandlerMap.Add(TEXT("wpSetActorRuntimeGrid"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleWPSetActorRuntimeGrid(Body); });
	HandlerMap.Add(TEXT("metahumanList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMetaHumanList(Body); });
	HandlerMap.Add(TEXT("metahumanSpawn"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMetaHumanSpawn(Body); });
	HandlerMap.Add(TEXT("groomList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleGroomList(Body); });
	HandlerMap.Add(TEXT("groomSetBinding"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleGroomSetBinding(Body); });
	HandlerMap.Add(TEXT("pythonExecFile"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePythonExecFile(Body); });
	HandlerMap.Add(TEXT("pythonExecString"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandlePythonExecString(Body); });

	// --- New subsystem routes ---
	HandlerMap.Add(TEXT("mRGGetStatus"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMRGGetStatus(Body); });
	HandlerMap.Add(TEXT("mRGCreateConfig"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMRGCreateConfig(Body); });
	HandlerMap.Add(TEXT("mRGRender"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMRGRender(Body); });
	HandlerMap.Add(TEXT("mRGListConfigs"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMRGListConfigs(Body); });
	HandlerMap.Add(TEXT("clothList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleClothList(Body); });
	HandlerMap.Add(TEXT("clothCreateAsset"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleClothCreateAsset(Body); });
	HandlerMap.Add(TEXT("clothSetConfig"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleClothSetConfig(Body); });
	HandlerMap.Add(TEXT("gASList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleGASList(Body); });
	HandlerMap.Add(TEXT("gASCreateAbility"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleGASCreateAbility(Body); });
	HandlerMap.Add(TEXT("gASCreateEffect"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleGASCreateEffect(Body); });
	HandlerMap.Add(TEXT("gASAddModifier"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleGASAddModifier(Body); });
	HandlerMap.Add(TEXT("massList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMassList(Body); });
	HandlerMap.Add(TEXT("massCreateConfig"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMassCreateConfig(Body); });
	HandlerMap.Add(TEXT("massAddTrait"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMassAddTrait(Body); });
	HandlerMap.Add(TEXT("interchangeGetStatus"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleInterchangeGetStatus(Body); });
	HandlerMap.Add(TEXT("interchangeImport"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleInterchangeImport(Body); });
	HandlerMap.Add(TEXT("interchangeExport"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleInterchangeExport(Body); });
	HandlerMap.Add(TEXT("vCamList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleVCamList(Body); });
	HandlerMap.Add(TEXT("vCamCreate"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleVCamCreate(Body); });
	HandlerMap.Add(TEXT("vCamAddModifier"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleVCamAddModifier(Body); });
	HandlerMap.Add(TEXT("variantList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleVariantList(Body); });
	HandlerMap.Add(TEXT("variantCreate"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleVariantCreate(Body); });
	HandlerMap.Add(TEXT("variantAddSet"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleVariantAddSet(Body); });
	HandlerMap.Add(TEXT("variantAddVariant"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleVariantAddVariant(Body); });
	HandlerMap.Add(TEXT("composureList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleComposureList(Body); });
	HandlerMap.Add(TEXT("composureCreateElement"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleComposureCreateElement(Body); });
	HandlerMap.Add(TEXT("composureAddPass"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleComposureAddPass(Body); });
	HandlerMap.Add(TEXT("waterList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleWaterList(Body); });
	HandlerMap.Add(TEXT("waterCreate"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleWaterCreate(Body); });
	HandlerMap.Add(TEXT("waterSetProperties"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleWaterSetProperties(Body); });
	HandlerMap.Add(TEXT("sCGetStatus"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSCGetStatus(Body); });
	HandlerMap.Add(TEXT("sCCheckout"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSCCheckout(Body); });
	HandlerMap.Add(TEXT("sCSubmit"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSCSubmit(Body); });
	HandlerMap.Add(TEXT("sCRevert"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSCRevert(Body); });
	HandlerMap.Add(TEXT("sCHistory"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleSCHistory(Body); });
	HandlerMap.Add(TEXT("replicationGetSettings"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleReplicationGetSettings(Body); });
	HandlerMap.Add(TEXT("replicationSetSettings"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleReplicationSetSettings(Body); });
	HandlerMap.Add(TEXT("replicationList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleReplicationList(Body); });
	HandlerMap.Add(TEXT("controlRigList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleControlRigList(Body); });
	HandlerMap.Add(TEXT("controlRigCreate"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleControlRigCreate(Body); });
	HandlerMap.Add(TEXT("controlRigGetGraph"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleControlRigGetGraph(Body); });
	HandlerMap.Add(TEXT("controlRigAddControl"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleControlRigAddControl(Body); });
	HandlerMap.Add(TEXT("controlRigAddBone"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleControlRigAddBone(Body); });
	HandlerMap.Add(TEXT("controlRigSetupIK"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleControlRigSetupIK(Body); });
	HandlerMap.Add(TEXT("chaosList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleChaosList(Body); });
	HandlerMap.Add(TEXT("chaosCreateGeometryCollection"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleChaosCreateGeometryCollection(Body); });
	HandlerMap.Add(TEXT("chaosFracture"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleChaosFracture(Body); });
	HandlerMap.Add(TEXT("chaosSpawnField"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleChaosSpawnField(Body); });
	HandlerMap.Add(TEXT("inputListActions"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleInputListActions(Body); });
	HandlerMap.Add(TEXT("inputListMappingContexts"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleInputListMappingContexts(Body); });
	HandlerMap.Add(TEXT("inputCreateAction"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleInputCreateAction(Body); });
	HandlerMap.Add(TEXT("inputCreateMappingContext"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleInputCreateMappingContext(Body); });
	HandlerMap.Add(TEXT("inputAddMapping"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleInputAddMapping(Body); });
	HandlerMap.Add(TEXT("liveLinkGetStatus"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLiveLinkGetStatus(Body); });
	HandlerMap.Add(TEXT("liveLinkListSources"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLiveLinkListSources(Body); });
	HandlerMap.Add(TEXT("liveLinkAddSource"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLiveLinkAddSource(Body); });
	HandlerMap.Add(TEXT("liveLinkRemoveSource"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleLiveLinkRemoveSource(Body); });
	HandlerMap.Add(TEXT("mediaList"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMediaList(Body); });
	HandlerMap.Add(TEXT("mediaCreatePlayer"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMediaCreatePlayer(Body); });
	HandlerMap.Add(TEXT("mediaCreateSource"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMediaCreateSource(Body); });
	HandlerMap.Add(TEXT("mediaSetSource"), [this](const TMap<FString, FString>& Params, const FString& Body) { return HandleMediaSetSource(Body); });
}

// ============================================================
// Authentication
// ============================================================

void FAgenticMCPServer::LoadApiKey()
{
	// Read from environment variable AGENTIC_MCP_API_KEY
	FString EnvKey = FPlatformMisc::GetEnvironmentVariable(TEXT("AGENTIC_MCP_API_KEY"));
	if (!EnvKey.IsEmpty())
	{
		ConfiguredApiKey = EnvKey;
		UE_LOG(LogTemp, Display, TEXT("AgenticMCP: API key loaded from environment (%d chars)"), ConfiguredApiKey.Len());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("AgenticMCP: No API key configured (AGENTIC_MCP_API_KEY not set). Auth disabled — all requests will be accepted."));
	}
}

bool FAgenticMCPServer::TimingSafeCompare(const FString& A, const FString& B)
{
	if (A.Len() != B.Len()) return false;
	uint8 Result = 0;
	for (int32 i = 0; i < A.Len(); ++i)
	{
		Result |= (uint8)((*A)[i] ^ (*B)[i]);
	}
	return Result == 0;
}

bool FAgenticMCPServer::AuthenticateRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// If no API key is configured, allow all requests (dev mode)
	if (ConfiguredApiKey.IsEmpty())
	{
		return true;
	}

	// Check Authorization header: "Bearer <key>"
	FString AuthHeader;
	for (const auto& Header : Request.Headers)
	{
		if (Header.Key.Equals(TEXT("Authorization"), ESearchCase::IgnoreCase))
		{
			if (Header.Value.Num() > 0)
			{
				AuthHeader = Header.Value[0];
			}
			break;
		}
	}

	FString ProvidedKey;
	if (AuthHeader.StartsWith(TEXT("Bearer ")))
	{
		ProvidedKey = AuthHeader.RightChop(7).TrimStartAndEnd();
	}

	// Also check X-API-Key header as fallback
	if (ProvidedKey.IsEmpty())
	{
		for (const auto& Header : Request.Headers)
		{
			if (Header.Key.Equals(TEXT("X-API-Key"), ESearchCase::IgnoreCase))
			{
				if (Header.Value.Num() > 0)
				{
					ProvidedKey = Header.Value[0].TrimStartAndEnd();
				}
				break;
			}
		}
	}

	if (ProvidedKey.IsEmpty() || !TimingSafeCompare(ProvidedKey, ConfiguredApiKey))
	{
		UE_LOG(LogTemp, Warning, TEXT("AgenticMCP: Unauthorized request rejected"));
		TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetStringField(TEXT("error"), TEXT("Unauthorized: invalid or missing API key"));
		TUniquePtr<FHttpServerResponse> R = FHttpServerResponse::Create(
			JsonToString(Err), TEXT("application/json"));
		R->Code = EHttpServerResponseCodes::Denied;
		OnComplete(MoveTemp(R));
		return false;
	}

	return true;
}

// ============================================================
// Start / Stop / ProcessOneRequest
// ============================================================

bool FAgenticMCPServer::Start(int32 InPort, bool bEditorMode)
{
	Port = InPort;
	bIsEditor = bEditorMode;

	// ---- Load API key ----
	LoadApiKey();

	// ---- Scan asset registry ----
	UE_LOG(LogTemp, Display, TEXT("AgenticMCP: Scanning asset registry..."));
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	ARM.Get().SearchAllAssets(true);

	ARM.Get().GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), AllBlueprintAssets, true);
	UE_LOG(LogTemp, Display, TEXT("AgenticMCP: Found %d Blueprint assets."), AllBlueprintAssets.Num());

	ARM.Get().GetAssetsByClass(UWorld::StaticClass()->GetClassPathName(), AllMapAssets, false);
	UE_LOG(LogTemp, Display, TEXT("AgenticMCP: Found %d Map assets."), AllMapAssets.Num());

	// ---- Register handler dispatch map ----
	RegisterHandlers();

	// ---- Start HTTP server ----
	FHttpServerModule& HttpModule = FModuleManager::LoadModuleChecked<FHttpServerModule>("HTTPServer");
	TSharedPtr<IHttpRouter> Router = HttpModule.GetHttpRouter(Port);
	if (!Router.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("AgenticMCP: Failed to create HTTP router on port %d"), Port);
		return false;
	}

	// Lambda factory: creates a queued handler that dispatches work to the game thread
	auto QueuedHandler = [this](const FString& Endpoint)
	{
		return FHttpRequestHandler::CreateLambda(
			[this, Endpoint](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				TSharedPtr<FPendingRequest> Req = MakeShared<FPendingRequest>();
				Req->Endpoint = Endpoint;
				Req->QueryParams = Request.QueryParams;
				// Capture POST body as UTF-8 string
				if (Request.Body.Num() > 0)
				{
					TArray<uint8> NullTerminated(Request.Body);
					NullTerminated.Add(0);
					Req->Body = UTF8_TO_TCHAR((const ANSICHAR*)NullTerminated.GetData());
				}
				Req->OnComplete = OnComplete;
				RequestQueue.Enqueue(Req);
				return true;
			});
	};

	// ---- /api/health -- answered directly on HTTP thread (no asset access needed) ----
	Router->BindRoute(FHttpPath(TEXT("/api/health")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda(
			[this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				if (!AuthenticateRequest(Request, OnComplete)) return true;
				TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
				J->SetStringField(TEXT("status"), TEXT("ok"));
				J->SetStringField(TEXT("server"), TEXT("AgenticMCP"));
				J->SetStringField(TEXT("version"), TEXT("1.0.0"));
				J->SetStringField(TEXT("mode"), bIsEditor ? TEXT("editor") : TEXT("commandlet"));
				J->SetNumberField(TEXT("blueprintCount"), AllBlueprintAssets.Num());
				J->SetNumberField(TEXT("mapCount"), AllMapAssets.Num());
				TUniquePtr<FHttpServerResponse> R = FHttpServerResponse::Create(
					JsonToString(J), TEXT("application/json"));
				OnComplete(MoveTemp(R));
				return true;
			}));

	// ---- /api/capabilities -- lists all registered endpoints ----
	Router->BindRoute(FHttpPath(TEXT("/api/capabilities")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda(
			[this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				if (!AuthenticateRequest(Request, OnComplete)) return true;
				TArray<TSharedPtr<FJsonValue>> EndpointsArray;
				for (const auto& Pair : HandlerMap)
				{
					TSharedRef<FJsonObject> EP = MakeShared<FJsonObject>();
					EP->SetStringField(TEXT("name"), Pair.Key);
					EndpointsArray.Add(MakeShared<FJsonValueObject>(EP));
				}
				TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
				J->SetStringField(TEXT("server"), TEXT("AgenticMCP"));
				J->SetStringField(TEXT("version"), TEXT("1.0.0"));
				J->SetNumberField(TEXT("endpointCount"), EndpointsArray.Num());
				J->SetArrayField(TEXT("endpoints"), EndpointsArray);
				TUniquePtr<FHttpServerResponse> R = FHttpServerResponse::Create(
					JsonToString(J), TEXT("application/json"));
				OnComplete(MoveTemp(R));
				return true;
			}));

	// ---- /api/shutdown -- commandlet only ----
	Router->BindRoute(FHttpPath(TEXT("/api/shutdown")), EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda(
			[this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				if (!AuthenticateRequest(Request, OnComplete)) return true;
				TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
				if (bIsEditor)
				{
					J->SetStringField(TEXT("error"),
						TEXT("Cannot shut down the editor's AgenticMCP server. Close the editor instead."));
				}
				else
				{
					J->SetStringField(TEXT("status"), TEXT("shutting_down"));
					RequestEngineExit(TEXT("AgenticMCP /api/shutdown"));
				}
				TUniquePtr<FHttpServerResponse> R = FHttpServerResponse::Create(
					JsonToString(J), TEXT("application/json"));
				OnComplete(MoveTemp(R));
				return true;
			}));

	// ---- /mcp/status -- MCP bridge health check ----
	Router->BindRoute(FHttpPath(TEXT("/mcp/status")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda(
			[this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				if (!AuthenticateRequest(Request, OnComplete)) return true;
				TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
				J->SetStringField(TEXT("status"), TEXT("connected"));
				J->SetStringField(TEXT("server"), TEXT("AgenticMCP"));
				J->SetStringField(TEXT("version"), TEXT("1.0.0"));
				J->SetStringField(TEXT("mode"), bIsEditor ? TEXT("editor") : TEXT("commandlet"));
				J->SetNumberField(TEXT("blueprintCount"), AllBlueprintAssets.Num());
				J->SetNumberField(TEXT("mapCount"), AllMapAssets.Num());
				TUniquePtr<FHttpServerResponse> R = FHttpServerResponse::Create(
					JsonToString(J), TEXT("application/json"));
				OnComplete(MoveTemp(R));
				return true;
			}));

	// ---- /mcp/tools -- Auto-discovery of all registered tools ----
	Router->BindRoute(FHttpPath(TEXT("/mcp/tools")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda(
			[this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				if (!AuthenticateRequest(Request, OnComplete)) return true;
				TArray<TSharedPtr<FJsonValue>> ToolsArray;
				for (const auto& Pair : HandlerMap)
				{
					TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
					Tool->SetStringField(TEXT("name"), Pair.Key);
					ToolsArray.Add(MakeShared<FJsonValueObject>(Tool));
				}
				TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
				J->SetArrayField(TEXT("tools"), ToolsArray);
				TUniquePtr<FHttpServerResponse> R = FHttpServerResponse::Create(
					JsonToString(J), TEXT("application/json"));
				OnComplete(MoveTemp(R));
				return true;
			}));

	// ---- /mcp/tool/<name> -- Execute a tool by name (queued to game thread) ----
	Router->BindRoute(FHttpPath(TEXT("/mcp/tool")), EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda(
			[this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				if (!AuthenticateRequest(Request, OnComplete)) return true;
				// Extract tool name from the relative path
				// The request path after /mcp/tool/ contains the tool name
				FString RelPath = Request.RelativePath.GetPath();
				FString ToolName;
				if (RelPath.StartsWith(TEXT("/")))
				{
					ToolName = RelPath.RightChop(1);
				}
				else
				{
					ToolName = RelPath;
				}

				if (ToolName.IsEmpty())
				{
					TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
					Err->SetStringField(TEXT("error"), TEXT("Missing tool name in URL"));
					TUniquePtr<FHttpServerResponse> R = FHttpServerResponse::Create(
						JsonToString(Err), TEXT("application/json"));
					OnComplete(MoveTemp(R));
					return true;
				}

				// Queue to game thread like all other handlers
					TSharedPtr<FPendingRequest> Req = MakeShared<FPendingRequest>();
					Req->Endpoint = ToolName;
					Req->QueryParams = Request.QueryParams;
					if (Request.Body.Num() > 0)
					{
						TArray<uint8> NullTerminated(Request.Body);
						NullTerminated.Add(0);
						Req->Body = UTF8_TO_TCHAR((const ANSICHAR*)NullTerminated.GetData());

						// Merge JSON body fields into QueryParams so handlers that
						// read from Params (GET-style) also work via POST /mcp/tool.
						// Only string/number values are merged; complex objects stay in Body.
						TSharedPtr<FJsonObject> BodyJson;
						TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Req->Body);
						if (FJsonSerializer::Deserialize(Reader, BodyJson) && BodyJson.IsValid())
						{
							for (const auto& Field : BodyJson->Values)
							{
								if (!Req->QueryParams.Contains(Field.Key))
								{
									FString Val;
									if (Field.Value->TryGetString(Val))
									{
										Req->QueryParams.Add(Field.Key, Val);
									}
									else
									{
										double NumVal;
										if (Field.Value->TryGetNumber(NumVal))
										{
											Req->QueryParams.Add(Field.Key, FString::SanitizeFloat(NumVal));
										}
										else
										{
											bool BoolVal;
											if (Field.Value->TryGetBool(BoolVal))
											{
												Req->QueryParams.Add(Field.Key, BoolVal ? TEXT("true") : TEXT("false"));
											}
										}
									}
								}
							}
						}
					}
					Req->OnComplete = OnComplete;
					RequestQueue.Enqueue(Req);
					return true;
			}));

	// ---- Bind all queued routes ----
	// Blueprint Read (GET)
	Router->BindRoute(FHttpPath(TEXT("/api/list")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("list")));
	Router->BindRoute(FHttpPath(TEXT("/api/blueprint")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("blueprint")));
	Router->BindRoute(FHttpPath(TEXT("/api/graph")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("graph")));
	Router->BindRoute(FHttpPath(TEXT("/api/search")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("search")));
	Router->BindRoute(FHttpPath(TEXT("/api/references")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("references")));

	// Blueprint Read (POST -- need body for complex queries)
	Router->BindRoute(FHttpPath(TEXT("/api/list-classes")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("listClasses")));
	Router->BindRoute(FHttpPath(TEXT("/api/list-functions")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("listFunctions")));
	Router->BindRoute(FHttpPath(TEXT("/api/list-properties")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("listProperties")));
	Router->BindRoute(FHttpPath(TEXT("/api/get-pin-info")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("getPinInfo")));

	// Blueprint Mutation (POST)
	Router->BindRoute(FHttpPath(TEXT("/api/add-node")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("addNode")));
	Router->BindRoute(FHttpPath(TEXT("/api/delete-node")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("deleteNode")));
	Router->BindRoute(FHttpPath(TEXT("/api/connect-pins")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("connectPins")));
	Router->BindRoute(FHttpPath(TEXT("/api/disconnect-pin")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("disconnectPin")));
	Router->BindRoute(FHttpPath(TEXT("/api/set-pin-default")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("setPinDefault")));
	Router->BindRoute(FHttpPath(TEXT("/api/move-node")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("moveNode")));
	Router->BindRoute(FHttpPath(TEXT("/api/refresh-all-nodes")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("refreshAllNodes")));
	Router->BindRoute(FHttpPath(TEXT("/api/create-blueprint")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("createBlueprint")));
	Router->BindRoute(FHttpPath(TEXT("/api/create-graph")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("createGraph")));
	Router->BindRoute(FHttpPath(TEXT("/api/delete-graph")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("deleteGraph")));
	Router->BindRoute(FHttpPath(TEXT("/api/add-variable")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("addVariable")));
	Router->BindRoute(FHttpPath(TEXT("/api/remove-variable")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("removeVariable")));
	Router->BindRoute(FHttpPath(TEXT("/api/compile-blueprint")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("compileBlueprint")));
	Router->BindRoute(FHttpPath(TEXT("/api/duplicate-nodes")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("duplicateNodes")));
	Router->BindRoute(FHttpPath(TEXT("/api/set-node-comment")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("setNodeComment")));

	// Actor Management (POST)
	Router->BindRoute(FHttpPath(TEXT("/api/list-actors")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("listActors")));
	Router->BindRoute(FHttpPath(TEXT("/api/get-actor")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("getActor")));
	Router->BindRoute(FHttpPath(TEXT("/api/spawn-actor")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("spawnActor")));
	Router->BindRoute(FHttpPath(TEXT("/api/delete-actor")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("deleteActor")));
	Router->BindRoute(FHttpPath(TEXT("/api/set-actor-property")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("setActorProperty")));
	Router->BindRoute(FHttpPath(TEXT("/api/set-actor-transform")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("setActorTransform")));

	// Level Management (POST)
	Router->BindRoute(FHttpPath(TEXT("/api/list-levels")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("listLevels")));
	Router->BindRoute(FHttpPath(TEXT("/api/load-level")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("loadLevel")));
	Router->BindRoute(FHttpPath(TEXT("/api/get-level-blueprint")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("getLevelBlueprint")));
	Router->BindRoute(FHttpPath(TEXT("/api/remove-sublevel")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("removeSublevel")));
	Router->BindRoute(FHttpPath(TEXT("/api/open-asset")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("openAsset")));

	// Validation and Safety (POST)
	Router->BindRoute(FHttpPath(TEXT("/api/validate-blueprint")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("validateBlueprint")));
	Router->BindRoute(FHttpPath(TEXT("/api/snapshot-graph")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("snapshotGraph")));
	Router->BindRoute(FHttpPath(TEXT("/api/restore-graph")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("restoreGraph")));

	// Visual Agent / Automation (POST)
	Router->BindRoute(FHttpPath(TEXT("/api/scene-snapshot")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("sceneSnapshot")));
	Router->BindRoute(FHttpPath(TEXT("/api/screenshot")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("screenshot")));
	Router->BindRoute(FHttpPath(TEXT("/api/focus-actor")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("focusActor")));
	Router->BindRoute(FHttpPath(TEXT("/api/select-actor")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("selectActor")));
	Router->BindRoute(FHttpPath(TEXT("/api/set-viewport")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("setViewport")));
	Router->BindRoute(FHttpPath(TEXT("/api/wait-ready")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("waitReady")));
	Router->BindRoute(FHttpPath(TEXT("/api/resolve-ref")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("resolveRef")));
	Router->BindRoute(FHttpPath(TEXT("/api/move-actor")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("moveActor")));
	Router->BindRoute(FHttpPath(TEXT("/api/get-camera")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("getCamera")));
	Router->BindRoute(FHttpPath(TEXT("/api/list-viewports")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("listViewports")));
	Router->BindRoute(FHttpPath(TEXT("/api/get-selection")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("getSelection")));

	// Level Sequences (POST)
	Router->BindRoute(FHttpPath(TEXT("/api/list-sequences")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("listSequences")));
	Router->BindRoute(FHttpPath(TEXT("/api/read-sequence")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("readSequence")));
	Router->BindRoute(FHttpPath(TEXT("/api/remove-audio-tracks")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("removeAudioTracks")));

	// Debug Drawing (POST)
	Router->BindRoute(FHttpPath(TEXT("/api/draw-debug")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("drawDebug")));
	Router->BindRoute(FHttpPath(TEXT("/api/clear-debug")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("clearDebug")));

	// Blueprint Snapshot (POST)
	Router->BindRoute(FHttpPath(TEXT("/api/blueprint-snapshot")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("blueprintSnapshot")));

	// Transaction/Undo (POST)
	Router->BindRoute(FHttpPath(TEXT("/api/begin-transaction")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("beginTransaction")));
	Router->BindRoute(FHttpPath(TEXT("/api/end-transaction")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("endTransaction")));
	Router->BindRoute(FHttpPath(TEXT("/api/undo")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("undo")));
	Router->BindRoute(FHttpPath(TEXT("/api/redo")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("redo")));

	// State Management (POST)
	Router->BindRoute(FHttpPath(TEXT("/api/save-state")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("saveState")));
	Router->BindRoute(FHttpPath(TEXT("/api/diff-state")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("diffState")));
	Router->BindRoute(FHttpPath(TEXT("/api/restore-state")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("restoreState")));
	Router->BindRoute(FHttpPath(TEXT("/api/list-states")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("listStates")));

	// Python Execution (POST)
	Router->BindRoute(FHttpPath(TEXT("/api/execute-python")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("executePython")));

	// Add Component to Blueprint (POST)
	Router->BindRoute(FHttpPath(TEXT("/api/add-component")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("addComponent")));

	// PIE Control (POST)
	Router->BindRoute(FHttpPath(TEXT("/api/start-pie")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("startPIE")));
	Router->BindRoute(FHttpPath(TEXT("/api/stop-pie")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("stopPIE")));
	Router->BindRoute(FHttpPath(TEXT("/api/pause-pie")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("pausePIE")));
	Router->BindRoute(FHttpPath(TEXT("/api/step-pie")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("stepPIE")));
	Router->BindRoute(FHttpPath(TEXT("/api/get-pie-state")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("getPIEState")));

	// Console Commands (POST)
	Router->BindRoute(FHttpPath(TEXT("/api/execute-console")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("executeConsole")));
	Router->BindRoute(FHttpPath(TEXT("/api/get-cvar")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("getCVar")));
	Router->BindRoute(FHttpPath(TEXT("/api/set-cvar")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("setCVar")));
	Router->BindRoute(FHttpPath(TEXT("/api/list-cvars")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("listCVars")));

	// Input Simulation (POST)
	Router->BindRoute(FHttpPath(TEXT("/api/simulate-input")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("simulateInput")));

	// Audio Endpoints
	Router->BindRoute(FHttpPath(TEXT("/api/audio/status")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("audioGetStatus")));
	Router->BindRoute(FHttpPath(TEXT("/api/audio/active-sounds")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("audioListActiveSounds")));
	Router->BindRoute(FHttpPath(TEXT("/api/audio/device-info")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("audioGetDeviceInfo")));
	Router->BindRoute(FHttpPath(TEXT("/api/audio/sound-classes")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("audioListSoundClasses")));
	Router->BindRoute(FHttpPath(TEXT("/api/audio/set-volume")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("audioSetVolume")));
	Router->BindRoute(FHttpPath(TEXT("/api/audio/stats")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("audioGetStats")));
	Router->BindRoute(FHttpPath(TEXT("/api/audio/play")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("audioPlaySound")));
	Router->BindRoute(FHttpPath(TEXT("/api/audio/stop")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("audioStopSound")));
	Router->BindRoute(FHttpPath(TEXT("/api/audio/set-listener")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("audioSetListener")));
	Router->BindRoute(FHttpPath(TEXT("/api/audio/debug-visualize")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("audioDebugVisualize")));

	// Niagara Endpoints
	Router->BindRoute(FHttpPath(TEXT("/api/niagara/status")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("niagaraGetStatus")));
	Router->BindRoute(FHttpPath(TEXT("/api/niagara/systems")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("niagaraListSystems")));
	Router->BindRoute(FHttpPath(TEXT("/api/niagara/system-info")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("niagaraGetSystemInfo")));
	Router->BindRoute(FHttpPath(TEXT("/api/niagara/emitters")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("niagaraGetEmitters")));
	Router->BindRoute(FHttpPath(TEXT("/api/niagara/set-parameter")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("niagaraSetParameter")));
	Router->BindRoute(FHttpPath(TEXT("/api/niagara/parameters")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("niagaraGetParameters")));
	Router->BindRoute(FHttpPath(TEXT("/api/niagara/activate")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("niagaraActivateSystem")));
	Router->BindRoute(FHttpPath(TEXT("/api/niagara/set-emitter")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("niagaraSetEmitterEnable")));
	Router->BindRoute(FHttpPath(TEXT("/api/niagara/reset")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("niagaraResetSystem")));
	Router->BindRoute(FHttpPath(TEXT("/api/niagara/stats")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("niagaraGetStats")));
	Router->BindRoute(FHttpPath(TEXT("/api/niagara/debug-hud")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("niagaraDebugHUD")));

	// PixelStreaming Endpoints
	Router->BindRoute(FHttpPath(TEXT("/api/pixelstreaming/status")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("pixelStreamingGetStatus")));
	Router->BindRoute(FHttpPath(TEXT("/api/pixelstreaming/start")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("pixelStreamingStart")));
	Router->BindRoute(FHttpPath(TEXT("/api/pixelstreaming/stop")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("pixelStreamingStop")));
	Router->BindRoute(FHttpPath(TEXT("/api/pixelstreaming/streamers")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("pixelStreamingListStreamers")));
	Router->BindRoute(FHttpPath(TEXT("/api/pixelstreaming/codec")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("pixelStreamingGetCodec")));
	Router->BindRoute(FHttpPath(TEXT("/api/pixelstreaming/set-codec")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("pixelStreamingSetCodec")));
	Router->BindRoute(FHttpPath(TEXT("/api/pixelstreaming/players")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("pixelStreamingListPlayers")));

	// Meta XR / OculusXR Endpoints
	Router->BindRoute(FHttpPath(TEXT("/api/xr/status")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("xrStatus")));
	Router->BindRoute(FHttpPath(TEXT("/api/xr/guardian")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("xrGuardian")));
	Router->BindRoute(FHttpPath(TEXT("/api/xr/guardian/visibility")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("xrSetGuardianVisibility")));
	Router->BindRoute(FHttpPath(TEXT("/api/xr/hand-tracking")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("xrHandTracking")));
	Router->BindRoute(FHttpPath(TEXT("/api/xr/controllers")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("xrControllers")));
	Router->BindRoute(FHttpPath(TEXT("/api/xr/passthrough")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("xrPassthrough")));
	Router->BindRoute(FHttpPath(TEXT("/api/xr/passthrough/set")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("xrSetPassthrough")));
	Router->BindRoute(FHttpPath(TEXT("/api/xr/display-frequency")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("xrSetDisplayFrequency")));
	Router->BindRoute(FHttpPath(TEXT("/api/xr/performance")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("xrSetPerformanceLevels")));
	Router->BindRoute(FHttpPath(TEXT("/api/xr/recenter")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("xrRecenter")));

	// MetaXR Audio/Haptics Endpoints
	Router->BindRoute(FHttpPath(TEXT("/api/xr/haptics/play")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("xrPlayHapticEffect")));
	Router->BindRoute(FHttpPath(TEXT("/api/xr/haptics/stop")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("xrStopHapticEffect")));
	Router->BindRoute(FHttpPath(TEXT("/api/xr/haptics/capabilities")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("xrGetHapticCapabilities")));
	Router->BindRoute(FHttpPath(TEXT("/api/xr/audio/spatial/set")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("xrSetSpatialAudioEnabled")));
	Router->BindRoute(FHttpPath(TEXT("/api/xr/audio/spatial/status")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("xrGetSpatialAudioStatus")));
	Router->BindRoute(FHttpPath(TEXT("/api/xr/audio/attenuation")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("xrConfigureAudioAttenuation")));

	// Story/Game Endpoints
	Router->BindRoute(FHttpPath(TEXT("/api/story/state")), EHttpServerRequestVerbs::VERB_GET,
		QueuedHandler(TEXT("storyState")));
	Router->BindRoute(FHttpPath(TEXT("/api/story/advance")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("storyAdvance")));
	Router->BindRoute(FHttpPath(TEXT("/api/story/goto")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("storyGoto")));
	Router->BindRoute(FHttpPath(TEXT("/api/story/play")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("storyPlay")));

	// DataTable Endpoints
	Router->BindRoute(FHttpPath(TEXT("/api/datatable/read")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("dataTableRead")));
	Router->BindRoute(FHttpPath(TEXT("/api/datatable/write")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("dataTableWrite")));

	// Animation Endpoints
	Router->BindRoute(FHttpPath(TEXT("/api/animation/play")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("animationPlay")));
	Router->BindRoute(FHttpPath(TEXT("/api/animation/stop")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("animationStop")));

	// Material Endpoints
	Router->BindRoute(FHttpPath(TEXT("/api/material/set-param")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("materialSetParam")));

	// Collision Endpoints
	Router->BindRoute(FHttpPath(TEXT("/api/collision/trace")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("collisionTrace")));

	// RenderDoc Endpoints
	Router->BindRoute(FHttpPath(TEXT("/api/renderdoc/capture")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("renderDocCapture")));

	// Level Visibility & Log Endpoints
	Router->BindRoute(FHttpPath(TEXT("/api/streaming-level-visibility")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("streamingLevelVisibility")));
	Router->BindRoute(FHttpPath(TEXT("/api/output-log")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("outputLog")));

	// ---- Start listening ----
	HttpModule.StartAllListeners();
	bRunning = true;

	UE_LOG(LogTemp, Display, TEXT("AgenticMCP: HTTP server started on port %d (%s mode)"),
		Port, bIsEditor ? TEXT("editor") : TEXT("commandlet"));

	return true;
}

void FAgenticMCPServer::Stop()
{
	if (bRunning)
	{
		FHttpServerModule& HttpModule = FModuleManager::LoadModuleChecked<FHttpServerModule>("HTTPServer");
		HttpModule.StopAllListeners();
		bRunning = false;
		UE_LOG(LogTemp, Display, TEXT("AgenticMCP: HTTP server stopped."));
	}
}

bool FAgenticMCPServer::ProcessOneRequest()
{
	TSharedPtr<FPendingRequest> Req;
	if (!RequestQueue.Dequeue(Req) || !Req.IsValid())
	{
		return false;
	}

	// FGCScopeGuard prevents garbage collection from running while we hold
	// raw UObject pointers (UBlueprint*, UWorld*, AActor*) during handler
	// dispatch. Without this, a GC pass triggered by blueprint compilation
	// or level loading can invalidate the UWorld that owns a level blueprint,
	// causing a null pointer crash in MarkBlueprintAsStructurallyModified.
	FGCScopeGuard GCGuard;

	// Dispatch to the registered handler with crash protection.
	// If a handler throws or crashes, we still send an error response
	// so the HTTP connection doesn't hang indefinitely.
	FString ResponseBody;

	FRequestHandler* Handler = HandlerMap.Find(Req->Endpoint);
	if (Handler)
	{
		try
		{
			ResponseBody = (*Handler)(Req->QueryParams, Req->Body);
		}
		catch (...)
		{
			UE_LOG(LogTemp, Error, TEXT("AgenticMCP: Exception in handler '%s'"), *Req->Endpoint);
			ResponseBody = MakeErrorJson(
				FString::Printf(TEXT("Handler '%s' crashed with an unhandled exception"), *Req->Endpoint));
		}
	}
	else
	{
		ResponseBody = MakeErrorJson(
			FString::Printf(TEXT("Unknown endpoint: %s"), *Req->Endpoint));
	}

	// Send HTTP response
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(
		ResponseBody, TEXT("application/json"));
	Req->OnComplete(MoveTemp(Response));

	return true;
}

// ============================================================
// HandleExecutePython
// POST /api/execute-python { "script": "...", "file": "..." }
// Executes Python code or a Python file in the editor.
// ============================================================

FString FAgenticMCPServer::HandleExecutePython(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Script = Json->GetStringField(TEXT("script"));
	FString FilePath = Json->GetStringField(TEXT("file"));

	if (Script.IsEmpty() && FilePath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: 'script' (inline code) or 'file' (path to .py file)"));
	}

	FString Command;
	if (!FilePath.IsEmpty())
	{
		// Execute a Python file
		Command = FString::Printf(TEXT("py \"%s\""), *FilePath);
	}
	else
	{
		// Execute inline Python script - escape quotes and newlines for console
		FString EscapedScript = Script.Replace(TEXT("\""), TEXT("\\\""));
		Command = FString::Printf(TEXT("py -c \"%s\""), *EscapedScript);
	}

	// Execute via editor console command
	if (GEditor)
	{
		GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Command);
	}
	else
	{
		return MakeErrorJson(TEXT("GEditor not available"));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("executed"), !FilePath.IsEmpty() ? FilePath : TEXT("inline script"));
	Result->SetStringField(TEXT("note"), TEXT("Check Output Log for script results"));

	return JsonToString(Result);
}

// ============================================================
// Safe Blueprint Modification Wrappers
// ============================================================
//
// All MCP mutation handlers MUST use these instead of calling
// FBlueprintEditorUtils::MarkBlueprintAs*Modified() directly.
//
// The problem: When called from the editor Tick() context (which is
// how ALL MCP requests are processed), the FSlowTask system can crash
// with EXCEPTION_ACCESS_VIOLATION in FText::Rebuild() because there's
// no valid parent FText scope for progress dialogs.
//
// The fix: Wrap in an editor transaction (provides valid FText scope)
// and SEH handler (catches any remaining crashes from corrupt BPs).
// ============================================================

#include "Misc/ScopedSlowTask.h"

bool FAgenticMCPServer::SafeMarkStructurallyModified(UBlueprint* BP, const TCHAR* TransactionDesc)
{
	if (!BP || !GEditor)
	{
		return false;
	}

	// Level blueprint safety check: validate the entire ownership chain
	// before attempting compilation. Level blueprints are owned by ULevel
	// which is owned by UWorld. If either is pending kill or null, the
	// compile will crash with a null pointer dereference.
	ULevelScriptBlueprint* LevelBP = Cast<ULevelScriptBlueprint>(BP);
	if (LevelBP)
	{
		ULevel* OwningLevel = LevelBP->GetLevel();
		if (!OwningLevel || !IsValid(OwningLevel))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("AgenticMCP: Level blueprint '%s' has invalid owning level - "
					 "marking dirty without recompile."),
				*BP->GetName());
			BP->MarkPackageDirty();
			return false;
		}

		UWorld* OwningWorld = OwningLevel->GetWorld();
		if (!OwningWorld || !IsValid(OwningWorld))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("AgenticMCP: Level blueprint '%s' has invalid owning world - "
					 "marking dirty without recompile."),
				*BP->GetName());
			BP->MarkPackageDirty();
			return false;
		}
	}

	bool bSuccess = true;

	// Create a slow task scope to prevent FText::Rebuild crashes
	FScopedSlowTask SlowTask(1.0f,
		FText::FromString(FString::Printf(TEXT("AgenticMCP: %s"), TransactionDesc)));

	// Wrap in editor transaction for valid FText scope + undo support
	GEditor->BeginTransaction(
		FText::FromString(FString::Printf(TEXT("AgenticMCP: %s"), TransactionDesc)));

#if PLATFORM_WINDOWS
	// Use SEH wrapper on Windows to catch access violations that C++ try/catch misses.
	// MarkBlueprintAsStructurallyModified triggers a full recompile which can hit
	// null pointers deep in the engine (FText::Rebuild, FLinkerLoad, etc.).
	int32 SEHResult = TryMarkStructurallyModifiedSEH(BP);
	if (SEHResult != 0)
	{
		bSuccess = false;
		UE_LOG(LogTemp, Warning,
			TEXT("AgenticMCP: MarkBlueprintAsStructurallyModified crashed (SEH) during '%s' on '%s' - "
				 "node was created but compile deferred."),
			TransactionDesc, *BP->GetName());
	}
#else
	try
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	}
	catch (...)
	{
		bSuccess = false;
		UE_LOG(LogTemp, Warning,
			TEXT("AgenticMCP: Blueprint compile crashed during '%s' on '%s' - "
				 "node was created but compile deferred. Blueprint may need manual recompile."),
			TransactionDesc, *BP->GetName());
	}
#endif

	GEditor->EndTransaction();

	if (!bSuccess)
	{
		// Even if compile crashed, mark as dirty so the editor knows
		// the BP needs attention
		BP->MarkPackageDirty();
	}

	return bSuccess;
}

bool FAgenticMCPServer::SafeMarkModified(UBlueprint* BP, const TCHAR* TransactionDesc)
{
	if (!BP || !GEditor)
	{
		return false;
	}

	bool bSuccess = true;

	FScopedSlowTask SlowTask(1.0f,
		FText::FromString(FString::Printf(TEXT("AgenticMCP: %s"), TransactionDesc)));

	GEditor->BeginTransaction(
		FText::FromString(FString::Printf(TEXT("AgenticMCP: %s"), TransactionDesc)));

#if PLATFORM_WINDOWS
	int32 SEHResult = TryMarkModifiedSEH(BP);
	if (SEHResult != 0)
	{
		bSuccess = false;
		UE_LOG(LogTemp, Warning,
			TEXT("AgenticMCP: MarkBlueprintAsModified crashed (SEH) during '%s' on '%s'"),
			TransactionDesc, *BP->GetName());
	}
#else
	try
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	}
	catch (...)
	{
		bSuccess = false;
		UE_LOG(LogTemp, Warning,
			TEXT("AgenticMCP: Blueprint modification crashed during '%s' on '%s'"),
			TransactionDesc, *BP->GetName());
	}
#endif

	GEditor->EndTransaction();

	return bSuccess;
}
