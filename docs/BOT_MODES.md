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
