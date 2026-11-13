# RESEARCH — Prototype (2009) y la viabilidad de un modo cooperativo

**Fecha:** 2026-11-10  
**Juego objetivo:** Prototype (2009), versión de Steam (App ID 10150, build 19788432)  
**Alcance:** identificación del build, búsqueda de trabajo previo, análisis del decomp `pablopenna/prototype-decomp`, evaluación técnica.  
**Reglas respetadas:** no se modificó la instalación de Steam, no se instalaron herramientas, no se programó nada. Todo el análisis local fue de solo lectura (listados, hashes, cabeceras PE, strings y consultas DNS).

> **Convención del informe:**  
> **[HECHO]** = comprobado directamente en los archivos, en las herramientas o en una fuente citada.  
> **[HIPÓTESIS]** = inferencia técnica razonable, todavía no verificada en ejecución.  
> **[NO VERIFICADO]** = no se pudo comprobar con los medios actuales.

---

## 0. Resumen ejecutivo

1. **El motor está identificado:** Prototype usa el **Titanium Engine** de Radical Entertainment, con renderizador **Pure3D**, física propia **ravenphysics**, Scaleform GFx, Lua 5.1, Bink, Direct3D 9 y compilador **Visual Studio 2005 SP1 (MSVC 8.0)**. `detect_engine` no lo reconoció porque su base de firmas no incluye Titanium; eso no significa que el motor sea desconocido.
2. **El build es el parche de Steam del 29-09-2025** (DLL con timestamp PE 2025-08-14). La DLL coincide exactamente con la versión de referencia de los proyectos de decompilación. El EXE actual **no** coincide porque está parcheado por PrototypeFix; el `.bak` sí coincide con el hash de referencia.
3. **No existe ningún mod cooperativo público conocido para Prototype.** El directorio de Co-op Anything (400 entradas, 252 juegos) no tiene ficha para Prototype ni para Titanium; GitHub, Nexus y ModDB no muestran un proyecto de co-op. Lo que sí existe son fixes, mods visuales, un menú de desarrollo y ports/recompilaciones no cooperativas.
4. **El hallazgo importante:** el binario retail **conserva gran parte del multijugador que Radical canceló**. Hay clases RTTI y código de `proto::CoOpBehaviour`, mensajes `CoOpMessage`, respawn de compañero, volúmenes de emparejamiento, sincronización de inputs (`SyncObject`), `TimeSync`, `OnlineManager`, `DesyncHunter`, `OLSPeer`, lobbies/sesiones y una capa Quazal. También queda el hostname `prototypepclive.quazal.net` (hoy resuelve a `127.0.0.1`). Es decir: **no se parte de cero**, pero el servicio online original está muerto y el flujo de sesión/UI no está demostrado.
5. **Viabilidad:** alta para un prototipo de “verse y moverse” en dos clientes; media para combate compartido en mundo abierto; baja-media para la campaña completa; baja para “revivir el online original” tal cual, porque exige reconstruir el backend Quazal.
6. **Arquitectura recomendada:** reutilizar la simulación determinista y la capa de sincronización existente, sustituyendo el transporte Quazal por un **shim de sesión directo (Steam relay/UDP)** y añadiendo un flujo host/join. Empezar con 2 jugadores en free roam y validar determinismo con `Replay`/`DesyncHunt` antes de tocar la campaña.
7. **El proyecto de `pablopenna` es útil como metodología, no como base completa:** 28 funciones verificadas (bucle principal, `SimulationTick`, `SimulationDriver`, `Time`, strings, opciones). No contiene trabajo de red. Para co-op es más relevante el decomp `Arefaris/prototype_decomp`, mucho más avanzado (53,48 % de funciones), que sí reconstruye la capa online.

---

## 1. FASE 1 — Identificación del build

### 1.1 Resultado de las herramientas MCP

**`detect_engine` sobre `<Steam library>\steamapps\common\Prototype`:**

- 124 archivos escaneados.
- Ejecutables: `prototypef.exe`, `vcredist_x86.exe`, `xdelta3-3.1.0-i686.exe`, `DirectX\DXSETUP.exe`.
- `best_guess`: `null`.
- `all_matches`: vacío.
- `hooking_tools`: “unknown engine”.
- Proyectos del mismo motor: ninguno.

**[HECHO]** La herramienta no identifica el motor. **No es evidencia de que no haya motor**, sino de que su base de firmas no contempla Titanium/Pure3D. La identificación se completó con cabeceras PE, strings y documentación pública.

**`game_identity` sobre `prototypef.exe`:**

| Campo | Valor |
|---|---|
| Archivo | `prototypef.exe` |
| Tamaño | 2.275.824 bytes |
| SHA-256 | `ac1542512ab5eddffcbf949919f3dcc4b26e38f07b57cb6a094def2a621b0b82` |
| Steam App ID | `10150` |
| Steam build ID | `19788432` |
| Nombre Steam | `Prototype` |
| Manifest | `appmanifest_10150.acf` |
| Arquitectura | x86 (32 bits), PE32 |
| Timestamp PE | `1755193331` = 2025-08-14 |
| Secciones | `.text`, `.rdata`, `.data`, `.rsrc` |
| Versión de archivo | 1.0.0.1 |
| Compañía / producto | Activision / Prototype |

### 1.2 Hashes y discrepancia del EXE

| Archivo | SHA-256 | Estado |
|---|---|---|
| `prototypeenginef.dll` | `d331be205bdde99897952ee88261ab7e63c5c65e36a4660e2a367886a3537071` | **[HECHO]** coincide con la versión de referencia de `prototype-decomp` y `prototype_decomp`. |
| `prototypef.exe` (actual) | `ac1542512ab5eddffcbf949919f3dcc4b26e38f07b57cb6a094def2a621b0b82` | **[HECHO]** parcheado por PrototypeFix. |
| `prototypef.exe.bak` | `0cd6834f72b4fd4a762a8ce7c705bfccc94dec58e404f0c0c09299ec42eb7de6` | **[HECHO]** coincide con el hash de referencia del EXE original. |

**Explicación comprobada:** el `.bak` conserva la sección `.bind` de SteamStub y un entry point en `0x229310`; el EXE actual ya no tiene `.bind` y su entry point es `0x25b7`. En la carpeta están `prototypef.vcdiff`, `xdelta3-3.1.0-i686.exe` y `ApplyGameLaunchFix.bat`, que aplican el parche de arranque de PrototypeFix (elimina el wrapper de Steam del lanzador y evita el cuelgue de arranque asociado al puerto 27015). Por eso el hash actual del EXE no coincide con la referencia, **pero la DLL sí coincide**, que es lo que importa para las direcciones de Ghidra y para el decomp.

**No hay contradicción con el proyecto:** su README fija el hash del EXE original en `0cd6834f…`, precisamente el del `.bak`.

### 1.3 Motor, toolchain y middleware

| Aspecto | Resultado | Evidencia |
|---|---|---|
| Motor | **Titanium Engine** (Radical Entertainment) | PCGamingWiki; Wikipedia alemana; strings `Pure3D`, rutas `code\\pure3d`; RTTI `engine`, `pure3d`, `proto` |
| Renderizador | **Pure3D / PDDI** sobre Direct3D 9 | Strings `..\\..\\..\\code\\pure3d\\pddi\\src\\dx9\\...`; import `d3d9.dll` |
| Física | **ravenphysics** (propia, no Havok) | RTTI `ravenphysics`; strings de solver/constraints |
| UI | **Scaleform GFx** | 77 exports, casi todos `scaleformp3d`; import de recursos Flash |
| Scripting | **Lua 5.1** | Strings `scripts/frevents.lua`, `fre_GetLua*` |
| Vídeo | **Bink** | `binkw32.dll`, `binkw32Hooked.dll` |
| Compilador | **Visual Studio 2005 SP1 / MSVC 8.0**, CRT `MSVCR80.dll`, `/MD` | Import `MSVCR80.dll`; linker 8.0; README del decomp |
| Arquitectura | **x86-32**, PE32, image base `0x10000000`, 14,2 MB de `.text` | Cabecera PE de `prototypeenginef.dll` |
| Build | Parche de Steam **2025-09-29**, build `19788432` | `game_identity`; PCGamingWiki/SteamDB |
| DRM | El EXE original lleva SteamStub; **la DLL no está envuelta** | Sección `.bind` del `.bak`; DLL con secciones normales |
| Anti-cheat | **Ninguno detectado** | No hay servicio/anti-cheat conocido en el binario |

**Imports principales de `prototypeenginef.dll`:** `USER32`, `SHELL32`, `MSVCR80`, `WS2_32`, `iphlpapi`, `d3d9`, `SHLWAPI`, `WINMM`, `binkw32`, `RPCRT4`, `XINPUT1_3`, `KERNEL32`, `ADVAPI32`, `ole32`, `OLEAUT32`, `SETUPAPI`, `DSOUND`.

**Exports:** 77 en total; solo 6 son interfaz del motor (`EngineInitialize`, `EngineService`, `EngineKeyboardService`, `EngineMouseService`, `EngineWindowFocus`, `EngineTerminate`); el resto son Scaleform. El EXE es un lanzador que carga la DLL y llama a esos 6 exports.

### 1.4 Mods y parches ya instalados

La instalación **no está limpia**; ya incluye herramientas que son relevantes para el proyecto:

| Archivo | Función | Relevancia |
|---|---|---|
| `prototype_fix.asi` | **PrototypeFix 1.8.0** de emoose | Plugin ASI con fixes de CPU/HID/VRAM, opción `PreferLooseFiles`, `UnlockDevMenu`, etc. |
| `binkw32Hooked.dll` | Proxy de Bink que carga plugins ASI | **[HECHO]** ya existe un mecanismo de carga de plugins sin tocar el EXE. |
| `ApplyGameLaunchFix.bat` + `prototypef.vcdiff` + `xdelta3` | Parche de arranque del EXE | Explica el hash distinto del EXE. |
| `prototype_fix.ini` | Configuración | `PreferLooseFiles = true`, `UnlockDevMenu = false`, `DInputKeyboardMouseOnly = true`. |

`PreferLooseFiles = true` es importante: permite que archivos sueltos (scripts, assets) tengan prioridad sobre los `.rcf`. `UnlockDevMenu = false` significa que el menú de desarrollo está desbloqueable pero no activo.

### 1.5 Estructura de la instalación

- 11 archivos `.rcf` (~6,4 GB en total): `00woi`, `01woi`, `02woi`, `03woi`, `00audio`, `01audio`, `cells`, `art`, `movies`, `scripts`, `shaders`.
- `scripts\frevents.lua` está compilado como bytecode Lua, no como fuente legible.
- Carpetas `art`, `audio`, `DirectX`, `Docs`, `scripts`.
- Archivos `proto_uv.dat`, `skuinfo.p3d`, `cementfiles.p3d`, `10150_install.vdf`.
- Saves en `Documentos\Prototype\slot-*.bin`, fuera de la carpeta del juego. **No se tocaron.**

---

## 2. FASE 2 — Investigación cooperativa

### 2.1 Búsqueda en Co-op Anything

| Consulta | Resultado |
|---|---|
| `Prototype` | 20 coincidencias difusas, **ninguna es Prototype (2009)** (son juegos con “prototype” en el nombre o en las notas). |
| `Titanium` | **0 entradas**. |
| `Scarface` | **0 entradas**. |
| `Hulk` | **0 entradas**. |
| `Quazal` | **0 entradas**. |
| `Radical Entertainment` | **0 entradas**. |

El directorio completo tiene **400 entradas y 252 juegos**, pero **no hay ficha de Prototype**. Esto es un hueco de investigación, no una prueba de que no exista algo privado.

### 2.2 ¿Existe algún intento previo de multijugador para Prototype?

**Búsqueda pública realizada:** GitHub (repositorios y términos), Nexus Mods, ModDB, PCGamingWiki, GameFAQs, Co-Optimus, prensa de 2008-2009.

**Resultado:** **[HECHO]** no se encontró ningún mod cooperativo online público, ni proyecto abandonado, ni fork de co-op para Prototype (2009). Lo que existe es:

- Mods de texturas, skins, ReShade, HUD, saves y mejoras gráficas.
- **PrototypeFix** (emoose): fixes técnicos, no co-op.
- **Debug Menu** (dron_3 / guía de Goob): desbloquea el menú de desarrollo `RAD_RELEASE`.
- **ProtoDroid / PrototypeRecomp** (Nazatric): port/recompilación estática para Android, no co-op.
- **prototype-decomp** (pablopenna) y **prototype_decomp** (Arefaris): decompilación, no co-op.

**Contexto histórico [HECHO]:** Prototype tuvo un modo multijugador/co-op en desarrollo, pero Radical lo canceló en 2008 para centrarse en la campaña. La prensa de la época lo confirma (Critical Hit, Co-Optimus, Wikipedia). El motor retail conserva gran parte de ese código (ver §3.3).

### 2.3 Herramientas y proyectos reutilizables

| Proyecto | Qué aporta | ¿Co-op? | Reutilización |
|---|---|---|---|
| **PrototypeFix** (emoose) | Loader ASI, fixes de arranque/rendimiento, `PreferLooseFiles`, dev menu | No | **[HECHO]** ya instalado; sirve de base de carga de plugins y de acceso a archivos sueltos. |
| **Debug Menu** (dron_3) | Desbloquea `RAD_RELEASE` / DEV MENU | No | Útil para pruebas y para inspeccionar modos de free roam. |
| **prototype-decomp** (pablopenna) | 28 funciones verificadas, proxy XInput, MinHook, anotaciones Ghidra, mapa de direcciones | No | Metodología, inyección, bucle principal, `Time`, `SimulationDriver`. **Sin licencia declarada.** |
| **prototype_decomp** (Arefaris) | Decompilación “matching” mucho más avanzada; headers, RTTI, fuente del subsistema online | No | **La fuente más valiosa** para co-op; licencia source-only no comercial. |
| **ProtoDroid / PrototypeRecomp** (Nazatric) | Recompilación estática a ARM64, documentación de análisis | No | Poco útil para x86; **cuidado:** el repo puede contener assets del juego; no copiar. |
| **QuazalShark / PRUDP / RDV emulators** (otros juegos) | Herramientas para protocolos Quazal Rendez-Vous | No | Relevante solo si se decide revivir el backend original; es un proyecto aparte. |

---

## 3. FASE 3 — Ingeniería inversa

### 3.1 `pablopenna/prototype-decomp`

**Datos del repositorio [HECHO]:**

- Creado 2026-09-10, último push 2026-10-18, 13 estrellas, 73 archivos, ~205 KB, C++.
- Enfoque: **decompilación incremental mediante DLL inyectada**, no “byte-matching”. El criterio de verdad es la equivalencia de comportamiento en el juego.
- Estado: **28 funciones reemplazadas y verificadas**.
- Ghidra: 103.091 funciones autoanalizadas, sin PDB.
- Target: `prototypeenginef.dll` con el hash `d331be20…`; el `setup` **aborta si el hash no coincide**.
- Proxy: `xinput1_3.dll`; hooking: MinHook 1.3.4; toolchain: llvm-mingw i686, CMake/Ninja, Ghidra 12.1.2; desarrollo en Linux/Proton.
- Sin licencia declarada (`license: null`).

**Qué está reconstruido y verificado:**

| Subsistema | Funciones | Estado |
|---|---|---|
| Punto de entrada del motor | `EngineService` | Verificada en juego |
| Bucle de juego | `UpdateGameflow`, `IsEngineRunning`, `SimulationTick` | Verificadas |
| Driver de simulación | `UpdateRealTime`, `AccumulateTicks`, `ConsumeTick`, `EndUpdate`, `Activate`, `Reset` | Verificadas |
| Tiempo | `Time` (constructor, `Set`, `Add`, `Subtract`, etc.) | Verificadas |
| Opciones de lanzamiento | `ParseCommandLineOptions`, `HasOption`, `GetOptionValue`, `CheckTimedExit` | Verificadas |
| Strings del motor | `StrCpy`, `StrTok`, `StrTokR`, `StrNICmp`, `StrICmp`, `AsciiToLower` | Verificadas |

**Qué no tiene:**

- **Nada de red, sesiones, sincronización, NPCs, combate, misiones, streaming ni UI.**
- Solo cubre el arranque y el bucle; es un proyecto de aprendizaje, no un port ni una base de co-op.
- No hay PDB; hay que tratar con `__thiscall`, `__fastcall`, x87 y optimizaciones agresivas.
- Las direcciones están atadas al hash exacto de la DLL. Si Steam actualiza, todo el mapa se invalida.
- El EXE de referencia no es el actual (por el parche), pero la DLL sí.

**Qué se puede reutilizar:**

1. **Infraestructura de inyección:** proxy `xinput1_3.dll`, `DllMain`, MinHook, thunks que preservan `ECX`/`EDX`, logging a archivo, verificación de reemplazos al arrancar.
2. **Implementaciones verificadas** del bucle y del tiempo, útiles como base para un mod que necesite enganchar `SimulationTick` o `UpdateGameflow`.
3. **Mapa de direcciones y anotaciones de Ghidra** (`notes/addresses.md`), incluida la interfaz EXE↔DLL.
4. **Metodología de test:** gamepad scriptado, `TimedExit`, ejecución en ventana, comparación de comportamiento.
5. **Hechos del motor:** tick variable (~43/s), `SimulationDriver` a 30 Hz por defecto pero el juego llama `SetTickRate(0)`, pausa por pérdida de foco, etc.

**Limitaciones para co-op:** 28 funciones no alcanzan ni de lejos para sincronizar un mundo abierto. Su valor es metodológico y de infraestructura, no funcional.

### 3.2 `Arefaris/prototype_decomp` — el proyecto realmente relevante

**Datos [HECHO]:**

- Decompilación “matching” con **Visual Studio 2005 SP1**, mismo target (`PROTOPC_2025`) y **mismo hash de DLL**.
- **75.061 / 140.347 funciones** coincidentes (**53,482 %**) y **31,059 %** de bytes de código (2026-11-08).
- La DLL híbrida ya arranca el juego y llega a free roam.
- Incluye headers, layouts de clases, RTTI, símbolos y fuente reconstruida de numerosos subsistemas.
- Licencia **source-only, no comercial**, forks bajo la misma licencia.

**Subsistema online reconstruido (parcial):**

```
src/code/engine/online/
  onlinemanager.{h,cpp}      OnlineManager, ConsoleManagerPeer, ChannelManager
  ols.{h,cpp}                OLS: singleton de servicios online
  olsgame / olsuser / olsfriend / olsprofile / olsstorage
  onlinelua.cpp              online_DeterminePlayerIndex
  onlinescripting.cpp        wrappers Lua de ConsoleManager
  replay.{h,cpp}             replay determinista
  replaysimdriver.cpp        driver de simulación para replay
  variablesimrate.{h,cpp}    control de tasa de simulación

src/code/engine/system/
  networksockets.cpp         capa Winsock, DNS asíncrono, tabla de sockets
  syncobject.{h,cpp}         sincronización de inputs entre máquinas
  desynchunter.{h,cpp}       detección de desincronización
  desyncchecker.cpp          checksums del estado

src/code/engine/netmessage.h  NetMessageSent / NetMessageAllocator / Dynamic
src/code/engine/fight/onlineconditions.*
src/code/proto/fightitems/online/
  online_conditions.*        condiciones de respawn, distancia, tipo de jugador
  online_tracks.*            muerte, respawn, pairing volume, streaming de celdas
src/code/proto/
  coopbehaviour.{h,cpp}      proto::CoOpBehaviour
  coopstateinstances.cpp     RespawnState, PlayerDeadState, PairingVolumeState
  messages/coopmessages.h    CoOpMessage y derivados
```

**Estado real:** el propio worklist indica que `engine/online/onlinemanager.cpp` tiene **solo 2 funciones coincidentes y 55 funciones “not placed”**. Es decir, la estructura y el comportamiento están documentados, pero **no es una implementación completa y lista para usar**. Hay que tratar esa fuente como mapa, no como binario sustituible.

### 3.3 Lo que el binario retail conserva del multijugador cancelado

Esto es lo más importante para la viabilidad. **[HECHO]** por strings, RTTI y código reconstruido:

**Clases y mensajes de co-op:**

- `proto::CoOpBehaviour`
- `proto::CoOpMessage`, `CoOpMessageGetPlayerDeathState`, `CoOpMessageGetRespawnState`, `CoOpMessagePlayerDead`, `CoOpMessageSetRespawnState`, `CoOpMessageSetPairingVolumeState`
- `coopPairingProgressionAllowed`, `coopPairingProgressionDisallowed`
- `proto::RespawnState`, `proto::PlayerDeadState`, `proto::PairingVolumeState`
- `proto::fightitems::online::*` (condiciones y tracks de respawn, muerte, distancia, pairing volume, streaming de celdas)

**Infraestructura online del motor:**

- `engine::OnlineManager`, `ConsoleManagerPeer`, `ChannelManager`
- `engine::SyncManager`, `engine::SyncObject`, `SyncObjectFactory`, `SyncObjectDefault`
- `engine::DesyncHunter`, `DesyncChecker`
- `engine::NetMessagePusher`, `NetMessagePuller`, `NetMessageAllocator*`
- `engine::TimeSync`, `OLSPeer`, `OLS`, `OLSGame`, `OLSUser`, `OLSFriend`, `OLSProfile`, `OLSStorage`
- `engine::NetSystemLink`, `NetSystemLinkPublish`, `NetSystemLinkDiscovery`
- `engine::OnlineControllerManager`, `OnlineSettings`, `OnlineGameDBHelper`, `OnlineAdvert`
- `engine::Replay`, `ReplaySimDriver`, `VariableSimRate`

**Strings reveladores:**

- `prototypepclive.quazal.net` (servicio online de PC basado en Quazal).
- `HostMatch`, `ols_IsConnectedToHost`, `LobbyConnected`, `LobbyDisconnected`, `PeerLost`.
- `Peer dead. Re-spawn in %d second(s)`.
- `No Multiplayer GUID supplied for Multiplayer Session.`
- `online_GetNumPlayers`, `online_DeterminePlayerIndex`, `onlinePlayerIndex`, `onlineNumPlayers`.
- `onlineSyncLocalWorldCellStreaming`, `onlineSetRespawnState`, `onlineFlagPlayerDeath`, `onlineSetPairingVolumeState`.

**Interpretación [HIPÓTESIS]:** el juego tuvo un modo online/co-op real, probablemente de 2 jugadores (el código habla de “partner” y de “pairing volume”), con sincronización de inputs, reloj, respawn y streaming de celdas. El código quedó en el binario retail, pero el flujo de menús/sesiones y el backend Quazal no están operativos. **No está demostrado que se pueda activar desde el juego actual sin escribir código.**

### 3.4 Quazal y el estado del servicio

**[HECHO]** `prototypepclive.quazal.net` resuelve hoy a `127.0.0.1` desde esta máquina, sin entrada en el archivo `hosts`. `nslookup` devuelve lo mismo. Esto es consistente con un dominio retirado o “sinkholeado” a localhost.

**[HIPÓTESIS]** El backend Rendez-Vous/Net-Z original ya no existe. Aunque se restaurara el código, no habría servidor al que conectarse. Revivirlo requeriría implementar un backend compatible con Quazal (existen emuladores para otros juegos, p. ej. Ghost Recon Future Soldier Research, pero es un proyecto de ingeniería inversa de protocolo aparte).

---

## 4. FASE 4 — Evaluación

### 4.1 Viabilidad por objetivo

| Objetivo | Viabilidad | Esfuerzo | Comentario |
|---|---|---|---|
| **Dos clientes, verse y moverse** (free roam, sin NPCs compartidos) | **Alta** | Medio | Se puede hacer con overlay/host-authoritative sin tocar la simulación. Es el MVP correcto. |
| **Free roam con combate y enemigos compartidos** | **Media** | Alto | Aquí entra la sincronización de daño, IA, física y streaming. La infraestructura original ayuda, pero hay que entenderla. |
| **Campaña completa co-op** | **Baja-media** | Muy alto (meses/años) | Misiones, scripts, checkpoints, cinemáticas, estados de mundo, guardado. No recomendable como primer objetivo. |
| **Revivir el online original tal cual (Quazal)** | **Baja** | Muy alto | Backend muerto, protocolo propietario, UI/sesión no demostrada. Solo tiene sentido si aparece un backend Quazal compatible. |
| **Port a otra plataforma** | Fuera de alcance | — | No es necesario para co-op en PC. |

**Conclusión:** la existencia de `CoOpBehaviour`, `SyncObject`, `OnlineManager` y `DesyncHunter` cambia el problema de “escribir netcode desde cero” a “reconstruir el transporte, el flujo de sesión y la UI, y verificar determinismo”. Sigue siendo un proyecto grande, pero hay una base real sobre la que trabajar.

### 4.2 Sistemas que habría que sincronizar

| Prioridad | Sistema | Dificultad | Notas |
|---|---|---|---|
| 1 | **Reloj y tick de simulación** | Alta | `SimulationDriver`, `OnlineManager`, `TimeSync`, `VariableSimRate`. El juego usa tick variable (~43/s), lo que complica la determinismo. |
| 2 | **Inputs del jugador** | Alta | `SyncObject` ya cuenta presses/releases y los compara entre máquinas. Es la pieza central del diseño original. |
| 3 | **Transform / animación / parkour del avatar** | Alta | Posición, rotación, estado de movimiento, animación, equipment visuals. |
| 4 | **Combate y daño** | Muy alta | Ataques, hit reactions, grabs, consume, projectiles, health, armor, regen. |
| 5 | **NPCs, IA y multitudes** | Muy alta | Spawns, alerta militar/infectados, pathing, ownership. El mundo abierto tiene cientos de actores. |
| 6 | **Física y destrucción** | Alta | `ravenphysics`, vehículos, colisiones, escombros. Determinismo entre máquinas no garantizado. |
| 7 | **Streaming de mundo/celdas** | Alta | `onlineSyncLocalWorldCellStreaming`; cada cliente debe cargar la misma celda y los mismos objetos. |
| 8 | **Misiones, scripts y eventos** | Muy alta | `fight`/`scenario`, objetivos, checkpoints, diálogos, cinemáticas, condiciones de misión. |
| 9 | **Respawn / muerte / pairing** | Media | Ya existe `CoOpBehaviour` y mensajes; es la parte más “regalada”. |
| 10 | **Guardado, perfil y upgrades** | Media | ¿Host-only o por jugador? El diseño original parece tener estados por jugador. |
| 11 | **UI/HUD/pausa/menús** | Media | Hay que añadir host/join, lobby, lista de jugadores, estados de conexión. |

### 4.3 Dificultades técnicas

1. **El online original está incompleto en la decomp.** `OnlineManager` tiene solo 2 funciones coincidentes; hay 55 “not placed”. La estructura está, pero no es código listo para compilar y ejecutar.
2. **Quazal está muerto.** `prototypepclive.quazal.net` → `127.0.0.1`. Sin backend, hay que sustituir la capa de sesión o emular el protocolo.
3. **No hay UI/flujo de sesión demostrado.** `art.rcf` contiene `protoonlineoption.swf` y `PROTO_ONLINEOPTION`, pero su función no está verificada; podría ser configuración de anuncios online, no un menú de multiplayer. **[NO VERIFICADO]**
4. **Determinismo frágil.** Tick variable, punto flotante x87, RNG, orden de streaming, IA y física. `DesyncHunter` existe precisamente porque el equipo original esperaba desincronizaciones.
5. **Build de 2025.** La DLL fue recompilada en 2025 con VS2005; el EXE está parcheado. Cualquier actualización de Steam puede invalidar direcciones y hooks.
6. **32 bits y dos instancias.** Espacio de direcciones limitado, memoria compartida, carga de CPU/GPU. Hay que aislar saves y evitar el chequeo de instancia única / puerto 27015.
7. **Código enorme y sin PDB.** 140.347 funciones; llamadas `__thiscall`/`__fastcall`; FPO; x87. La decompilación es un proyecto de años.
8. **Mods existentes.** PrototypeFix ya parchea el EXE y la DLL; un plugin de co-op debe convivir con sus hooks y con `PreferLooseFiles`.
9. **Legalidad.** No se pueden distribuir binarios, assets ni datos extraídos. El decomp de Arefaris es source-only no comercial; el de pablopenna no declara licencia. Hay que contactar a los autores antes de reutilizar código.

### 4.4 Herramientas adicionales necesarias

**Análisis / RE:**

- Ghidra 12.1.x + JDK 21 (proyecto persistente; no reimportar cada sesión).
- x32dbg / WinDbg para debugging en vivo.
- `objdiff` / `reccmp` si se sigue la vía “matching”.
- `Process Monitor` / `API Monitor` para mutex, ventanas, puerto 27015 y carga de DLLs.

**Inyección / mod:**

- MinHook (ya usado por pablopenna) o Detours.
- Proxy `xinput1_3.dll` (método probado por pablopenna) o plugin ASI (ya hay loader con `binkw32Hooked.dll`).
- VS2005 SP1 o llvm-mingw i686 para compilar x86-32.

**Red:**

- **Steamworks SDK** (`ISteamNetworkingMessages` / relay) para conexión sin abrir puertos.
- **ENet** o **GameNetworkingSockets** como alternativa UDP.
- Wireshark para capturas; `QuazalShark`/PRUDP solo si se intenta revivir Quazal.

**Testing:**

- `gameslot` / `gamepool` de Co-op Anything para instancias aisladas y leases.
- `flight` (trazas, divergencia, shrink), `chaos`, `fuzz`, `ownership`, `scenarios`, `bots`, `animcheck`, `inspect`, `hotreload`.
- `Replay`, `ReplayDesync` y `DesyncHunt` que ya trae el motor como opciones de lanzamiento.
- Dos copias del juego con saves privados, ventana y `TimedExit` para pruebas automáticas.

### 4.5 Arquitectura de multijugador recomendada

**Recomendación principal: host-autoritativo con reutilización de la sincronización determinista existente, y transporte sustituto de Quazal.**

```
                 ┌─────────────────────────────┐
                 │        Host (jugador 1)      │
                 │  simulación autoritativa     │
                 │  OnlineManager + SyncObject  │
                 │  CoOpBehaviour + DesyncHunter│
                 └──────────────┬──────────────┘
                                │  transporte propio
                                │  (Steam relay / UDP-ENet)
                 ┌──────────────┴──────────────┐
                 │        Cliente (jugador 2)   │
                 │  simulación local + input    │
                 │  predicción / interpolación  │
                 └─────────────────────────────┘
```

**Por qué:**

- El motor ya tiene un diseño de **inputs sincronizados + simulación determinista + detector de desync**. Intentar reescribir eso desde cero sería tirar el activo más valioso.
- `OLSPeer`/`OLS`/Quazal es la capa muerta. Lo más práctico es **sustituirla por un shim** que implemente las mismas interfaces (`OLSSession`, `OLSGame`, `OLSUser`, callbacks) sobre un transporte propio.
- Si el determinismo falla entre dos PCs, el plan B es **host-autoritativo con ownership de NPCs** y avatar client-authoritative, usando snapshots e interpolación. Eso es lo que hacen la mayoría de mods de co-op y es más tolerante a un motor de 2009.

**Decisiones concretas:**

1. **MVP de 2 jugadores**, no 4+. El código original parece orientado a pareja (“partner”, “pairing volume”); no hay evidencia de cuántos jugadores soportaba. **[HIPÓTESIS]**
2. **Tick fijo de simulación para pruebas** (p. ej. 30 Hz) aunque el juego retail use tick variable; así se puede validar determinismo. En producción, decidir si se mantiene el tick variable o se fuerza uno fijo.
3. **Transporte Steam** (`ISteamNetworkingMessages`) para NAT traversal y relay; fallback LAN/UDP directo.
4. **UI mínima propia** (overlay o Scaleform) para host/join; no depender de `protoonlineoption.swf` hasta verificar qué es.
5. **Aislar saves y perfiles** por instancia; no tocar la instalación ni los saves del propietario.

### 4.6 Hoja de ruta propuesta (sin programar todavía)

| Fase | Objetivo | Criterio de salida |
|---|---|---|
| **0. Laboratorio** | Dos instancias aisladas, ventana, saves privados; comprobar si hay chequeo de instancia única / puerto 27015. | Dos clientes arrancan y llegan al menú sin tocar la instalación original. |
| **1. Mapa online** | Localizar en Ghidra `OnlineManager`, `OLS`, `SyncManager`, `CoOpBehaviour`; identificar puntos de entrada y estados. | Diagrama de clases y direcciones; preguntas resueltas. |
| **2. Handshake** | Shim de sesión en loopback; dos clientes se “ven” sin gameplay. | Host y cliente establecen sesión; log de conexión. |
| **3. Movimiento** | Sincronizar input, reloj, transform, animación y parkour. | Dos avatares se mueven de forma consistente en la misma zona. |
| **4. Combate** | Daño, salud, ataques, grabs, consume; NPCs con ownership. | Dos jugadores luchan contra los mismos enemigos sin desync crítico. |
| **5. Mundo** | Streaming de celdas, vehículos, física, eventos ambientales. | Free roam estable durante 10+ minutos. |
| **6. Historia** | Misiones, scripts, checkpoints, cinemáticas, guardado. | Una misión completa en co-op. |
| **7. Online** | Transporte Steam, UI host/join, reconexión, tolerancia a latencia. | Partida entre dos PCs por Steam relay. |

**Puerta de decisión crítica:** en la fase 2-3, comprobar con `Replay`/`DesyncHunt` si dos instancias con los mismos inputs producen el mismo estado. Si no, cambiar a host-autoritativo puro antes de invertir en misiones.

---

## 5. Conclusiones

1. **No hay un mod cooperativo previo que reutilizar.** Prototype es un hueco en el directorio de Co-op Anything. La comunidad ha hecho fixes, texturas y ports, pero no co-op.
2. **El activo real está dentro del binario:** Radical dejó `CoOpBehaviour`, mensajes de respawn/pairing, `SyncObject`, `TimeSync`, `OnlineManager` y `DesyncHunter`. Es un esqueleto de multijugador cancelado, no una implementación completa.
3. **El mayor obstáculo no es el netcode básico, sino el backend y el flujo de sesión.** Quazal está muerto (`prototypepclive.quazal.net` → `127.0.0.1`) y no hay evidencia de una UI de multiplayer funcional.
4. **`pablopenna/prototype-decomp` sirve para aprender y para la infraestructura de inyección, no como base de co-op.** Sus 28 funciones cubren el bucle y el tiempo; no tocan red, mundo ni combate. Su falta de licencia es un problema para reutilizar código.
5. **`Arefaris/prototype_decomp` es mucho más relevante:** 53,48 % de funciones, headers, RTTI y fuente del subsistema online. Aun así, `OnlineManager` está lejos de estar completo (2 funciones coincidentes, 55 sin colocar).
6. **Veredicto:** co-op de Prototype es **técnicamente viable pero es un proyecto de ingeniería inversa serio**, no un mod de fin de semana. El camino realista es: free roam de 2 jugadores → combate compartido → campaña, reutilizando la sincronización existente y sustituyendo Quazal por un transporte propio.

---

## Apéndice A — Evidencia local recogida

- `detect_engine`: 124 archivos, 4 ejecutables, `best_guess: null`, 0 coincidencias.
- `game_identity`: App ID 10150, build 19788432, PE32 x86, timestamp 2025-08-14, SHA-256 del EXE actual.
- Hash de `prototypeenginef.dll`: `d331be20…537071` (coincide con la referencia).
- Hash de `prototypef.exe.bak`: `0cd6834f…eb7de6` (coincide con la referencia).
- Cabecera PE de la DLL: image base `0x10000000`, `.text` 14.213.988 bytes, 77 exports, imports `WS2_32`/`iphlpapi`/`d3d9`/`binkw32`/`XINPUT1_3`/`DSOUND`/`MSVCR80`.
- Strings/RTTI: `proto::CoOpBehaviour`, `CoOpMessage*`, `coopPairingProgression*`, `online_*`, `prototypepclive.quazal.net`, `HostMatch`, `Peer dead. Re-spawn in %d second(s)`.
- `art.rcf`: `PROTO_ONLINEOPTION`, `protoonlineoption.swf`, `OnlineAdvert`, `RAD_RELEASE`.
- DNS: `prototypepclive.quazal.net` → `127.0.0.1`; sin entrada en `hosts`.
- `prototype_fix.ini`: `PreferLooseFiles = true`, `UnlockDevMenu = false`, `DInputKeyboardMouseOnly = true`.
- `prototype_fix.asi`: PrototypeFix 1.8.0, autor emoose, exporta `InitializeASI`.
- `prototype_fix.log`: confirma `PrototypeFix v1.8.0.0`, el módulo ASI cargado y la DLL del motor en su base preferida `0x10000000`.

## Apéndice B — Fuentes principales

- `pablopenna/prototype-decomp` — README, `STATE.md`, `notes/addresses.md`, commits y árbol del repositorio (GitHub, consultado 2026-11-10).
- `Arefaris/prototype_decomp` — README, `docs/plan.md`, `docs/progress.md`, `docs/engine_worklist.md`, `src/code/engine/online/*`, `src/code/proto/coop*` (GitHub, consultado 2026-11-10).
- `Nazatric/ProtoDroid` y `Nazatric/PrototypeRecomp` — README y árbol (GitHub, consultado 2026-11-10).
- Co-op Anything MCP: `list_docs`, `search_directory` (Prototype, Titanium, Scarface, Hulk, Quazal), `get_skill("coop-triage")`, `read_playbook("networking")`, `read_playbook("desync-hunting")`, `read_playbook("multi-instance-testing")`.
- PCGamingWiki — Prototype (motor Titanium, parche 2025, PrototypeFix).
- SteamDB — build 19788432, parche 2025-09-29.
- Wikipedia (DE) — Titanium Engine; Wikipedia (IT) — multijugador cancelado.
- Critical Hit (2008-03-31), Co-Optimus (2008-12-15), Engadget (2008-04-30) — cancelación del multijugador.
- Nexus Mods — PrototypeFix (emoose), Debug Menu (dron_3).
- ModDB — mods de texturas/skins; sin co-op.
