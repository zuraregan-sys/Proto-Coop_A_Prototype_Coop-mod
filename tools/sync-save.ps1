param(
    [string]$From = "",
    [string]$To = "",
    [switch]$Force
)

# Copies the host's save slots into the client's save folder, so both players
# start a session with the same progress, unlocks and story.
#
# On one PC both instances already share %USERPROFILE%\Documents\Prototype, so
# the default is a no-op. For two PCs point the parameters at the folders (the
# client can use a UNC path, e.g. \\HOST\PrototypeSaves).
#
#   .\tools\sync-save.ps1 -From "C:\Users\me\Documents\Prototype" -To "\\HOST\PrototypeSaves"

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Default = Join-Path $env:USERPROFILE "Documents\Prototype"
if (-not $From) { $From = $Default }
if (-not $To) { $To = $Default }

$From = [System.IO.Path]::GetFullPath($From)
$To = [System.IO.Path]::GetFullPath($To)

if (-not (Test-Path $From)) { throw "host save folder not found: $From" }
if (-not (Test-Path $To)) { New-Item -ItemType Directory -Force -Path $To | Out-Null }

if ($From -eq $To) {
    Write-Host "host and client share the same save folder ($To); nothing to copy"
    return
}

$slots = Get-ChildItem -Path $From -Filter "slot-*.bin" -File
if (-not $slots) { throw "no slot-*.bin in $From" }

# Keep whatever the client had, so the copy can be undone.
$backup = Join-Path $Root ("Backup\saves-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
New-Item -ItemType Directory -Force -Path $backup | Out-Null
Get-ChildItem -Path $To -Filter "slot-*.bin" -File | ForEach-Object {
    Copy-Item $_.FullName (Join-Path $backup $_.Name) -Force
}

$copied = 0
foreach ($slot in $slots) {
    $target = Join-Path $To $slot.Name
    Copy-Item $slot.FullName $target -Force
    $sourceHash = (Get-FileHash $slot.FullName -Algorithm SHA256).Hash
    $targetHash = (Get-FileHash $target -Algorithm SHA256).Hash
    if ($sourceHash -ne $targetHash) { throw "hash mismatch after copying $($slot.Name)" }
    $copied++
}
Write-Host "copied $copied save slot(s) from $From to $To (previous files in $backup)"
