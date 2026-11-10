# DoD game code (HL1SDK-based)

Steam Day of Defeat ships **assets only** for this port. The retail client is
vgui2-blocked in stock Xash3D, and Valve has not released DoD game source.

Runtime plan:

1. Start from **HL1SDK / hlsdk-portable deathmatch** (linked via `../hlsdk-portable`).
2. Re-implement Day of Defeat **game modes and features** against those DM
   foundations (teams, classes, objectives, weapons, HUD hooks).
3. **Mirror retail DoD assets** from `../content/dod` (models, maps, sounds,
   sprites, materials) without redistributing them in git.
4. Link the result into `dodgc.dol` via the xash3d-gc GameCube HLSDK path
   (`HLSDK_GAMECUBE_GAMEDIR=dod`), packaged as `out/dodgc.iso`.

## Rules core

| File | Role |
|------|------|
| `dod_game.h` / `dod_game.c` | Engine-independent server rules state: Allies/Axis and player classes, team/player scoring, capture point progress, passive objective scoring, round clock, and round winner/intermission state. |

The rules core is not yet called by the HLSDK server. Connect its lifecycle,
player, objective, and HUD events to the HLSDK game DLL before treating these
rules as active in a packaged match. The current runtime still links the
HL1SDK deathmatch archives under `OUT/hlsdk-gamecube/dod`.

The C API uses 1-based client slots and objective IDs. `Dod_TickObjective`
returns 1 only when ownership changes; contested zones pause capture progress.
Round score is awarded for enemy kills, completed captures, and held objectives
when their configured scoring rate is nonzero.

Do not patch HL1 release artifacts (`halflife1.dol`, `boot.dol`,
`xash3d-gc.iso`) from this repo.
