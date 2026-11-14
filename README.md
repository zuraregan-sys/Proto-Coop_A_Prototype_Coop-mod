# Prototype Co-op - online co-op for Prototype (2009)

Online co-op for **Prototype (2009)** (Steam App ID `10150`): a second player joins your
city, sees you move, transform and cast your powers - and you see them.

The mod is an **ASI plugin + input proxy**. It does **not** modify or ship any game file:
the Steam installation is never touched, everything runs from the copies in `TestInstances\`.

Based on the [coop-anything](https://github.com/cranebeaker/coop-anything) research toolkit.

[![Image](https://imgsharehub.com/i/zbhtc4bz.webp)](https://imgsharehub.com/es/zbhtc4bz)
[![Image](https://imgsharehub.com/i/rxvcu1da.webp)](https://imgsharehub.com/es/rxvcu1da)

---

## Progress

```text
Co-op core (transport, avatar, pose)   #################---  82%
Powers & transformations               ###################-  92%
Effects (world + attached)             ###-----------------  15%
Stability / edge cases                 ##############------  64%
==================================================================
Overall                                ############--------  61%
```

## Features

| Area | State | Notes |
|---|---|---|
| Two players online | ok | Custom UDP transport, host-authoritative state |
| Remote avatar | ok | Full clone of the player template, spawned in the peer's world |
| Position & rotation | ok | ~20 Hz, smoothed on the receiver |
| Full-body animation | ok | 67-joint pose streamed at 30 Hz (head, torso, legs, arms) |
| Powers (all five) | ok | Applied through the game's own transformation system: model, left arm and the game's transform effect |
| Equip / unequip | ok | Both directions; reverting to normal hands uses the stock "no power" name |
| Armour & shield | ok | Same path as the arm powers |
| Whip Fist | local only | The whip cannot be worn by two actors in one process (engine limit), so the remote puppet keeps its previous appearance; the connecting tendril is not replicated either |
| City (characters) | partial | Characters around each player are mirrored on the peer while both players are close; the copies are static (no AI, animation or physics) and the local world is not suppressed yet |
| Effects | partial | World effects (spikes, explosions) replicate; attached effects and motion trails pending |
| Disguise | pending | Separate subsystem (`DisguiseMessage`), not replicated yet |
| Impact slow motion | removed | Disabled by default to keep the two games in sync (`no_slow_motion=1`) |
| Steam install untouched | ok | The mod runs from copies in `TestInstances\` |

### City replication

The host does not replicate actors, only **positions and a category**. Each
snapshot (`C2`, 4 Hz, up to 32 entries) carries, per actor, its id, a category
(civilian, soldier, infected, mutant, vehicle, military vehicle) and a quantized
position. The peer puts **its own** actor of that category at the position: any
soldier for a soldier, any car for a taxi, any infected for a zombie. Nothing
about animation, physics, facing or the exact model crosses the network.

The representative template of each category is learned from the peer's own
world (it is cached from the first actor of that category it sees, anywhere in
the level), with a fallback chain: a mutant falls back to an infected, an
infected to a civilian/soldier, a soldier to a civilian, and a military vehicle
to a normal vehicle. Military vehicles are not spawned from a tank template
because the physics setup of a duplicated tank faults in the engine.

The stream is gated by the distance between players (on below 150 m, off above
200 m) and the copies are static. Both windows therefore show the same actors in
the same places, in the same broad categories.

## How it works right now## How it works right now (for testing)

Two instances of the game run side by side, each with the plugin. Each instance keeps a
local **puppet**: a copy of the player template spawned into the same map, whose pose is
driven by the packets from the other game.

- **Transport:** UDP, one socket per role (`27016` host / `27017` client). The engine's own
  sync session is not used.
- **Packets:** `P1` position, `R1` object basis (facing), `A1` 67-joint pose (quaternions +
  translations), `S1` transformation slots.
- **Pose:** read from the player's `Attribute<Pose*>` (`0x10E80688`), sent at 30 Hz,
  nlerp-smoothed on the receiver and pushed into the puppet's pose.
- **Transformations:** the player's `TransformationBehaviour` slot table is observed
  (input-agnostic: wheel, D-pad, quick keys, pad). The `S1` packet carries the slot name
  and the power's description name as 64-bit hashes; the receiver resolves the name in its
  own game and calls the stock Lua helpers `go_SetDesiredTransformation` and
  `go_EnableTransformationForSlot` on the puppet. The engine then does everything itself:
  model, left arm, pose re-fit, transform effect.
- **HUD isolation:** the engine also publishes the applied form into the local player's
  quick-select database value (`HUDPlayerTransform`). Those writes are dropped while the
  plugin is applying a power to the puppet, so the remote player's power never touches the
  local player's own selection.

### Puppet setup

The puppet keeps only its visual behaviours. 19 of its 38 behaviours are disabled on spawn:
combat/threat/physics ones so it cannot hurt or push anyone, and the motion drivers
(`CharacterMotionBehaviour`, `MotionTreeBehaviour`, `MotionStateBehaviour`, `IKBehaviour`)
so they cannot overwrite the streamed pose.

The avatar is also removed from the engine's animation system (`choreo::Puppet`): the pose
that arrives over the network is the only animator. Keeping the clone in the animation job
queue eventually deadlocked `choreo::RunPuppetJobs` (the game thread waited forever on a
job) after a few minutes of free roam; without it the avatar still animates, transforms and
runs for a whole session.

## Configuration

`coop.ini` next to the game executable:

```ini
[coop]
role=host            ; host | client
port=27016
peer_port=27017
no_slow_motion=1     ; 1 = remove the impact slow motion (keeps both games in sync)
lua_powers=1         ; apply powers through the game's own Lua transformation helpers
sync_model=0         ; legacy archetype swap (unused while lua_powers=1)
dump_lua=0           ; 1 = log the power catalog, HUD writes and every Lua call
whip_puppet=0        ; 1 = also apply the whip to the puppet (crashes: engine limit)
puppet_nochoreo=1    ; 1 = keep the avatar out of the engine's animation jobs (default)
city_radius=300      ; stream the actors around each player within this radius
city_census=0        ; 1 = log the city composition around the player every 2 s
city_near=60         ; reserved (the category stream does not filter by it)
city_vehicles=1      ; also replicate vehicles (its own local template)
city_pose=0          ; reserved (driving the copies' poses faults in the engine)
city_dump=1          ; 1 = log template/behaviour of the actors around the player
```

## Building

Requires the 32-bit MinGW toolchain used by the project (`tools\build-coop.ps1`).

```powershell
.\tools\build-coop.ps1
```

Outputs `build\coop\prototype_coop.asi` and `build\coop\xinput1_3.dll`.

## Play with friends

1. Both players need their own **Prototype (Steam, App ID 10150)** and
   **PrototypeFix** installed (PrototypeFix is the ASI loader the game uses).
2. Build the mod and create the friend package:

   ```powershell
   .\tools\build-coop.ps1
   .\tools\make-package.ps1
   ```

   `dist\ProtoCoop-<build>.zip` contains only the mod (`prototype_coop.asi`),
   a `coop.ini` template and a plain-words `LEEME.txt`, no game files.
3. Both copy `prototype_coop.asi` next to `prototypef.exe` and edit `coop.ini`:

   | | Host | Guest |
   |---|---|---|
   | `role` | `host` | `client` |
   | `port` | `27016` | `27017` |
   | `peer_port` | `27017` | `27016` |
   | `peer_ip` | the guest's address | the host's address |

4. Same network: use each other's local IP (`ipconfig`). Over the Internet the
   simple path is a mesh VPN (Tailscale, ZeroTier, Hamachi/Radmin) and the VPN
   address as `peer_ip`; otherwise forward UDP 27016/27017 and open them in the
   firewall. Allow `prototypef.exe` on private networks when Windows asks.
5. Progress: on one PC both instances share the save folder. For two PCs copy
   the host's slots first with `tools\sync-save.ps1`.

## Saves

On one PC both instances share `%USERPROFILE%\Documents\Prototype`, so they
already start with the same progress. For two PCs, `tools\sync-save.ps1` copies
the host's `slot-*.bin` files into the client's save folder (hash checked, the
client's previous files are kept in `Backup\saves-<time>`):

```powershell
.\tools\sync-save.ps1 -From "C:\Users\me\Documents\Prototype" -To "\\HOST\PrototypeSaves"
```

## Testing

The repository keeps two prepared copies of the game in `TestInstances\0` and
`TestInstances\1` (not the Steam install, no game assets are committed).

```powershell
.\tools\run-coop.ps1        # fast-boot args, both windows side by side
.\tools\capture.ps1         # screenshot both windows at the same instant
```

## Roadmap

1. Animate the city copies (stream the nearest few poses, or let them run an idle).
2. Whip Fist: give the remote puppet a whip representation that does not load the arm
   content twice (the engine cannot share it between two actors), and replicate the
   tendril that connects the blade to the arm.
3. Effects: world effects (ground spikes, explosions) are replicated; attached effects and
   motion trails still duplicate or crash when replayed.
4. Disguise subsystem.
5. Weapons on the ground and vehicles for the players.
6. Story mode.
7. Health / damage / grab interactions between players (optional, likely annoying).
8. Polish: stability, packet rate, desync recovery, co-op menu.

## Notes

Deep engine findings (addresses, crashes, layouts) live in
[`games/prototype/NOTES.md`](games/prototype/NOTES.md) and
[`DECOMP_AUDIT.md`](DECOMP_AUDIT.md).

## Legal

This project contains **no game code or assets**. It is a plugin that runs on top of a
legitimately owned copy of Prototype (2009). Prototype and all related assets are property
of their respective owners.

---

## Resumen (ES)

Co-op online para **Prototype (2009)**: un segundo jugador entra en tu ciudad y ve tu
personaje moverse, luchar, transformarse y usar los poderes - y tu ves el suyo. El mod es un
plugin ASI mas un proxy de entrada; **no modifica ni distribuye archivos del juego**.

Estado actual: movimiento, avatar remoto, posicion/rotacion, animacion completa de 67
joints, **Garras, Cuchilla, Puno de demolicion, Masa muscular, armadura y escudo** con
equipar y desequipar en ambos sentidos y el efecto de transformacion del propio juego, y el
Latigo funcionando en el jugador que lo usa. Se aplican con la API Lua nativa del motor
sobre el actor remoto, con aislamiento del HUD local. Pendiente: una representacion del
Latigo para el actor remoto (el motor no permite cargar su contenido dos veces en el mismo
proceso), su tendril, los efectos anclados, el disfraz y el dano/salud.
