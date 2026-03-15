#if TOOLS
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Godot;

namespace UEImporter;

/// <summary>
/// Converts extracted material data into native Godot StandardMaterial3D .tres files.
/// Meshes and textures are handled natively by Godot's glTF/PNG importers --
/// this class only handles assets that need explicit Godot resource generation.
/// </summary>
public static class GodotResourceBuilder
{
    /// <summary>
    /// Generates a StandardMaterial3D .tres file from extracted UE material parameters.
    /// Maps UE PBR values directly to Godot's material system.
    /// </summary>
    public static bool BuildMaterial(ImportedAssetRecord record, ImportConfig config,
        Dictionary<string, ImportedAssetRecord> textureMap)
    {
        if (record.Type != AssetType.Material || record.MaterialParameters == null)
            return false;

        var p = record.MaterialParameters;
        string outputDir = ProjectSettings.GlobalizePath(config.MaterialOutputDir);
        string outputPath = Path.Combine(outputDir, $"{record.UEAssetName}.tres");
        Directory.CreateDirectory(Path.GetDirectoryName(outputPath));

        // Build the .tres file content
        // Godot's text resource format for StandardMaterial3D
        var lines = new List<string>();

        lines.Add("[gd_resource type=\"StandardMaterial3D\" format=3]");
        lines.Add("");
        lines.Add("[resource]");

        // Albedo / Base Color
        if (p.BaseColor != null && p.BaseColor.Length >= 3)
        {
            float r = p.BaseColor[0], g = p.BaseColor[1], b = p.BaseColor[2];
            float a = p.BaseColor.Length >= 4 ? p.BaseColor[3] : 1.0f;
            lines.Add($"albedo_color = Color({r:F4}, {g:F4}, {b:F4}, {a:F4})");
        }

        // Albedo texture
        string albedoTexPath = ResolveTexturePath(p.BaseColorTexture, textureMap, config);
        if (albedoTexPath != null)
            lines.Add($"albedo_texture = ExtResource(\"{albedoTexPath}\")");

        // Metallic
        lines.Add($"metallic = {p.Metallic:F4}");
        string metallicTexPath = ResolveTexturePath(p.MetallicTexture, textureMap, config);
        if (metallicTexPath != null)
            lines.Add($"metallic_texture = ExtResource(\"{metallicTexPath}\")");

        // Roughness
        lines.Add($"roughness = {p.Roughness:F4}");
        string roughnessTexPath = ResolveTexturePath(p.RoughnessTexture, textureMap, config);
        if (roughnessTexPath != null)
            lines.Add($"roughness_texture = ExtResource(\"{roughnessTexPath}\")");

        // Specular
        if (p.Specular > 0)
            lines.Add($"metallic_specular = {p.Specular:F4}");

        // Normal map
        string normalTexPath = ResolveTexturePath(p.NormalTexture, textureMap, config);
        if (normalTexPath != null)
        {
            lines.Add("normal_enabled = true");
            lines.Add($"normal_texture = ExtResource(\"{normalTexPath}\")");
        }

        // Emission
        if (p.EmissiveStrength > 0 || (p.EmissiveColor != null && p.EmissiveColor.Any(c => c > 0)))
        {
            lines.Add("emission_enabled = true");
            if (p.EmissiveColor != null && p.EmissiveColor.Length >= 3)
                lines.Add($"emission = Color({p.EmissiveColor[0]:F4}, {p.EmissiveColor[1]:F4}, {p.EmissiveColor[2]:F4}, 1.0)");
            lines.Add($"emission_energy_multiplier = {p.EmissiveStrength:F4}");

            string emissiveTexPath = ResolveTexturePath(p.EmissiveTexture, textureMap, config);
            if (emissiveTexPath != null)
                lines.Add($"emission_texture = ExtResource(\"{emissiveTexPath}\")");
        }

        // AO
        string aoTexPath = ResolveTexturePath(p.OcclusionTexture, textureMap, config);
        if (aoTexPath != null)
        {
            lines.Add("ao_enabled = true");
            lines.Add($"ao_texture = ExtResource(\"{aoTexPath}\")");
        }

        // Transparency
        if (p.IsTranslucent || p.Opacity < 1.0f)
        {
            lines.Add("transparency = 1"); // ALPHA
            if (p.Opacity < 1.0f)
                lines.Add($"albedo_color = Color(1.0, 1.0, 1.0, {p.Opacity:F4})");
        }

        File.WriteAllText(outputPath, string.Join("\n", lines) + "\n");
        GD.Print($"[UE Importer] Material written: {record.UEAssetName}.tres");
        return true;
    }

    /// <summary>
    /// Resolves a UE texture path to its corresponding Godot res:// path
    /// using the texture import map.
    /// </summary>
    private static string ResolveTexturePath(string ueTexturePath,
        Dictionary<string, ImportedAssetRecord> textureMap, ImportConfig config)
    {
        if (string.IsNullOrEmpty(ueTexturePath))
            return null;

        // Try exact match first
        if (textureMap.TryGetValue(ueTexturePath, out var record))
            return record.GodotResourcePath;

        // Try matching by asset name (strip path, match filename)
        string texName = Path.GetFileNameWithoutExtension(ueTexturePath);
        var match = textureMap.Values.FirstOrDefault(r => r.UEAssetName == texName);
        return match?.GodotResourcePath;
    }
}
#endif
