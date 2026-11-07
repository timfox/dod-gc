# dod-gc

Isolated GameCube port of **Day of Defeat**, built on the **xash3d-gc** engine
and **HL1SDK** deathmatch code while mirroring legally owned Steam DoD assets.

This repo does **not** overwrite the Half-Life 1 GameCube artifacts
(`halflife1.dol` / `boot.dol` / `xash3d-gc.iso`). The deliverable here is
**`out/dodgc.iso`**.

## Layout

| Path | Points at |
|------|-----------|
| `engine/` | xash3d-gc checkout (symlink) |
| `content/dod/` | Steam `Half-Life/dod` (symlink) |
| `content/valve/` | Steam `Half-Life/valve` basedir fallback (symlink) |
| `hlsdk-portable/` | HL1SDK used as the DM/code base (symlink) |
| `src/` | Local DoD game-mode / feature port work |
| `out/dodgc.iso` | DoD disc image |
| `playtests/` | Screenshots, videos, and notes from every playtest |

See [docs/LAYOUT.md](docs/LAYOUT.md).

## Setup

Symlinks are created for this machine as:

```bash
engine        → /run/media/tim/Expansion/xash3d-gc
content/dod   → …/Steam/steamapps/common/Half-Life/dod
content/valve → …/Steam/steamapps/common/Half-Life/valve
```

Re-link if paths move:

```bash
ln -sfn /path/to/xash3d-gc engine
ln -sfn "/path/to/Half-Life/dod" content/dod
ln -sfn "/path/to/Half-Life/valve" content/valve
```

## Build `dodgc.iso`

```bash
scripts/build-dodgc.sh          # DOL + ISO into out/
scripts/build-dodgc.sh --play   # play-oriented disc cfg
scripts/build-dodgc.sh --dol-only
scripts/build-dodgc.sh --iso-only
```

Requires the same GameCube toolchain as xash3d-gc (devkitPro / libogc2).

## Playtest + media

Progress screenshots and session videos are **recorded by default** into
`playtests/` (not the HL1 `.ai/playtests` tree):

```bash
scripts/playtest.sh --build --shot --note "first DoD boot smoke"
scripts/capture-progress.sh --screenshot "menu chrome matches retail"
scripts/capture-progress.sh --from-file ./frame.png "map load"
```

- Videos → `playtests/videos/*.mp4` + `.json` sidecars  
- Stills → `playtests/screenshots/`  
- Notes → `playtests/notes/`  
- Index → `playtests/INDEX.md`

## Approach

Steam DoD provides **assets only** (source unavailable; client is vgui2-blocked
on stock Xash). We take HL1SDK deathmatch features and port DoD game modes /
systems on top, matching retail assets under `content/dod`. Engine multi-game
notes live in `engine/docs/gamecube/MULTI_GAME.md`.
