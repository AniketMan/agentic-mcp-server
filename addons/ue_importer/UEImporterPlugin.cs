#if TOOLS
using Godot;

namespace UEImporter;

/// <summary>
/// Main EditorPlugin entry point. Adds "Import from Unreal" to the Project menu.
/// When triggered, opens a folder dialog for the user to select a UE Content directory,
/// then hands off to ContentFolderWalker for asset discovery and extraction.
/// </summary>
[Tool]
public partial class UEImporterPlugin : EditorPlugin
{
    private const string MenuName = "Import from Unreal...";

    private EditorFileDialog _folderDialog;
    private ImportProgressWindow _progressWindow;
    private ContentFolderWalker _walker;
    private ImportConfig _config;

    public override void _EnterTree()
    {
        AddToolMenuItem(MenuName, new Callable(this, MethodName.OnImportRequested));
        _config = new ImportConfig();
        GD.Print("[UE Importer] Plugin loaded. Use Project > Tools > Import from Unreal...");
    }

    public override void _ExitTree()
    {
        RemoveToolMenuItem(MenuName);
        _folderDialog?.QueueFree();
        _progressWindow?.QueueFree();
        GD.Print("[UE Importer] Plugin unloaded.");
    }

    /// <summary>
    /// Called when user clicks "Import from Unreal..." in the Tools menu.
    /// Opens a folder selection dialog.
    /// </summary>
    private void OnImportRequested()
    {
        _folderDialog?.QueueFree();

        _folderDialog = new EditorFileDialog
        {
            FileMode = EditorFileDialog.FileModeEnum.OpenDir,
            Title = "Select Unreal Engine Content Folder",
            Access = EditorFileDialog.AccessEnum.Filesystem
        };
        _folderDialog.DirSelected += OnContentFolderSelected;
        EditorInterface.Singleton.GetBaseControl().AddChild(_folderDialog);
        _folderDialog.PopupCentered(new Vector2I(800, 600));
    }

    /// <summary>
    /// Called after the user selects the UE Content folder.
    /// Validates the folder, then launches the import pipeline.
    /// </summary>
    private void OnContentFolderSelected(string path)
    {
        _folderDialog.QueueFree();
        _folderDialog = null;

        if (!ContentFolderValidator.IsValidContentFolder(path))
        {
            GD.PrintErr($"[UE Importer] Invalid Content folder: {path}");
            GD.PrintErr("[UE Importer] Expected a folder containing .uasset files.");
            ShowError("Invalid Content Folder",
                "The selected folder does not appear to be a valid Unreal Engine Content directory.\n" +
                "Please select a folder containing .uasset files.");
            return;
        }

        GD.Print($"[UE Importer] Content folder selected: {path}");

        // Show progress window
        _progressWindow?.QueueFree();
        _progressWindow = new ImportProgressWindow();
        EditorInterface.Singleton.GetBaseControl().AddChild(_progressWindow);
        _progressWindow.PopupCentered(new Vector2I(600, 400));

        // Start the import pipeline
        _walker = new ContentFolderWalker(path, _config, _progressWindow);
        _walker.StartImport();
    }

    /// <summary>
    /// Displays an error dialog to the user.
    /// </summary>
    private void ShowError(string title, string message)
    {
        var dialog = new AcceptDialog
        {
            Title = title,
            DialogText = message
        };
        EditorInterface.Singleton.GetBaseControl().AddChild(dialog);
        dialog.PopupCentered();
    }
}
#endif
