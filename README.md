# Prototype Co-op — online co-op for Prototype (2009)

Online co-op for **Prototype (2009)** (Steam App ID `10150`): a second player joins your
city, sees you move, fight, transform and cast your powers — and you see them.

The mod is an **ASI plugin + input proxy**. It does **not** modify or ship any game file:
the Steam installation is never touched, everything runs from the copies in `TestInstances\`.

Based on the [coop-anything](https://github.com/cranebeaker/coop-anything) research toolkit.

[![Image](https://imgsharehub.com/i/zbhtc4bz.webp)](https://imgsharehub.com/es/zbhtc4bz)
[![Image](https://imgsharehub.com/i/rxvcu1da.webp)](https://imgsharehub.com/es/rxvcu1da)

---

## Progress

```text
Co-op core (transport, avatar, pose)   ████████████████████░░░░  82%
Powers & transformations               █████████████████░░░░░░░  72%
Effects (world + attached)             ██████████░░░░░░░░░░░░░░  42%
Stability / edge cases                 ████████████████░░░░░░░░  64%
Local co-op / split screen             ░░░░░░░░░░░░░░░░░░░░░░░░   0%
──────────────────────────────────────────────────────────────────
Overall                                █████████████████░░░░░░░  65%
```

## Features

| Area | State | Notes |
|---|---|---|
| Two players online | ✅ | Custom UDP transport, host-authoritative state |
| Remote avatar | ✅ | Full clone of the player template, spawned in the peer's world |
| Position & rotation | ✅ | ~20 Hz, smoothed on the receiver |
| Full-body animation | ✅ | 67-joint pose streamed at 30 Hz (head, torso, legs, arms) |
| Character model sync | ✅ | Equipped power's model is replicated to the remote avatar |
| Powers: Claws | ✅ | |
| Powers: Blade | ✅ | |
| Powers: Whip Fist | ✅ | arm + pose; the tendril itself is an attached effect (WIP) |
| Powers: Muscle Mass | ✅ | |
| Powers: Hammer Fist | ✅ | |
| World effects | ✅ | Ground spikes, explosions, devastator effects |
| Attached effects | ⏳ | Whip tendrils, armour glow (disabled: they duplicated the arms) |
| Disguise | ⏳ | Separate subsystem (`DisguiseMessage`), not replicated yet |
| Impact slow motion | ⚙️ | Removed by default to keep the two games in sync (`no_slow_motion=1`) |
| Local co-op / split screen | ❌ | Planned after online is solid |
| Steam install untouched | ✅ | The mod runs from copies in `TestInstances\` |

> Powers appear on the remote avatar as soon as the **receiving** game has the power's
> model loaded (it loads the first time that player equips that power). Preloading the
> models is the next milestone.

## How it works

Two instances of the game run side by side, each with the plugin. Each instance keeps a
local **puppet**: a copy of the player template spawned into the same map, whose pose is
driven by the packets from the other game.

- **Transport:** UDP, one socket per role (`27016` host / `27017` client). The engine's own
  sync session is not used.
- **Packets:** `P1` position, `R1` object basis (facing), `A1` 67-joint pose (quaternions +
  translations), `S1` transformation slots, `M2` character model name, `E1` world effect,
  `M1` motion trail (whip).
- **Pose:** read from the player's `Attribute<Pose*>` (`0x10E80688`), sent at 30 Hz,
  nlerp-smoothed on the receiver and pushed into the puppet's pose.
- **Transformations:** observed from the player's `TransformationBehaviour` slot table
  (input-agnostic: wheel, D-pad, quick keys, pad), filtered by `hudPower` and applied to
  the puppet with `StartTransformationMessage` only when `transform_start=1`. The shipped
  path replicates the **model swap** instead, which is what the game really does.
- **Model swap:** `RenderBehaviour +0x18` holds the drawable archetype name. The plugin
  streams it, resolves the archetype by hash in the local content inventories and instances
  it on the puppet (`CreateInstance` → `SetDrawable` → reuse of the streamed pose →
  `Drawable::SetPose`).
- **Effects:** `EffectAction::Slot5` is hooked on the player; only actions that target the
  local player are forwarded, then the archetype is resolved locally and `StartEffect` is
  called, so no process-local pointer ever crosses the network.

### Puppet setup

The puppet keeps only its visual behaviours. 19 of its 38 behaviours are disabled on spawn:
combat/threat/physics ones so it cannot hurt or push anyone, and the motion drivers
(`CharacterMotionBehaviour`, `MotionTreeBehaviour`, `MotionStateBehaviour`, `IKBehaviour`)
so they cannot overwrite the streamed pose.

## Configuration

`coop.ini` next to the game executable:

```ini
[coop]
role=host            ; host | client
port=27016
peer_port=27017
no_slow_motion=1     ; 1 = remove the impact slow motion (keeps both games in sync)
sync_model=1         ; replicate the equipped power's model
transform_start=0    ; 1 = also start transformations on the puppet (debug)
sync_attached=0      ; 1 = replicate attached effects (debug, can duplicate arms)
```

## Building

Requires the 32-bit MinGW toolchain used by the project (`tools\build-coop.ps1`).

```powershell
.\tools\build-coop.ps1
```

Outputs `build\coop\prototype_coop.asi` and `build\coop\xinput1_3.dll`.

## Testing

The repository keeps two prepared copies of the game in `TestInstances\0` and
`TestInstances\1` (not the Steam install, no game assets are committed).

```powershell
.\tools\run-coop.ps1        # fast-boot args, both windows side by side
.\tools\capture.ps1         # screenshot both windows at the same instant
```

## Roadmap

1. Preload the power models on both sides (no more "equip it once").
2. Attached effects: whip tendrils, armour glow, motion trails.
3. Disguise subsystem.
4. Health / damage / grab interactions between players.
5. Local co-op (split screen).
6. Polish: stability, packet rate, desync recovery.

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
personaje moverse, luchar, transformarse y usar los poderes — y tú ves el suyo. El mod es un
plugin ASI más un proxy de entrada; **no modifica ni distribuye archivos del juego**.

Estado actual: transporte, avatar remoto, posición/rotación, animación completa de 67
joints, sincronización del modelo del personaje, los cinco poderes (Claws, Blade, Whip,
Muscle Mass y Hammer Fist) y los efectos de mundo. Pendiente: precarga de modelos, efectos
anclados (tentáculos, armadura), disfraz, daño/salud y co-op local.
