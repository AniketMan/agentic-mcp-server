# Manus Integration Guide: UE 5.6 Level Editor

**Objective:** This guide provides instructions for an autonomous agent to use the UE 5.6 Level Editor toolchain. The tool allows for safe inspection and modification of Unreal Engine 5.6 assets by generating and executing Python scripts within the editor.

---

## 1. Core Concepts

The tool operates on a three-layer architecture to ensure project integrity:

1.  **Layer 1: Binary Inspection (Read-Only)**
    - The tool reads `.uasset` and `.umap` files directly using the UAssetAPI library.
    - This layer is for inspection only (reading actors, Blueprints, properties). It **never** writes back to the binary files, eliminating the risk of corruption.

2.  **Layer 2: Script Generation**
    - Based on an edit request, the tool generates a Python script that uses Unreal Engine's native `unreal` module.
    - These scripts contain the logic to perform the requested edits (e.g., spawn an actor, change a material parameter).

3.  **Layer 3: C++ Plugin Execution**
    - For operations not exposed by the default `unreal` Python module (specifically, **Blueprint graph editing**), a C++ plugin (`JarvisEditor`) provides the necessary functions.
    - The generated Python script calls these C++ functions from within the editor.

**The fundamental principle is that all write operations are executed by the Unreal Editor itself**, leveraging the engine's native APIs for serialization, validation, and reference management. This is the only guaranteed safe way to modify UE assets programmatically.

---

## 2. Setup and Server Launch

Execute the following commands in your shell to set up the environment and launch the API server.

```bash
# Navigate to the tool's root directory
cd /path/to/ue56-level-editor

# Run the bootstrap script (one-time setup)
# This installs the .NET SDK, builds the UAssetAPI bridge, and installs Python dependencies.
chmod +x setup.sh
./setup.sh

# Launch the API server
# The server will be available at http://localhost:8080
python3 run_dashboard.py
```

---

## 3. Agent Workflow

Follow this sequence of API calls and actions to inspect and edit assets.

### Step 1: Load an Asset or Project

All operations require an asset or project to be loaded first.

**To load a single level file (.umap):**
```bash
curl -X POST http://localhost:8080/api/load \
  -H "Content-Type: application/json" \
  -d '{"path": "/absolute/path/to/YourLevel.umap"}'
```

**To load a full project (.uproject):**
```bash
curl -X POST http://localhost:8080/api/project/scan \
  -H "Content-Type: application/json" \
  -d '{"project_path": "/absolute/path/to/YourProjectRoot"}'
```

### Step 2: Inspect the Asset (Read-Only)

Use the following GET endpoints to understand the asset's contents.

-   **List Actors:** `GET http://localhost:8080/api/actors`
-   **Get Actor Detail:** `GET http://localhost:8080/api/actor/<ActorName>`
-   **List Blueprint Functions:** `GET http://localhost:8080/api/functions`
-   **Get Blueprint Graph:** `GET http://localhost:8080/api/graph/<ExportIndex>`
-   **List All Script Operations:** `GET http://localhost:8080/api/script/operations`

### Step 3: Generate an Edit Script

To perform an edit, you must first generate a Python script. Send a POST request to the `/api/script/generate` endpoint.

**Example: Generate a script to spawn a `StaticMeshActor`**
```bash
curl -X POST http://localhost:8080/api/script/generate \
  -H "Content-Type: application/json" \
  -d '{
    "domain": "actors",
    "method": "spawn",
    "params": {
      "class_name": "StaticMeshActor",
      "label": "GeneratedActor_01",
      "location": [150.0, 0.0, 100.0]
    }
  }'
```

The API will respond with a JSON object containing the full Python script text.

### Step 4: Execute the Script in Unreal Editor

The generated script must be run inside the target Unreal Engine project.

1.  Save the script from the API response to a file (e.g., `/tmp/edit_script.py`).
2.  Execute the script using the UE command line:

    ```bash
    # Path to your UE Editor executable
    UE_EDITOR="/path/to/UnrealEngine/Engine/Binaries/Linux/UE5Editor"

    # Path to your .uproject file
    UPROJECT="/path/to/YourProject/YourProject.uproject"

    # Path to the generated script
    SCRIPT_FILE="/tmp/edit_script.py"

    # Execute the command
    $UE_EDITOR "$UPROJECT" -ExecutePythonScript="$SCRIPT_FILE"
    ```

3.  The editor will launch, run the script, and then close. The changes will be saved to the asset.

---

## 4. The `JarvisEditor` C++ Plugin (For Blueprint Graph Editing)

**This is a critical step for any task involving the modification of Blueprint graphs.**

### Why It's Necessary

The standard `unreal` Python module **cannot**:
- Add or remove nodes from a Blueprint graph (e.g., EventGraph).
- Connect or disconnect pins between nodes.
- Trigger Blueprint compilation.

The `JarvisEditor` C++ plugin bridges this gap by exposing these functions to Python.

### When It's Needed

You must install and compile this plugin if the agent needs to perform tasks like:
- "Add a `PrintString` node after the `BeginPlay` event."
- "Create a new custom event named `OnPlayerDeath`."
- "Connect the `OnComponentHit` event to a `Cast To BP_Player` node."

For any other task (spawning actors, changing properties, editing Level Sequences), the plugin is **not** required.

### Installation and Compilation (One-Time Setup Per Project)

1.  **Copy Plugin Source:**
    Copy the `ue_plugin/JarvisEditor` directory from this tool into your Unreal project's `Plugins/` directory.

    ```bash
    # Source: from this tool's directory
    SOURCE_PLUGIN="/path/to/ue56-level-editor/ue_plugin/JarvisEditor"

    # Destination: your Unreal project
    DEST_PLUGINS_DIR="/path/to/YourProject/Plugins/"

    # Create the destination directory if it doesn't exist
    mkdir -p "$DEST_PLUGINS_DIR"

    # Copy the plugin
    cp -r "$SOURCE_PLUGIN" "$DEST_PLUGINS_DIR"
    ```

2.  **Regenerate Project Files:**
    This step detects the new plugin and adds it to the build system.

    -   **Linux:** Run `./GenerateProjectFiles.sh` in your project's root directory.
    -   **Windows:** Right-click the `.uproject` file and select "Generate Visual Studio project files".

3.  **Compile the Project:**
    Build the project using your standard workflow. The Unreal Build Tool will automatically find and compile the `JarvisEditor` plugin.

    -   **Linux/Rider:** Build the `Development Editor` configuration.
    -   **Windows/Visual Studio:** Build the `Development Editor` configuration.
    -   **Command Line:**
        ```bash
        # Path to Unreal Build Tool
        UBT="/path/to/UnrealEngine/Engine/Build/BatchFiles/RunUAT.sh"

        # Build the project editor
        $UBT Build -project="/path/to/YourProject/YourProject.uproject" -target="UnrealEditor" -platform="Linux" -configuration="Development"
        ```

### Verification

After compiling, launch the editor and run the following in the Python console (`Window > Developer Tools > Output Log`):

```python
import unreal

# This should print True if the plugin is loaded correctly
print(hasattr(unreal, 'JarvisBlueprintLibrary'))

# This should list all the C++ functions
print(dir(unreal.JarvisBlueprintLibrary))
```

If this verification passes, the agent can now generate and execute scripts that perform Blueprint graph edits.

---

## 5. API and Script Quick Reference

### Key API Endpoints

| Method | Endpoint | Description |
| :--- | :--- | :--- |
| POST | `/api/load` | Load a single `.umap` or `.uasset` file. |
| POST | `/api/project/scan` | Scan a full `.uproject` directory. |
| GET | `/api/actors` | List all actors in the loaded level. |
| GET | `/api/functions` | List all Blueprint functions in the loaded asset. |
| GET | `/api/script/operations` | Get a list of all available script generation operations. |
| POST | `/api/script/generate` | Generate a Python script for a specified operation. |

### Common Script Operations (`/api/script/generate`)

| Domain | Method | Description |
| :--- | :--- | :--- |
| `actors` | `spawn` | Spawn an actor from a class. |
| `actors` | `spawn_blueprint` | Spawn an actor from a Blueprint asset. |
| `actors` | `set_property` | Set a property on an actor. |
| `assets` | `duplicate` | Duplicate an asset. |
| `sequences` | `add_track` | Add a track to a Level Sequence. |
| `sequences` | `add_keyframe` | Add a keyframe to a track. |
| `materials` | `set_parameter` | Set a parameter on a Material Instance. |
| `blueprints` | `add_variable` | Add a new variable to a Blueprint. |
| `utility` | `custom` | Wrap arbitrary Python code in the standard safety header. |

**To get the full list of all 45+ operations and their parameters, query the `/api/script/operations` endpoint.**
