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
