# Isolated layout

```
dod-gc/
  engine/          → symlink to xash3d-gc (build tooling, GX, disc scripts)
  hlsdk-portable/  → symlink to engine's 3rdparty/hlsdk-portable
  content/
    dod/           → symlink to Steam Half-Life/dod (owned assets)
    valve/         → symlink to Steam Half-Life/valve (DoD basedir fallback)
  src/             → local DoD game-mode port (HL1SDK DM → DoD features)
  scripts/         → dod-gc only build / playtest / capture helpers
  out/
    dodgc.dol      → DoD-linked DOL (copied from engine OUT/bin/dodgc.dol)
    dodgc.iso      → DoD disc image (never named xash3d-gc.iso)
  playtests/
    screenshots/   → stills from progress + playtests
    videos/        → ffmpeg captures (mp4 + json sidecars)
    notes/         → dated markdown checkpoints
    INDEX.md       → video index
```

## Isolation rules

| Artifact | HL1 port | dod-gc |
|----------|----------|--------|
| DOL | `halflife1.dol` / `boot.dol` | `out/dodgc.dol` |
| ISO | `OUT/xash3d-gc.iso` | `out/dodgc.iso` |
| Dolphin user dir | `~/.local/share/xash3d-gc/dolphin` | `~/.local/share/dod-gc/dolphin` |
| Playtest media | engine `.ai/playtests/` | `playtests/` |
| Config | `~/.config/xash3d-gc/` | local scripts + `playtests/notes` |

Engine builds still run *inside* the linked xash3d-gc tree (waf/devkitPro),
but disc publish and evidence always land under `dod-gc/out` and
`dod-gc/playtests`.
