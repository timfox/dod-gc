# Day of Defeat GameCube — broadband / online

DoD online play rides the xash3d-gc GameCube net stack (DOL profile `dod`,
`XASH_GAMECUBE_LINK_BBA=1` → libbba).

## Hardware / adapters

| Path | Slot | Notes |
|------|------|--------|
| Official **Broadband Adapter** | Slot B | Retail BBA (EXI ch1.dev0) |
| **ETH2SP1** (W5500/ENC28J60) | SP1 | Swiss BBA emulation (v0.6r2018+) — **this is what libbba probes** (EXI ch0.dev2) |
| Dolphin **EthernetIPC** (EXI=14) | SP1 | **Same-machine dual-guest** L2 (proven join) |
| Dolphin **BuiltIn** (EXI=12) | **SP1** | Single-guest hardware-equivalent for eth2sp1; Slot B alone → `if_config` fails with current libbba |
| Dolphin **TapServer** (EXI=11) | SP1 | Needs local L2 hub; guest TX was silent in prior probes |

Swiss: enable BBA emulation for eth2sp1 so libogc sees the same stack as retail BBA.
BuiltIn probe: `DODGC_BBA_PLACE=sp1 scripts/builtin-bba-netprobe.sh` → `net ready` + listen.

### Why dual-guest uses EthernetIPC (not BuiltIn)

Dolphin BuiltIn binds real host UDP sockets for guest ports (`BuiltIn.cpp`
`InitUDPPort` / `HandleUDPFrame`). Two guests on one PC both need `27015` →
second instance logs `Couldn't open UDP socket` / port-in-use and never reaches
`net ready` (see `playtests/notes/*-dual-builtin-join.md`). EthernetIPC shares
raw L2 frames over a named IPC channel instead, so both guests keep guest IPs
`10.0.0.2` / `10.0.0.3` without host port collisions — that is the dual-emulator
stand-in for two Cubes + eth2sp1/BBA on a LAN.

## Guest bring-up

```text
-net                 # early DHCP (soft-fail if no adapter)
-netstatic [a.b.c.d] # static IP for dual-guest / LAN peers
-nonet               # skip bring-up
```

Disc `gamecube.cfg` lines (lean ISO staging):

```text
net
netstatic 10.0.0.2
listenserver <map>
# peer:
netstatic 10.0.0.3
connect 10.0.0.2:27015
```

OSReport markers: `net bring-up`, `net ready ip=… (eth2sp1/BBA)`, or soft-fail.

## Proven dual-guest join (emulator)

```bash
# Host listen + client connect ISOs — DoD retail map (dod_flash)
scripts/build-lean-iso.sh --menu-only --listen dod_flash --net --net-ip 10.0.0.2 \
  --full-map --skip-startup-vids
cp out/dodgc-menu.iso out/dodgc-flash-host.iso

scripts/build-lean-iso.sh --menu-only --connect 10.0.0.2:27015 --net --net-ip 10.0.0.3 \
  --map dod_flash --full-map --skip-startup-vids
cp out/dodgc-menu.iso out/dodgc-flash-client.iso

scripts/dual-bba-ipc-join.sh out/dodgc-flash-host.iso out/dodgc-flash-client.iso

# BuiltIn BBA (EXI=12) single-guest — put adapter on SP1 (libbba locus)
DODGC_BBA_PLACE=sp1 scripts/builtin-bba-netprobe.sh
```

Success bar (see `playtests/notes/*-dual-ipc-join.md`, `*-builtin-bba-netprobe.md`):

- host: `net ready`, `Server IPv4 …:27015`, `MAP READY`, `client frames BSS slot=0`
- client: `Connecting…`, `Serverdata`, signon / peer present path
- BuiltIn: `Created Built in network interface` + `net ready` (SP1 only; Slot B alone fails)

Scripts only `pkill` Dolphin instances whose `-u` path is under `dodgc-ipc-` /
`dodgc-dual-` / `dodgc-builtin-` / `dodgc-fly-` — never bare `dolphin-emu`.

## Hardware play (real Cube + BBA / eth2sp1)

1. Build `out/dodgc.dol` with `XASH_GAMECUBE_LINK_BBA=1`.
2. Burn / Swiss-load a lean listen ISO (`--listen <map> --net`) or full pack with
   `gamecube.cfg` listen + netstatic on the host Cube.
3. Peer Cube: same DOL, `--connect <host-ip>:27015 --net --net-ip <peer-ip>`,
   and stage the same map (`--map` / `--full-map`) so `-gcmap` tip / disc world works.
4. Both on one LAN — prefer **eth2sp1 on SP1** (matches libbba probe). Retail Slot B BBA
   needs a libbba that also probes EXI ch1 (current cube libbba is SP1-centric).
5. Host listens; peer connects. Expect the same SignonReply path as IPC.

## Status

- Engine: `engine/platform/gamecube/net_gamecube.*` (via this repo’s `engine/` symlink).
- DoD DOL builds default `XASH_GAMECUBE_LINK_BBA=1` (libbba); without it join is offline-stub only.
- Emulator dual join: **proven** on EthernetIPC — stalkyard and **dod_flash** (2026-11-07),
  with `net ready`, `sendto`, `Serverdata`, `game-rejected=0`.
- BuiltIn BBA: **proven** single-guest listen/`net ready` on SP1 (EXI=12). Dual BuiltIn on one
  host **fails** (UDP bind) — use IPC for two Dolphins; use BuiltIn for eth2sp1-shaped bring-up.
- Hardware BBA / eth2sp1: same guest code path; validate on device with the steps above.
- Lean menu discs are SP bring-up friendly; online maps need `--full-map` (or fuller packs).
- Compact BSP (strip LUMP_LIGHTING) + world-array spill required for dense DoD maps under BBA MEM1.
