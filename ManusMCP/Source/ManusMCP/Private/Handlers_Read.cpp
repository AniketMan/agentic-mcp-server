// Handlers_Read.cpp
// Blueprint read-only handlers for ManusMCP.
// These handlers inspect Blueprints, graphs, nodes, and pins without modifying them.
//
// Endpoints implemented:
//   /api/list             - List all Blueprint and Map assets
//   /api/blueprint        - Get Blueprint details (graphs, variables, parent class)
//   /api/graph            - Get graph nodes and connections
//   /api/search           - Search Blueprints by name pattern
//   /api/references       - Find references to an asset
//   /api/list-classes     - List UClasses matching a filter
//   /api/list-functions   - List UFunctions on a class
//   /api/list-properties  - List UProperties on a class
//   /api/get-pin-info     - Introspect pin types and connections

#include "ManusMCPServer.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Engine/LevelScriptBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"

// ============================================================
// HandleList - List all Blueprint and Map assets
// GET /api/list?filter=<optional_pattern>&type=<blueprint|map|all>
// ============================================================

FString FManusMCPServer::HandleList(const TMap<FString, FString>& Params)
{
	FString Filter = Params.Contains(TEXT("filter")) ? Params[TEXT("filter")] : TEXT("");
	FString Type = Params.Contains(TEXT("type")) ? Params[TEXT("type")] : TEXT("all");

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	// Blueprints
	if (Type == TEXT("all") || Type == TEXT("blueprint"))
	{
		TArray<TSharedPtr<FJsonValue>> BPArray;
		for (const FAssetData& Asset : AllBlueprintAssets)
		{
			FString Name = Asset.AssetName.ToString();
			if (!Filter.IsEmpty() && !Name.Contains(Filter))
			{
				continue;
			}
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Name);
			Entry->SetStringField(TEXT("path"), Asset.PackageName.ToString());
			BPArray.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Result->SetArrayField(TEXT("blueprints"), BPArray);
	}

	// Maps (level blueprints)
	if (Type == TEXT("all") || Type == TEXT("map"))
	{
		TArray<TSharedPtr<FJsonValue>> MapArray;
		for (const FAssetData& Asset : AllMapAssets)
		{
			FString Name = Asset.AssetName.ToString();
			if (!Filter.IsEmpty() && !Name.Contains(Filter))
			{
				continue;
			}
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Name);
			Entry->SetStringField(TEXT("path"), Asset.PackageName.ToString());
			MapArray.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Result->SetArrayField(TEXT("maps"), MapArray);
	}

	return JsonToString(Result);
}

// ============================================================
// HandleGetBlueprint - Get Blueprint details
// GET /api/blueprint?name=<name_or_path>
// ============================================================

FString FManusMCPServer::HandleGetBlueprint(const TMap<FString, FString>& Params)
{
	FString Name = Params.Contains(TEXT("name")) ? Params[TEXT("name")] : TEXT("");
	if (Name.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required parameter: name"));
	}

	// URL-decode the name
	Name = UrlDecode(Name);

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(Name, LoadError);
	if (!BP)
	{
		return MakeErrorJson(LoadError);
	}

	TSharedRef<FJsonObject> Result = SerializeBlueprint(BP);
	return JsonToString(Result);
}

// ============================================================
// HandleGetGraph - Get graph nodes and connections
// GET /api/graph?blueprint=<name>&graph=<graph_name>
// ============================================================

FString FManusMCPServer::HandleGetGraph(const TMap<FString, FString>& Params)
{
	FString BlueprintName = Params.Contains(TEXT("blueprint")) ? Params[TEXT("blueprint")] : TEXT("");
	FString GraphName = Params.Contains(TEXT("graph")) ? Params[TEXT("graph")] : TEXT("");

	if (BlueprintName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required parameter: blueprint"));
	}

	BlueprintName = UrlDecode(BlueprintName);
	GraphName = UrlDecode(GraphName);

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP)
	{
		return MakeErrorJson(LoadError);
	}

	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);

	// If no graph name specified, return all graphs
	if (GraphName.IsEmpty())
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("blueprint"), BlueprintName);

		TArray<TSharedPtr<FJsonValue>> GraphArray;
		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph)
			{
				TSharedPtr<FJsonObject> GraphJson = SerializeGraph(Graph);
				if (GraphJson.IsValid())
				{
					GraphArray.Add(MakeShared<FJsonValueObject>(GraphJson.ToSharedRef()));
				}
			}
		}
		Result->SetArrayField(TEXT("graphs"), GraphArray);
		return JsonToString(Result);
	}

	// Find specific graph
	UEdGraph* TargetGraph = nullptr;
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			TargetGraph = Graph;
			break;
		}
	}

	if (!TargetGraph)
	{
		// Return available graph names for debugging
		TArray<TSharedPtr<FJsonValue>> Names;
		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph) Names.Add(MakeShared<FJsonValueString>(Graph->GetName()));
		}
		TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Graph '%s' not found in '%s'"), *GraphName, *BlueprintName));
		E->SetArrayField(TEXT("availableGraphs"), Names);
		return JsonToString(E);
	}

	TSharedPtr<FJsonObject> GraphJson = SerializeGraph(TargetGraph);
	if (!GraphJson.IsValid())
	{
		return MakeErrorJson(TEXT("Failed to serialize graph"));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("blueprint"), BlueprintName);
	Result->SetObjectField(TEXT("graph"), GraphJson);
	return JsonToString(Result);
}

// ============================================================
// HandleSearch - Search Blueprints by name pattern
// GET /api/search?query=<pattern>&limit=<max_results>
// ============================================================

FString FManusMCPServer::HandleSearch(const TMap<FString, FString>& Params)
{
	FString Query = Params.Contains(TEXT("query")) ? Params[TEXT("query")] : TEXT("");
	int32 Limit = 50;
	if (Params.Contains(TEXT("limit")))
	{
		Limit = FCString::Atoi(*Params[TEXT("limit")]);
		if (Limit <= 0) Limit = 50;
	}

	if (Query.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required parameter: query"));
	}

	Query = UrlDecode(Query);

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 Count = 0;

	// Search Blueprints
	for (const FAssetData& Asset : AllBlueprintAssets)
	{
		if (Count >= Limit) break;
		FString Name = Asset.AssetName.ToString();
		if (Name.Contains(Query))
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Name);
			Entry->SetStringField(TEXT("path"), Asset.PackageName.ToString());
			Entry->SetStringField(TEXT("type"), TEXT("Blueprint"));
			Results.Add(MakeShared<FJsonValueObject>(Entry));
			Count++;
		}
	}

	// Search Maps
	for (const FAssetData& Asset : AllMapAssets)
	{
		if (Count >= Limit) break;
		FString Name = Asset.AssetName.ToString();
		if (Name.Contains(Query))
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Name);
			Entry->SetStringField(TEXT("path"), Asset.PackageName.ToString());
			Entry->SetStringField(TEXT("type"), TEXT("Map"));
			Results.Add(MakeShared<FJsonValueObject>(Entry));
			Count++;
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("query"), Query);
	Result->SetNumberField(TEXT("count"), Count);
	Result->SetArrayField(TEXT("results"), Results);
	return JsonToString(Result);
}

// ============================================================
// HandleFindReferences - Find references to an asset
// GET /api/references?asset=<name_or_path>
// ============================================================

FString FManusMCPServer::HandleFindReferences(const TMap<FString, FString>& Params)
{
	FString AssetName = Params.Contains(TEXT("asset")) ? Params[TEXT("asset")] : TEXT("");
	if (AssetName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required parameter: asset"));
	}

	AssetName = UrlDecode(AssetName);

	// Find the asset's package path
	FString PackagePath;
	FAssetData* BPAsset = FindBlueprintAsset(AssetName);
	if (BPAsset)
	{
		PackagePath = BPAsset->PackageName.ToString();
	}
	else
	{
		FAssetData* MapAsset = FindMapAsset(AssetName);
		if (MapAsset)
		{
			PackagePath = MapAsset->PackageName.ToString();
		}
	}

	if (PackagePath.IsEmpty())
	{
		return MakeErrorJson(FString::Printf(TEXT("Asset '%s' not found"), *AssetName));
	}

	// Query asset registry for referencers
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetIdentifier> Referencers;
	ARM.Get().GetReferencers(FAssetIdentifier(FName(*PackagePath)), Referencers);

	TArray<TSharedPtr<FJsonValue>> RefArray;
	for (const FAssetIdentifier& Ref : Referencers)
	{
		RefArray.Add(MakeShared<FJsonValueString>(Ref.PackageName.ToString()));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset"), AssetName);
	Result->SetStringField(TEXT("packagePath"), PackagePath);
	Result->SetNumberField(TEXT("referencerCount"), RefArray.Num());
	Result->SetArrayField(TEXT("referencers"), RefArray);
	return JsonToString(Result);
}

// ============================================================
// HandleListClasses - List UClasses matching a filter
// POST /api/list-classes { "filter": "Actor", "parentClass": "AActor" }
// ============================================================

FString FManusMCPServer::HandleListClasses(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString Filter = Json->GetStringField(TEXT("filter"));
	FString ParentClassName = Json->GetStringField(TEXT("parentClass"));
	int32 Limit = 100;
	if (Json->HasField(TEXT("limit")))
	{
		Limit = (int32)Json->GetNumberField(TEXT("limit"));
	}

	// Find parent class if specified
	UClass* ParentClass = nullptr;
	if (!ParentClassName.IsEmpty())
	{
		ParentClass = FindClassByName(ParentClassName);
		if (!ParentClass)
		{
			return MakeErrorJson(FString::Printf(TEXT("Parent class '%s' not found"), *ParentClassName));
		}
	}

	TArray<TSharedPtr<FJsonValue>> ClassArray;
	int32 Count = 0;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (Count >= Limit) break;

		UClass* Class = *It;
		if (!Class) continue;

		// Filter by parent class
		if (ParentClass && !Class->IsChildOf(ParentClass)) continue;

		// Filter by name
		FString ClassName = Class->GetName();
		if (!Filter.IsEmpty() && !ClassName.Contains(Filter)) continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), ClassName);
		Entry->SetStringField(TEXT("path"), Class->GetPathName());
		if (Class->GetSuperClass())
		{
			Entry->SetStringField(TEXT("parent"), Class->GetSuperClass()->GetName());
		}
		ClassArray.Add(MakeShared<FJsonValueObject>(Entry));
		Count++;
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), Count);
	Result->SetArrayField(TEXT("classes"), ClassArray);
	return JsonToString(Result);
}

// ============================================================
// HandleListFunctions - List UFunctions on a class
// POST /api/list-functions { "className": "AActor", "filter": "Get" }
// ============================================================

FString FManusMCPServer::HandleListFunctions(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString ClassName = Json->GetStringField(TEXT("className"));
	FString Filter = Json->GetStringField(TEXT("filter"));

	if (ClassName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: className"));
	}

	UClass* TargetClass = FindClassByName(ClassName);
	if (!TargetClass)
	{
		return MakeErrorJson(FString::Printf(TEXT("Class '%s' not found"), *ClassName));
	}

	TArray<TSharedPtr<FJsonValue>> FuncArray;

	for (TFieldIterator<UFunction> It(TargetClass); It; ++It)
	{
		UFunction* Func = *It;
		if (!Func) continue;

		FString FuncName = Func->GetName();
		if (!Filter.IsEmpty() && !FuncName.Contains(Filter)) continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), FuncName);
		Entry->SetStringField(TEXT("ownerClass"), Func->GetOwnerClass()->GetName());

		// Function flags
		TArray<TSharedPtr<FJsonValue>> Flags;
		if (Func->HasAnyFunctionFlags(FUNC_BlueprintCallable))
			Flags.Add(MakeShared<FJsonValueString>(TEXT("BlueprintCallable")));
		if (Func->HasAnyFunctionFlags(FUNC_BlueprintPure))
			Flags.Add(MakeShared<FJsonValueString>(TEXT("BlueprintPure")));
		if (Func->HasAnyFunctionFlags(FUNC_Static))
			Flags.Add(MakeShared<FJsonValueString>(TEXT("Static")));
		if (Func->HasAnyFunctionFlags(FUNC_Event))
			Flags.Add(MakeShared<FJsonValueString>(TEXT("Event")));
		Entry->SetArrayField(TEXT("flags"), Flags);

		// Parameters
		TArray<TSharedPtr<FJsonValue>> ParamArray;
		for (TFieldIterator<FProperty> PropIt(Func); PropIt; ++PropIt)
		{
			TSharedRef<FJsonObject> ParamEntry = MakeShared<FJsonObject>();
			ParamEntry->SetStringField(TEXT("name"), PropIt->GetName());
			ParamEntry->SetStringField(TEXT("type"), PropIt->GetCPPType());
			ParamEntry->SetBoolField(TEXT("isReturn"),
				PropIt->HasAnyPropertyFlags(CPF_ReturnParm));
			ParamEntry->SetBoolField(TEXT("isOutput"),
				PropIt->HasAnyPropertyFlags(CPF_OutParm));
			ParamArray.Add(MakeShared<FJsonValueObject>(ParamEntry));
		}
		Entry->SetArrayField(TEXT("parameters"), ParamArray);

		FuncArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("className"), ClassName);
	Result->SetNumberField(TEXT("count"), FuncArray.Num());
	Result->SetArrayField(TEXT("functions"), FuncArray);
	return JsonToString(Result);
}

// ============================================================
// HandleListProperties - List UProperties on a class
// POST /api/list-properties { "className": "AActor", "filter": "Transform" }
// ============================================================

FString FManusMCPServer::HandleListProperties(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString ClassName = Json->GetStringField(TEXT("className"));
	FString Filter = Json->GetStringField(TEXT("filter"));

	if (ClassName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: className"));
	}

	UClass* TargetClass = FindClassByName(ClassName);
	if (!TargetClass)
	{
		return MakeErrorJson(FString::Printf(TEXT("Class '%s' not found"), *ClassName));
	}

	TArray<TSharedPtr<FJsonValue>> PropArray;

	for (TFieldIterator<FProperty> It(TargetClass); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop) continue;

		FString PropName = Prop->GetName();
		if (!Filter.IsEmpty() && !PropName.Contains(Filter)) continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), PropName);
		Entry->SetStringField(TEXT("type"), Prop->GetCPPType());
		Entry->SetBoolField(TEXT("editable"),
			Prop->HasAnyPropertyFlags(CPF_Edit));
		Entry->SetBoolField(TEXT("blueprintVisible"),
			Prop->HasAnyPropertyFlags(CPF_BlueprintVisible));
		PropArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("className"), ClassName);
	Result->SetNumberField(TEXT("count"), PropArray.Num());
	Result->SetArrayField(TEXT("properties"), PropArray);
	return JsonToString(Result);
}

// ============================================================
// HandleGetPinInfo - Introspect pin types and connections
// POST /api/get-pin-info { "blueprint": "BP_Name", "nodeId": "GUID", "pinName": "Execute" }
// ============================================================

FString FManusMCPServer::HandleGetPinInfo(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	FString NodeId = Json->GetStringField(TEXT("nodeId"));
	FString PinName = Json->GetStringField(TEXT("pinName"));

	if (BlueprintName.IsEmpty() || NodeId.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required fields: blueprint, nodeId"));
	}

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP)
	{
		return MakeErrorJson(LoadError);
	}

	UEdGraphNode* Node = FindNodeByGuid(BP, NodeId);
	if (!Node)
	{
		return MakeErrorJson(FString::Printf(TEXT("Node '%s' not found"), *NodeId));
	}

	// If pinName is specified, return info for that specific pin
	if (!PinName.IsEmpty())
	{
		UEdGraphPin* Pin = Node->FindPin(FName(*PinName));
		if (!Pin)
		{
			// List available pins for debugging
			TArray<TSharedPtr<FJsonValue>> Available;
			for (UEdGraphPin* P : Node->Pins)
			{
				if (P)
				{
					Available.Add(MakeShared<FJsonValueString>(
						FString::Printf(TEXT("%s (%s)"),
							*P->PinName.ToString(),
							P->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"))));
				}
			}
			TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("error"),
				FString::Printf(TEXT("Pin '%s' not found on node '%s'"), *PinName, *NodeId));
			E->SetArrayField(TEXT("availablePins"), Available);
			return JsonToString(E);
		}

		TSharedPtr<FJsonObject> PinJson = SerializePin(Pin);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetStringField(TEXT("nodeId"), NodeId);
		Result->SetObjectField(TEXT("pin"), PinJson);
		return JsonToString(Result);
	}

	// No pinName specified — return all pins on the node
	TArray<TSharedPtr<FJsonValue>> PinArray;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		TSharedPtr<FJsonObject> PinJson = SerializePin(Pin);
		if (PinJson.IsValid())
		{
			PinArray.Add(MakeShared<FJsonValueObject>(PinJson.ToSharedRef()));
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("blueprint"), BlueprintName);
	Result->SetStringField(TEXT("nodeId"), NodeId);
	Result->SetStringField(TEXT("nodeTitle"),
		Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	Result->SetArrayField(TEXT("pins"), PinArray);
	return JsonToString(Result);
}
