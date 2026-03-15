#if TOOLS
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using Godot;

namespace UEImporter;

/// <summary>
/// Generates a JSON manifest of all imported assets.
/// This manifest is the primary input for the agentic pipeline --
/// agents read it to understand what assets are available, where they are,
/// and how they map from the original UE project to the Godot project.
/// </summary>
public class ManifestGenerator
{
    private readonly ImportConfig _config;

    public ManifestGenerator(ImportConfig config)
    {
        _config = config;
    }

    /// <summary>
    /// Writes the import manifest JSON file.
    /// </summary>
    public void Generate(List<ImportedAssetRecord> records)
    {
        var manifest = new ImportManifest
        {
            Version = "1.0.0",
            GeneratedAt = DateTime.UtcNow.ToString("o"),
            SourceEngine = "Unreal Engine",
            SourceVersion = _config.UEVersion,
            TargetEngine = "Godot Engine",
            TotalAssets = records.Count,
            Summary = new AssetSummary
            {
                StaticMeshes = records.Count(r => r.Type == AssetType.StaticMesh),
                SkeletalMeshes = records.Count(r => r.Type == AssetType.SkeletalMesh),
                Textures = records.Count(r => r.Type == AssetType.Texture),
                Materials = records.Count(r => r.Type == AssetType.Material),
                Animations = records.Count(r => r.Type == AssetType.AnimSequence),
                Audio = records.Count(r => r.Type == AssetType.Audio)
            },
            Assets = records.Select(r => new ManifestAssetEntry
            {
                UEPath = r.UEAssetPath,
                UEName = r.UEAssetName,
                UEClass = r.UEClassName,
                Type = r.Type.ToString(),
                GodotPath = r.GodotResourcePath,
                FileName = r.GodotFileName,
                Format = r.ExportFormat,
                SizeBytes = r.FileSizeBytes,
                HasSkeleton = r.HasSkeleton,
                HasAnimations = r.HasAnimations,
                AnimationCount = r.AnimationCount,
                MaterialSlots = r.MaterialSlots,
                TextureReferences = r.TextureReferences,
                MaterialParameters = r.MaterialParameters != null ? new ManifestMaterialParams
                {
                    BaseColor = r.MaterialParameters.BaseColor,
                    Metallic = r.MaterialParameters.Metallic,
                    Roughness = r.MaterialParameters.Roughness,
                    Specular = r.MaterialParameters.Specular,
                    EmissiveStrength = r.MaterialParameters.EmissiveStrength,
                    EmissiveColor = r.MaterialParameters.EmissiveColor,
                    Opacity = r.MaterialParameters.Opacity,
                    IsTranslucent = r.MaterialParameters.IsTranslucent,
                    BaseColorTexture = r.MaterialParameters.BaseColorTexture,
                    NormalTexture = r.MaterialParameters.NormalTexture,
                    MetallicTexture = r.MaterialParameters.MetallicTexture,
                    RoughnessTexture = r.MaterialParameters.RoughnessTexture,
                    EmissiveTexture = r.MaterialParameters.EmissiveTexture,
                    OcclusionTexture = r.MaterialParameters.OcclusionTexture,
                    OpacityTexture = r.MaterialParameters.OpacityTexture
                } : null
            }).ToList(),
            // UE-to-Godot path mapping for quick lookups
            PathMap = records.ToDictionary(
                r => r.UEAssetPath,
                r => r.GodotResourcePath
            )
        };

        string outputDir = ProjectSettings.GlobalizePath(_config.ManifestOutputDir);
        Directory.CreateDirectory(outputDir);
        string outputPath = Path.Combine(outputDir, _config.ManifestFilename);

        var options = new JsonSerializerOptions
        {
            WriteIndented = true,
            DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase
        };

        string json = JsonSerializer.Serialize(manifest, options);
        File.WriteAllText(outputPath, json);

        GD.Print($"[UE Importer] Manifest written to: {_config.ManifestOutputDir}/{_config.ManifestFilename}");
        GD.Print($"[UE Importer] {manifest.TotalAssets} assets mapped.");
    }
}

// --- Manifest JSON schema classes ---

public class ImportManifest
{
    public string Version { get; set; }
    public string GeneratedAt { get; set; }
    public string SourceEngine { get; set; }
    public string SourceVersion { get; set; }
    public string TargetEngine { get; set; }
    public int TotalAssets { get; set; }
    public AssetSummary Summary { get; set; }
    public List<ManifestAssetEntry> Assets { get; set; }
    public Dictionary<string, string> PathMap { get; set; }
}

public class AssetSummary
{
    public int StaticMeshes { get; set; }
    public int SkeletalMeshes { get; set; }
    public int Textures { get; set; }
    public int Materials { get; set; }
    public int Animations { get; set; }
    public int Audio { get; set; }
}

public class ManifestAssetEntry
{
    public string UEPath { get; set; }
    public string UEName { get; set; }
    public string UEClass { get; set; }
    public string Type { get; set; }
    public string GodotPath { get; set; }
    public string FileName { get; set; }
    public string Format { get; set; }
    public long SizeBytes { get; set; }
    public bool HasSkeleton { get; set; }
    public bool HasAnimations { get; set; }
    public int AnimationCount { get; set; }
    public string[] MaterialSlots { get; set; }
    public string[] TextureReferences { get; set; }
    public ManifestMaterialParams MaterialParameters { get; set; }
}

public class ManifestMaterialParams
{
    public float[] BaseColor { get; set; }
    public float Metallic { get; set; }
    public float Roughness { get; set; }
    public float Specular { get; set; }
    public float EmissiveStrength { get; set; }
    public float[] EmissiveColor { get; set; }
    public float Opacity { get; set; }
    public bool IsTranslucent { get; set; }
    public string BaseColorTexture { get; set; }
    public string NormalTexture { get; set; }
    public string MetallicTexture { get; set; }
    public string RoughnessTexture { get; set; }
    public string EmissiveTexture { get; set; }
    public string OcclusionTexture { get; set; }
    public string OpacityTexture { get; set; }
}
#endif
