param(
    [string]$Dir = "",
    [string]$Out = "",
    [int]$Columns = 4,
    [int]$TileWidth = 480
)

# Builds a contact sheet from the frames capture.ps1 saved, so a whole second of
# gameplay can be reviewed in one image.

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not $Dir) { $Dir = Join-Path $Root "local\capture" }
if (-not $Out) { $Out = Join-Path $Dir "contact.png" }

Add-Type -AssemblyName System.Drawing

$files = @(Get-ChildItem $Dir -Filter "frame_*.png" | Sort-Object Name)
if ($files.Count -eq 0) { throw "no frames in $Dir" }

$probe = [System.Drawing.Image]::FromFile($files[0].FullName)
$aspect = $probe.Height / $probe.Width
$probe.Dispose()
$tileHeight = [int]($TileWidth * $aspect)
$rows = [int][Math]::Ceiling($files.Count / [double]$Columns)
$sheet = New-Object System.Drawing.Bitmap(($Columns * $TileWidth), ($rows * $tileHeight))
$graphics = [System.Drawing.Graphics]::FromImage($sheet)
$graphics.Clear([System.Drawing.Color]::Black)
$font = New-Object System.Drawing.Font("Consolas", 12)

for ($i = 0; $i -lt $files.Count; $i++)
{
    $image = [System.Drawing.Image]::FromFile($files[$i].FullName)
    $column = $i % $Columns
    $row = [int][Math]::Floor($i / $Columns)
    $graphics.DrawImage($image, ($column * $TileWidth), ($row * $tileHeight), $TileWidth, $tileHeight)
    $graphics.DrawString($files[$i].BaseName, $font, [System.Drawing.Brushes]::Yellow,
        ($column * $TileWidth + 6), ($row * $tileHeight + 4))
    $image.Dispose()
}

$graphics.Dispose()
$font.Dispose()
$sheet.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()
Write-Host "contact sheet ($($files.Count) frames) -> $Out"
