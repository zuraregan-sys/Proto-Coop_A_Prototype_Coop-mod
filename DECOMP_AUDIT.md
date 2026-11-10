# DECOMP_AUDIT — `Arefaris/prototype_decomp`

**Fecha:** 2026-11-10  
**Repositorio auditado:** `https://github.com/Arefaris/prototype_decomp`  
**Commit de referencia:** `3009bcafda741c3659d48b745e1679845c8fd9da` (“progress: after the eleventh and twelfth crunch waves”, 2026-11-08)  
**Objetivo:** determinar cuánto del multijugador original de Prototype es realmente aprovechable.  
**Naturaleza:** auditoría de solo lectura. No se compiló, no se inyectó, no se modificó el juego ni la DLL/EXE. El repositorio se descargó a una carpeta temporal para analizarlo; no se copió código de terceros al proyecto.

> **Etiquetas:** **[HECHO]** = comprobado en el repositorio/documentación. **[HIPÓTESIS]** = inferencia técnica. **[NO VERIFICADO]** = no se pudo comprobar; se indica por qué.

---

## 0. Resumen ejecutivo

1. **El decomp global está al 53,482 % de funciones** (75.061 / 140.347) y al **31,059 % de bytes** de código. Es un proyecto de decompilación “matching”, no un port funcional.
2. **El subsistema online está mucho menos reconstruido que el resto.** El propio worklist marca como **no hecha** la capa de red (`ConsoleManager`, `Channel`, `MultiTailQueue`, sync; ~270 funciones) y también `OLSPeer`, `ConsoleManagerPeer`, `SyncManager` y `DesyncHunterOnline`. Sin esa capa, el online no funciona.
3. **Lo más aprovechable es `SyncObject`** (sincronización de inputs press/release), junto con `DesyncHunter`/`DesyncChecker` y la clase `Replay`. Son piezas relativamente autocontenidas y bien documentadas, pero **no están cableadas**: no hay llamadores reconstruidos de `StartSync`/`UpdateSync`, y `ApplySyncMessage` está en `NONMATCHING` (62 %).
4. **`CoOpBehaviour` está reconstruido solo en parte en la fuente, pero el binario sí tiene la lógica completa.** Ghidra recuperó `Update` (0x104DB8E0), `OnMessage` (0x104DB980) y el destructor (0x104DB960); es un state machine pequeño para respawn/pairing/muerte. Sigue siendo la pieza más fácil de reimplementar.
5. **La abstracción de transporte real es `ChannelManager` / `Channel`**, no `NetSession`/`NetConnection`: esos nombres de `gamelink.h` son suposiciones. `OnlineManager+0x70` contiene un `ChannelManager`; `ChannelManager::Open` crea `MultiTailQueue` y `Channel`. Esa capa es exactamente la que el decomp marca como no reconstruida, pero la interfaz es pequeña y sustituible.
6. **`CoOpBehaviour` no depende directamente de `OnlineManager`** en el código reconstruido. Conceptualmente podría funcionar en local/loopback si se reconstruyen sus métodos y se inyectan mensajes, pero **no funciona tal cual** y no aporta red por sí solo.
7. **Licencia:** `prototype_decomp License v1`, source-only, **no comercial**, sin archivos originales, sin distribución de binarios que contengan el programa original, atribución y misma licencia para forks. Además, la regla de este proyecto prohíbe copiar código de terceros: debe usarse como referencia, no como código pegado.
8. **Siguiente paso recomendado:** mapear `ChannelManager`/`Channel`/`ConsoleManagerPeer`, decidir si se hookea el bootstrap de sesión `FUN_108BF1B0` o se sustituye `ChannelManager` en `OnlineManager+0x70`, y probar loopback. `CreateOnlineManager` (0x104A29E0) **no** es el punto de entrada online: es el `SoundManager`.

---

## 0.b Addendum — verificación con Ghidra (2026-11-10)

Después de la auditoría documental se importó la DLL en Ghidra 12.1.4 y se decompilaron las funciones clave. Esto **corrige** varias suposiciones del informe original:

| Afirmación original | Corrección verificada |
|---|---|
| `0x104A29E0` = `CreateOnlineManager` | Falso. Es el constructor de `SoundManager` (800 bytes, registros `snd_*`). El nombre del decomp es erróneo. |
| `OnlineManager+0x70` = `ChannelManager*` (header) / `NetSession*` (gamelink) | Es un **`ChannelManager`**. `gamelink.h` llama `NetSession` por error. |
| La abstracción es `NetSession`/`NetConnection` | Son **`ChannelManager`/`Channel`**; `ChannelManager::Open` crea `MultiTailQueue` y `Channel`. |
| `StartSync` no tenía llamadores | `FUN_108BF1B0` (bootstrap de sesión) lo llama. |
| `UpdateSync` no tenía llamadores | `SimulationTick` (0x10838BA0) lo llama. |
| `CoOpBehaviour` sin lógica | Ghidra recuperó `Update`, `OnMessage` y destructor; la lógica es pequeña y completa. |

**Arranque real:**

- `EngineInitialize` (0x1085EFB0) → `FUN_108BF5E0` (0x1085F47E): inicializa el subsistema online, crea `OnlineManager` (0x9C) y `OnlineSync` (0x144).
- `OLSPeer` ctor (0x108E7E80) → `FUN_108BF1B0`: bootstrap de sesión host/cliente.
- `FUN_108BF1B0` crea `ChannelManager` (0x7C) en `OnlineManager+0x70`, `ConsoleManagerPeer` (0x8C) en `+0x74`, abre dos canales en `+0x94`/`+0x98`, instala el `OnlineManager` como `SimulationDriver`, inicializa `OnlineSync`, llama `StartSync` y crea `DesyncHunterOnline`.
- `OnlineManager::Slot4` (0x108BF780) es el pump de red: actualiza reloj, envía cola, recibe del canal y despacha a `OnlineSync`.

**`CoOpBehaviour` recuperado:**

- `Update` (0x104DB8E0): avanza los tres estados y emite evento 4.
- `OnMessage` (0x104DB980): tipo 2 y tipo `0x30` ids 0..4; crea perezosamente `RespawnState` (0xC), `PlayerDeadState` (8) y `PairingVolumeState` (0x10); activa/consulta bits por jugador y dispara eventos.
- Tamaño 0x18; campos en +0xC, +0x10, +0x14; el creator `0x10294380` asigna 0x18 y llama al ctor.

Estas correcciones tienen prioridad sobre las secciones 3, 7, 8 y 11 cuando haya conflicto de nombres.

---


---

## 1. Licencia y condiciones de reutilización

### 1.1 Lo que permite la licencia

**[HECHO]** `LICENSE.md` concede usar, copiar, modificar y distribuir el contenido **en forma de fuente**, para fines privados o públicos **no comerciales**, con estas condiciones:

1. Sin uso comercial (venta, bundle, paywall, publicidad, producto comercial).
2. Sin archivos originales del juego ni material extraído/derivado byte a byte.
3. Sin distribuir binarios que contengan parte del programa original; cada usuario debe construir su propia DLL híbrida desde su copia legal.
4. El contenido vendido separadamente queda desactivado por defecto.
5. Sin eludir DRM/protección.
6. Atribución a Radical Entertainment/Activision y a los contribuidores.
7. Las versiones modificadas deben marcarse como modificadas.
8. Sin derechos de marca.
9. La misma licencia para forks/derivados.
10. Origen limpio: nada de código filtrado, SDK confidencial o material bajo NDA.

**[HECHO]** `LEGAL.md` añade que la licencia **solo cubre lo que escribieron los contribuidores**; no concede derechos sobre el programa original, el juego, sus assets o sus marcas. También advierte que decompilar y publicar código reconstruido puede infringir copyright según la jurisdicción, y que la licencia no es asesoría legal.

### 1.2 Implicaciones para nuestro proyecto

- **No podemos copiar código de este repo a `ProtoCoop`**: además de la licencia, la regla explícita de esta auditoría lo prohíbe. Podemos leerlo, entenderlo y reimplementarlo de forma independiente.
- Si en el futuro se quisiera reutilizar fuente bajo la licencia, el proyecto resultante sería un derivado con **licencia source-only, no comercial, sin binarios distribuibles**. Eso es incompatible con un mod instalable convencional si se distribuye un binario que contiene código original.
- **No se pueden distribuir** la DLL híbrida, un instalador, ni archivos del juego. Cada usuario tendría que compilar desde su copia.
- **`pablopenna/prototype-decomp` no declara licencia** (`license: null`). Sin permiso expreso, no se debe reutilizar su código; solo sirve como referencia metodológica.
- **`ProtoDroid`/`PrototypeRecomp`** no son co-op y además pueden contener archivos del juego; no son una fuente segura para copiar.

**Recomendación legal operativa:** usar ambos repos solo como documentación de comportamiento, nombres, layouts y direcciones; escribir nuestro propio código; no copiar archivos ni fragmentos; no distribuir binarios híbridos.

---

## 2. Estado global del decomp

| Métrica | Valor |
|---|---:|
| Funciones coincidentes | 75.061 / 140.347 (**53,482 %**) |
| Bytes de código coincidentes | 3.028.396 / 9.750.615 (**31,059 %**) |
| Módulo `engine` | 16.905 funciones, 699.609 bytes |
| Módulo `proto` | 35.945 funciones, 1.332.738 bytes |
| Estado del híbrido | Arranca y llega a free roam; **no hay validación online** |
| Target | `PROTOPC_2025`, DLL `d331be20…537071` |
| Compilador | Visual Studio 2005 SP1 (MSVC 8.0) |

**[HECHO]** El proyecto distingue:

- **match**: el código compila al mismo binario que el original.
- **EQUIVALENT**: mismas operaciones en el mismo orden, pero no cuenta como match.
- **NONMATCHING**: cuerpo dentro de `#ifdef NONMATCHING`, no compilado en la build de matching.
- **declared/mapped**: solo dirección y nombre en `symbols.txt`; sin cuerpo reconstruido.

Esto es importante: **“match” no significa “funcionalmente conectado”**. Hay funciones que coinciden pero no tienen llamadores reconstruidos, y hay clases completas que solo existen como declaración.

---

## 3. Mapa de dependencias

```
                         [proto::CreateOnlineManager 0x104A29E0]
                                      |
                                      v
                         [engine::OnlineManager 0x108BF160]
                          +0x70  session / ChannelManager  -----> [NetSession] --Open(kind=4)--> [NetConnection]
                          +0x74  ConsoleManagerPeer                    |                         |
                          +0x78  inner SimulationDriver                |                         +--> Send / Receive
                          +0x80  stepsThisFrame                        |                         |
                          +0x84  clock                                  +--> [GameLinkManager] --Open(kind=6)--> ...
                                                                        |
                                                                        +--> [SyncObject::StartSync 0x10833380]
                                                                              |
                                                                              +--> [SyncObject / ApplySyncMessage]
                                                                              +--> [Replay / ReplaySimDriver]

 [OLS singleton 0x108441E0] --> [OLSGame] --> [RequestGameState / RequestTransition] --> [OnlineSync 0x108C7750]
        |                          |                                                          |
        |                          +--> [OLSUserList]                                        +--> [SyncCommands]
        +--> [OLSStorage] / [OLSProfileProto] / [OLSPeer] / [SyncManager] / [DesyncHunterOnline]
                                                        ^
                                                        |
                                      [ConsoleManager / Channel / MultiTailQueue]
                                             (NO reconstruido, ~270 funciones)

 [ISimulationDriver::Slot7] --> [OnlineClock::GetTime 0x108C7340]   (NO reconstruido)
 [ReplaySimDriver] --> [DesyncHunter] --> [DesyncChecker]
 [CoOpBehaviour] --> [BehaviourController] (dispatch de mensajes); escucha tipos 0x30, 2 y 4
```

### 3.1 Hallazgo clave: el puntero de sesión en `OnlineManager+0x70`

**[HECHO]** `onlinemanager.h` llama a `+0x70` `ChannelManager* channels` (nombre “guess”). Sin embargo, `gamelink.cpp` define una vista de `OnlineManager` en la que `+0x70` es `NetSession* session`, y `GameLinkManager::Connect()` hace:

- `session = gOnlineManager->session;`
- `connection = session->Open(6, -1, 0x2000);`

Es decir, **el objeto de transporte real está en `OnlineManager+0x70`**, y el nombre `channels` del header es una suposición. `SyncObject::StartSync` hace lo mismo a través de `SyncOwner->session->Open(4, ...)`.

Esto convierte `OnlineManager+0x70` en el punto más prometedor para instalar un `NetSession` propio, siempre que se entienda quién lo crea y cuándo.

### 3.2 Dependencias resumidas

| Nodo | Depende de | Es usado por |
|---|---|---|
| `OnlineManager` | `ISimulationDriver`, `SimulationDriverTimeData`, `SimulationDriver* inner`, `ChannelManager`, `ConsoleManagerPeer`, `NetSession` (+0x70) | `CreateOnlineManager`; `GameLinkManager` (lee la sesión); vtable del driver |
| `OLS` | `gOLS`, `ConsoleManager::Callback`, `OLSGame`, `OLSUserList`, `OLSProfile`, `OLSStorage` | `OLSGame`, `ExceptionManager`, `PauseState`, `OLSStorage`, `OnlineGameDBHelper` |
| `SyncObject` | `NetSession`, `NetConnection`, `NetSystem`, `gSyncOwner->machines`, `Replay` | `SyncObjectDefault::Press/Release`; `ReplaySimDriver`; sin llamadores reconstruidos de `StartSync`/`UpdateSync` |
| `OnlineSync` / `OnlineClock` | ninguna implementación | `ISimulationDriver::Slot7`, `RequestTransition`, `CancelStateRequest`, `StartNextRequest` |
| `DesyncHunter` | `DesyncChecker`, `gDesyncSettings` | `ReplaySimDriver`; transiciones de estado |
| `CoOpBehaviour` | `Behaviour`, `BehaviourController`, `CoOpMessage`, `RespawnState`/`PlayerDeadState`/`PairingVolumeState` | `BehaviourController` (update/mensajes); `MetaBehaviourCreatorImpl<CoOpBehaviour>` |
| `Replay` | `ReplaySimDriver`, `DesyncHunter`, `RequestGameState`, `PauseState` | scripts `Replay_*`; tareas de simulación/tiempo real |
| `NetSession`/`NetConnection` | sockets, `NetSystem` | `SyncObject`, `GameLinkManager` |

---

## 4. Auditoría por subsistema

### 4.1 `OnlineManager`

**Archivos:** `src/code/engine/online/onlinemanager.h`, `onlinemanager.cpp`; worklist `engine/online/onlinemanager.cpp` y `sn-ei`.

| Aspecto | Estado |
|---|---|
| Constructor `0x108BF160` | Reconstruido; inicializa `+0x70`, `+0x74`, `+0x78`, `clock`, `+0x94` |
| Destructor `0x108BF880` | Reconstruido; llama `Shutdown()` |
| Slots `1, 2, 3, 5, 9` | Reconstruidos; delegan en `inner` o actualizan el reloj |
| `Slot4` `0x108BF780` | **Declarado/mapeado; sin cuerpo** |
| `UpdateClock` `0x108BF520` | **Declarado/mapeado; sin cuerpo** |
| `Shutdown` `0x108BF690` | **Declarado/mapeado; sin cuerpo** |
| `ChannelManager` | Solo `operator new/delete`; sin layout/métodos |
| `ConsoleManagerPeer` | Solo `operator new`; sin cuerpo |
| `CreateOnlineManager` `0x104A29E0` | **Declarado; sin cuerpo**; su llamada sí está en `protomain.cpp` (arranque) |

**Dependencias:** `ISimulationDriver`, `SimulationDriverTimeData`, `SimulationDriver* inner`, `ChannelManager`, `ConsoleManagerPeer`, `NetSession` en `+0x70`.  
**Llamadores:** `proto::CreateOnlineManager` (arranque); `GameLinkManager` lee `+0x70`; los slots se invocan por vtable desde el sistema de simulación.  
**Veredicto:** **parcial**. Es un envoltorio de driver, no un gestor online completo. Sin `Slot4`, `UpdateClock`, `Shutdown`, `ChannelManager` y la sesión, no se puede usar como sistema de red.

**Nota sobre el recuento:** el worklist inicial de la unidad dice “2 funciones match, 0 nonmatching”; un pase posterior (`sn-ei`) lista constructor, destructor y slots. No hay un recuento final único por unidad en el repositorio, así que se trata como estado parcial, no como cifra cerrada.

### 4.2 `OLS` / `OLSPeer` / sesión y lobby

**Archivos:** `src/code/engine/online/ols.{h,cpp}`, `olsgame.{h,cpp}`, `olsuser.cpp`, `olsprofile.{h,cpp}`, `olsstorage.{h,cpp}`, `olsfriend.*`, `olswrappers.cpp`, `onlinescripting.cpp`; `src/code/proto/olsproto.*`, `olsgameproto.*`.

| Clase | Estado |
|---|---|
| `OLS` (singleton `0x108441E0`) | **Solo `Get()` reconstruido.** El resto del layout existe parcialmente en `ols.h`; `Update()`, callbacks y creación del singleton no están. |
| `OLSGame` | Muchos métodos reconstruidos (`TryRequestState`, `Slot2`, `Slot3`, `Slot6`, `Slot9`, `SetSetting`, `RequestState`). **Faltan `OnMessage` 0x10887FC0, slots 4/5/7/8 y `OnFinished` 0x108880C0.** |
| `OLSUserList` (`engine::OLSUser`) | Mayoría reconstruida; `AddUser`/`OnFlags`/`OnValue` NONMATCHING; **`Start` 0x108878B0 abierto**; `OLSUserProto` no reconstruido. |
| `OLSProfile` | Parte reconstruida; `OLSProfileProto` sí; `OLSUserProto`/`OLSGameProto` parciales. |
| `OLSStorage` | **Reconstruido casi completo** (constructor, cola, mensajes, `ReadLine` EQUIVALENT); las operaciones de plataforma son virtuales puros. |
| `OLSPeer` | **No reconstruido** (rango 0x108E7E80–0x108E9CF0); solo wrappers de script. |
| `SyncManager` / `DesyncHunterOnline` | **No reconstruidos**; dependen de `ConsoleManager`, que tampoco está. |
| `ConsoleManager` | **No reconstruido**; es la base de `OLSPeer`, `SyncManager` y `DesyncHunterOnline`. |

**Dependencias:** `OLS` es el nodo central de callbacks; `OLSGame` usa `RequestGameState`/`RequestTransition`; `OLSStorage` implementa la cola de archivos globales; `OLSPeer` y `SyncManager` cuelgan de `ConsoleManager`.  
**Veredicto:** **no hay un lobby/sesión funcional**. Hay piezas de UI/perfil/almacenamiento, pero el núcleo OLS y los peers están sin reconstruir.

### 4.3 `SyncObject`

**Archivo:** `src/code/engine/system/syncobject.{h,cpp}`; worklist `sn-fr`.

| Función | Estado |
|---|---|
| `SyncObject` ctor/dtor, `OnPress`, `OnRelease`, `Reset`, `ResetIf` | Reconstruidos; comportamiento claro |
| `SyncObjectDefault` ctor, `Press`, `Release`, `SetFactory`, getters | Reconstruidos |
| `SyncObjectFactoryDefault` ctor/dtor, `GetObject`, `Update`, `IndexOf` | Reconstruidos |
| `InitSyncFactories`, `StartSync` `0x10833380`, `StopSync` `0x108333D0` | Reconstruidos; `StartSync` abre `NetSession` con kind **4** |
| `RegisterSyncFactory` / `UnregisterSyncFactory` | Reconstruidos |
| `ApplySyncMessage` `0x10833450` | **NONMATCHING (62 %)**; mismo comportamiento, no compilado en la build de matching |
| `SetSyncEnabled` / `GetSyncEnabled` | Reconstruidos |
| `RecordedInputsReady` | Reconstruido |
| `SendSyncMessage` `0x10833790` | Reconstruido; envía por `gSyncConnection` o aplica localmente |
| `UpdateSync` `0x108336F0` | Reconstruido; drena `gSyncConnection->Receive` |
| `LoadRecordedInputs` / `SaveRecordedInputs` | **No reconstruidos**; quedan en el motor |

**Dependencias:** `NetSession`/`NetConnection`, `NetSystem`, `gSyncOwner`, `gSyncConnection`, `gReplay`.  
**Llamadores:** `SyncObjectDefault::Press/Release` → `OnPress/OnRelease` → `SendSyncMessage`; `ReplaySimDriver::Slot4` → `RecordedInputsReady`; **no hay llamadores reconstruidos de `StartSync`/`UpdateSync`**.  
**Veredicto:** **la mejor pieza reutilizable**. Es autocontenida y su lógica es clara, pero para usarla en vivo hay que implementar el transporte y llamar a `StartSync`/`UpdateSync` desde algún bucle. `ApplySyncMessage` se puede reimplementar funcionalmente, aunque no coincida byte a byte.

### 4.4 `TimeSync` / `OnlineClock` / `OnlineSync`

| Elemento | Estado |
|---|---|
| `TimeSync::Callback` (RTTI 0x111DC228) | Solo el tipo; **no hay clase `TimeSync` reconstruida** |
| `OnlineClock::GetTime` `0x108C7340` | **Declarado en `gamestate.cpp`; sin cuerpo** |
| `OnlineSync` (0x108C7750 y métodos 0x108C7B50, 0x108C7BE0, 0x108C7820, 0x108C7860, 0x108C77C0) | **Declarados; sin cuerpo** |
| `ISimulationDriver::Slot7` | Reconstruido; llama `gOnlineClock->GetTime(&flag)` |
| `RequestTransition`, `IsMessageSent`, `CancelStateRequest`, `StartNextRequest` | Reconstruidos; llaman a `Unknown108C7750()->...` |

**Dependencias:** `OnlineClock` es usado por el driver de simulación; `OnlineSync` por las transiciones de estado del juego.  
**Veredicto:** **falta una pieza central**. Sin `OnlineClock`/`OnlineSync`, las transiciones de estado online y el reloj de red no existen en la decomp. Un plugin puede hookear/no-op estos puntos, pero no hay código reutilizable.

### 4.5 `DesyncHunter` / `DesyncChecker`

**Archivos:** `src/code/engine/system/desynchunter.{h,cpp}`, `desyncchecker.cpp`; worklist `sn-bj`.

| Función | Estado |
|---|---|
| `DesyncHunter` ctor/dtor, `Start`, `Stop`, `Update` | Reconstruidos |
| `SetInstance`, `GetInstance`, `UpdateInstance`, `StartInstance`, `StopInstance` | Reconstruidos |
| `PushMode`, `PopMode`, `GetMode`, `RecordThread` | Reconstruidos |
| `DesyncChecker` ctor/dtor, `Append`, `Reset`, `GetValue` | Reconstruidos |
| `DesyncChecker::Verify` `0x108BFBD0` | **Cuerpo vacío en la fuente**; no está claro si el original también lo es **[NO VERIFICADO]** |
| `DesyncPrintf`, `DesyncChecksum`, registro de variables, `FormatRandomState` | Reconstruidos |
| `gDesyncSettings` | **Declarado extern; no definido en la fuente** |
| `DesyncHunterOnline` | **No reconstruido**; depende de `ConsoleManager` |

**Dependencias:** `DesyncChecker` usa `content::NamedMetaType::Find("GameObject")` y `math::gRandom`; `DesyncHunter` es usado por `ReplaySimDriver` y por las transiciones de estado.  
**Veredicto:** **muy útil para QA/determinismo**, pero incompleto: `Verify` vacío y settings sin definición. No es un detector de desync listo.

### 4.6 `NetMessage` y la capa de transporte

**Archivos:** `src/code/engine/netmessage.{h,cpp}`, `gamelink.{h,cpp}`, `networksockets.cpp`; worklist `sn-w`, `sn-dn`, `sn-bj`.

| Elemento | Estado |
|---|---|
| `NetMessageSent`, `NetMessageAllocator`, `NetMessageAllocatorDynamic` | Declarados; solo el constructor base `0x10853BA0` reconstruido |
| `NetMessagePusher` / `NetMessagePuller` | **No reconstruidos**; bases de `SyncManager`/`MultiTailQueue` |
| `NetSession` `Open` 0x108C31D0 / `Close` 0x108C3670 | **Declarados; sin cuerpo** |
| `NetConnection` `Send` 0x108C3F90 / `Receive` 0x108C4030 / `IsReady` 0x108C40C0 | **Declarados; sin cuerpo** |
| `NetSystem` / `GetNetSystem` 0x108C4260 | Declarado/mapeado; sin cuerpo |
| `NetLink` / `NetPacket` | Declarados; usados por `GameLinkManager` |
| `GameLinkManager` | Mayoría reconstruido; constructor NONMATCHING; usa `NetSession::Open(6,...)` y `NetConnection::Send/Receive` |
| `networksockets.cpp` | Capa Winsock reconstruida (handles, bind/connect/send/receive, DNS asíncrono); **`GetLocalNetworkInfo` 0x1093F920 no hecha** |
| Capa `ConsoleManager`/`Channel`/`MultiTailQueue` | **No reconstruida** (~270 funciones, rangos 0x108C2F60–0x108C5870 y 0x108C7210–0x108C8D20) |

**Dependencias:** `SyncObject` y `GameLinkManager` usan `NetSession`/`NetConnection`; `GameLinkManager` abre kind **6**, `SyncObject` abre kind **4**.  
**Veredicto:** **la interfaz de transporte existe, la implementación no**. `networksockets` es una buena base de bajo nivel, pero no sustituye a `NetSession`.

### 4.7 `CoOpBehaviour` / `CoOpMessage`

**Archivos:** `src/code/proto/coopbehaviour.{h,cpp}`, `coopstateinstances.cpp`, `src/code/proto/messages/coopmessages.h`; worklist `sn-by`.

| Función | Estado |
|---|---|
| `GetClassInfo` `0x104DAE80` | Reconstruido |
| Constructor `0x104DAEC0` | Reconstruido; escucha tipos **0x30**, **2** y **4** (`Listen`/`Listen2`) |
| `Slot1` `0x104DAF20` | Reconstruido (vacío) |
| `GetMembers` `0x104DB780` | Reconstruido; tres miembros: `respawnPendingState`, `partnerDeadState`, `pairingVolumeState` |
| `Update` `0x104DB8E0` | **No reconstruido** (0x5A bytes) |
| `OnMessage` `0x104DB980` | **No reconstruido** (0x183 bytes) |
| Destructor `0x104DB960` | **No reconstruido** (0x16 bytes) |
| `RespawnState` / `PlayerDeadState` / `PairingVolumeState` | `ClassInfo`/`GetMembers` reconstruidos, pero **sin miembros reflectados ni layout recuperado** |
| `CoOpMessage` base + `GetPlayerDeathState` + `GetRespawnState` | Reconstruidos |
| `CoOpMessagePlayerDead`, `SetRespawnState`, `SetPairingVolumeState` | Nombres vistos en strings del binario en la auditoría previa; **sin fuente ni símbolos en este repo** |
| `MetaBehaviourCreatorImpl<CoOpBehaviour>` `0x10294380` / `RegisterMetaBehaviourCreator` `0x1029BA60` | Instanciaciones explícitas; el call site de registro no está visible en la fuente reconstruida |

**Dependencias:** `engine::Behaviour` (mensajes), `BehaviourController` (dispatch/update), `CoOpMessage`, estados de respawn/pairing.  
**Llamadores:** `BehaviourController` llama `Update`/`OnMessage`; el comportamiento se crea con `MetaBehaviourCreatorImpl<CoOpBehaviour>`.  
**Veredicto:** **cascarón, no funcional**. Sin `Update`, `OnMessage`, destructor, layouts y mensajes, no se puede ejecutar la lógica de respawn/pairing.

### 4.8 `Replay` / `ReplaySimDriver`

**Archivos:** `src/code/engine/online/replay.{h,cpp}`, `replaysimdriver.cpp`; worklist `sn-w`.

| Función | Estado |
|---|---|
| `Replay` ctor/dtor, `SetState`, `SetMode`, `StartDriver`, `Stop`, `Record`, `Play`, `PlayDesync`, `Resume`, `Reset`, `Quit`, scripts | Reconstruidos; un `EQUIVALENT` en `Update` |
| `ReplaySimDriver` ctor/dtor, `Slot1/2/3/4/5/6/9`, `Stop`, `AddListener`, `RemoveListener`, `NotifyListeners`, `PollListeners` | Reconstruidos |
| `ReplaySimDriver::Start` `0x10900960` | **No reconstruido** |
| `ReplaySimDriver::Query` `0x109004B0` | **No reconstruido** |
| `VerifyChecksum` `0x10900690` / `RecordChecksum` `0x109007C0` | **No reconstruidos** (el header dice “left to the engine”) |
| `LoadRecordedInputs` / `SaveRecordedInputs` | **No reconstruidos**; el driver los llama |

**Dependencias:** `DesyncHunter`, `inner` SimulationDriver, `content::SaveStream`, `ReplayReader`.  
**Llamadores:** scripts `Replay_*`, `PauseState`, tareas de simulación/tiempo real.  
**Veredicto:** **la clase `Replay` está muy avanzada y sirve como base de test de determinismo**, pero el driver no arranca ni verifica checksums todavía. No está listo para ejecutar un replay completo.

### 4.9 Condiciones y tracks online

**Archivos:** `src/code/engine/fight/onlineconditions.cpp`, `src/code/proto/fightitems/online/online_conditions.cpp`, `online_tracks.cpp`.

| Elemento | Estado |
|---|---|
| `OnlineNumPlayersCondition` | Reconstruido; `Evaluate` usa `controller::InputSystem::Get()->slotCount` (jugadores locales, **no OLS**) |
| `OnlinePlayerIndexCondition` | Reconstruido; `Evaluate` usa `GetControllingPlayer` y `CompareInts` |
| `OnlineTestForRespawnStateCondition` | Propiedades/constructor; **`Evaluate` 0x102650E0 no reconstruido** |
| `DistanceToPlayerCondition` | Propiedades/constructor; **`Evaluate` no reconstruido** |
| `OnlineTestForPlayerDeathCondition` | Propiedades/constructor; **`Evaluate` no reconstruido** |
| `OnlinePlayerTypeCondition` | Propiedades/constructor; **`Evaluate` no reconstruido** |
| `OnlineFlagPlayerDeathTrack` | Propiedades/constructor/factory y `CreateAction` de muerte reconstruidos |
| `OnlineSetRespawnStateTrack` / `OnlineSetPairingVolumeStateTrack` | Propiedades/constructor/factory; **`CreateAction` 0x10266390 / 0x102663E0 no reconstruidos** |
| `OnlineSyncLocalWorldCellStreamingTrack` | Propiedades/constructor/factory; acción de streaming **no reconstruida** |

**Veredicto:** **metadatos y registro aprovechables; comportamiento incompleto**. Las dos condiciones de `engine/fight` funcionan en local; las condiciones de respawn/distancia/muerte/tipo y las acciones de pairing/respawn faltan.

### 4.10 Red de sockets

**[HECHO]** `networksockets.cpp` reconstruye una capa Winsock completa: tabla de handles, `bind`/`connect`/`send`/`receive`, `select`, DNS asíncrono, opciones, startup/cleanup. El worklist indica que la unidad se compiló sin `/arch`, con declaraciones locales de Winsock/Win32 y `#pragma alternatename`.

**Veredicto:** **útil como base de transporte de bajo nivel**, pero no es una sesión ni un canal; habría que envolverla en `NetSession`/`NetConnection`.

---

## 5. Tabla resumen de funciones y estado

> “Match” según el worklist; “Parcial” = algunas funciones coinciden, otras no están; “Sin cuerpo” = declarada/mapeada, no reconstruida. No se inventan recuentos por unidad cuando el repo no los da.

| Subsistema | Funciones clave | Reconstruido | Coincide | Falta | Veredicto |
|---|---|---|---|---|---|
| `CreateOnlineManager` | 0x104A29E0 | No | Mapeado | Cuerpo | Punto de entrada |
| `OnlineManager` | 0x108BF160, 0x108BF880, slots | Parcial | Ctor/dtor/slots 1,2,3,5,9 | `Slot4`, `UpdateClock`, `Shutdown`, `ChannelManager` | Envoltorio parcial |
| `OLS` | 0x108441E0 | Solo `Get` | `Get` | Layout, `Update`, callbacks, creación | Núcleo ausente |
| `OLSGame` | 0x10887DA0 etc. | Parcial | Varios métodos | `OnMessage`, slots 4/5/7/8, `OnFinished` | Parcial |
| `OLSUserList` | 0x10886A70–0x108879EA | Parcial | Mayoría | `Start`, `OLSUserProto` | Parcial |
| `OLSStorage` | 0x108C5950–0x108C6700 | Sí | Casi todo; `ReadLine` EQUIVALENT | Virtuales de plataforma | Reutilizable con plataforma |
| `OLSPeer` | 0x108E7E80–0x108E9CF0 | No | — | Todo | Bloqueado por `ConsoleManager` |
| `SyncManager` / `DesyncHunterOnline` | — | No | — | Todo | Bloqueado por `ConsoleManager` |
| `SyncObject` | 0x10833330–0x10833BC0 | Sí | Núcleo; `ApplySyncMessage` NONMATCHING 62 % | `Load/SaveRecordedInputs`, llamadores | **Mejor pieza** |
| `TimeSync` | — | No | — | Todo | Ausente |
| `OnlineClock` | 0x108C7340 | No | — | `GetTime` | Ausente |
| `OnlineSync` | 0x108C7750 + métodos | No | — | Todo | Ausente |
| `DesyncHunter` | 0x108215E0 etc. | Sí | Métodos | `DesyncHunterOnline` | Útil para QA |
| `DesyncChecker` | 0x108BFA80 etc. | Parcial | Ctor/Append/Reset/checksums | `Verify` vacío; `gDesyncSettings` sin definir | Parcial |
| `NetMessage` | 0x10853BA0 | Solo ctor base | Ctor | Allocators, pushers/pullers | Casi nada |
| `CoOpBehaviour` | 0x104DAEC0 etc. | Parcial | ClassInfo/ctor/Slot1/GetMembers | `Update`, `OnMessage`, dtor, layouts | Cascarón |
| `CoOpMessage` | base vftable 0x10DDDE10 | Parcial | Base + 2 mensajes `Get*` | `PlayerDead`, `SetRespawnState`, `SetPairingVolumeState` | Parcial |
| `Replay` | 0x108C27A0–0x108C2F60 | Sí | Casi todo; 1 EQUIVALENT | — | Reutilizable |
| `ReplaySimDriver` | 0x10900260–0x10900EC0 | Parcial | Ctor/dtor/slots/Stop/listeners | `Start`, `Query`, checksums, inputs | Incompleto |
| `NetSession`/`NetConnection` | 0x108C31D0/0x108C3F90/0x108C4030 | No | Mapeado | Implementación | Seam de transporte |
| `ConsoleManager`/`Channel` | 0x108C2F60–0x108C5870 | No | — | ~270 funciones | Bloqueo principal |
| `networksockets` | 0x1093E5D0–0x1093FD20 | Sí | Mayoría | `GetLocalNetworkInfo` | Base UDP/TCP |
| Condiciones online (`engine`) | 0x108B0840 etc. | Parcial | 2 condiciones | 3 `Evaluate` | Parcial |
| Tracks/acciones online | 0x10264E40–0x102689E0 | Parcial | Propiedades/fábricas | `Evaluate`/acciones | Parcial |

---

## 6. ¿Puede funcionar `CoOpBehaviour` sin `OnlineManager`?

### 6.1 Lo que dice el código

**[HECHO]**

- `CoOpBehaviour` **no incluye ni referencia `OnlineManager` ni `OLS`** en su header/fuente reconstruidos.
- Su constructor solo llama a `engine::Behaviour` y registra la escucha de los tipos de mensaje `0x30`, `2` y `4`.
- Sus únicos miembros son tres punteros a `RespawnState`, `PlayerDeadState` y `PairingVolumeState`.
- `BehaviourController` (que despacha `Update` y `OnMessage`) sí está reconstruido y funciona.
- Las condiciones `OnlineNumPlayersCondition` y `OnlinePlayerIndexCondition` usan `InputSystem`/`GetControllingPlayer`, **no** OLS.
- `CoOpMessage` es un mensaje de tipo `0x30` que viaja por el sistema de mensajes del `GameObject`.

### 6.2 Lo que falta

**[HECHO]**

- `CoOpBehaviour::Update` (0x104DB8E0), `OnMessage` (0x104DB980) y el destructor (0x104DB960) no están reconstruidos.
- `RespawnState`, `PlayerDeadState` y `PairingVolumeState` tienen `GetMembers` vacío: **no se recuperó su layout**.
- Faltan `CoOpMessagePlayerDead`, `CoOpMessageSetRespawnState` y `CoOpMessageSetPairingVolumeState`.
- No hay evidencia de cómo se crea/asigna el compañero (`partner`) ni de dónde salen los mensajes entre jugadores.

### 6.3 Conclusión

**[HIPÓTESIS]** `CoOpBehaviour` **no necesita `OnlineManager` para existir ni para despachar mensajes locales**; podría funcionar en un escenario loopback si se reconstruyen sus métodos, se recuperan los layouts y se inyectan los mensajes. Pero **no funciona tal cual** y **no proporciona red**: la transmisión entre máquinas seguiría faltando.

`OnlineManager` sí es necesario para el reloj/sesión de red y para el stepping del driver; `CoOpBehaviour` es lógica de juego, no transporte.

---

## 7. ¿Existe una abstracción para sustituir Quazal?

### 7.1 Respuesta corta

**Sí, a nivel de `ChannelManager` / `Channel`** (los nombres `NetSession`/`NetConnection` de `gamelink.h` son suposiciones).  
**No, a nivel de `OLS`/Quazal completo.**

### 7.2 Detalle

**[HECHO]** Ghidra confirma que `OnlineManager+0x70` es un `ChannelManager` (0x7C bytes), creado en `FUN_108BF1B0`. Su método `Open` (0x108C31D0) crea entradas de 0x28 bytes, un `MultiTailQueue` (0x18) y un `Channel` (4). El valor devuelto se usa como conexión para `Send`/`Receive`.

**[HECHO]** La interfaz observada es pequeña:

- `ChannelManager::Open(kind, unknown, capacity)` → `Channel*`
- `Channel::Send(target, channel, data, length, unknown)`
- `Channel::Receive(buffer, capacity, &length, &flag, &source)`
- `Channel::IsReady()`
- `NetSystem` con `unknown20` como `target`

**[HECHO]** Usos conocidos:

- `StartSync` (0x10833380) abre kind **4** y guarda el `Channel*` en `gSyncConnection`.
- `SendSyncMessage` usa `target = GetNetSystem()->unknown20`, `channel = type`, 8 bytes, flag 1.
- `UpdateSync` drena `Receive` y usa `source` como máscara de máquinas.
- `GameLinkManager::Connect` abre kind **6**, canal `0xFF`; usa `Receive`/`Send`.

**[HECHO]** Las implementaciones de `ChannelManager`/`Channel` **no están reconstruidas**; el worklist marca `ConsoleManager`, `Channel` y `MultiTailQueue` como no hechos (~270 funciones, 0x108C2F60–0x108C5870 y 0x108C7210–0x108C8D20).

### 7.3 Implicación

**[HIPÓTESIS]** Se puede escribir un `ChannelManager`/`Channel` propio sobre UDP/Steam e instalarlo en `OnlineManager+0x70` (o interceptar `ChannelManager::Open`) para que `SyncObject` y `GameLinkManager` funcionen sin Quazal. Eso evita reconstruir el backend Quazal y permite conservar `SyncObject`, `Replay` y `DesyncHunter`.

Pero **no evita**:

- implementar la semántica de `kind` 4/6, `target`, `channel`, flags y `source`;
- crear/destruir la sesión y el lobby (`ConsoleManagerPeer`/`OLSPeer`);
- sustituir `OnlineClock`/`OnlineSync` para las transiciones de estado;
- reimplementar `CoOpBehaviour` y los mensajes;
- decidir si `OLSGame`/`OLSUserList` se usan o se puentean.

**Conclusión:** el seam `ChannelManager`/`Channel` es la vía más realista para **evitar Quazal**, no para evitar todo el sistema online.

---

## 8. Puntos de entrada para un plugin ASI

> Direcciones con image base `0x10000000`; en runtime: `base de prototypeenginef.dll + (addr - 0x10000000)`. No se hookeó nada; solo se identifican candidatos.

| Dirección | Función | Uso potencial |
|---|---|---|
| `0x1085EFB0` | `EngineInitialize` (export) | Bootstrap temprano del plugin; conoce los argumentos de lanzamiento |
| `0x1085EB20` | `EngineService` (export) | Bucle principal; ya usado por otros proyectos |
| `0x108BF5E0` | Init del subsistema online (llamada desde `EngineInitialize` en `0x1085F47E`) | **Hook real de arranque**: crea `OnlineManager` y `OnlineSync` |
| `0x108E7E80` | `OLSPeer` constructor | Entrada del host/join; llama al bootstrap de sesión |
| `0x108BF1B0` | Bootstrap de sesión host/cliente | **Hook principal**: crea `ChannelManager`, `ConsoleManagerPeer`, canales, `StartSync` y `DesyncHunterOnline` |
| `0x11318530` | `gOnlineManager` (global) | Acceso al gestor; `+0x70` es el `ChannelManager` |
| `0x108BF160` | `OnlineManager` constructor | Sustituir/envolver el driver online |
| `0x108BF780` | `OnlineManager::Slot4` | **Pump de red**: reloj, envío, recepción y dispatch |
| `0x108BF520` | `OnlineManager::UpdateClock` | Reloj online |
| `0x108BF690` | `OnlineManager::Shutdown` | Cierre de sesión |
| `0x108C31D0` | `ChannelManager::Open` | **Seam de transporte**: interceptar kind 4/6 |
| `0x108C3F90` | `Channel::Send` | Enviar por transporte propio |
| `0x108C4030` | `Channel::Receive` | Recibir por transporte propio |
| `0x108C40C0` | `Channel::IsReady` | Estado de conexión |
| `0x10833380` | `StartSync` | Iniciar sync de inputs; abre kind 4 |
| `0x108333D0` | `StopSync` | Cerrar sync |
| `0x10833790` | `SendSyncMessage` | Enviar press/release |
| `0x108336F0` | `UpdateSync` | Drenar mensajes de sync (llamado desde `SimulationTick`) |
| `0x10833450` | `ApplySyncMessage` | Aplicar mensaje de sync (NONMATCHING) |
| `0x108C7340` | `OnlineClock::GetTime` | Reloj de red (implementado en el binario) |
| `0x108C7750` | `OnlineSync` accessor | Sincronización de transiciones de estado |
| `0x108C7B50` / `0x108C7BE0` / `0x108C7820` | `OnlineSync` methods | Inicio/fin/consulta de transición |
| `0x104DAEC0` | `CoOpBehaviour` constructor | Crear el comportamiento en un GameObject |
| `0x104DB8E0` | `CoOpBehaviour::Update` | Lógica por frame; recuperada con Ghidra |
| `0x104DB980` | `CoOpBehaviour::OnMessage` | Manejo de mensajes co-op; recuperado con Ghidra |
| `0x10294380` | `MetaBehaviourCreatorImpl<CoOpBehaviour>::CreateBehaviour` | Crear instancia desde el sistema de behaviours |
| `0x1029BA60` | `RegisterMetaBehaviourCreator<CoOpBehaviour>` | Registro de la factoría |
| `0x10821520`–`0x10821560` | `DesyncHunter` statics | Arrancar/parar/actualizar QA |
| `0x108216A0` | `DesyncHunter::Update` | Comparar estado |
| `0x108BFA80` | `DesyncChecker` ctor | Crear checker |
| `0x108C2D30` | `Replay` ctor | Crear replay |
| `0x10900960` | `ReplaySimDriver::Start` | Arrancar grabación/reproducción |
| `0x108441E0` | `OLS::Get` | Acceso al singleton OLS |
| `0x10887DA0` | `OLSGame::TryRequestState` | Flujo de estado de sesión |

**Nota:** `0x104A29E0` es `SoundManager`, no online. `CoOpBehaviour::Update/OnMessage` no estaban en la fuente, pero sí en el binario y ya se recuperaron con Ghidra.

---

## 9. Subsistemas aprovechables y subsistemas a implementar

### 9.1 Aprovechables como referencia o pieza funcional

| Subsistema | Nivel | Por qué |
|---|---|---|
| `SyncObject` | **Alto** | Lógica completa de press/release, fábricas, reset y máscaras; solo falta transporte y llamadores |
| `networksockets` | **Alto** | Capa Winsock completa; base para un `NetSession` UDP/TCP |
| `Replay` (manager) | **Medio-alto** | Todos los métodos y scripts; falta el driver para arrancar/grabar |
| `OLSStorage` | **Medio** | Cola de archivos globales completa; solo faltan las operaciones de plataforma |
| `DesyncHunter`/`Checker` | **Medio** | Base de QA; `Verify` vacío y settings sin definir |
| `BehaviourController` | **Alto** | Dispatch/update de behaviours ya reconstruido; `CoOpBehaviour` puede colgarse |
| `OLSUserList` / `OLSGame` | **Medio-bajo** | Estructuras de usuarios/sesión; faltan métodos clave |
| `OnlineNumPlayers` / `OnlinePlayerIndex` | **Medio-bajo** | Funcionan en local sin OLS; útiles para condiciones simples |
| `GameLinkManager` | **Medio** | Patrón de `NetSession` kind 6, send/receive y packets; no es co-op |
| `CoOpBehaviour` shell | **Bajo** | Solo constructor/reflexión; falta toda la lógica |

### 9.2 A implementar / reconstruir

1. **Capa de red**: `ConsoleManager`, `Channel`, `MultiTailQueue`, `LoopbackConnection`, `ChannelConnection`, `NetMessagePusher/Puller` (rango ~270 funciones).
2. **Peers**: `OLSPeer`, `SyncManager`, `DesyncHunterOnline`, `NetSystemLink*`.
3. **Reloj/sincronización de estado**: `TimeSync`, `OnlineClock`, `OnlineSync`, `OnlineManager::Slot4/UpdateClock/Shutdown`.
4. **CoOpBehaviour**: `Update`, `OnMessage`, destructor, layouts de `RespawnState`/`PlayerDeadState`/`PairingVolumeState`, mensajes `PlayerDead`/`SetRespawnState`/`SetPairingVolumeState`.
5. **Replay**: `ReplaySimDriver::Start`, `Query`, `VerifyChecksum`, `RecordChecksum`, `LoadRecordedInputs`/`SaveRecordedInputs`.
6. **Condiciones/acciones online**: `Evaluate` de respawn/distancia/muerte/tipo y `CreateAction` de pairing/respawn/streaming.
7. **OLS/sesión**: `OLS` completo, creación del singleton, perfiles/usuarios/juego de plataforma, lobby/matchmaking.
8. **UI/flujo**: host/join, lista de jugadores, estados de conexión, manejo de errores.

---

## 10. Dificultades técnicas

1. **La capa de red es el bloqueo principal.** El worklist la declara “not done (judged too heavy for the time)”; son ~270 funciones de `ConsoleManager`/`Channel`/sync. Sin ella, `OLSPeer`, `SyncManager` y `DesyncHunterOnline` no se pueden reconstruir.
2. **Dependencia circular:** `OLSPeer`/`SyncManager`/`DesyncHunterOnline` derivan de `ConsoleManager`; si `ConsoleManager` no está, esas clases quedan bloqueadas.
3. **`OLS` está casi entero sin reconstruir.** Solo `Get()`; el layout es parcial, la creación del singleton no está en la fuente y los callbacks no están.
4. **`TimeSync`/`OnlineClock`/`OnlineSync` faltan por completo.** Son usados por el driver de simulación y por las transiciones de estado; sin ellos no hay reloj/estado online.
5. **`CoOpBehaviour` es un cascarón.** Faltan 0x5A + 0x183 + 0x16 bytes de lógica, los layouts de estado y varios mensajes. No se puede probar la lógica de respawn/pairing.
6. **`DesyncChecker::Verify` está vacío** y `gDesyncSettings` no se define en la fuente; el detector de desync no está operativo.
7. **`ReplaySimDriver` no arranca.** `Start`, `Query` y los checksums no están; `Load/SaveRecordedInputs` siguen en el motor.
8. **“Match” no es “usado”.** Hay funciones que coinciden pero no tienen llamadores reconstruidos (`StartSync`, `UpdateSync`); el código puede compilar y no ejecutarse nunca.
9. **Quazal está muerto.** `prototypepclive.quazal.net` resuelve a 127.0.0.1; no hay backend. Sustituirlo exige shim de `NetSession` o emulación de protocolo.
10. **El híbrido no está validado online.** El híbrido llega a free roam; no hay evidencia de una sesión de dos clientes, ni de que el path online se ejecute.
11. **Direcciones atadas al build.** `PROTOPC_2025` y el hash de DLL; cualquier actualización invalida los hooks.
12. **Licencia y distribución.** Source-only, no comercial, sin binarios híbridos; no se puede empaquetar un mod con código reconstruido. Nuestra regla prohíbe copiar código de terceros.

---

## 11. Cinco siguientes objetivos de investigación (por prioridad)

### 1. Trazar el arranque online y la creación de la sesión — **resuelto con Ghidra**

**Hecho:** `EngineInitialize` → `0x108BF5E0` inicializa `OnlineManager` y `OnlineSync`; `OLSPeer` (0x108E7E80) → `0x108BF1B0` arranca la sesión host/cliente. `0x104A29E0` es `SoundManager`, no online.  
**Pendiente:** confirmar en ejecución cuándo `OLSPeer` se instancia en single-player y si `0x108BF1B0` se llama sin sesión.

### 2. Reversar el seam `ChannelManager`/`Channel`

**Pregunta:** ¿qué semántica tienen `kind` 4/6, `target`, `channel`, `flag` y `source`? ¿Se puede implementar un shim sobre UDP/Steam que satisfaga a `SyncObject` y `GameLinkManager`?  
**Método:** `ChannelManager::Open` 0x108C31D0, `Channel::Send` 0x108C3F90, `Receive` 0x108C4030, `IsReady` 0x108C40C0, `GetNetSystem` 0x108C4260; comparar con `GameLinkManager::Connect` (kind 6) y `StartSync` (kind 4).  
**Criterio de salida:** especificación de la interfaz y un test de loopback de 8 bytes (mensaje `SyncMessage`).

### 3. Reconstruir la lógica de `CoOpBehaviour`

**Pregunta:** ¿qué hacen `Update` (0x104DB8E0), `OnMessage` (0x104DB980) y el destructor (0x104DB960)? ¿Qué layouts tienen los tres estados? ¿Qué mensajes construyen/consumen?  
**Método:** Ghidra sobre el rango 0x104DAD00–0x104DBC00; RTTI/strings de `CoOpMessage*`; cruzar con `BehaviourController` y `online_tracks`.  
**Criterio de salida:** tabla de mensajes, estados y transiciones; respuesta definitiva a si necesita `OnlineManager`.

### 4. Completar el harness de determinismo (`Replay` + `DesyncHunter`)

**Pregunta:** ¿el juego es determinista entre dos instancias con los mismos inputs? ¿Qué diverge primero?  
**Método:** completar `ReplaySimDriver::Start/Query/VerifyChecksum/RecordChecksum` y `Load/SaveRecordedInputs`; usar `Replay_Record`, `Replay_Replay`, `Replay_RecordDesync`, `Replay_ReplayDesync`; dos instancias aisladas.  
**Criterio de salida:** una grabación corta que se reproduce igual en dos instancias, o el primer punto de divergencia documentado.

### 5. Decidir la estrategia OLS/peer: shim vs reimplementación

**Pregunta:** ¿conviene implementar `ConsoleManager`/`Channel`/`OLSPeer`/`SyncManager` o basta un shim en `NetSession` y puentear OLS?  
**Método:** auditar `OLSPeer` 0x108E7E80–0x108E9CF0, `SyncManager`, `DesyncHunterOnline`, `NetSystemLink*`, `TimeSync`/`OnlineClock`/`OnlineSync`; estimar cuántas funciones usa realmente el modo co-op.  
**Criterio de salida:** decisión arquitectónica documentada (shim de bajo nivel vs reimplementación de OLS) y lista de funciones mínimas.

---

## 12. Limitaciones de esta auditoría

- **Auditoría inicial basada en el repositorio; el addendum 0.b sí inspeccionó el binario con Ghidra 12.1.4.** Las direcciones, estados y nombres originales provienen de `config/PROTOPC_2025/symbols.txt`, `classes.tsv`, `rtti.tsv`, `docs/engine_worklist.md` y el código fuente del repo; el addendum corrige los nombres que el binario desmiente.
- **No se compiló ni ejecutó** el proyecto; no se probó el híbrido ni una sesión de dos clientes.
- **No se pudieron inspeccionar** (no están en el repo):
  - `ConsoleManager`, `Channel`, `MultiTailQueue`, `LoopbackConnection`, `ChannelConnection`.
  - `OLSPeer`, `SyncManager`, `DesyncHunterOnline`.
  - `OnlineClock`, `OnlineSync`, `TimeSync` (solo RTTI/declaraciones).
  - `NetSession`/`NetConnection` (solo declaraciones).
  - `CoOpBehaviour::Update`, `OnMessage`, destructor.
  - `ReplaySimDriver::Start`, `Query`, checksums.
  - El layout completo de `OLS` y la creación del singleton.
- **No se leyó el worklist completo** (8.766 líneas); se leyeron las secciones relevantes (`onlinemanager`, `onlinescripting`, `sn-t`, `sn-w`, `sn-bj`, `sn-by`, `sn-er`, `sn-dy`, `sn-fr`, `sn-ei`, `sn-dn`, `sn-fs`). Otras secciones pueden contener detalles adicionales.
- **`DesyncChecker::Verify` vacío** puede ser un stub de la reconstrucción o el comportamiento real; no se verificó contra el binario.
- **La semántica de `NetSession`/`NetConnection`** (target/channel/kind) es desconocida; solo se infiere de los llamadores.
- **La licencia no es asesoría legal.** Antes de reutilizar cualquier fragmento, hay que revisar `LICENSE.md`/`LEGAL.md` y, si se distribuye algo, buscar asesoría.

---

## Apéndice — Direcciones y artefactos citados

| Artefacto | Dirección / ubicación |
|---|---|
| `proto::CreateOnlineManager` | 0x104A29E0 |
| `gOnlineManager` | 0x11318530 |
| `OnlineManager` ctor / dtor | 0x108BF160 / 0x108BF880 |
| `OnlineManager::Slot4` | 0x108BF780 |
| `OnlineManager::UpdateClock` | 0x108BF520 |
| `OnlineManager::Shutdown` | 0x108BF690 |
| `NetSession::Open` / `Close` | 0x108C31D0 / 0x108C3670 |
| `NetConnection::Send` / `Receive` / `IsReady` | 0x108C3F90 / 0x108C4030 / 0x108C40C0 |
| `GetNetSystem` | 0x108C4260 |
| `StartSync` / `StopSync` | 0x10833380 / 0x108333D0 |
| `SendSyncMessage` / `UpdateSync` | 0x10833790 / 0x108336F0 |
| `ApplySyncMessage` | 0x10833450 (NONMATCHING) |
| `OnlineClock::GetTime` | 0x108C7340 |
| `OnlineSync` accessor / métodos | 0x108C7750 / 0x108C7B50 / 0x108C7BE0 / 0x108C7820 |
| `CoOpBehaviour` ctor / Update / OnMessage / dtor | 0x104DAEC0 / 0x104DB8E0 / 0x104DB980 / 0x104DB960 |
| `MetaBehaviourCreatorImpl<CoOpBehaviour>::CreateBehaviour` | 0x10294380 |
| `RegisterMetaBehaviourCreator<CoOpBehaviour>` | 0x1029BA60 |
| `DesyncHunter::Update` / `Start` / `Stop` | 0x108216A0 / 0x10821680 / 0x10821690 |
| `DesyncChecker` ctor | 0x108BFA80 |
| `Replay` ctor | 0x108C2D30 |
| `ReplaySimDriver::Start` / `Query` | 0x10900960 / 0x109004B0 |
| `ReplaySimDriver::VerifyChecksum` / `RecordChecksum` | 0x10900690 / 0x109007C0 |
| `OLS::Get` | 0x108441E0 |
| `OLSGame::TryRequestState` | 0x10887DA0 |
| `GameLinkManager::Connect` | 0x10853B40 |
| `EngineInitialize` / `EngineService` | 0x1085EFB0 / 0x1085EB20 |

**Fuentes principales:** `README.md`, `LICENSE.md`, `LEGAL.md`, `CONTRIBUTING.md`, `docs/progress.md`, `docs/engine_worklist.md`, `docs/plan.md`, `config/PROTOPC_2025/symbols.txt`, `classes.tsv`, `rtti.tsv`, y las fuentes de `src/code/engine/online/`, `src/code/engine/system/`, `src/code/proto/coop*`, `src/code/proto/fightitems/online/`.
