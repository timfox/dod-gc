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
public C API. A complete Create Game match is not verified yet. A small-map
listen-server probe from the linked, in-progress engine checkout reached map
load and local signon using static edicts. The isolated engine PR build also
selects the static pool, but its current Dolphin run stalls during early map
setup and ends in a DSI before map-ready; that build is not gameplay-verified.
`dod_avalanche` remains blocked: an 800 KiB spill build fails while reloading
the 190.59 KiB BSP node lump; a 960 KiB experimental build reaches signon but
crashes during the ordinary intro/menu boot before `gc_menu_ready`. Neither
large-map experiment is accepted as the normal gameplay build. The 800 KiB
normal boot reaches the Valve intro and `gc_menu_ready`; the Create Game
controller route has not yet produced a map-start marker or a valid guest
frame. The regular two-player Create Game route still needs a clean runtime
capture.

The Garand viewmodel is now textured and framed in the lower center of the
screen in the latest valid in-game capture
(`playtests/screenshots/20261110-182518-ava-vm15/final.png`). The world behind
it remains visibly fragmented, with large missing or misplaced surfaces, so
this is not yet a playable-world claim. Earlier runs also recorded a 1.72 MiB
BSP-surface allocation failure and a DSI after map startup. Map-bearing play
ISOs enable the packed BSP renderer by default so low-memory loads can select
the CapFaces path when the live surface table cannot fit.

## Grenades

`dod_grenade.cpp` puts the DoD hand grenade on top of the HL1 `CHandGrenade` /
`CGrenade` code, which still does the pin pull, throw, bounce and fuse timing.
Class loadouts issue grenades (`Dod_GetLoadout`), and the rules core
(`Dod_GetGrenade` and friends in `dod_game.c`) holds the numbers:

- Allies throw the Mk2 grenade, Axis the stick grenade (`v_grenade`/`p_grenade`/
  `w_grenade`, `v_stick`/`p_stick`/`w_stick`). Values are approximate DoD 1.3:
  5 second fuse that cooking shortens (a grenade held past its fuse goes off in
  the thrower's hand), 100 damage at the centre falling off linearly to a 250
  unit radius, 800 unit/second maximum throw.
- Damage is applied with the engine's `RadiusDamage`, so team damage rules and
  kill credit work as for any other weapon. The thrower can be hurt.
- The explosion replaces `CGrenade::Explode` for DoD grenades only (marker in
  `pev->iuser3`); rockets and every other user of `CGrenade` are untouched.

### Memory

Everything here is sized for MEM1 + ARAM:

- **No new sprites.** The fireball, smoke and underwater sprites are the ones HL1
  already precaches for every weapon.
- **Effect budget.** `Dod_FxPlanExplosion` decides which layers one blast sends
  (fireball, smoke, sparks, debris). A lone blast sends all four; blasts landing
  within two seconds of each other shed debris, then sparks, then smoke, down to
  the fireball alone. The fireball and damage are never skipped. The HL1
  `spark_shower` entities are replaced by client-side `TE_SPARKS`, so a blast
  costs no edicts.
- **Live cap.** At most `DOD_MAX_LIVE_GRENADES` (8) grenades exist at once, so
  the static edict pool cannot be exhausted by throwing spam. A new pin cannot
  be pulled while the cap is full.
- **Viewmodels** ride the engine's studio allowlist, are lean-stripped into
  `gc_studio/` with a downsampled skin companion like the other DoD viewmodels,
  and are cached whole in ARAM after their first load.
- **Audio.** The eleven grenade clips are cut to fit the 16 KiB WAV ceiling
  (about 110 KiB in total) and listed in `media/c0a0.prefetch`, so the engine
  copies them into ARAM during menu idle instead of reading them from disc
  mid-fight. Half-Life discs get no such entries.

Both viewmodels use the `pinpull` / `throw` / `draw` / `holster` / `idle`
sequences through the engine's `gc_vmanim` remap.

Not verified in Dolphin yet: viewmodel animation, throw feel, the explosion and
particle rendering, and the Mk2 versus stick world models. The rules core, the
integration anchors, audio staging and the prefetch manifest have host tests.

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

`Dod_FormatStatus` builds the one-line match status (team scores, held points,
round clock) that `CHalfLifeDoD::Think` sends once per second as a HUD text
channel. It is a stand-in for the retail scoreboard and has not been run on
hardware or Dolphin.

Run the host-side rules-core checks with `python3 tests/test_dod_game.py`.

Do not patch HL1 release artifacts (`halflife1.dol`, `boot.dol`, or
`xash3d-gc.iso`) from this repository.
