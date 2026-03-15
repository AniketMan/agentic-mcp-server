# UE Content Importer for Godot

A Godot EditorPlugin that imports assets directly from an Unreal Engine `Content` folder without needing to open Unreal Engine.

This tool is designed to work as the first step in the **Agentic Godot Pipeline**. It extracts the raw assets (meshes, textures, materials, animations) and generates a JSON manifest. The local LLM agents then read this manifest to understand the available assets and reconstruct the scenes in Godot.

## Features

- **Direct `.uasset` parsing**: Uses CUE4Parse to read Unreal Engine files directly from disk.
- **Static & Skeletal Meshes**: Extracts geometry and rig data to `glTF 2.0` (`.glb`).
- **Textures**: Decodes UE textures and saves them as `.png`.
- **Materials**: Extracts PBR parameters (Base Color, Metallic, Roughness, Normal, Emissive) and builds native Godot `StandardMaterial3D` (`.tres`) resources.
- **Animations**: Extracts `UAnimSequence` data to `glTF 2.0` clips.
- **Audio**: Decodes `USoundWave` to `.ogg` or `.wav`.
- **Manifest Generation**: Creates `import_manifest.json` mapping UE asset paths to Godot `res://` paths for agent use.

## Requirements

- Godot 4.3+ **.NET build** (C# support is required to run CUE4Parse).
- A valid Unreal Engine project `Content` folder.

## Installation

1. Copy the `addons/ue_importer` folder into your Godot project.
2. Open Godot and go to **Project > Project Settings > Plugins**.
3. Enable the **UE Content Importer** plugin.

*Note: Since this plugin uses C#, you must build your Godot solution (`MSBuild`) before enabling the plugin.*

## Usage

1. In the Godot editor, click **Project > Tools > Import from Unreal...**
2. A folder selection dialog will appear. Select either an Unreal Engine `.uproject` folder or its `Content` subfolder.
3. The plugin will scan the folder, identify all compatible `.uasset` files, and begin extraction.
4. Progress is displayed in a popup window.
5. Once complete, the extracted assets will be in `res://imported/` and Godot will automatically scan them.
6. The `import_manifest.json` will be written to `res://imported/` for the agents to use.

## Architecture

- `UEImporterPlugin.cs`: The Godot EditorPlugin entry point.
- `ContentFolderWalker.cs`: Scans the folder and categorizes assets by type.
- `CUE4ParseBridge.cs`: Wraps the CUE4Parse C# library to extract `.uasset` files.
- `GodotResourceBuilder.cs`: Converts extracted UE parameters into Godot `.tres` resources.
- `ManifestGenerator.cs`: Outputs the JSON manifest for the agent pipeline.

## Dependencies

This plugin relies on [CUE4Parse](https://github.com/FabianFG/CUE4Parse) (Apache 2.0) for `.uasset` extraction.
