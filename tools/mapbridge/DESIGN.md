# GoldSrc map bridge: Maya -> Dolphin (design + prototype status)

Goal: edit a DoD level in Maya, press Save, see it in the GameCube port running in Dolphin within seconds, using stock GoldSrc formats (.map -> v30 BSP + stock WADs).

## Pipeline

```
Maya (gs_world meshes + gs_classname locators)
  | maya_export.py  (scriptJob on SceneSaved)         [BUILT, tested in mayapy]
  v  ~/.cache/gsbridge/live.map   (Valve-220 .map, atomic write)
watch.py (ceres, polls mtime)                         [BUILT, tested]
  | sdHLCSG -> sdHLBSP [-> sdHLVIS -> sdHLRAD]        [BUILT from source, bin/]
  v  live.bsp  (v30, references dod_caen.wad, not embedded)
isoslot.py: pwrite into the fixed-size ISO extent of /xash3d/dod/maps/dod_live.bsp, then bump
  /xash3d/dod/livemap.txt ("<serial> dod_live"), trigger written LAST   [BUILT, tested on a copy]
Dolphin serves the .iso from the host file; engine DVD reads see new bytes [reasoned, NOT yet observed]
engine-livemap.patch: SV_GCLiveMapFrame polls livemap.txt ~1/s, serial change -> `map dod_live`
                                                      [WRITTEN, applies cleanly, NOT built, NOT run]
```

## What already existed (reused)
- `dod-punch-maya/tools/open_in_maya.py`: Maya command port :7001 pattern (not needed on the Maya -> ceres leg, which is file based; a command port can later push "recompile now").
- `scripts/stage-lean-dod.sh`, `ship-allgeo-map.sh`, `bake_worlddl_goldsrc.py`: the offline bake (world DL `wd_*.gcd`, `wt_*.gct` textures, CapFaces `cf_*.gcp` + spatial pk3). For a live map these are skipped; the engine falls back to rendering the live BSP (stage-lean log: "CapFaces/live BSP fallback"). Baked variants are the next step and can become more fixed-size slots.
- `engine -gcchangelevel/-gcmap`, `probecmd`: boot-time only. There is no runtime command channel, hence the small poll patch.
- dod-usd-v2 / goldsrc_to_usd.py: BSP -> USD import into Maya (the reverse direction: use it to pull existing stock maps into Maya as a starting scene; not wired here).

## Key findings that shaped the design
1. The disc is a hybrid: a GC header/FST bootstrap plus an ISO9660 tree (`/xash3d/dod`, `/xash3d/valve`) that the engine reads itself with raw DVD sector reads (`iso9660_gamecube.c`, one-sector cache). The GC FST does NOT map to the game files, so Dolphin's folder-disc (DirectoryBlob) mode cannot host this layout. Dolphin reads the .iso lazily from the host file, so in-place same-size patching is the hot path.
2. The disc builder cannot be re-run per save (xorriso + layout), but a file extent is fixed once built: pad `dod_live.bsp` (<= 3 MiB engine BSP buffer) and `livemap.txt` at build time, then patch in place.
3. Compiler: SDHLT (vluzacn/ZHLT lineage, github seedee/SDHLT) built clean on ceres. Not installed before. Speed: room test, CSG+BSP 0.01 s, with VIS+RAD ~1 s. A real DoD map will be much slower (RAD dominates): fast loop = CSG+BSP fullbright; `--vis/--rad` for lit checks.
4. Maya has no brush model. Convention: each mesh under `gs_world` must be convex (one brush); texture = shader name from the map's WAD; entities = transforms with string attr `gs_classname` (+ `gs_key_*`). Non-convex meshes must be split.

## Not done / blocked
- Engine patch not built into a DOL and no Dolphin run yet: another thread holds /tmp/dolphin-ceres.lock and others are queued ahead. `dolphin_demo.sh` implements the queue/lock protocol but is an untested stub (no screenshot step).
- Disc with the slots: needs a build with a zero-padded `dod_live.bsp` (<= 3 MiB) staged via stage-lean-dod.sh --map dod_live, plus `livemap.txt` padded to 2048 bytes. Not built.
- Unverified: whether Dolphin returns patched sectors to a running game without staleness (stdio buffering in PlainFileReader).
- Stock-map accuracy: the exporter writes plain brushes with box-projected textures; no UV round-trip, no brush-entity origin handling, no detail/water/trigger conventions yet.

## Build recipe for the live-map DoD disc (retail HUD, full render path)
1. Game code: clone the pinned SDK privately (`scripts/sources.lock` in dod-gc), then
   `scripts/build-dodgc.sh --dol-only` with DODGC_ENGINE=<engine worktree>, DODGC_HLSDK=<private SDK clone>,
   HLSDK_GAMECUBE_BRANCH=<sdk branch>, DODGC_ALLOW_STALE_MAINUI=1 (the engine's mainui pin is a symlink blob).
2. Engine DOL: `XASH_GC_PM_MAX_PHYSENTS=256 XASH_GC_PM_MAX_MOVEENTS=32` MUST be exported for the engine build exactly as
   build-dodgc.sh does; the game archives are built with those playermove_t caps and a mismatch corrupts the stack in
   PM_SortTextures at spawn (that was the "retail HUD crash").
3. Disc: `tools/mapbridge/build_slot_disc.sh <dol> <iso> <seed.bsp>` (cfg has `nohud`: the retail client HUD draws, the engine's
   placeholder TEAM/HP/AMMO bars do not).
4. Watcher: `live_session.sh <iso>` compiles, bakes wt/wd/lf/sb packs and patches every slot in place on each save.
Notes: VIS output from the ZHLT fork made the world draw white in-engine; watch.py leaves VIS off unless --vis is given.
