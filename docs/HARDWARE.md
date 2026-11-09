# Real GameCube hardware handoff (DoD)

Swiss / SD Loader path for `out/dodgc.dol` and lean listen ISOs. Emulator CapFaces
geometry + **real WAD/CMPR textures** (ARAM bounce) are proven on Dolphin IPC
(`playtests/notes/20261107-120407-capfaces-aram-bounce.md`); this is the on-device
boot recipe.

## Artifacts

| File | Role |
|------|------|
| `out/dodgc.dol` | Swiss-loadable DOL (`XASH_GAMECUBE_LINK_BBA=1`) |
| `out/dodgc-flash-solo.iso` / `out/dodgc.iso` | Offline solo listen + CapFaces (no net) |
| `out/dodgc-flash-host.iso` | Lean listen + net (BBA peer host; headless local) |
| `out/dodgc-flash-client.iso` | Lean connect peer (optional second Cube) |
| `out/gamecube-handoff.txt` | Build stamp / renderer / loader metadata |

Rebuild:

```bash
scripts/build-dodgc.sh --dol-only --skip-hlsdk
scripts/build-lean-iso.sh --listen dod_flash --full-map --world-render --net --net-ip 10.0.0.2
cp -f out/dodgc.iso out/dodgc-flash-host.iso
# optional peer:
scripts/build-lean-iso.sh --connect 10.0.0.2:27015 --map dod_flash --full-map --world-render --net --net-ip 10.0.0.3
cp -f out/dodgc.iso out/dodgc-flash-client.iso
```

## Solo Cube (Swiss / SD)

**Use the offline listen build** (`out/dodgc.iso` / `out/dodgc-flash-solo.iso`).
Do **not** stage `netstatic` on solo — `-net`/`-netstatic` keeps the host headless
(no local CapFaces). Rebuild:

```bash
scripts/build-lean-iso.sh --menu-only --listen dod_flash --world-render --skip-startup-vids
cp -f out/dodgc-menu.iso out/dodgc-flash-solo.iso
cp -f out/dodgc-menu.iso out/dodgc.iso
```

1. Copy `out/dodgc.iso` / `apps/dod-gc/boot.dol` to Swiss SD (`gamecube.cfg`:
   `listenserver dod_flash` + `gcworldrender` only).
2. Boot via Swiss. Expect: `MAP_READY dod_flash`, CapFaces prefetch `e=384`,
   `G151 GX world live`, `CapFaces drawn>0` (not status-panel).
3. Controllers: GameCube pad on port 1.
4. Optional BBA later: separate `--net` host ISO (peer slots; no local CapFaces).

Dolphin stand-in (2026-11-07): EDGE CapFaces proven (`e=384/384`, `drawn≈365`,
pack `77/77`). `swiss-evidence.txt` on FAT remains the Cube playability gate.

## Two Cubes (networking)

1. Host: flash-host ISO (static IP e.g. `10.0.0.2`).
2. Peer: flash-client ISO (`--connect <host>:27015`, peer IP e.g. `10.0.0.3`).
3. Same LAN. Peer should reach `CL_SignonReply: 2` then CapFaces `drawn>0`.

## Emulator stand-in before hardware

```bash
scripts/dual-bba-ipc-join.sh out/dodgc-flash-host.iso out/dodgc-flash-client.iso
```

Proven (Dolphin solo listen): CapFaces `packed=77/77`, `CMPR ARAM bounce`,
`drawn≈371`, sticky wall-aim + labeled WAD stills (SAND/WOOD)
(`playtests/screenshots/20261107-133342-flash-solo-sky77/`,
`playtests/notes/20261107-133342-sky77-pack.md`). Dual IPC textured
proof remains `20261107-120407-capfaces-aram-bounce.md`.

## Validation checklist

- [ ] Swiss boots `dodgc.iso` / `apps/dod-gc/boot.dol` without Guru
- [ ] World CapFaces visible with real textures (not magenta/noise / status-panel)
- [ ] `xash3d/dod/logs/swiss-evidence.txt` lists CapFaces bake + drawn + CMPR bounce
- [ ] Pad input moves view / player
- [ ] ETH2SP1: `net ready`, both players active, movement/firing replicated
- [ ] Internet: sustained play from two separate networks with host UDP forwarding

## Staged Swiss SD (this machine)

FAT32 volume mounted at `/run/media/tim/EXTRA` (hp v220w):

| Path | Contents |
|------|----------|
| `apps/dod-gc/boot.dol` | Current offline solo `out/dodgc.dol` (synced with root `boot.dol`) |
| `boot.dol` / `dodgc.dol` | Same DOL |
| `dodgc.iso` | Offline solo listen CapFaces (`dod_flash` + `gcworldrender`, no netstatic) |
| `xash3d/dod/gamecube.cfg` | `video prog` / `listenserver dod_flash` / `gcworldrender` |
| `xash3d/dod/logs/` | Target for `swiss-evidence.txt` after Cube run |
| `DOD-GC-HW.txt` | On-card boot card |

Unmount → insert in GameCube SD adapter → Swiss → `apps/dod-gc/boot.dol` or `dodgc.iso`.

After a run, remount and collect `xash3d/dod/logs/swiss-evidence.txt` into `playtests/notes/`.
**Cube boot evidence is the remaining playability gate** (no USB Gecko required).


Current native ETH2SP1 build and matching host/peer images: see [NETWORKING.md](NETWORKING.md). Earlier headless BBA probes do not validate two-player hardware play.
