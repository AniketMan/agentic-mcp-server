> **This is the single source of truth for the UE5.6 Level Editor architecture. All other documents are secondary to this one. If another document contradicts this one, this one is correct.**

# Ground Truth: UE5.6 Level Editor Architecture

**Date:** Mar 04, 2026
**Author:** JARVIS

## 1. The Core Principle

The entire purpose of this toolchain is to **programmatically and safely edit Unreal Engine 5.6 assets *without* needing to open the Unreal Editor application.**

This is achieved by directly reading and writing the binary `.uasset` and `.umap` files using the **UAssetAPI** library, wrapped in a Python-to-.NET bridge. All operations occur in memory on a server and are validated before being saved.

There is no script generation. There is no `-ExecutePythonScript`. There is no interaction with a running Unreal Editor process. The workflow is entirely headless and binary-focused.

---

## 2. The Two Distinct Workflows

There are two completely separate, mutually exclusive workflows. They do not interact. Understanding which workflow you are in is critical.

### Workflow A: The Manus Agent Workflow (Headless, Binary Editing)

This is the primary workflow for all Manus agents.

| Component | Role | Technology | Execution Environment |
| :--- | :--- | :--- | :--- |
| **Level Editor API** | The central service. Reads `.uasset` files, modifies them in memory, validates changes, and writes the new binary file. | Flask, Python, **UAssetAPI** (.NET) | Sandbox (Linux) |
| **Manus Agent (You)** | The client. Pulls assets from Perforce, sends them to the Level Editor API, receives the modified assets, and (with user approval) submits them back to Perforce. | Manus Environment | Sandbox |

**The process is:**

1.  You (`Manus Agent`) get a `.uasset` file from Perforce.
2.  You send this binary file to the `Level Editor API` via a `POST` request.
3.  The `Level Editor API` loads the asset into memory using **UAssetAPI**.
4.  You send further API calls to the `Level Editor API` to perform edits (e.g., `POST /api/edit/add-node`).
5.  The `Level Editor API` uses **UAssetAPI** to make these changes directly to the in-memory representation of the asset.
6.  When all edits are complete, you call `POST /api/asset/save`.
7.  The `Level Editor API` validates the in-memory asset, serializes it back into a binary `.uasset` file, and returns the new file to you.
8.  You show the user the proposed changes and, upon approval, submit the new binary file to Perforce.

**In this workflow, the Unreal Editor is never opened. The C++ plugin is never used.**

### Workflow B: The Local AI Workflow (Interactive, Editor-Focused)

This workflow is for a future use case where an AI runs locally on a developer's workstation with the Unreal Editor open.

| Component | Role | Technology | Execution Environment |
| :--- | :--- | :--- | :--- |
| **Local AI Agent** | Sends commands to the running Unreal Editor. | Python | User's Workstation (Windows/macOS) |
| **Unreal Editor** | The running application. Receives commands and executes them. | UE 5.6 | User's Workstation |
| **JarvisEditor C++ Plugin** | An extension **inside the Unreal Editor** that exposes Blueprint graph editing functions to Python. | C++ | Inside the Unreal Editor process |

**The process is:**

1.  A developer has the SOH_VR project open in the **Unreal Editor**.
2.  A `Local AI Agent` running on the same machine wants to modify a Blueprint.
3.  The `Local AI Agent` executes a Python script that calls `unreal.JarvisBlueprintLibrary.add_node(...)`.
4.  The **JarvisEditor C++ Plugin** receives this call and uses the editor's internal C++ functions to add the node to the graph.
5.  The change appears instantly in the open **Unreal Editor**.

**The Manus agents will NEVER use Workflow B. The `JarvisEditor` C++ plugin is irrelevant to the Manus workflow.** The presence of the plugin files in this repository is solely for the future development of the local-only tool.

---

## 3. Final API Surface (Manus Workflow A)

These are the primary endpoints for the Manus workflow. All endpoints are prefixed with `/api`.

### Read Endpoints

- `GET /api/status`: Check if an asset is loaded.
- `POST /api/load`: Load a new asset file.
- `GET /api/summary`: Get asset summary (counts, etc.).
- `GET /api/exports`: List all exports.
- `GET /api/imports`: List all imports.
- `GET /api/names`: List the name map.
- `GET /api/export/<int:index>`: Get detailed info for one export.
- `GET /api/actors`: List all level actors.
- `GET /api/actor/<name>`: Get detailed info for one actor.

### Write Endpoints (`/api/write/*`)

These endpoints perform direct binary edits via UAssetAPI.

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/api/write/add-import` | `POST` | Add a new class/package reference to the import table. |
| `/api/write/find-or-add-import` | `POST` | Find an existing import or add it if missing. |
| `/api/write/add-export` | `POST` | Create a new NormalExport (actor, component, etc.). |
| `/api/write/add-actor` | `POST` | Add an export to the LevelExport.Actors list. |
| `/api/write/remove-actor` | `POST` | Remove an actor from the LevelExport.Actors list. |
| `/api/write/add-property` | `POST` | Add a new property to an export's Data list. |
| `/api/write/remove-property` | `POST` | Remove a property from an export by name. |
| `/api/property` | `POST` | Set a property value on an export (legacy, use `/api/write/set-property` instead). |
| `/api/write/validate` | `GET` | Run pre-save validation on the loaded asset. |
| `/api/write/backup` | `POST` | Create a backup of the current asset file. |
| `/api/write/save` | `POST` | Save the modified asset to disk with validation and backup. |

---

## 4. Codebase Audit and Mandate

As of this document, the codebase is being audited to enforce this architectural separation.

-   **`core/script_generator.py` is deprecated and will be removed.** It was the source of all confusion. The Manus workflow does not generate or execute `.py` scripts.
-   **The `ue_plugin/` directory is irrelevant to the Manus workflow.** It will be ignored by all Manus agents.
-   **The `core/uasset_bridge.py` file will be expanded** to include write operations (add/remove nodes, connect/disconnect pins, set properties) that directly call UAssetAPI's binary modification functions.
-   **The `ui/server.py` file will be updated** to expose these new write operations through secure, validated REST endpoints (e.g., `POST /api/edit/add-node`).

This document is the law. If you find any part of the system that appears to contradict this document, report it as a bug. Do not attempt to use the system in a way that violates these stated principles.
