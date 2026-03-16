# ===========================================================================
#  AgenticMCP File Watchdog
#  Monitors all tracked source files against a SHA256 manifest.
#  Alerts immediately when any file is modified, created, or deleted.
#
#  Usage:
#    .\Watch-PluginFiles.ps1                   # default 30s poll interval
#    .\Watch-PluginFiles.ps1 -Interval 10      # 10s poll interval
#    .\Watch-PluginFiles.ps1 -Once             # single check, no loop
#
#  The manifest (manifest.txt) is generated alongside this script.
#  To refresh the manifest after intentional changes:
#    .\Watch-PluginFiles.ps1 -Refresh
# ===========================================================================

param(
    [int]$Interval = 30,
    [switch]$Once,
    [switch]$Refresh
)

$ErrorActionPreference = "Continue"

# Resolve paths
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$ManifestPath = Join-Path $ScriptDir "manifest.txt"
$LogPath = Join-Path $ScriptDir "watchdog.log"
$Extensions = @("*.cpp", "*.h", "*.cs", "*.js", "*.json", "*.bat", "*.uplugin")

# ---------------------------------------------------------------------------
# Generate or refresh the manifest
# ---------------------------------------------------------------------------
function New-Manifest {
    Write-Host "[WATCHDOG] Generating fresh manifest from: $RepoRoot" -ForegroundColor Cyan
    $lines = @()
    foreach ($ext in $Extensions) {
        Get-ChildItem -Path $RepoRoot -Recurse -Filter $ext -File |
            Where-Object { $_.FullName -notmatch "node_modules|\.git[\\\/]|watchdog" } |
            ForEach-Object {
                $hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash
                $rel  = $_.FullName.Substring($RepoRoot.Length + 1)
                $lines += "$hash|$rel|$($_.Length)|$($_.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
            }
    }
    $lines | Out-File -Encoding UTF8 -FilePath $ManifestPath
    Write-Host "[WATCHDOG] Manifest written: $($lines.Count) files tracked." -ForegroundColor Green
    return $lines.Count
}

# ---------------------------------------------------------------------------
# Load manifest into a hashtable: relative_path -> hash
# ---------------------------------------------------------------------------
function Read-Manifest {
    if (-not (Test-Path $ManifestPath)) {
        Write-Host "[WATCHDOG] ERROR: manifest.txt not found at $ManifestPath" -ForegroundColor Red
        Write-Host "[WATCHDOG] Run with -Refresh to generate it." -ForegroundColor Yellow
        return $null
    }
    $map = @{}
    Get-Content $ManifestPath | ForEach-Object {
        $parts = $_ -split '\|', 4
        if ($parts.Count -ge 2) {
            # Handle both absolute and relative paths in manifest
            $filePath = $parts[1].Trim()
            if ([System.IO.Path]::IsPathRooted($filePath)) {
                $filePath = $filePath.Substring($RepoRoot.Length + 1)
            }
            $map[$filePath] = $parts[0].Trim()
        }
    }
    return $map
}

# ---------------------------------------------------------------------------
# Compare current files against manifest
# ---------------------------------------------------------------------------
function Compare-Against-Manifest {
    param([hashtable]$Manifest)

    $changes = @()
    $currentFiles = @{}

    foreach ($ext in $Extensions) {
        Get-ChildItem -Path $RepoRoot -Recurse -Filter $ext -File |
            Where-Object { $_.FullName -notmatch "node_modules|\.git[\\\/]|watchdog" } |
            ForEach-Object {
                $rel = $_.FullName.Substring($RepoRoot.Length + 1)
                $currentFiles[$rel] = $true

                if ($Manifest.ContainsKey($rel)) {
                    # File exists in manifest -- check hash
                    $currentHash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash
                    if ($currentHash -ne $Manifest[$rel]) {
                        $changes += [PSCustomObject]@{
                            Status   = "MODIFIED"
                            File     = $rel
                            OldHash  = $Manifest[$rel].Substring(0, 12) + "..."
                            NewHash  = $currentHash.Substring(0, 12) + "..."
                            Modified = $_.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')
                        }
                    }
                } else {
                    # New file not in manifest
                    $changes += [PSCustomObject]@{
                        Status   = "NEW FILE"
                        File     = $rel
                        OldHash  = "N/A"
                        NewHash  = "N/A"
                        Modified = $_.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')
                    }
                }
            }
    }

    # Check for deleted files
    foreach ($rel in $Manifest.Keys) {
        if (-not $currentFiles.ContainsKey($rel)) {
            $changes += [PSCustomObject]@{
                Status   = "DELETED"
                File     = $rel
                OldHash  = $Manifest[$rel].Substring(0, 12) + "..."
                NewHash  = "N/A"
                Modified = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
            }
        }
    }

    return $changes
}

# ---------------------------------------------------------------------------
# Log and alert
# ---------------------------------------------------------------------------
function Write-Alert {
    param([array]$Changes)

    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"

    # Console output with color
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor Red
    Write-Host "  WATCHDOG ALERT: $($Changes.Count) UNAUTHORIZED CHANGE(S)" -ForegroundColor Red
    Write-Host "  Detected at: $timestamp" -ForegroundColor Red
    Write-Host "============================================================" -ForegroundColor Red

    foreach ($c in $Changes) {
        $color = switch ($c.Status) {
            "MODIFIED"  { "Yellow" }
            "NEW FILE"  { "Cyan" }
            "DELETED"   { "Magenta" }
            default     { "White" }
        }
        Write-Host "  [$($c.Status)] $($c.File)" -ForegroundColor $color
        if ($c.Status -eq "MODIFIED") {
            Write-Host "    Was: $($c.OldHash)  Now: $($c.NewHash)  At: $($c.Modified)" -ForegroundColor DarkGray
        }
    }

    Write-Host "============================================================" -ForegroundColor Red
    Write-Host ""

    # Also append to log file
    $logEntry = "[$timestamp] $($Changes.Count) changes detected:`n"
    foreach ($c in $Changes) {
        $logEntry += "  [$($c.Status)] $($c.File)`n"
    }
    $logEntry += "---`n"
    Add-Content -Path $LogPath -Value $logEntry -Encoding UTF8

    # Windows toast notification (best effort)
    try {
        [System.Reflection.Assembly]::LoadWithPartialName("System.Windows.Forms") | Out-Null
        $balloon = New-Object System.Windows.Forms.NotifyIcon
        $balloon.Icon = [System.Drawing.SystemIcons]::Warning
        $balloon.BalloonTipIcon = "Warning"
        $balloon.BalloonTipTitle = "AgenticMCP Watchdog"
        $balloon.BalloonTipText = "$($Changes.Count) file(s) changed without authorization!"
        $balloon.Visible = $true
        $balloon.ShowBalloonTip(5000)
        Start-Sleep -Seconds 6
        $balloon.Dispose()
    } catch {
        # Toast failed silently -- console alert is sufficient
    }
}

# ===========================================================================
# Main
# ===========================================================================

Write-Host ""
Write-Host "  AgenticMCP File Watchdog" -ForegroundColor Cyan
Write-Host "  Repo: $RepoRoot" -ForegroundColor DarkGray
Write-Host "  Manifest: $ManifestPath" -ForegroundColor DarkGray
Write-Host ""

# Handle -Refresh flag
if ($Refresh) {
    New-Manifest
    Write-Host "[WATCHDOG] Manifest refreshed. Exiting." -ForegroundColor Green
    exit 0
}

# Load manifest
$manifest = Read-Manifest
if ($null -eq $manifest) { exit 1 }

Write-Host "[WATCHDOG] Loaded manifest: $($manifest.Count) files tracked." -ForegroundColor Green

if ($Once) {
    # Single check mode
    $changes = Compare-Against-Manifest -Manifest $manifest
    if ($changes.Count -gt 0) {
        Write-Alert -Changes $changes
    } else {
        Write-Host "[WATCHDOG] All files match manifest. No changes detected." -ForegroundColor Green
    }
    exit 0
}

# Polling loop
Write-Host "[WATCHDOG] Polling every ${Interval}s. Press Ctrl+C to stop." -ForegroundColor DarkGray
Write-Host ""

$checkCount = 0
while ($true) {
    $checkCount++
    $changes = Compare-Against-Manifest -Manifest $manifest

    if ($changes.Count -gt 0) {
        Write-Alert -Changes $changes
    } else {
        # Quiet status every 10 checks
        if ($checkCount % 10 -eq 0) {
            Write-Host "[WATCHDOG] Check #$checkCount - All clear. ($($manifest.Count) files)" -ForegroundColor DarkGray
        }
    }

    Start-Sleep -Seconds $Interval
}
