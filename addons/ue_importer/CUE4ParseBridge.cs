#if TOOLS
using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using CUE4Parse.FileProvider;
using CUE4Parse.UE4.Assets.Exports;
using CUE4Parse.UE4.Assets.Exports.Animation;
using CUE4Parse.UE4.Assets.Exports.Material;
using CUE4Parse.UE4.Assets.Exports.SkeletalMesh;
using CUE4Parse.UE4.Assets.Exports.Sound;
using CUE4Parse.UE4.Assets.Exports.StaticMesh;
using CUE4Parse.UE4.Assets.Exports.Texture;
using CUE4Parse.UE4.Versions;
using CUE4Parse_Conversion;
using CUE4Parse_Conversion.Meshes;
using CUE4Parse_Conversion.Textures;
using Godot;

namespace UEImporter;

/// <summary>
/// Bridge between the Godot plugin and CUE4Parse.
/// Initializes a file provider against the UE Content folder,
/// then extracts individual assets to Godot-compatible formats.
/// </summary>
public class CUE4ParseBridge
{
    private readonly string _contentPath;
    private readonly ImportConfig _config;
    private DefaultFileProvider _provider;

    public CUE4ParseBridge(string contentPath, ImportConfig config)
    {
        _contentPath = contentPath;
        _config = config;
    }

    /// <summary>
    /// Initializes the CUE4Parse file provider.
    /// Scans the Content directory for all .uasset/.uexp/.ubulk files.
    /// </summary>
    public async Task<bool> Initialize()
    {
        try
        {
            EGame gameVersion = ParseGameVersion(_config.UEVersion);

            _provider = new DefaultFileProvider(
                _contentPath,
                SearchOption.AllDirectories,
                isCaseInsensitive: true,
                new VersionContainer(gameVersion)
            );

            _provider.Initialize();
            await _provider.MountAsync();

            GD.Print($"[CUE4Parse] Initialized. Found {_provider.Files.Count} files.");
            return _provider.Files.Count > 0;
        }
        catch (Exception ex)
        {
            GD.PrintErr($"[CUE4Parse] Initialization failed: {ex.Message}");
            GD.PrintErr(ex.StackTrace);
            return false;
        }
    }

    /// <summary>
    /// Extracts a single asset from the Content folder.
    /// Routes to the appropriate extractor based on asset type.
    /// Returns an ImportedAssetRecord on success, null on failure.
    /// </summary>
    public async Task<ImportedAssetRecord> ExtractAsset(string assetPath, AssetType hintType)
    {
        try
        {
            string relativePath = Path.GetRelativePath(_contentPath, assetPath);
            // CUE4Parse uses forward slashes and no extension for package paths
            string packagePath = relativePath
                .Replace('\\', '/')
                .Replace(".uasset", "");

            var allExports = _provider.LoadAllObjects(packagePath);
            if (allExports == null || !allExports.Any())
            {
                GD.PrintErr($"[CUE4Parse] No exports found in: {packagePath}");
                return null;
            }

            // Find the primary export (the main asset object)
            foreach (var export in allExports)
            {
                switch (export)
                {
                    case UStaticMesh staticMesh:
                        return await ExtractStaticMesh(staticMesh, relativePath);

                    case USkeletalMesh skeletalMesh:
                        return await ExtractSkeletalMesh(skeletalMesh, relativePath);

                    case UTexture2D texture:
                        return await ExtractTexture(texture, relativePath);

                    case UMaterialInterface material:
                        return ExtractMaterial(material, relativePath);

                    case UAnimSequence animSequence:
                        return await ExtractAnimation(animSequence, relativePath);

                    case USoundWave sound:
                        return await ExtractAudio(sound, relativePath);
                }
            }

            // If no recognized type, log and skip
            var firstExport = allExports.First();
            GD.Print($"[CUE4Parse] Skipping unrecognized type: {firstExport.GetType().Name} in {packagePath}");
            return null;
        }
        catch (Exception ex)
        {
            GD.PrintErr($"[CUE4Parse] Error extracting {assetPath}: {ex.Message}");
            return null;
        }
    }

    /// <summary>
    /// Extracts a UStaticMesh to glTF/glb format.
    /// </summary>
    private async Task<ImportedAssetRecord> ExtractStaticMesh(UStaticMesh mesh, string relativePath)
    {
        string outputDir = ProjectSettings.GlobalizePath(_config.MeshOutputDir);
        string subDir = Path.GetDirectoryName(relativePath)?.Replace('\\', '/') ?? "";
        string fullOutputDir = Path.Combine(outputDir, subDir);
        Directory.CreateDirectory(fullOutputDir);

        var exporter = new Exporter(mesh, new ExporterOptions
        {
            MeshFormat = EMeshFormat.Gltf2,
            TextureFormat = ETextureFormat.Png,
            LodFormat = ELodFormat.FirstLod
        });

        bool success = exporter.TryWriteToDir(
            new DirectoryInfo(fullOutputDir),
            out string label,
            out string savedFilePath
        );

        if (!success)
            return null;

        string[] materialSlots = mesh.Materials?
            .Select(m => m?.Name ?? "unnamed")
            .ToArray() ?? Array.Empty<string>();

        return new ImportedAssetRecord
        {
            UEAssetPath = relativePath,
            UEAssetName = mesh.Name,
            Type = AssetType.StaticMesh,
            UEClassName = "UStaticMesh",
            GodotResourcePath = ToResPath(savedFilePath, outputDir, _config.MeshOutputDir),
            GodotFileName = Path.GetFileName(savedFilePath),
            ExportFormat = "glb",
            FileSizeBytes = new FileInfo(savedFilePath).Length,
            HasSkeleton = false,
            HasAnimations = false,
            MaterialSlots = materialSlots
        };
    }

    /// <summary>
    /// Extracts a USkeletalMesh to glTF/glb format with skeleton data.
    /// </summary>
    private async Task<ImportedAssetRecord> ExtractSkeletalMesh(USkeletalMesh mesh, string relativePath)
    {
        string outputDir = ProjectSettings.GlobalizePath(_config.MeshOutputDir);
        string subDir = Path.GetDirectoryName(relativePath)?.Replace('\\', '/') ?? "";
        string fullOutputDir = Path.Combine(outputDir, subDir);
        Directory.CreateDirectory(fullOutputDir);

        var exporter = new Exporter(mesh, new ExporterOptions
        {
            MeshFormat = EMeshFormat.Gltf2,
            TextureFormat = ETextureFormat.Png,
            LodFormat = ELodFormat.FirstLod
        });

        bool success = exporter.TryWriteToDir(
            new DirectoryInfo(fullOutputDir),
            out string label,
            out string savedFilePath
        );

        if (!success)
            return null;

        string[] materialSlots = mesh.Materials?
            .Select(m => m?.MaterialInterface?.Name ?? "unnamed")
            .ToArray() ?? Array.Empty<string>();

        return new ImportedAssetRecord
        {
            UEAssetPath = relativePath,
            UEAssetName = mesh.Name,
            Type = AssetType.SkeletalMesh,
            UEClassName = "USkeletalMesh",
            GodotResourcePath = ToResPath(savedFilePath, outputDir, _config.MeshOutputDir),
            GodotFileName = Path.GetFileName(savedFilePath),
            ExportFormat = "glb",
            FileSizeBytes = new FileInfo(savedFilePath).Length,
            HasSkeleton = true,
            HasAnimations = false,
            MaterialSlots = materialSlots
        };
    }

    /// <summary>
    /// Extracts a UTexture2D to PNG format.
    /// </summary>
    private async Task<ImportedAssetRecord> ExtractTexture(UTexture2D texture, string relativePath)
    {
        string outputDir = ProjectSettings.GlobalizePath(_config.TextureOutputDir);
        string subDir = Path.GetDirectoryName(relativePath)?.Replace('\\', '/') ?? "";
        string fullOutputDir = Path.Combine(outputDir, subDir);
        Directory.CreateDirectory(fullOutputDir);

        var bitmap = texture.Decode(ETexturePlatform.DesktopMobile);
        if (bitmap == null)
        {
            GD.PrintErr($"[CUE4Parse] Failed to decode texture: {texture.Name}");
            return null;
        }

        string outputPath = Path.Combine(fullOutputDir, $"{texture.Name}.png");
        await using var stream = File.Create(outputPath);
        bitmap.Encode(stream, SkiaSharp.SKEncodedImageFormat.Png, 100);

        return new ImportedAssetRecord
        {
            UEAssetPath = relativePath,
            UEAssetName = texture.Name,
            Type = AssetType.Texture,
            UEClassName = "UTexture2D",
            GodotResourcePath = ToResPath(outputPath, outputDir, _config.TextureOutputDir),
            GodotFileName = $"{texture.Name}.png",
            ExportFormat = "png",
            FileSizeBytes = new FileInfo(outputPath).Length
        };
    }

    /// <summary>
    /// Extracts material parameter values from a UMaterialInterface.
    /// Does not export the node graph -- only the final parameter values
    /// that can be mapped to Godot's StandardMaterial3D.
    /// </summary>
    private ImportedAssetRecord ExtractMaterial(UMaterialInterface material, string relativePath)
    {
        var parameters = new MaterialParams();

        // Extract scalar parameters
        if (material is UMaterialInstanceConstant instance)
        {
            foreach (var param in instance.ScalarParameterValues ?? Array.Empty<FScalarParameterValue>())
            {
                string name = param.ParameterInfo?.Name?.Text?.ToLowerInvariant() ?? "";
                switch (name)
                {
                    case "metallic":
                        parameters.Metallic = param.ParameterValue;
                        break;
                    case "roughness":
                        parameters.Roughness = param.ParameterValue;
                        break;
                    case "specular":
                        parameters.Specular = param.ParameterValue;
                        break;
                    case "opacity":
                        parameters.Opacity = param.ParameterValue;
                        break;
                    case "emissive_strength":
                    case "emissivestrength":
                        parameters.EmissiveStrength = param.ParameterValue;
                        break;
                }
            }

            // Extract vector parameters (base color, emissive)
            foreach (var param in instance.VectorParameterValues ?? Array.Empty<FVectorParameterValue>())
            {
                string name = param.ParameterInfo?.Name?.Text?.ToLowerInvariant() ?? "";
                var v = param.ParameterValue;
                switch (name)
                {
                    case "basecolor":
                    case "base_color":
                    case "base color":
                    case "diffusecolor":
                        parameters.BaseColor = new[] { v.R, v.G, v.B, v.A };
                        break;
                    case "emissivecolor":
                    case "emissive_color":
                    case "emissive color":
                        parameters.EmissiveColor = new[] { v.R, v.G, v.B };
                        break;
                }
            }

            // Extract texture parameter references
            foreach (var param in instance.TextureParameterValues ?? Array.Empty<FTextureParameterValue>())
            {
                string name = param.ParameterInfo?.Name?.Text?.ToLowerInvariant() ?? "";
                string texPath = param.ParameterValue?.GetPathName() ?? "";
                switch (name)
                {
                    case "basecolor":
                    case "base_color":
                    case "diffuse":
                        parameters.BaseColorTexture = texPath;
                        break;
                    case "normal":
                    case "normalmap":
                        parameters.NormalTexture = texPath;
                        break;
                    case "metallic":
                        parameters.MetallicTexture = texPath;
                        break;
                    case "roughness":
                        parameters.RoughnessTexture = texPath;
                        break;
                    case "emissive":
                        parameters.EmissiveTexture = texPath;
                        break;
                    case "occlusion":
                    case "ao":
                    case "ambientocclusion":
                        parameters.OcclusionTexture = texPath;
                        break;
                    case "opacity":
                        parameters.OpacityTexture = texPath;
                        break;
                }
            }
        }

        return new ImportedAssetRecord
        {
            UEAssetPath = relativePath,
            UEAssetName = material.Name,
            Type = AssetType.Material,
            UEClassName = material.GetType().Name,
            GodotResourcePath = $"{_config.MaterialOutputDir}/{material.Name}.tres",
            GodotFileName = $"{material.Name}.tres",
            ExportFormat = "tres",
            MaterialParameters = parameters
        };
    }

    /// <summary>
    /// Extracts a UAnimSequence to glTF format.
    /// The animation is exported with its skeleton reference for correct bone mapping.
    /// </summary>
    private async Task<ImportedAssetRecord> ExtractAnimation(UAnimSequence anim, string relativePath)
    {
        string outputDir = ProjectSettings.GlobalizePath(_config.AnimationOutputDir);
        string subDir = Path.GetDirectoryName(relativePath)?.Replace('\\', '/') ?? "";
        string fullOutputDir = Path.Combine(outputDir, subDir);
        Directory.CreateDirectory(fullOutputDir);

        var exporter = new Exporter(anim, new ExporterOptions
        {
            MeshFormat = EMeshFormat.Gltf2,
            AnimFormat = EAnimFormat.Gltf2
        });

        bool success = exporter.TryWriteToDir(
            new DirectoryInfo(fullOutputDir),
            out string label,
            out string savedFilePath
        );

        if (!success)
            return null;

        return new ImportedAssetRecord
        {
            UEAssetPath = relativePath,
            UEAssetName = anim.Name,
            Type = AssetType.AnimSequence,
            UEClassName = "UAnimSequence",
            GodotResourcePath = ToResPath(savedFilePath, outputDir, _config.AnimationOutputDir),
            GodotFileName = Path.GetFileName(savedFilePath),
            ExportFormat = "glb",
            FileSizeBytes = new FileInfo(savedFilePath).Length,
            HasAnimations = true,
            AnimationCount = 1
        };
    }

    /// <summary>
    /// Extracts a USoundWave to its native audio format (OGG/WAV).
    /// </summary>
    private async Task<ImportedAssetRecord> ExtractAudio(USoundWave sound, string relativePath)
    {
        string outputDir = ProjectSettings.GlobalizePath(_config.AudioOutputDir);
        string subDir = Path.GetDirectoryName(relativePath)?.Replace('\\', '/') ?? "";
        string fullOutputDir = Path.Combine(outputDir, subDir);
        Directory.CreateDirectory(fullOutputDir);

        sound.Decode(true, out string audioFormat, out byte[] data);
        if (data == null || data.Length == 0)
        {
            GD.PrintErr($"[CUE4Parse] Failed to decode audio: {sound.Name}");
            return null;
        }

        string ext = audioFormat?.ToLowerInvariant() ?? "ogg";
        string outputPath = Path.Combine(fullOutputDir, $"{sound.Name}.{ext}");
        await File.WriteAllBytesAsync(outputPath, data);

        return new ImportedAssetRecord
        {
            UEAssetPath = relativePath,
            UEAssetName = sound.Name,
            Type = AssetType.Audio,
            UEClassName = "USoundWave",
            GodotResourcePath = ToResPath(outputPath, outputDir, _config.AudioOutputDir),
            GodotFileName = $"{sound.Name}.{ext}",
            ExportFormat = ext,
            FileSizeBytes = data.Length
        };
    }

    /// <summary>
    /// Converts an absolute filesystem path to a res:// path.
    /// </summary>
    private static string ToResPath(string absolutePath, string outputDirAbsolute, string outputDirRes)
    {
        string relative = Path.GetRelativePath(outputDirAbsolute, absolutePath).Replace('\\', '/');
        return $"{outputDirRes}/{relative}";
    }

    /// <summary>
    /// Parses the UE version string from config into a CUE4Parse EGame enum.
    /// </summary>
    private static EGame ParseGameVersion(string version)
    {
        return version switch
        {
            "GAME_UE5_0" => EGame.GAME_UE5_0,
            "GAME_UE5_1" => EGame.GAME_UE5_1,
            "GAME_UE5_2" => EGame.GAME_UE5_2,
            "GAME_UE5_3" => EGame.GAME_UE5_3,
            "GAME_UE5_4" => EGame.GAME_UE5_4,
            "GAME_UE5_5" => EGame.GAME_UE5_5,
            "GAME_UE4_27" => EGame.GAME_UE4_27,
            _ => EGame.GAME_UE5_4
        };
    }
}
#endif
