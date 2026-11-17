# What the game needs from the engine: prone, crouch gaits, bipod deploy

Written 2026-11-16 from reading `engine/client/cl_frame.c` (lean player emit, about lines 2030-2120), `ref/gx/r_studio.c`
and `engine/common/pm_trace.c` of xash3d-gc master 019b67a61. The game side (dod-gc) can do the rules; these are the engine
pieces it cannot.

## Prone

1. **No new collision hull.** BSPs carry four clip hulls; `host.player_mins/maxs` (pm_trace.c:58) only describe those. Prone has to
   use the existing duck hull (36 units high). No engine change; the game sets `FL_DUCKING` and lowers the view.
2. **A state the lean emit can read.** The client emit reads `ed->v.*` of the server entity (it is a listen server). The game will set
   `pev->iuser3 = 1` (prone) and `pev->iuser4 = 1` (deployed). Please keep those two fields intact in the lean emit path and, if the
   client reads networked `entity_state_t` anywhere for players, make sure `iuser3/iuser4` are in `delta.lst` for players.
3. **Prone and crouch sequences in the lean emit** (cl_frame.c gait selection, about lines 2081-2100). Today: aim sequence is
   `stand_<token>_aim` / `crouch_<token>_aim`, the legs are `dod_walk/jog/sprint` by speed, and crouching only changes the aim token, so
   a crouch-walking player shows standing legs. Needed:
   - `FL_DUCKING` still: gait `dod_crouch_idle` (retail model sequence 14); moving: `dod_crawl` (13).
   - `iuser3 == 1` (prone): aim sequence `prone_<token>_aim` (falls back to `prone_idle` if the weapon has none), gait `prone_forward`
     (16) when speed > 10, none when still.
   - Sprint already uses `dod_sprint` above 200 u/s (the game's sprint speed is 300), so no change there.
4. **Studio allowlist** (`GC_STUDIO_MODELS` in build-gamecube-disc.py): the deployed third-person weapon models the game will switch to
   through `pev->weaponmodel`: `p_fg42bd`, `p_fg42bu`, `p_barbd`, `p_barbu` (BAR bipod down/up), `p_scopedfg42bu`, `p_30calbd/bu`,
   `p_mg42prbd`-style names as the retail models spell them. I will list the exact names once the game picks them; the set follows the
   retail `p_*bd/bu` models in `dod/models`.

## Deploy (bipod, sandbag) for MG, BAR, FG42

Game side, no engine help: toggle on crouch + attack2 with a wall/sandbag in front or prone, `DOD_ACC_DEPLOYED` spread (already in
`dod_weapons.c`), movement locked, `up_to_down` / `down_to_up` view animations (the sequences exist in `v_bar`, `v_fg42`, `v_30cal`,
`v_mg42`; `src/dod_viewanims.h` currently maps only the "up" set).

Engine side: (a) the view model must keep drawing the deployed sequences, which the lean viewmodel path already does for any
sequence the game sends; (b) item 4 above for the third-person models; (c) the shared `pm_shared.c` (built into both DLLs from the
SDK pin) is patched by the integrator in dod-gc, so client prediction matches the server: nothing for the engine to do, but it means
the lean client must keep running the client DLL's PM_Move (it does today).

## Order that makes sense

Crouch gaits first (item 3, first bullet): it fixes the standing-legs-while-crouching look on its own and is a few lines. Then prone,
then deploy.
