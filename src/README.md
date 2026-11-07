# DoD game code (HL1SDK-based)

Steam Day of Defeat ships **assets only** for this port. The retail client is
vgui2-blocked in stock Xash3D, and Valve has not released DoD game source.

Plan for this tree:

1. Start from **HL1SDK / hlsdk-portable deathmatch** (linked via `../hlsdk-portable`).
2. Re-implement Day of Defeat **game modes and features** against those DM
   foundations (teams, classes, objectives, weapons, HUD hooks).
3. **Mirror retail DoD assets** from `../content/dod` (models, maps, sounds,
   sprites, materials) without redistributing them in git.
4. Link the result into `dodgc.dol` via the xash3d-gc GameCube HLSDK path
   (`HLSDK_GAMECUBE_GAMEDIR=dod`), packaged as `out/dodgc.iso`.

Do not patch HL1 release artifacts (`halflife1.dol`, `boot.dol`,
`xash3d-gc.iso`) from this repo.
