param(
    [int]$Seconds = 3,
    [int]$Fps = 8,
    [string]$Out = "",
    [int]$ProcessId = 0,
    [int]$Index = 0
)

# Captures one of the Prototype test windows at a fixed rate, so a whole second
# of gameplay can be reviewed afterwards as a contact sheet (see contact.ps1).

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
}
"@
Add-Type -AssemblyName System.Drawing

$windows = @(Get-Process prototypef -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero })
if ($ProcessId -ne 0) { $windows = @($windows | Where-Object { $_.Id -eq $ProcessId }) }
if ($windows.Count -eq 0) { throw "no Prototype window found" }

$windows = @($windows | Sort-Object {
    $rect = [CaptureWindow+RECT]::new()
    [CaptureWindow]::GetWindowRect($_.MainWindowHandle, [ref]$rect) | Out-Null
    $rect.Left
})
if ($Index -lt 0 -or $Index -ge $windows.Count) { throw "window index $Index out of range ($($windows.Count) windows)" }

$target = $windows[$Index]
$rect = [CaptureWindow+RECT]::new()
[CaptureWindow]::GetWindowRect($target.MainWindowHandle, [ref]$rect) | Out-Null
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -le 0 -or $height -le 0) { throw "window has no size" }

New-Item -ItemType Directory -Force -Path $Out | Out-Null
Get-ChildItem $Out -Filter "frame_*.png" -ErrorAction SilentlyContinue | Remove-Item -Force

$count = [int]($Seconds * $Fps)
$interval = 1000.0 / $Fps
$watch = [System.Diagnostics.Stopwatch]::StartNew()
for ($i = 0; $i -lt $count; $i++)
{
    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, (New-Object System.Drawing.Size($width, $height)))
    $graphics.Dispose()
    $bitmap.Save((Join-Path $Out ("frame_{0:D3}.png" -f $i)), [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()

    $due = ($i + 1) * $interval
    $wait = $due - $watch.Elapsed.TotalMilliseconds
    if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]$wait) }
}
$watch.Stop()
Write-Host "captured $count frames from '$($target.MainWindowTitle)' ($width x $height) into $Out"
