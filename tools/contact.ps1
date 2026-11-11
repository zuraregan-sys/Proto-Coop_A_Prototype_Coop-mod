param(
    [string]$Dir = "",
    [string]$Out = "",
    [int]$Columns = 0,
    [int]$TileWidth = 0
)

# Builds a contact sheet from the frames capture.ps1 saved. Wide frames (the
# default now contains both game windows side by side) get two columns; narrow
# single-window frames keep the old four.

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not $Dir) { $Dir = Join-Path $Root "local\capture" }
if (-not $Out) { $Out = Join-Path $Dir "contact.png" }

Add-Type -AssemblyName System.Drawing

$files = @(Get-ChildItem $Dir -Filter "frame_*.png" | Sort-Object Name)
if ($files.Count -eq 0) { throw "no frames in $Dir" }

$probe = [System.Drawing.Image]::FromFile($files[0].FullName)
$frameWidth = $probe.Width
$aspect = $probe.Height / $probe.Width
$probe.Dispose()

if ($Columns -le 0) { $Columns = if ($frameWidth -ge 1200) { 2 } else { 4 } }
if ($TileWidth -le 0) { $TileWidth = if ($frameWidth -ge 1200) { 960 } else { 480 } }

$tileHeight = [int]($TileWidth * $aspect)
$rows = [int][Math]::Ceiling($files.Count / [double]$Columns)
$sheet = New-Object System.Drawing.Bitmap(($Columns * $TileWidth), ($rows * $tileHeight))
$graphics = [System.Drawing.Graphics]::FromImage($sheet)
$graphics.Clear([System.Drawing.Color]::Black)
$font = New-Object System.Drawing.Font("Consolas", 12)

# Optional timing manifest written by capture.ps1.
$times = @{}
$manifest = Join-Path $Dir "frames.csv"
if (Test-Path $manifest)
{
    foreach ($line in Get-Content $manifest | Select-Object -Skip 1)
    {
        $parts = $line -split ","
        if ($parts.Count -ge 2) { $times[$parts[0]] = [int]$parts[1] }
    }
}

for ($i = 0; $i -lt $files.Count; $i++)
{
    $image = [System.Drawing.Image]::FromFile($files[$i].FullName)
    $column = $i % $Columns
    $row = [int][Math]::Floor($i / $Columns)
    $graphics.DrawImage($image, ($column * $TileWidth), ($row * $tileHeight), $TileWidth, $tileHeight)
    $label = $files[$i].BaseName
    if ($times.ContainsKey($label)) { $label = "$label  $($times[$label]) ms" }
    $graphics.DrawString($label, $font, [System.Drawing.Brushes]::Yellow,
        ($column * $TileWidth + 6), ($row * $tileHeight + 4))
    $image.Dispose()
}

$graphics.Dispose()
$font.Dispose()
$sheet.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()
Write-Host "contact sheet ($($files.Count) frames, $Columns columns) -> $Out"
