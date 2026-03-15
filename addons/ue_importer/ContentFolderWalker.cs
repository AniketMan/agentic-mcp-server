#if TOOLS
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Godot;

namespace UEImporter;

/// <summary>
/// Walks a UE Content folder, discovers all .uasset files, categorizes them by type,
/// and orchestrates the extraction pipeline via CUE4ParseBridge.
/// </summary>
[Tool]
public partial class ContentFolderWalker : RefCounted
{
    private readonly string _contentPath;
    private readonly ImportConfig _config;
    private readonly ImportProgressWindow _progress;

    // Discovered assets grouped by type
    private readonly List<string> _staticMeshes = new();
    private readonly List<string> _skeletalMeshes = new();
    private readonly List<string> _textures = new();
    private readonly List<string> _materials = new();
    private readonly List<string> _animSequences = new();
    private readonly List<string> _audio = new();
    private readonly List<string> _other = new();

    // Import results for manifest generation
    private readonly List<ImportedAssetRecord> _importedAssets = new();

    public ContentFolderWalker(string contentPath, ImportConfig config, ImportProgressWindow progress)
    {
        _contentPath = ContentFolderValidator.ResolveContentPath(contentPath);
        _config = config;
        _progress = progress;
    }

    /// <summary>
    /// Entry point. Discovers assets, extracts them, and generates the manifest.
    /// Runs asynchronously to avoid blocking the editor.
    /// </summary>
    public async void StartImport()
    {
        try
        {
            _progress?.SetStatus("Scanning Content folder...");
            DiscoverAssets();

            int totalAssets = CountEnabledAssets();
            _progress?.SetTotal(totalAssets);
            GD.Print($"[UE Importer] Found {totalAssets} importable assets.");
            GD.Print($"  Static Meshes:   {_staticMeshes.Count}");
            GD.Print($"  Skeletal Meshes: {_skeletalMeshes.Count}");
            GD.Print($"  Textures:        {_textures.Count}");
            GD.Print($"  Materials:       {_materials.Count}");
            GD.Print($"  Animations:      {_animSequences.Count}");
            GD.Print($"  Audio:           {_audio.Count}");

            // Ensure output directories exist
            EnsureOutputDirectories();

            // Extract via CUE4Parse
            _progress?.SetStatus("Initializing CUE4Parse...");
            var bridge = new CUE4ParseBridge(_contentPath, _config);
            bool initialized = await bridge.Initialize();
            if (!initialized)
            {
                GD.PrintErr("[UE Importer] Failed to initialize CUE4Parse. Check console for details.");
                _progress?.SetStatus("ERROR: CUE4Parse initialization failed.");
                return;
            }

            // Process each asset type
            if (_config.ImportMeshes)
            {
                await ProcessAssetList(bridge, _staticMeshes, AssetType.StaticMesh, "Static Meshes");
                await ProcessAssetList(bridge, _skeletalMeshes, AssetType.SkeletalMesh, "Skeletal Meshes");
            }

            if (_config.ImportTextures)
                await ProcessAssetList(bridge, _textures, AssetType.Texture, "Textures");

            if (_config.ImportMaterials)
                await ProcessAssetList(bridge, _materials, AssetType.Material, "Materials");

            if (_config.ImportAnimations)
                await ProcessAssetList(bridge, _animSequences, AssetType.AnimSequence, "Animations");

            if (_config.ImportAudio)
                await ProcessAssetList(bridge, _audio, AssetType.Audio, "Audio");

            // Generate manifest
            if (_config.GenerateManifest)
            {
                _progress?.SetStatus("Generating import manifest...");
                var manifestGen = new ManifestGenerator(_config);
                manifestGen.Generate(_importedAssets);
                GD.Print("[UE Importer] Manifest written.");
            }

            // Trigger Godot reimport
            _progress?.SetStatus("Triggering Godot reimport...");
            EditorInterface.Singleton.GetResourceFilesystem().Scan();

            _progress?.SetStatus("Import complete!");
            GD.Print($"[UE Importer] Import complete. {_importedAssets.Count} assets imported.");
        }
        catch (Exception ex)
        {
            GD.PrintErr($"[UE Importer] Import failed: {ex.Message}");
            GD.PrintErr(ex.StackTrace);
            _progress?.SetStatus($"ERROR: {ex.Message}");
        }
    }

    /// <summary>
    /// Walks the Content folder and categorizes every .uasset by its type.
    /// Type detection is based on filename conventions and folder structure.
    /// CUE4Parse will confirm the actual type during extraction.
    /// </summary>
    private void DiscoverAssets()
    {
        var allAssets = Directory.EnumerateFiles(_contentPath, "*.uasset", SearchOption.AllDirectories);

        foreach (string assetPath in allAssets)
        {
            string relativePath = Path.GetRelativePath(_contentPath, assetPath);
            string fileName = Path.GetFileNameWithoutExtension(assetPath);
            string dirName = Path.GetDirectoryName(relativePath)?.Replace('\\', '/') ?? "";

            AssetType guessedType = GuessAssetType(fileName, dirName);

            switch (guessedType)
            {
                case AssetType.StaticMesh:
                    _staticMeshes.Add(assetPath);
                    break;
                case AssetType.SkeletalMesh:
                    _skeletalMeshes.Add(assetPath);
                    break;
                case AssetType.Texture:
                    _textures.Add(assetPath);
                    break;
                case AssetType.Material:
                    _materials.Add(assetPath);
                    break;
                case AssetType.AnimSequence:
                    _animSequences.Add(assetPath);
                    break;
                case AssetType.Audio:
                    _audio.Add(assetPath);
                    break;
                default:
                    _other.Add(assetPath);
                    break;
            }
        }
    }

    /// <summary>
    /// Heuristic type detection based on UE naming conventions and folder structure.
    /// CUE4Parse confirms the real type during extraction -- this is just for categorization.
    /// </summary>
    private static AssetType GuessAssetType(string fileName, string dirPath)
    {
        string upper = fileName.ToUpperInvariant();
        string dirUpper = dirPath.ToUpperInvariant();

        // Prefix-based detection (UE naming conventions)
        if (upper.StartsWith("SM_") || dirUpper.Contains("STATICMESH") || dirUpper.Contains("STATIC_MESH"))
            return AssetType.StaticMesh;
        if (upper.StartsWith("SK_") || upper.StartsWith("SKM_") || dirUpper.Contains("SKELETALMESH"))
            return AssetType.SkeletalMesh;
        if (upper.StartsWith("T_") || upper.StartsWith("TX_") || dirUpper.Contains("TEXTURE"))
            return AssetType.Texture;
        if (upper.StartsWith("M_") || upper.StartsWith("MI_") || dirUpper.Contains("MATERIAL"))
            return AssetType.Material;
        if (upper.StartsWith("A_") || upper.StartsWith("AS_") || upper.StartsWith("ANIM_") ||
            dirUpper.Contains("ANIMATION") || dirUpper.Contains("ANIM"))
            return AssetType.AnimSequence;
        if (upper.StartsWith("SFX_") || upper.StartsWith("SND_") || dirUpper.Contains("AUDIO") ||
            dirUpper.Contains("SOUND"))
            return AssetType.Audio;

        // Folder-based fallback
        if (dirUpper.Contains("MESH"))
            return AssetType.StaticMesh;

        return AssetType.Unknown;
    }

    /// <summary>
    /// Processes a list of assets through CUE4Parse extraction.
    /// </summary>
    private async Task ProcessAssetList(CUE4ParseBridge bridge, List<string> assets, AssetType type, string label)
    {
        if (assets.Count == 0)
            return;

        _progress?.SetStatus($"Extracting {label} ({assets.Count})...");

        for (int i = 0; i < assets.Count; i++)
        {
            string assetPath = assets[i];
            string fileName = Path.GetFileNameWithoutExtension(assetPath);
            _progress?.SetCurrentAsset($"{fileName} ({i + 1}/{assets.Count})");

            try
            {
                ImportedAssetRecord record = await bridge.ExtractAsset(assetPath, type);
                if (record != null)
                {
                    _importedAssets.Add(record);
                    _progress?.IncrementProgress();
                }
                else
                {
                    GD.PrintErr($"[UE Importer] Failed to extract: {fileName}");
                }
            }
            catch (Exception ex)
            {
                GD.PrintErr($"[UE Importer] Error extracting {fileName}: {ex.Message}");
            }
        }
    }

    /// <summary>
    /// Creates all output directories if they don't exist.
    /// </summary>
    private void EnsureOutputDirectories()
    {
        string[] dirs = {
            _config.MeshOutputDir,
            _config.TextureOutputDir,
            _config.MaterialOutputDir,
            _config.AnimationOutputDir,
            _config.AudioOutputDir,
            _config.ManifestOutputDir
        };

        foreach (string dir in dirs)
        {
            string globalPath = ProjectSettings.GlobalizePath(dir);
            if (!Directory.Exists(globalPath))
                Directory.CreateDirectory(globalPath);
        }
    }

    private int CountEnabledAssets()
    {
        int count = 0;
        if (_config.ImportMeshes) count += _staticMeshes.Count + _skeletalMeshes.Count;
        if (_config.ImportTextures) count += _textures.Count;
        if (_config.ImportMaterials) count += _materials.Count;
        if (_config.ImportAnimations) count += _animSequences.Count;
        if (_config.ImportAudio) count += _audio.Count;
        return count;
    }
}
#endif
