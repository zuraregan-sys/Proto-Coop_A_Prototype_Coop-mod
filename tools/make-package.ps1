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

# Pinned Steamworks DLL for the Steam transport (AppID 480), verified by hash.
$SteamUrl = "https://github.com/rlabrecque/Steamworks.NET/releases/download/2025.164.1/Steamworks.NET-Standalone_2025.164.1.zip"
$SteamDllHash = "FDAFB7C65EC7D4182B42BF92CC8671C68E4DC547CC6E613899D2618A47EB65DA"
$SteamCache = Join-Path $Root "local\steamworks-2025.164.1"
$SteamDll = Join-Path $SteamCache "Windows-x86\steam_api.dll"
if (-not (Test-Path $SteamDll)) {
    try {
        New-Item -ItemType Directory -Force -Path $SteamCache | Out-Null
        $zip = Join-Path $env:TEMP "steamworks-2025.164.1.zip"
        Invoke-WebRequest -Uri $SteamUrl -OutFile $zip -UseBasicParsing
        Expand-Archive -Path $zip -DestinationPath $SteamCache -Force
    } catch {
        Write-Warning "could not fetch the Steamworks DLL ($($_.Exception.Message)); the package will be UDP only"
    }
}
$HaveSteam = Test-Path $SteamDll
if ($HaveSteam) {
    $hash = (Get-FileHash $SteamDll -Algorithm SHA256).Hash
    if ($hash -ne $SteamDllHash) {
        Write-Warning "Steamworks DLL hash mismatch ($hash); not including it"
        $HaveSteam = $false
    }
}

$Version = (git -C $Root rev-parse --short HEAD)
$Stage = Join-Path $env:TEMP ("protocoop-package-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $Stage | Out-Null
Copy-Item $Asi (Join-Path $Stage "prototype_coop.asi") -Force
if ($IncludeProxy) { Copy-Item $Proxy (Join-Path $Stage "xinput1_3.dll") -Force }
if ($HaveSteam) {
    Copy-Item $SteamDll (Join-Path $Stage "steam_api.dll") -Force
    Set-Content -Path (Join-Path $Stage "steam_appid.txt") -Value "480" -NoNewline
}

@"
[coop]
role=client
port=27017
peer_port=27016
peer_ip=127.0.0.1
transport=steam
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

Conectar por Steam (recomendado)
--------------------------------
1. Los dos tenéis Steam abierto y este mismo mod instalado (steam_api.dll y
   steam_appid.txt tienen que estar junto a prototypef.exe).
2. coop.ini en los dos:  transport=steam
   - Anfitrión: role=host
   - Invitado:  role=client
3. El anfitrión entra al juego. El mod crea un lobby y abre la ventana de
   invitación de Steam: invita a su amigo desde ahí (o con clic derecho ->
   Invitar a jugar).
4. El invitado arranca el juego, acepta la invitación (overlay de Steam) y entra.
   No hay IPs, ni puertos, ni router que tocar.

Nota: Steam identifica la partida como AppID 480 ("Spacewar"), así que en el
overlay apareceréis jugando a Spacewar. Es normal en este método.

Conectar por LAN o VPN (alternativa)
------------------------------------
  transport=udp (por defecto)
  HOST                            CLIENTE
  [coop]                          [coop]
  role=host                       role=client
  port=27016                      port=27017
  peer_port=27017                 peer_port=27016
  peer_ip=<IP del cliente>        peer_ip=<IP del host>

- Misma red: la IP local del otro (ipconfig).
- Por Internet sin Steam: VPN de malla (Tailscale/ZeroTier/Hamachi) o abrir y
  redirigir UDP 27016/27017. Permite prototypef.exe en redes privadas.

Ojo: dos instancias en el mismo PC comparten cuenta de Steam y no pueden hablar
entre sí por Steam (Steam no conecta una cuenta consigo misma). Para probar en
un solo PC usa transport=udp con 127.0.0.1.

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
