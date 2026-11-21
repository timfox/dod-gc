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
