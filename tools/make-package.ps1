param(
    [string]$Out = "",
    [switch]$IncludeProxy
)

# Builds a friend-ready zip with the co-op mod only: no game files, no saves.
# The friend still needs their own Prototype (Steam) and PrototypeFix (it is the
# ASI loader the game uses).

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Asi = Join-Path $Root "build\coop\prototype_coop.asi"
$Proxy = Join-Path $Root "build\coop\xinput1_3.dll"
if (-not (Test-Path $Asi)) { throw "missing $Asi; run tools\build-coop.ps1 first" }

$Version = (git -C $Root rev-parse --short HEAD)
$Stage = Join-Path $env:TEMP ("protocoop-package-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $Stage | Out-Null
Copy-Item $Asi (Join-Path $Stage "prototype_coop.asi") -Force
if ($IncludeProxy) { Copy-Item $Proxy (Join-Path $Stage "xinput1_3.dll") -Force }

@"
[coop]
role=client
port=27017
peer_port=27016
peer_ip=127.0.0.1
"@ | Set-Content -Path (Join-Path $Stage "coop.ini") -Encoding ASCII

@"
Prototype Co-op - build $Version
================================

Requisitos
----------
1. Prototype (Steam, App ID 10150), ya instalado y con PrototypeFix instalado
   (PrototypeFix es el cargador de .asi que usa el juego; instálalo desde su
   fuente oficial).
2. Copia prototype_coop.asi en la MISMA carpeta que prototypef.exe.

Conectar
--------
El anfitrión (host) y el invitado (client) usan el mismo mod. Edita coop.ini
junto a prototypef.exe:

  HOST                            CLIENTE
  [coop]                          [coop]
  role=host                       role=client
  port=27016                      port=27017
  peer_port=27017                 peer_port=27016
  peer_ip=<IP del cliente>        peer_ip=<IP del host>

- En la misma red: usa la IP local del otro PC (ipconfig).
- Por Internet: lo más simple es una VPN de malla (Tailscale, ZeroTier,
  Hamachi/Radmin) y usar la IP que os da la VPN. Si no, abre el UDP 27016 en el
  cortafuegos del host y del cliente y reenvía el puerto en el router.

Cortafuegos de Windows: permite prototypef.exe en redes privadas la primera vez.

Progreso compartido: en un solo PC ambos usan la misma carpeta de guardado. Para
dos PCs, copia los slot-*.bin del host a la carpeta del cliente antes de jugar
(tools\sync-save.ps1).

Limitaciones conocidas (build $Version)
---------------------------------------
- El látigo no se ve en el avatar remoto.
- La ciudad replica posiciones y categorías (civil, soldado, infectado,
  mutante, vehículo); las copias son estáticas y los vehículos militares se
  muestran como vehículos normales.
- Efectos anclados, disfraz, daño entre jugadores y modo historia, pendientes.

Nada de esto incluye archivos del juego: solo el mod.
"@ | Set-Content -Path (Join-Path $Stage "LEEME.txt") -Encoding UTF8

if (-not $Out) { $Out = Join-Path $Root ("dist\ProtoCoop-$Version.zip") }
$OutDir = Split-Path -Parent $Out
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
if (Test-Path $Out) { Remove-Item $Out -Force }
Compress-Archive -Path (Join-Path $Stage "*") -DestinationPath $Out -CompressionLevel Optimal
Remove-Item -Recurse -Force $Stage
Write-Host "package: $Out"
Get-ChildItem $Out | Select-Object Name, Length | Format-Table -AutoSize
