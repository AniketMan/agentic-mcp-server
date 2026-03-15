#if TOOLS
using System.IO;
using System.Linq;

namespace UEImporter;

/// <summary>
/// Validates that a selected folder is a legitimate UE Content directory.
/// Checks for the presence of .uasset files and expected folder structure.
/// </summary>
public static class ContentFolderValidator
{
    /// <summary>
    /// Returns true if the folder contains .uasset files (directly or in subdirectories).
    /// Also accepts the parent project folder if it contains a Content/ subfolder.
    /// </summary>
    public static bool IsValidContentFolder(string path)
    {
        if (string.IsNullOrEmpty(path) || !Directory.Exists(path))
            return false;

        // If user selected the project root (contains .uproject), redirect to Content/
        string contentPath = ResolveContentPath(path);
        if (contentPath == null)
            return false;

        // Check for at least one .uasset file in the tree
        return Directory.EnumerateFiles(contentPath, "*.uasset", SearchOption.AllDirectories).Any();
    }

    /// <summary>
    /// Resolves the actual Content folder path.
    /// If the user selected a .uproject parent, returns the Content/ subfolder.
    /// If the user selected the Content folder directly, returns it as-is.
    /// </summary>
    public static string ResolveContentPath(string path)
    {
        if (string.IsNullOrEmpty(path) || !Directory.Exists(path))
            return null;

        // Direct Content folder selection
        if (Path.GetFileName(path) == "Content")
            return path;

        // User selected project root -- look for Content/ subfolder
        string contentSubfolder = Path.Combine(path, "Content");
        if (Directory.Exists(contentSubfolder))
            return contentSubfolder;

        // Check if there are .uasset files directly in this folder (loose Content structure)
        if (Directory.EnumerateFiles(path, "*.uasset", SearchOption.TopDirectoryOnly).Any())
            return path;

        // Recurse one level to find a Content folder
        foreach (string dir in Directory.GetDirectories(path))
        {
            if (Path.GetFileName(dir) == "Content")
                return dir;
        }

        return null;
    }
}
#endif
