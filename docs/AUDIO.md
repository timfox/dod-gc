# Day of Defeat GameCube audio

Lean discs no longer omit menu UI clicks or map soundscapes. Staging is driven by
`scripts/stage_dod_audio.py` (called from `scripts/stage-lean-dod.sh`).

## Main menu

| Asset | Role |
|-------|------|
| `media/gamestartup.wav` | Looping menu music (PCM from retail `gamestartup.mp3`) |
| `media/launch_upmenu1.wav` | Menu open |
| `media/launch_dnmenu1.wav` | Menu close |
| `media/launch_select1.wav` / `launch_select2.wav` | Activate / confirm |
| `media/launch_glow1.wav` | Hover |
| `media/launch_deny1.wav` / `launch_deny2.wav` | Rejected action |

`launch_deny2.wav` is pulled from Valve `sound/common/` when DoD `media/` lacks it.

## Per-map soundscapes

Every `dod_*.bsp` entity lump is scanned for `.wav` paths (`message`, `noise*`,
and any other embedded wav string). Files are staged under `sound/` with:

1. exact DoD hit
2. case-insensitive DoD / Valve hit
3. known substitutes for retail-missing names (e.g. `Opera.wav` → radio bed)

**22 / 22 maps stage with 0 missing refs** (126 unique wavs, ~36 MiB).

## Core gameplay SFX

`--core-sfx` (explicit; **not** implied by `--full-map`) stages:

`weapons`, `player`, `common`, `ui`, `items`, `doors`, `buttons`, `debris`, `object`

(~375 files / ~20 MiB). Leaving this on by default OOMs interactive intro/menu
via FS searchpaths — same class of bug as dumping every mapmodel.

## Mapmodels (MEM1 tip)

`--full-map` stages **only** studio MDLs the BSP entity lump names, via
`scripts/stage_bsp_mapmodels.py` (hard caps: 32 files / 4 MiB). The whole
`models/mapmodels/` tree must never ship on a Cube disc. World textures already
stream ARAM→MEM1; props are disc-lazy + PRECACHE at map load.

## Commands

```bash
# Menu music + UI only (default when no --map)
scripts/stage-lean-dod.sh

# Every map soundscape + menu + core SFX
scripts/stage-lean-dod.sh --all-maps-audio --core-sfx

# One map: BSP-referenced props only + that map's soundscapes (no core SFX)
scripts/stage-lean-dod.sh --map dod_flash --full-map

# ISO helpers
scripts/build-lean-iso.sh --menu-only
scripts/build-lean-iso.sh --smoke dod_avalanche --full-map --all-maps-audio
```

Inventory JSON is written to `$LEAN/audio-manifest.json` (also mirrored under
`playtests/notes/audio-manifest-all-maps.json` from the last full audit).

## Weapon sounds (retail parity)

Retail DoD plays only the gunshot from the game DLL. The reload clips, bolt cycling and the draw rustle are
animation events (event 5004) stored in the view models, so each gun has its own sequence of sounds at fixed
offsets (the MP40 reload is `tommy_reload_clipout` at 0.30 s, `tommy_reload_clipin` at 1.65 s,
`tommy_draw_slideback` at 2.20 s; the Kar98k shot cycles `boltback` / `boltforward`; the bazooka reload is three
different clips). The GameCube DoD route does not run view model events, so `CDodWeapon` plays them itself from
`src/dod_viewsounds.h`, which `tools/gen_viewsounds.py` writes from the retail models:

```bash
tools/gen_viewsounds.py --models <retail dod>/models --write src/dod_viewsounds.h
tools/gen_viewsounds.py --models <retail dod>/models --check src/dod_viewsounds.h
```

`scripts/stage_dod_audio.py` stages every sound named there with the other weapon clips.

## Retail sound audit (2026-11-18)

How it was checked: the retail game DLL's precache list (`strings dlls/dod.so | grep .wav`, 430 names) and the
animation events inside `models/v_*.mdl`. What changed is in the weapon, player-event and announcer sections above;
the integrator rewrites the SDK's stock HL names in `integrate_retail_sounds`.

Open items: gun and explosion clips are staged cut to 0.45-0.7 s (retail explode3/4 are 3-5 s); bullet whizz
(`weapons/whizz1-17`), per-surface bullet impacts (`weapons/hit_*`), mortar and knife/grenade draw sounds are not
played; the lean play route skips bullet-impact sounds in the SDK.

## Captures to take (output/media on ceres)

Take each when the feature is checked in Dolphin; name files `sound_<feature>_<map>.png|mp4`.

| Feature | Capture |
|---------|---------|
| Reload / bolt / draw sounds | clip: Kar98k shot + reload, MP40 reload, bazooka reload (audio on) |
| Pain and death | clip: a bot hits the player, then kills them |
| Wood / gravel / snow steps | clip: walking a wooden floor, gravel and snow on maps that have them |
| Round start and point captured | clip: a capture and the next round start; still of the HUD message |
| Clip lengths (when done) | clip: a grenade explosion at full length |
