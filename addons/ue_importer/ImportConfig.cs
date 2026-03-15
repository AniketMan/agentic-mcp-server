#if TOOLS
namespace UEImporter;

/// <summary>
/// Configuration for the import pipeline.
/// Controls which asset types to import and where to place them.
/// </summary>
public class ImportConfig
{
    // Output directories (relative to res://)
    public string MeshOutputDir { get; set; } = "res://imported/meshes";
    public string TextureOutputDir { get; set; } = "res://imported/textures";
    public string MaterialOutputDir { get; set; } = "res://imported/materials";
    public string AnimationOutputDir { get; set; } = "res://imported/animations";
    public string AudioOutputDir { get; set; } = "res://imported/audio";
    public string ManifestOutputDir { get; set; } = "res://imported";

    // Asset type toggles
    public bool ImportMeshes { get; set; } = true;
    public bool ImportTextures { get; set; } = true;
    public bool ImportMaterials { get; set; } = true;
    public bool ImportAnimations { get; set; } = true;
    public bool ImportAudio { get; set; } = false; // User said they'll add audio manually

    // Export format preferences
    public string MeshFormat { get; set; } = "glb"; // glb or gltf
    public string TextureFormat { get; set; } = "png"; // png or tga

    // UE version hint (helps CUE4Parse parse correctly)
    public string UEVersion { get; set; } = "GAME_UE5_4";

    // Manifest generation
    public bool GenerateManifest { get; set; } = true;
    public string ManifestFilename { get; set; } = "import_manifest.json";
}
#endif
