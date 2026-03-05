# Explainer: The JarvisEditor C++ Plugin

**Purpose:** This document explains what the `JarvisEditor` C++ plugin is, why it is necessary, and when you need to use it.

---

## The Problem: The Limits of Unreal's Python API

Unreal Engine's built-in Python module (`import unreal`) is powerful, but it has intentional limitations. It is designed primarily for **automating content management and level manipulation**, not for deep-level editing of Blueprint logic.

You **CAN** use the standard Python API to:
- Spawn and delete actors.
- Modify actor properties (location, scale, materials).
- Create and modify assets (Level Sequences, Data Tables).
- Add variables to a Blueprint.
- Save and load assets.

However, you **CANNOT** use the standard Python API to perform actions that modify the internal graph structure of a Blueprint. These operations are part of the editor-only C++ modules (`BlueprintGraph`, `KismetCompiler`, `UnrealEd`) that are not exposed to Python by default.

Specifically, you **cannot**: 
- Add a new node (like `Branch`, `ForLoop`, `PrintString`) to an Event Graph or function.
- Connect the output pin of one node to the input pin of another.
- Disconnect pins.
- Trigger a Blueprint to compile after modifying it.

Attempting to do so would require manually manipulating the binary `.uasset` file, which is extremely dangerous and guaranteed to cause crashes and data corruption.

## The Solution: A C++ Bridge to Python

The `JarvisEditor` plugin is a small, lightweight C++ library that solves this exact problem. It acts as a **bridge**, creating a link between the editor's internal C++ functions and the Python environment.

1.  **It's written in C++:** It has direct access to the editor's internal code, including the functions for creating nodes and connecting pins.
2.  **It exposes functions to Blueprints:** Using Unreal's `UFUNCTION(BlueprintCallable)` macro, it makes these C++ functions available to the Blueprint system.
3.  **Python can call Blueprint functions:** Because Unreal's Python API can call any `BlueprintCallable` function, it can now access the C++ functions from the plugin.

```
+-----------------------+
| Python Script         |
| (e.g., "add a node")  |
+-----------------------+
           |
           v
+-----------------------+
| unreal.JarvisBlueprintLibrary.add_node() |
+-----------------------+
           |
           v
+-----------------------+
| JarvisEditor C++ Plugin |
| (UFUNCTION)           |
+-----------------------+
           |
           v
+-----------------------+
| UE C++ Editor Internals |
| (BlueprintGraph module) |
+-----------------------+
```

This allows the Manus agent to safely and reliably edit Blueprint graphs by calling functions like `unreal.JarvisBlueprintLibrary.connect_pins(...)`, while the C++ plugin handles the complex and sensitive operations inside the engine.

---

## When to Use It: A Simple Checklist

**You DO need the plugin installed and compiled if your task involves:**

- [X] Adding any node to a Blueprint graph (EventGraph, functions, macros).
- [X] Connecting or disconnecting pins between nodes.
- [X] Creating a new Custom Event.
- [X] Deleting a node from a graph.
- [X] Compiling a Blueprint from a script.

**You DO NOT need the plugin for tasks like:**

- [ ] Spawning or deleting an actor in a level.
- [ ] Moving an actor.
- [ ] Changing an actor's properties (e.g., setting a new Static Mesh).
- [ ] Creating a new Level Sequence.
- [ ] Adding a track or keyframe to a Level Sequence.
- [ ] Setting a parameter on a Material Instance.
- [ ] Adding a new variable to a Blueprint's variable list.

For all tasks in the second list, the standard script generator is sufficient.

---

## Plugin Function Reference

The plugin provides the following functions, which become available in Python under `unreal.JarvisBlueprintLibrary` after the plugin is compiled into your project.

| Function | Purpose |
| :--- | :--- |
| `AddCallFunctionNode` | Adds a node that calls a function (e.g., `PrintString`). |
| `AddCustomEventNode` | Adds a new Custom Event node. |
| `AddBranchNode` | Adds an If/Then (Branch) node. |
| `AddVariableGetNode` | Adds a node to get the value of a variable. |
| `AddVariableSetNode` | Adds a node to set the value of a variable. |
| `AddDynamicCastNode` | Adds a "Cast To..." node. |
| `RemoveNodeByGuid` | Deletes a node from a graph. |
| `ConnectPins` | Connects two pins (data or execution). |
| `DisconnectPins` | Disconnects two specific pins. |
| `DisconnectAllPins` | Breaks all connections on a node. |
| `CompileBlueprint` | Compiles a Blueprint and reports errors. |
| `GetLevelScriptBlueprint` | Gets the Level Blueprint for the current world, which is required for many edits. |
| `BeginTransaction` / `EndTransaction` | Wraps a series of edits into a single Undo/Redo step. |

By using these functions, the agent can perform complex logical edits to Blueprints in a way that is **100% safe, stable, and supported by the Unreal Editor.**
