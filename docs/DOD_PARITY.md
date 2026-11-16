# Day of Defeat parity: retail behaviour vs this port

Goal: the game code behaves like retail Day of Defeat (HLMP as the base, DoD on top). A difference is
allowed only as a GameCube optimisation or a platform fact, and is listed here with its reason.
Status words: **done** (seen working in Dolphin), **partial**, **missing**, **unverified**.
Evidence below comes from Dolphin logs on `dod_charlie` (libogc recipe, 3 slots) and from scanning the
retail maps' entity lumps against the entities this DLL exports; it is not a complete audit.

## Map entities (entity lump vs exported spawn functions)

Scan of retail `dod_*.bsp` entity lumps against `gamecube_server_entity_exports.inc` (275 classes):

| Class | Maps / count | Retail meaning | Ours | GC deviation (why) |
|---|---|---|---|---|
| `dod_capture_area` | charlie 8, avalanche 5, anzio 2 | Brush trigger; players of the capturing team inside it capture a point over `area_time_to_cap` seconds (`area_axis_numcap`/`area_allies_numcap` needed, `area_*_cancap` gating, fires `area_*_startcap`/`area_*_endcap`/`target`, optional `master`) | **missing** (`No spawn function`) | none intended |
| `dod_point_relay` | charlie 4 | Relay between capture events and map logic | **missing** | none |
| `dod_round_timer` | charlie 1 | Round clock entity | **missing** (clock is in the rules core) | none |
| `dod_location` | caen 21 | Named area for the HUD location text | **missing** | HUD text only |
| `info_initial_player_axis/allies` | avalanche 16 | Spawn points used at round start | **missing** (falls back to `info_player_*`) | none |
| `particle_shooter` | charlie 17, caen 7 | Particle emitter | **missing** | optional: particles are budgeted on GC (24 particles, 8 beams, 8 tempents) |
| `weapon_piat`, `weapon_fg42`, `weapon_pschreck` (caen) | caen 3+3, charlie 1 | Pickup weapons | **partial** (table covers 14 guns; PIAT/FG42 not in the table) | none |
| `dod_control_point`, `dod_control_point_master`, `dod_object`, `dod_score_ent`, `dod_trigger_sandbag` | exported | Objectives, scoring, sandbag zones | **partial**: simplified sphere capture instead of the retail brush logic | none |
| `info_player_axis/allies` | all maps | Team spawns | **done** (bots and humans spawn on them) | spawn entities survive heap starvation in `SV_LoadFromFile`; most other entities are skipped when the heap is empty (GC memory) |
| 484 entities "game-rejected" on charlie | charlie | decorative/logic entities | skipped | GC heap budget at map load (documented in the engine) |

`dod_flash` has no missing classes in this scan.

## Rules and match flow

| Retail | Ours | GC deviation |
|---|---|---|
| Team play rules (`CHalfLifeTeamplay` + DoD) | `CHalfLifeDoD`, `mp_teamplay` forced for the dod gamedir; **done** | none |
| Rounds, score, objectives, round clock | rules core (`dod_game.c`) with host tests; **partial** (objectives are the simplified ones above) | none |
| Class selection menu (VGUI) | text menu (`DAY OF DEFEAT - CHOOSE A SIDE`); **partial** | lean HUD, no VGUI |
| Scoreboard, objective HUD, map | `dod_score` text, `cl_dodhud` flags/clock; **partial** | lean HUD memory |
| Bots (retail has none; community bots) | `dod_bot.cpp`, team-balanced, respawn; **done** | up to 2 bots (3 slots) for memory |
| Server slots | 3 (`XASH_GC_DEFAULT_SLOTS=3`) | MEM1: each slot costs about 60 KB of the free heap at client init |

## Weapons and combat

| Retail | Ours | GC deviation |
|---|---|---|
| Per-weapon stats, fire modes, recoil, accuracy, reload | table-driven (`dod_weapons.c`), 14 guns; **partial** | none |
| Bolt/semi/auto, bipod/deploy, sandbags, zoom, scope | bolt/semi/auto and deploy flag present; zoom/scope/sandbag **unverified** | none |
| Bazooka/Panzerschreck rockets | `dod_rocket` entity; **unverified** | explosions budgeted (`dod_grenade`) |
| Grenades (cook, throw) | **partial** | explosion effects budgeted |
| Gun starts loaded | **fixed** in `CDodWeapon::Spawn` (`m_iClip = 0`; with -1 every gun was permanently empty) | none |
| Bullets hit players | needs engine traces (xash3d-gc #95 stub traces only without a world-DL pack); **done** with it | lean server has no world trace hull when the baked world pack is absent |
| Weapon sounds | **missing** on GC (late precache of sounds stalled client resource verification) | sound precache disabled |

## Players, animation, models

| Retail | Ours | GC deviation |
|---|---|---|
| Player model per team/class with DoD sequences chosen by the game DLL (`stand_<weapon>_aim`, shoot, reload, prone, bipod, death) | server player model is a hull-only stub, so `pev->sequence` stays 0; the client emitter picks `stand_<weapon>_aim` / `dod_walk|jog|sprint` by label (xash3d-gc #87); **partial** | model memory: bodies split into a mesh core plus an animation pack streamed from ARAM |
| Class models (`axis-inf`, `us-inf`, para, brit) | `axis-inf` / `us-inf` via `dod_axis_body` / `dod_allies_body`; other bodies staged (axis-para, brit-inf, us-para) but not selected by class; **partial** | split into core + animpack (ARAM) |
| Third-person weapon (`p_*`) | not resident (`weapon models/p_mp40.mdl index 301 not resident`); hands hold nothing | studio budget |
| First-person weapon (`v_*`) | `v_garand` draws untextured grey; 30cal/mg42 skipped by the budget | studio budget (`deferred studio budget skip`) |
| Death (ragdoll-free, `die_*` sequences) | **unverified** | none |
| Spectating | chase/in-eye/free cameras added (dod-gc #25/#27) | lean HUD; spectator state set up in the lean client |

## Known gaps in verification

- No full-match test (objectives, round end, team win) has been run in Dolphin.
- The Allies bot has not scored a frag in the 2-bot runs (Axis bot kills it repeatedly); cause not yet found.
- Tilt/bob/punch in first person not tested with pad input.
