#if TOOLS
using Godot;

namespace UEImporter;

/// <summary>
/// Progress dialog shown during import. Displays current status,
/// asset being processed, and a progress bar.
/// </summary>
[Tool]
public partial class ImportProgressWindow : AcceptDialog
{
    private Label _statusLabel;
    private Label _assetLabel;
    private ProgressBar _progressBar;
    private Label _countLabel;

    private int _total;
    private int _current;

    public ImportProgressWindow()
    {
        Title = "Importing from Unreal Engine";
        Exclusive = true;
        Unresizable = false;

        var vbox = new VBoxContainer();
        vbox.SetAnchorsPreset(Control.LayoutPreset.FullRect);
        vbox.AddThemeConstantOverride("separation", 12);

        _statusLabel = new Label { Text = "Initializing..." };
        _statusLabel.AddThemeFontSizeOverride("font_size", 16);
        vbox.AddChild(_statusLabel);

        _progressBar = new ProgressBar
        {
            MinValue = 0,
            MaxValue = 100,
            Value = 0,
            CustomMinimumSize = new Vector2(500, 24)
        };
        vbox.AddChild(_progressBar);

        _countLabel = new Label { Text = "0 / 0" };
        vbox.AddChild(_countLabel);

        _assetLabel = new Label
        {
            Text = "",
            AutowrapMode = TextServer.AutowrapMode.WordSmart
        };
        _assetLabel.AddThemeColorOverride("font_color", new Color(0.7f, 0.7f, 0.7f));
        vbox.AddChild(_assetLabel);

        AddChild(vbox);
    }

    public void SetStatus(string status)
    {
        _statusLabel.Text = status;
    }

    public void SetTotal(int total)
    {
        _total = total;
        _current = 0;
        _progressBar.MaxValue = total;
        _progressBar.Value = 0;
        _countLabel.Text = $"0 / {total}";
    }

    public void SetCurrentAsset(string assetName)
    {
        _assetLabel.Text = assetName;
    }

    public void IncrementProgress()
    {
        _current++;
        _progressBar.Value = _current;
        _countLabel.Text = $"{_current} / {_total}";
    }
}
#endif
