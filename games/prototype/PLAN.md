# Prototype co-op — MVP design

**Goal:** two players, online, free roam first. Host-authoritative once players can see each other.

## Why this path

Ghidra shows the retail build already has a deterministic input-sync path. Instead of replacing the whole online stack, the MVP keeps the engine's `SyncObject` and swaps only the transport at the `ChannelManager`/`Channel` boundary.

## Transport seam (verified addresses)

| Address | Function | Plan |
|---|---|---|
| `0x108C31D0` | `ChannelManager::Open` | Hook. Return a custom `Channel` for sync kind 4; call original for other kinds. |
| `0x108C3F90` | `Channel::Send` | Hook. If the channel is ours, send the packet over UDP; otherwise call original. |
| `0x108C4030` | `Channel::Receive` | Hook. If ours, fill the engine buffer from UDP; otherwise call original. |
| `0x108C40C0` | `Channel::IsReady` | Hook. Report the custom socket state. |
| `0x108C3670` | `ChannelManager::Close` | Hook. No-op for our channel. |
| `0x10833380` | `StartSync` | Leave original; it calls the hooked `Open` and stores our channel in `gSyncConnection`. |
| `0x108336F0` | `UpdateSync` | Leave original; it calls the hooked `Receive`. |
| `0x10833790` | `SendSyncMessage` | Leave original; it calls the hooked `Send`. |

## Session bootstrap

`EngineInitialize` -> `0x108BF5E0` creates `OnlineManager` + `OnlineSync`. `OLSPeer` (0x108E7E80) -> `0x108BF1B0` starts host/client. The MVP does not need to replace that bootstrap on the first attempt: hooking `ChannelManager::Open` after the session exists is enough to intercept kind 4.

If the original bootstrap does not run in single-player, add a small session starter that sets `gOnlineManager+0x70` and calls the original `StartSync` with a valid `SyncOwner`.

## Wire format

Start with the engine's own 8-byte `SyncMessage`:

```
factory: u8
value:   u8
index:   i32
```

The `Send` hook receives `(target, channel, data, length, flag)`. Send `length` bytes plus a small header (protocol version, sender id, sequence). The `Receive` hook fills the engine buffer and sets the source mask (host = 1, client = 2).

## Session config

`coop.ini` next to the game:

```ini
enabled=1
role=host        ; host | client
port=27016
peer=127.0.0.1  ; client only; later Steam relay
```

For the first test both roles run on one PC, so `peer=127.0.0.1` and different ports.

## Milestones

1. **Loopback sync:** both instances exchange `SyncMessage`s; the engine's `gSyncConnection` is our channel.
2. **Remote avatar:** spawn a second player object / puppet using the engine's player handles.
3. **Free-roam combat:** host owns enemies; damage/death events.
4. **Story:** mission/flag sync.
5. **Online transport:** Steam relay or a UDP relay; keep direct IP as fallback.

## Test plan

- Two isolated instances from `TestInstances/0` and `1`.
- Normal windows only; minimized launch crashes at `prototypeenginef.dll+0x615feb`.
- Back up `%USERPROFILE%\Documents\Prototype`; never write to the Steam install.
- Log every hook call to `coop.log`; assert `Send`/`Receive` lengths and sequence.
- QA level starts at `none`; it becomes `automated` only after a scripted two-client test.

## Open questions

- Does `0x108BF1B0` run in single-player, or only after `OLSPeer` is created?
- What is the real `SyncOwner->machines` mask for two players?
- Does `OnlineManager::Slot4` also pump the kind 4 channel, or only `UpdateSync`?
- Which vtable slots does the `Channel` object need besides `Send`/`Receive`/`IsReady`?
- Can `CoOpBehaviour` be attached to the local player object without a second physical player?
