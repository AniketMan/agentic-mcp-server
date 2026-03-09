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
		// Continue to save anyway — the Blueprint may still be in a usable state
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
		// New package — derive filename from package name
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

	// ---- Level Management ----
	HandlerMap.Add(TEXT("listLevels"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleListLevels(Body);
	});
	HandlerMap.Add(TEXT("loadLevel"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleLoadLevel(Body);
	});
	HandlerMap.Add(TEXT("getLevelBlueprint"), [this](const TMap<FString, FString>& Params, const FString& Body)
	{
		return HandleGetLevelBlueprint(Body);
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
}

// ============================================================
// Start / Stop / ProcessOneRequest
// ============================================================

bool FAgenticMCPServer::Start(int32 InPort, bool bEditorMode)
{
	Port = InPort;
	bIsEditor = bEditorMode;

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

	// ---- /api/health — answered directly on HTTP thread (no asset access needed) ----
	Router->BindRoute(FHttpPath(TEXT("/api/health")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda(
			[this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
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

	// ---- /api/shutdown — commandlet only ----
	Router->BindRoute(FHttpPath(TEXT("/api/shutdown")), EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda(
			[this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
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

	// Blueprint Read (POST — need body for complex queries)
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

	// Validation and Safety (POST)
	Router->BindRoute(FHttpPath(TEXT("/api/validate-blueprint")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("validateBlueprint")));
	Router->BindRoute(FHttpPath(TEXT("/api/snapshot-graph")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("snapshotGraph")));
	Router->BindRoute(FHttpPath(TEXT("/api/restore-graph")), EHttpServerRequestVerbs::VERB_POST,
		QueuedHandler(TEXT("restoreGraph")));

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

	// Dispatch to the registered handler
	FString ResponseBody;

	FRequestHandler* Handler = HandlerMap.Find(Req->Endpoint);
	if (Handler)
	{
		ResponseBody = (*Handler)(Req->QueryParams, Req->Body);
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
