# Day of Defeat game code

This port uses the HL1SDK-based `hlsdk-portable` game DLL because Steam DoD
provides assets but no game source, and its retail client depends on VGUI2,
which the current Xash3D GameCube build does not support.

## Runtime bridge

`dod_game.c` and `dod_game.h` implement engine-independent team, class, scoring,
capture, and round state. `dod_runtime.cpp` and `dod_runtime.h` connect that
state to the HL1SDK game DLL:

- The `dod` game folder selects `CHalfLifeDoD`, a teamplay rules class.
- Players choose Allies or Axis, then a class, through GoldSrc `ShowMenu`.
  The GameCube HUD integration lets the pad navigate and confirm those menus.
- Class choices select DoD view and player models and issue a compatible HL1
  weapon as the firing and ammunition implementation.
- Team spawn entities use `info_player_allies` and `info_player_axis`.
- `dod_control_point` entities register capture objectives. Living players
  within 128 GoldSrc units capture a point in ten seconds; both teams in range
  pause capture. The rules core tracks scoring and round state.

This is an initial rules bridge, not a complete recreation of retail DoD. The
menus are text menus, class weapons use HL1 weapon behavior, and control points
use a fixed spherical capture area. HUD score presentation, weapon behavior,
and objective handling can be extended without changing the rules core's
public C API. A complete Create Game match is not verified yet. The menu and
Create Server dialog render, and recent solo listens reach signon and draw the
DoD HUD, but the world is fragmented and the Garand viewmodel covers most of
the screen (`playtests/screenshots/20261110-174021-ava-vm3/final.png`). The
matching trace reports a viewmodel NDC Y span of `[-42.21, 20.76]` despite
emitted Garand bounds of only `9×27×9` world units. Earlier Create Game runs
also recorded a 1.72 MiB BSP-surface allocation failure or a DSI after map
startup. The ordinary surface path exposed a retained-BSP overlap on the
low-memory Create Game route; map-bearing play ISOs now enable the packed BSP
renderer by default, which selects the CapFaces lean path when the live
surface table cannot fit.

## Build integration

`scripts/integrate-dod-gameplay.py` copies the tracked runtime into the linked
HLSDK source tree and applies the DoD hooks to the game DLL, client HUD, and
engine. It checks its insertion anchors so a changed upstream source layout
fails the build instead of silently skipping an integration. The DoD build
invokes this overlay before building the GameCube archives and DOL:

```sh
scripts/build-dodgc.sh --dol-only
```

The build uses DoD assets from `content/dod`; those assets are not stored in
this repository. The resulting DOL is packaged as `out/dodgc.iso` by the
normal build command.

## Rules core API

Player slots and objective IDs are 1-based and 0-based respectively. A
contested objective pauses capture progress. `Dod_TickObjective` returns 1
when ownership changes. Round scoring covers enemy kills, completed captures,
and held objectives when the configured scoring rate is nonzero.

Run the host-side rules-core checks with `python3 tests/test_dod_game.py`.

Do not patch HL1 release artifacts (`halflife1.dol`, `boot.dol`, or
`xash3d-gc.iso`) from this repository.
