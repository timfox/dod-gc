# GameCube memory policy (dod-gc)

MEM1 tip is tiny. **Never preload asset farms.** Stream a working set.

## Proven (keep doing this)

| Asset | Disc | Runtime |
|-------|------|---------|
| World textures | `media/wt_*.gct` | ARAM residency → MEM1 hot window / CMPR bounce |
| CapFaces geom | `media/cf_*.gcp` (+ spatial) | Offline Flipper draw; not a full live surface table |
| Studio prop DLs | `media/sd_<map>.gsd` | Rest-pose mapmodels → ARAM + `GX_CallDispList` (no CPU skin) |
| Lightmaps | disc LM → ARAM (G154) | Per-face hydrate into ~32 KiB MEM1 buf |
| Menu music / UI | `media/gamestartup.wav`, `launch_*.wav` | Stream / play after GameUI is up |

## Disc laziness (required for intro/menu)

| Asset | Rule |
|-------|------|
| Mapmodels | **BSP-referenced only** via `scripts/stage_bsp_mapmodels.py` (cap 32 / 4 MiB). Fail the stage if someone dumps `models/mapmodels/`. |
| Map WAD | Under `maps/<stem>.wad`, **not** gamedir root (root `*.wad` auto-mounts at `FS_Rescan`). |
| Shared `dod.wad` / `dod_shared.wad` | Do not put in gamedir root on interactive discs. |
| Core SFX / FX sprites / world DL | Opt-in (`--core-sfx`, `DODGC_STAGE_FX_SPRITES`, `DODGC_STAGE_WORLDDL`). |
| Full class / `player.mdl` arsenal | Opt-in (`DODGC_STAGE_ALL_WEAPONS=1`). CapFaces team VMs only by default. |

## Why “load everything” OOMs

Interactive boot with ~160 mapmodels + root map WAD left GameUI at ~27 KiB,
then `Mem_FreeBlockBig` during font upload — before Valve intro or menu music.
Preflight (flash, lean file set) reaches `gc_menu_ready` with ~270 KiB free.

## Studio models

| Piece | Status |
|-------|--------|
| Disc laziness | BSP-referenced MDLs only (`stage_bsp_mapmodels.py`) |
| Draw efficiency | `tools/bake_studio_dl.py` → `media/sd_<map>.gsd`; runtime `r_gx_studiodl.c` |
| Animated (player / VM) | Still CPU skin + strip emit (cannot be one static DL) |
| MDL tip residency | MDLs still `Mod_ForName` for skins/bbox; mesh draw uses DL when present |

Bake: `python3 tools/bake_studio_dl.py --map content/dod/maps/dod_avalanche.bsp`.
Disable at runtime with `-gcnostudiodl`.
