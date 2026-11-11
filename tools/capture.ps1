param(
    [int]$Seconds = 3,
    [int]$Fps = 8,
    [string]$Out = "",
    [int]$ProcessId = 0,
    [int]$Index = -1
)

# Captures the Prototype test windows at a fixed rate. By default every window
# is grabbed on the same tick and composited side by side into one frame, so a
# contact sheet shows what BOTH instances were doing at the same instant. This
# matters because an effect can appear in one window and not the other.
# -Index N keeps the old behaviour of capturing a single window.

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not $Out) { $Out = Join-Path $Root "local\capture" }

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class CaptureWindow
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
}
"@
Add-Type -AssemblyName System.Drawing

function Get-InstanceRole([string]$path)
{
    if (-not $path) { return "?" }
    $dir = Split-Path -Parent $path
    $ini = Join-Path $dir "coop.ini"
    $role = ""
    if (Test-Path $ini)
    {
        $match = Select-String -Path $ini -Pattern '^\s*role\s*=\s*(\w+)' | Select-Object -First 1
        if ($match) { $role = $match.Matches[0].Groups[1].Value.ToUpper() }
    }
    $name = Split-Path -Leaf $dir
    if ($role) { return "$name $role" }
    return $name
}

$windows = @(Get-Process prototypef -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero })
if ($ProcessId -ne 0) { $windows = @($windows | Where-Object { $_.Id -eq $ProcessId }) }
if ($windows.Count -eq 0) { throw "no Prototype window found" }

$entries = @(foreach ($process in $windows)
{
    $rect = [CaptureWindow+RECT]::new()
    [CaptureWindow]::GetWindowRect($process.MainWindowHandle, [ref]$rect) | Out-Null
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) { continue }
    [pscustomobject]@{
        Process = $process
        Rect = $rect
        Width = $width
        Height = $height
        Label = "$(Get-InstanceRole $process.Path) pid $($process.Id)"
    }
})
if ($entries.Count -eq 0) { throw "no capturable Prototype window" }

$entries = @($entries | Sort-Object { $_.Rect.Left }, { $_.Rect.Top })
if ($Index -ge 0)
{
    if ($Index -ge $entries.Count) { throw "window index $Index out of range ($($entries.Count) windows)" }
    $entries = @($entries[$Index])
}

$header = 20
$totalWidth = 0
$maxHeight = 0
foreach ($entry in $entries)
{
    $totalWidth += $entry.Width
    if ($entry.Height -gt $maxHeight) { $maxHeight = $entry.Height }
}

New-Item -ItemType Directory -Force -Path $Out | Out-Null
Get-ChildItem $Out -Filter "frame_*.png" -ErrorAction SilentlyContinue | Remove-Item -Force
Remove-Item (Join-Path $Out "frames.csv") -Force -ErrorAction SilentlyContinue

$count = [int]($Seconds * $Fps)
$interval = 1000.0 / $Fps
$watch = [System.Diagnostics.Stopwatch]::StartNew()
$font = New-Object System.Drawing.Font("Consolas", 12, [System.Drawing.FontStyle]::Bold)
$labels = ($entries | ForEach-Object { $_.Label }) -join "   |   "
Set-Content -Path (Join-Path $Out "frames.csv") -Value "frame,elapsed_ms,tick_ms" -Encoding ASCII

for ($i = 0; $i -lt $count; $i++)
{
    $bitmap = New-Object System.Drawing.Bitmap($totalWidth, ($maxHeight + $header))
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([System.Drawing.Color]::Black)

    $offsetX = 0
    foreach ($entry in $entries)
    {
        $graphics.CopyFromScreen($entry.Rect.Left, $entry.Rect.Top, $offsetX, $header,
            (New-Object System.Drawing.Size($entry.Width, $entry.Height)))
        $graphics.DrawString($entry.Label, $font, [System.Drawing.Brushes]::Yellow, ($offsetX + 6), 2)
        $offsetX += $entry.Width
    }

    $graphics.Dispose()
    $bitmap.Save((Join-Path $Out ("frame_{0:D3}.png" -f $i)), [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    Add-Content -Path (Join-Path $Out "frames.csv") -Value ("frame_{0:D3},{1},{2}" -f $i, [int]$watch.Elapsed.TotalMilliseconds, [Environment]::TickCount) -Encoding ASCII

    $due = ($i + 1) * $interval
    $wait = $due - $watch.Elapsed.TotalMilliseconds
    if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]$wait) }
}
$watch.Stop()
$font.Dispose()
Write-Host "captured $count frames ($labels) ($totalWidth x $($maxHeight + $header)) into $Out"
