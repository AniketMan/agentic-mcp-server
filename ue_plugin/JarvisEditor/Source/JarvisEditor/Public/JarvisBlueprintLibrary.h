// Copyright Aniket Bhatt. All Rights Reserved.
// JarvisBlueprintLibrary.h - Python-callable Blueprint graph manipulation functions
//
// ARCHITECTURE:
// This is a UBlueprintFunctionLibrary exposing static functions via UFUNCTION(BlueprintCallable).
// Because PythonScriptPlugin auto-wraps all BlueprintCallable UFUNCTIONs, every function here
// is directly callable from Python as: unreal.JarvisBlueprintLibrary.function_name(args)
//
// WHY C++ INSTEAD OF PURE PYTHON:
// The unreal Python module does NOT expose:
// - UK2Node spawning (UBlueprintNodeSpawner is editor-only C++)
// - Pin connection/disconnection (UEdGraphPin::MakeLinkTo is C++)
// - Blueprint compilation trigger (FKismetEditorUtilities::CompileBlueprint)
// - Graph transaction scoping (FScopedTransaction)
// - EdGraph node property modification with undo support
//
// SAFETY:
// Every function that modifies a graph wraps operations in FScopedTransaction
// so all changes are undoable via Ctrl+Z. Every function logs entry, exit, and errors
// to LogJarvis category for full traceability.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"
#include "Engine/Blueprint.h"
#include "JarvisBlueprintLibrary.generated.h"

// Log category - matches the one in JarvisEditorModule.cpp
DECLARE_LOG_CATEGORY_EXTERN(LogJarvis, Log, All);

/**
 * UJarvisBlueprintLibrary
 *
 * Static function library for AI-controlled Blueprint graph manipulation.
 * All functions are callable from Python via:
 *   unreal.JarvisBlueprintLibrary.function_name(args)
 *
 * DOMAINS:
 * 1. Graph Node Operations  - Add, remove, find nodes
 * 2. Pin Operations         - Connect, disconnect, find pins
 * 3. Blueprint Compilation  - Compile, validate, check errors
 * 4. Graph Inspection       - List nodes, dump graph state, export JSON
 * 5. Level Script Access    - Get/modify Level Blueprint for a given world
 * 6. Transaction Control    - Begin/end undo scopes for batch operations
 */
UCLASS()
class JARVISEDITOR_API UJarvisBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// =========================================================================
	// SECTION 1: GRAPH NODE OPERATIONS
	// These functions add, remove, and find nodes in Blueprint graphs.
	// Every add operation is wrapped in a transaction for undo support.
	// =========================================================================

	/**
	 * Add a CallFunction node to a Blueprint graph.
	 * This is the most common node type - calls a UFUNCTION by name.
	 *
	 * @param Blueprint       The Blueprint to modify
	 * @param GraphName       Name of the graph (e.g., "EventGraph", "UserConstructionScript")
	 * @param FunctionName    Fully qualified function name (e.g., "KismetSystemLibrary.PrintString")
	 * @param NodePosX        X position in the graph editor
	 * @param NodePosY        Y position in the graph editor
	 * @return                The created node, or nullptr on failure
	 *
	 * Python usage:
	 *   node = unreal.JarvisBlueprintLibrary.add_call_function_node(
	 *       bp, "EventGraph", "KismetSystemLibrary.PrintString", 200, 100)
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Nodes")
	static UK2Node_CallFunction* AddCallFunctionNode(
		UBlueprint* Blueprint,
		const FString& GraphName,
		const FString& FunctionName,
		int32 NodePosX = 0,
		int32 NodePosY = 0);

	/**
	 * Add a Custom Event node to a Blueprint graph.
	 * Creates a named event that can be called from other graphs or externally.
	 *
	 * @param Blueprint       The Blueprint to modify
	 * @param GraphName       Name of the graph
	 * @param EventName       Name for the custom event
	 * @param NodePosX        X position
	 * @param NodePosY        Y position
	 * @return                The created node, or nullptr on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Nodes")
	static UK2Node_CustomEvent* AddCustomEventNode(
		UBlueprint* Blueprint,
		const FString& GraphName,
		const FString& EventName,
		int32 NodePosX = 0,
		int32 NodePosY = 0);

	/**
	 * Add a Branch (if/then/else) node to a Blueprint graph.
	 *
	 * @param Blueprint       The Blueprint to modify
	 * @param GraphName       Name of the graph
	 * @param NodePosX        X position
	 * @param NodePosY        Y position
	 * @return                The created node, or nullptr on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Nodes")
	static UK2Node_IfThenElse* AddBranchNode(
		UBlueprint* Blueprint,
		const FString& GraphName,
		int32 NodePosX = 0,
		int32 NodePosY = 0);

	/**
	 * Add a Variable Get node to a Blueprint graph.
	 *
	 * @param Blueprint       The Blueprint to modify
	 * @param GraphName       Name of the graph
	 * @param VariableName    Name of the variable to get
	 * @param NodePosX        X position
	 * @param NodePosY        Y position
	 * @return                The created node, or nullptr on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Nodes")
	static UK2Node_VariableGet* AddVariableGetNode(
		UBlueprint* Blueprint,
		const FString& GraphName,
		const FString& VariableName,
		int32 NodePosX = 0,
		int32 NodePosY = 0);

	/**
	 * Add a Variable Set node to a Blueprint graph.
	 *
	 * @param Blueprint       The Blueprint to modify
	 * @param GraphName       Name of the graph
	 * @param VariableName    Name of the variable to set
	 * @param NodePosX        X position
	 * @param NodePosY        Y position
	 * @return                The created node, or nullptr on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Nodes")
	static UK2Node_VariableSet* AddVariableSetNode(
		UBlueprint* Blueprint,
		const FString& GraphName,
		const FString& VariableName,
		int32 NodePosX = 0,
		int32 NodePosY = 0);

	/**
	 * Add a Dynamic Cast node to a Blueprint graph.
	 *
	 * @param Blueprint       The Blueprint to modify
	 * @param GraphName       Name of the graph
	 * @param TargetClassName Name of the class to cast to (e.g., "BP_PlayerPawn")
	 * @param NodePosX        X position
	 * @param NodePosY        Y position
	 * @return                The created node, or nullptr on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Nodes")
	static UK2Node_DynamicCast* AddDynamicCastNode(
		UBlueprint* Blueprint,
		const FString& GraphName,
		const FString& TargetClassName,
		int32 NodePosX = 0,
		int32 NodePosY = 0);

	/**
	 * Remove a node from a Blueprint graph by its GUID.
	 * The GUID uniquely identifies a node across sessions.
	 *
	 * @param Blueprint       The Blueprint to modify
	 * @param GraphName       Name of the graph
	 * @param NodeGuid        The node's GUID as a string (e.g., "A1B2C3D4-...")
	 * @return                True if the node was found and removed
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Nodes")
	static bool RemoveNodeByGuid(
		UBlueprint* Blueprint,
		const FString& GraphName,
		const FString& NodeGuid);

	/**
	 * Remove all nodes from a Blueprint graph.
	 * WARNING: This is destructive. Use with caution.
	 *
	 * @param Blueprint       The Blueprint to modify
	 * @param GraphName       Name of the graph
	 * @return                Number of nodes removed
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Nodes")
	static int32 RemoveAllNodes(
		UBlueprint* Blueprint,
		const FString& GraphName);

	/**
	 * Find a node in a graph by its GUID.
	 *
	 * @param Blueprint       The Blueprint to search
	 * @param GraphName       Name of the graph
	 * @param NodeGuid        The node's GUID as a string
	 * @return                The found node, or nullptr
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Nodes")
	static UEdGraphNode* FindNodeByGuid(
		UBlueprint* Blueprint,
		const FString& GraphName,
		const FString& NodeGuid);

	// =========================================================================
	// SECTION 2: PIN OPERATIONS
	// These functions connect and disconnect pins between nodes.
	// Pin connections define the execution flow and data flow in Blueprints.
	// =========================================================================

	/**
	 * Connect two pins by their node GUIDs and pin names.
	 * This is the primary way to wire nodes together.
	 * Handles both execution pins (white arrows) and data pins (colored).
	 *
	 * @param Blueprint       The Blueprint containing the graph
	 * @param GraphName       Name of the graph
	 * @param SourceNodeGuid  GUID of the source node
	 * @param SourcePinName   Name of the output pin (e.g., "ReturnValue", "then", "exec")
	 * @param TargetNodeGuid  GUID of the target node
	 * @param TargetPinName   Name of the input pin (e.g., "self", "execute", "InString")
	 * @return                True if connection was successful
	 *
	 * Python usage:
	 *   success = unreal.JarvisBlueprintLibrary.connect_pins(
	 *       bp, "EventGraph",
	 *       source_guid, "then",
	 *       target_guid, "execute")
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Pins")
	static bool ConnectPins(
		UBlueprint* Blueprint,
		const FString& GraphName,
		const FString& SourceNodeGuid,
		const FString& SourcePinName,
		const FString& TargetNodeGuid,
		const FString& TargetPinName);

	/**
	 * Disconnect two specific pins.
	 *
	 * @param Blueprint       The Blueprint containing the graph
	 * @param GraphName       Name of the graph
	 * @param SourceNodeGuid  GUID of the source node
	 * @param SourcePinName   Name of the output pin
	 * @param TargetNodeGuid  GUID of the target node
	 * @param TargetPinName   Name of the input pin
	 * @return                True if disconnection was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Pins")
	static bool DisconnectPins(
		UBlueprint* Blueprint,
		const FString& GraphName,
		const FString& SourceNodeGuid,
		const FString& SourcePinName,
		const FString& TargetNodeGuid,
		const FString& TargetPinName);

	/**
	 * Disconnect ALL pins on a specific node.
	 * Useful before removing a node to ensure clean disconnection.
	 *
	 * @param Blueprint       The Blueprint containing the graph
	 * @param GraphName       Name of the graph
	 * @param NodeGuid        GUID of the node to disconnect
	 * @return                Number of connections broken
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Pins")
	static int32 DisconnectAllPins(
		UBlueprint* Blueprint,
		const FString& GraphName,
		const FString& NodeGuid);

	/**
	 * List all pins on a node with their names, types, directions, and connections.
	 * Returns a JSON string for easy parsing in Python.
	 *
	 * @param Blueprint       The Blueprint containing the graph
	 * @param GraphName       Name of the graph
	 * @param NodeGuid        GUID of the node to inspect
	 * @return                JSON string with pin details
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Pins")
	static FString GetNodePins(
		UBlueprint* Blueprint,
		const FString& GraphName,
		const FString& NodeGuid);

	// =========================================================================
	// SECTION 3: BLUEPRINT COMPILATION
	// These functions compile Blueprints and check for errors.
	// Compilation converts the visual graph into Kismet bytecode.
	// =========================================================================

	/**
	 * Compile a Blueprint and return whether compilation succeeded.
	 * This triggers the full KismetCompiler pipeline:
	 * Graph validation -> Node expansion -> Bytecode generation -> Class linking
	 *
	 * @param Blueprint       The Blueprint to compile
	 * @return                True if compilation succeeded with no errors
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Compile")
	static bool CompileBlueprint(UBlueprint* Blueprint);

	/**
	 * Get compilation errors and warnings for a Blueprint.
	 * Returns a JSON array of {type, message, node_guid} objects.
	 *
	 * @param Blueprint       The Blueprint to check
	 * @return                JSON string with error/warning details
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Compile")
	static FString GetCompileErrors(UBlueprint* Blueprint);

	/**
	 * Mark a Blueprint as modified (dirty) without compiling.
	 * Use this after batch operations before a final compile.
	 *
	 * @param Blueprint       The Blueprint to mark dirty
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Compile")
	static void MarkBlueprintDirty(UBlueprint* Blueprint);

	// =========================================================================
	// SECTION 4: GRAPH INSPECTION
	// These functions read graph state without modifying anything.
	// Useful for understanding the current state before making changes.
	// =========================================================================

	/**
	 * List all graphs in a Blueprint with their names and node counts.
	 * Returns a JSON array of {name, node_count, graph_type} objects.
	 *
	 * @param Blueprint       The Blueprint to inspect
	 * @return                JSON string with graph list
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Inspect")
	static FString ListGraphs(UBlueprint* Blueprint);

	/**
	 * Dump the full state of a graph as JSON.
	 * Includes all nodes with their types, positions, GUIDs, pin names,
	 * and connection topology. This is the primary inspection function.
	 *
	 * @param Blueprint       The Blueprint to inspect
	 * @param GraphName       Name of the graph to dump
	 * @return                JSON string with complete graph state
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Inspect")
	static FString DumpGraphState(
		UBlueprint* Blueprint,
		const FString& GraphName);

	/**
	 * Get the connection topology of a graph as an adjacency list.
	 * Returns JSON: {node_guid: [{pin, target_guid, target_pin}, ...]}
	 *
	 * @param Blueprint       The Blueprint to inspect
	 * @param GraphName       Name of the graph
	 * @return                JSON adjacency list
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Inspect")
	static FString GetGraphTopology(
		UBlueprint* Blueprint,
		const FString& GraphName);

	// =========================================================================
	// SECTION 5: LEVEL SCRIPT ACCESS
	// These functions access and modify the Level Script Blueprint
	// for the currently loaded world.
	// =========================================================================

	/**
	 * Get the Level Script Blueprint for the current editor world.
	 * This is the Blueprint that contains the level's event graph.
	 *
	 * @return                The Level Script Blueprint, or nullptr if not in editor
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Level")
	static UBlueprint* GetLevelScriptBlueprint();

	/**
	 * Get the Level Script Blueprint for a specific level by path.
	 *
	 * @param LevelPath       Asset path to the level (e.g., "/Game/Maps/SL_Trailer_Logic")
	 * @return                The Level Script Blueprint, or nullptr
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Level")
	static UBlueprint* GetLevelScriptBlueprintByPath(const FString& LevelPath);

	// =========================================================================
	// SECTION 6: TRANSACTION CONTROL
	// These functions manage undo/redo scoping for batch operations.
	// Wrap multiple operations in a single transaction so Ctrl+Z undoes them all.
	// =========================================================================

	/**
	 * Begin a named undo transaction.
	 * All subsequent graph modifications will be grouped under this transaction
	 * until EndTransaction is called.
	 *
	 * @param TransactionName  Human-readable name shown in the Undo History
	 *
	 * Python usage:
	 *   unreal.JarvisBlueprintLibrary.begin_transaction("JARVIS: Add teleport logic")
	 *   # ... multiple node/pin operations ...
	 *   unreal.JarvisBlueprintLibrary.end_transaction()
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Transaction")
	static void BeginTransaction(const FString& TransactionName);

	/**
	 * End the current undo transaction.
	 * All operations since BeginTransaction are now a single undo step.
	 */
	UFUNCTION(BlueprintCallable, Category = "Jarvis|Transaction")
	static void EndTransaction();

private:

	// =========================================================================
	// INTERNAL HELPERS
	// =========================================================================

	/** Find a graph by name within a Blueprint */
	static UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName);

	/** Find a pin by name on a node. Handles both display name and internal name matching. */
	static UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction);

	/** Convert a GUID string to FGuid */
	static bool StringToGuid(const FString& GuidString, FGuid& OutGuid);

	/** Active transaction pointer for batch operations */
	static TSharedPtr<class FScopedTransaction> ActiveTransaction;
};
