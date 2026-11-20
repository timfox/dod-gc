# Bot coverage by game mode

Generated from each map's BSP entity lump (`content/dod/maps/*.bsp`) and `data/waypoints/`. Modes are read from the entities, not from a map's name.

| Map | Flags | Areas needing >1 player | Satchel/TNT areas | Toggle walls | Round timer | Paras | Waypoints | Bots can play it |
|---|--:|--:|--:|--:|--:|---|---|---|
| dod_anzio | 5 | 2 | 0 | 0 | 0 | - | no | no waypoint graph: steers by look-ahead only; needs a group (crowd rule exists) |
| dod_avalanche | 5 | 3 | 0 | 0 | 0 | - | yes | needs a group (crowd rule exists) |
| dod_caen | 6 | 0 | 0 | 15 | 0 | - | no | no waypoint graph: steers by look-ahead only; toggle walls: breach on stall |
| dod_charlie | 4 | 0 | 8 | 6 | 1 | - | yes | satchel objective: no plant logic; toggle walls: breach on stall |
| dod_chemille | 4 | 0 | 0 | 0 | 0 | - | no | no waypoint graph: steers by look-ahead only |
| dod_donner | 5 | 1 | 0 | 0 | 0 | - | no | no waypoint graph: steers by look-ahead only; needs a group (crowd rule exists) |
| dod_escape | 3 | 0 | 2 | 1 | 0 | detect,detect | no | no waypoint graph: steers by look-ahead only; satchel objective: no plant logic; toggle walls: breach on stall |
| dod_falaise | 5 | 3 | 0 | 3 | 0 | detect,detect | no | no waypoint graph: steers by look-ahead only; toggle walls: breach on stall; needs a group (crowd rule exists) |
| dod_flash | 5 | 0 | 0 | 0 | 0 | - | yes | flag capture |
| dod_flugplatz | 6 | 0 | 0 | 10 | 0 | detect,detect | no | no waypoint graph: steers by look-ahead only; toggle walls: breach on stall |
| dod_forest | 4 | 4 | 0 | 2 | 0 | - | no | no waypoint graph: steers by look-ahead only; toggle walls: breach on stall; needs a group (crowd rule exists) |
| dod_glider | 7 | 0 | 3 | 9 | 1 | detect,detect | no | no waypoint graph: steers by look-ahead only; satchel objective: no plant logic; toggle walls: breach on stall |
| dod_jagd | 3 | 0 | 1 | 5 | 0 | detect | no | no waypoint graph: steers by look-ahead only; satchel objective: no plant logic; toggle walls: breach on stall |
| dod_kalt | 5 | 1 | 0 | 16 | 0 | - | no | no waypoint graph: steers by look-ahead only; toggle walls: breach on stall; needs a group (crowd rule exists) |
| dod_kraftstoff | 3 | 2 | 0 | 0 | 1 | detect,detect | no | no waypoint graph: steers by look-ahead only; needs a group (crowd rule exists) |
| dod_merderet | 6 | 0 | 0 | 0 | 0 | - | no | no waypoint graph: steers by look-ahead only |
| dod_northbound | 5 | 1 | 0 | 0 | 0 | - | no | no waypoint graph: steers by look-ahead only; needs a group (crowd rule exists) |
| dod_saints | 5 | 1 | 0 | 0 | 0 | - | no | no waypoint graph: steers by look-ahead only; needs a group (crowd rule exists) |
| dod_sturm | 5 | 1 | 0 | 0 | 0 | detect | no | no waypoint graph: steers by look-ahead only; needs a group (crowd rule exists) |
| dod_switch | 6 | 1 | 0 | 0 | 0 | detect | no | no waypoint graph: steers by look-ahead only; needs a group (crowd rule exists) |
| dod_vicenza | 4 | 0 | 2 | 9 | 0 | - | no | no waypoint graph: steers by look-ahead only; satchel objective: no plant logic; toggle walls: breach on stall |
| dod_zalec | 5 | 0 | 0 | 14 | 1 | detect | no | no waypoint graph: steers by look-ahead only; toggle walls: breach on stall |

Modes seen: plain flag capture (control points touched), group capture (a capture area needing several players of a side),
objective destruction (`dod_capture_area` with `area_object_group`, i.e. satchel/TNT: charlie's shingles, escape, glider, jagd, vicenza),
timed attack/defend (`dod_round_timer`: charlie, glider, kraftstoff, zalec), and paratrooper settings (`info_doddetect` `*_paras`).

Bot logic today: flag capture (goal capture/defend), the crowd rule for points that need more than one player, a breach of a solid
`func_wall_toggle` after a stall (charlie's shingles; a stand-in for planting a satchel), no satchel pickup/plant, nothing for paras.
Only dod_charlie, dod_avalanche and dod_flash have a waypoint graph.


## Measured with bots (headless Dolphin, 30 fps, 2026-11-19)

Charlie and avalanche at 8 bots (4 a side) on the memory thread's engine; the others at 4 bots, because 8 bots on switch, donner and kalt hit the
edict wall (`CapFaces edict tip (460) soft-skip further spawns`) at about 70 s of game time and the simulation wedges. Keep those maps at 4 bots
until the static func_wall records land.

| Map | Bots | Result |
|---|--:|---|
| dod_charlie | 8 | round won by capture in 3 of 3 runs (about 340, 510, 570 s); 4 bots: rounds end on the 720 s timer |
| dod_avalanche | 8 | round won by capture in about 1 run of 3-4 (about 340 s); the others reach 2-3 points in 400 s (no round timer on this map) |
| dod_switch | 4 | all-attackers at 4 bots: 1460 s of game time, points 1, 2, 4, 5 change hands 11 times, the two-player point 3 is never taken, no round end; 2 bots a side cannot hold four points and take a two-player one, so it needs 6+ bots (edict wall) |
| dod_flash | 4 | no captures in 900 s before the flag-box fix (bots stopped 139 units from a flag whose capture sphere is 128, FillView box 128 -> 64); 6 bots wedge on the edict wall at about 180 s |
| dod_donner | 4 | point_index -1 used to collapse all five points into one (fixed); touch-captured flags had the same 139-unit stop (fixed); the group point 20 (mainstreet) needs 2 and one Allies bot waited inside; round end not yet seen |
| dod_kalt | 4 | graph pit fixed by linking 52-unit ladders (baker threshold 60 -> 46); Allies reach the centre again |

## Satchels and TNT (dod_object + area_object_group)

Charlie, escape, glider, jagd and vicenza use `dod_object` entities (a charge a player picks up and carries) and `dod_capture_area` entities with
`area_object_group`. Implemented in dod_runtime.cpp: a player of the team that may take it (`object_owner` 0 = anyone, else the other team
than the owner) takes an object by touching it, carries one object per group, drops it where he dies (back home after `object_resettime`),
and plants it by standing in an area of the same group: only carriers count in such an area, and the charge is consumed on capture and returns
after `object_resettime`. A map with no object of a group keeps its areas plain. Objects are not drawn (no heap for hk_satchel.mdl), score
(`object_cappoints`) and sounds/sprites are not implemented. Bots (dod_bot.cpp): an attacker whose point wants a group and who carries none
goes to the nearest object first, then to the point.

The engine spawns a `dod_object` as an entity only when a capture area of the map names its group (SV_GCFunctionalObject); every other unnamed
one stays a static prop. Per map (counted from the entity lumps):

| Map | dod_object | functional (group a capture area wants) | static props | cap of 6 (DOD_MAX_OBJECTS) changes it |
|---|--:|---|--:|---|
| dod_charlie | 21 | 20 `tnt` (8 areas) | 1 (`dev42`) | 20 -> 6 |
| dod_escape | 22 | 6 `allies_obj` (2 areas) | 16 (`axis_obj`) | no |
| dod_glider | 17 | 13 `bunkerbomb` (3 areas) | 4 (`axis_bomb`) | 13 -> 6 |
| dod_jagd | 2 | 1 `documents` (1 area) | 1 (no group) | no |
| dod_vicenza | 8 | 8 `tnt` (2 areas) | 0 | 8 -> 6 |
| dod_caen | 12 | 0 (`tnt`, no area) | 12 | no |

The cap exists because edicts are scarce (charlie plus 8 bots sits near the 460 wall); the first six objects of the lump stay. On charlie
those are the beach (3) and the bunker (3) satchels. Not done: areas without a control point (charlie's `*8` wall area) still do nothing.
