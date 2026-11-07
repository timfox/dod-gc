# Status: HL1 intro → DoD menu

## Done
- `out/dodgc.dol` — same HL1SDK archives as HL1, linked with `gamedir=dod`
- `out/dodgc.iso` — lean DoD stage + HL1 `media/valve.avi` intro
- `out/dodgc-menu.iso` — same content, startup vids skipped (menu path)
- Boot logs: `linked gamedir=dod`, `FS_Rescan( Day of Defeat )`, intro plays `media/valve.avi`, menu activates (`gc_menu_ready`, steam background tiles)

## Blocker
- MEM1 exhausted at menu present (`heap_free≈496 bytes` after server init). Guest reports menu ready but `visible=0` / GHOST frames — Flipper never shows the sheet.
- Full retail DoD tree OOMs harder; lean stage is required. Next: cut static HLSDK/menu MEM1 use for the dod profile (mirror HL1 retail-menu budget).

## Evidence
- OSReport playtests under `playtests/notes/` and engine probe logs
- Baked menu preview: `playtests/screenshots/baked-menu-preview.png`
