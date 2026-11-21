# Maya → Xash3D GameCube: the short workflow

**One time:** `gsbridge compiler` (builds the ZHLT compilers), and make sure `~/gsbridge/dodgc_live.dol` exists (see DESIGN.md).

**Every day**
1. `gsbridge up`: builds the live disc if missing, starts the watcher, starts Dolphin on it.
2. In Maya: GoldSrc menu (or `import maya_gsmenu; maya_gsmenu.install()`), open the panels: **GoldSrc Bridge**, **Object Manager**, **Map Manager**.
3. **New map:** GoldSrc > New Map… → a sealed template room with spawns, doddetect, two control points, sky and lights, saved to `~/gsbridge/maps/<name>.mb` and added to the Map Manager.
4. Build with brushes under `gs_world` (convex meshes; texture = shader name from the map's WAD), place entities from the Object Manager.
5. **Save** (or Export + Compile + Push). In about a second the map is compiled with lighting, baked into the engine's own render packs and written into the disc; the game reloads and puts you back where you stood. The panel shows compile / bake / patch times, "game reloaded in N s", or the error in plain words.
6. Mistakes are caught before they reach the game: missing spawns, a missing `info_doddetect`, non-convex brushes and unknown textures are reported in Maya and nothing is pushed if the map cannot work.

**A real disc:** `gsbridge hw <map_name>` builds `~/gsbridge/out/<map_name>.iso` from the current Maya map (lit, full render packs, retail HUD, boots straight into the map). Run it in Dolphin, or put it on the SD/USB for Swiss.

**Levels and missions:** Map Manager lists every stock DoD map plus yours: sub-levels, next map, objectives; *Export rotation* writes `mapcycle.txt`, `maps.lst`, `progression.json` to `~/gsbridge/export`.

**When something is off:** `gsbridge status`; `~/.cache/gsbridge/watch.log`; the compile logs are summarised in the panel (leaks, missing textures, bad brushes).

## Make your first map (10 minutes)
1. `gsbridge up` (first run builds the compilers and the live disc; later runs just start things).
2. Maya: GoldSrc > **New Map…**: name `dod_myvalley`, pick a WAD (`dod_caen.wad` has the Normandy set) and a sky. You get a sealed room with spawns, control points and lights, already saved and linked to the game.
3. Edit: move or resize the brushes under `gs_world` (convex boxes; the shader name is the texture), drop props and entities from the **Object Manager**, assign textures from the **GoldSrc Bridge** texture list. Everything exports on **Save**.
4. Watch the Dolphin window: the map reloads about 6 s after you save and puts you back where you stood. Errors show in the panel in plain words and nothing broken reaches the game.
5. Open **Map Manager**: set the title, objectives, where the map sits in the cycle (the "next map" chain), then *Export rotation*.

## The packaged final disc
`gsbridge release` builds `~/gsbridge/out/dodgc-release.iso`:
- all 22 stock maps and every Maya map in the Map Manager that has a saved scene (it uses the last exported `.map` of each, kept in `~/gsbridge/maps/`);
- `maps.lst` (the Create Game list, in project order) and `mapcycle.txt` (the rotation; only maps on the disc);
- retail HUD, the engine's baked render packs for every map, and a build stamp: `build-info.txt` on the disc and `dodgc-release.iso.build-info.txt` beside it (dod-gc, engine, game SDK and submodule commit hashes).
It boots like the normal DoD disc (intro, menu); the map list in Create Game follows your project. Put the ISO on SD/USB for Swiss, or run it in Dolphin.
Limits: Maya maps with `env_model` props need a baked prop pack to show them on hardware (the build continues without one); one run takes a few minutes the first time because every map's render packs are baked, then it is cached.
