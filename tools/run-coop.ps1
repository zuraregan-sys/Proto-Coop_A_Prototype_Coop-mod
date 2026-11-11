param(
    [int]$Width = 960,
    [int]$Height = 540,
    [switch]$NoPosition
)

# Launches the two co-op test instances as real windows and puts them side by
# side, so neither one is minimised (a minimised game pauses itself, and its
# avatar freezes in the other window).

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Dirs = @((Join-Path $Root "TestInstances\0"), (Join-Path $Root "TestInstances\1"))
$FastOptions = @("windowed", "SkipMovies", "NoMovie", "NoFELoadingPrompt", "NoAutoSave", "NoSleepOnLostFocus")

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class CoopWindow
{
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int h2, bool repaint);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@

$Processes = @()
for ($i = 0; $i -lt $Dirs.Count; $i++)
{
    $exe = Join-Path $Dirs[$i] "prototypef.exe"
    if (-not (Test-Path $exe)) { throw "missing $exe" }
    # The engine reads these from args.txt at start: no intro movies or legal
    # screen, no "press a key" after loading, no autosave and no sleep on focus.
    Set-Content -Path (Join-Path $Dirs[$i] "args.txt") -Value ($FastOptions -join "`n") -Encoding ASCII
    $Processes += Start-Process -FilePath $exe -WorkingDirectory $Dirs[$i] `
        -ArgumentList $FastOptions -PassThru
}

if ($NoPosition) { return }

for ($i = 0; $i -lt $Processes.Count; $i++)
{
    $handle = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 120; $attempt++)
    {
        Start-Sleep -Milliseconds 500
        $Processes[$i].Refresh()
        if ($Processes[$i].HasExited) { break }
        if ($Processes[$i].MainWindowHandle -ne [IntPtr]::Zero)
        {
            $handle = $Processes[$i].MainWindowHandle
            break
        }
    }
    if ($handle -eq [IntPtr]::Zero)
    {
        Write-Warning "no window for instance $i"
        continue
    }
    [CoopWindow]::MoveWindow($handle, $i * $Width, 0, $Width, $Height, $true) | Out-Null
    Write-Host "instance $i -> $($i * $Width),0 ${Width}x${Height}"
}
