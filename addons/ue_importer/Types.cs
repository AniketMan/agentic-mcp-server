#if TOOLS
namespace UEImporter;

/// <summary>
/// Asset type categories for UE Content folder parsing.
/// </summary>
public enum AssetType
{
    Unknown,
    StaticMesh,
    SkeletalMesh,
    Texture,
    Material,
    AnimSequence,
    Audio
}

/// <summary>
/// Record of a successfully imported asset.
/// Used for manifest generation and agent scene reconstruction.
/// </summary>
public class ImportedAssetRecord
{
    // Source info
    public string UEAssetPath { get; set; }        // Original .uasset path relative to Content/
    public string UEAssetName { get; set; }         // Original asset name (e.g., "SM_Chair")
    public AssetType Type { get; set; }             // Detected asset type
    public string UEClassName { get; set; }         // Actual UE class (e.g., "UStaticMesh")

    // Godot output info
    public string GodotResourcePath { get; set; }   // res:// path to the imported resource
    public string GodotFileName { get; set; }        // Output filename (e.g., "SM_Chair.glb")
    public string ExportFormat { get; set; }         // Format used (glb, png, tres, etc.)

    // Metadata
    public long FileSizeBytes { get; set; }
    public bool HasSkeleton { get; set; }
    public bool HasAnimations { get; set; }
    public int AnimationCount { get; set; }
    public string[] MaterialSlots { get; set; }      // Material slot names for meshes
    public string[] TextureReferences { get; set; }  // Texture paths referenced by materials

    // Material parameters (for material assets)
    public MaterialParams MaterialParameters { get; set; }
}

/// <summary>
/// PBR material parameter values extracted from UE materials.
/// These map directly to Godot's StandardMaterial3D properties.
/// </summary>
public class MaterialParams
{
    public float[] BaseColor { get; set; }           // RGBA [0-1]
    public float Metallic { get; set; }
    public float Roughness { get; set; }
    public float Specular { get; set; }
    public float EmissiveStrength { get; set; }
    public float[] EmissiveColor { get; set; }       // RGB [0-1]
    public float Opacity { get; set; } = 1.0f;
    public bool IsTranslucent { get; set; }

    // Texture map references (UE asset paths)
    public string BaseColorTexture { get; set; }
    public string NormalTexture { get; set; }
    public string MetallicTexture { get; set; }
    public string RoughnessTexture { get; set; }
    public string EmissiveTexture { get; set; }
    public string OcclusionTexture { get; set; }
    public string OpacityTexture { get; set; }
}
#endif
