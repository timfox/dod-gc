# D3: Day of Defeat Nov 11 stills short. Review only: nothing uploaded, scheduled, or published

Rendered 2026-11-11 ~12:41 PT on ceres with a copy of the Nov 10 `render.py` (WORK pointed at `~/sv-work-2026-11-11`). Same approved look as D1/D2. Spec: `dod03.json`. New output only; Nov 10 shorts were not overwritten.

| # | File | Dur | Music | LUFS I / TP | md5 |
|---|---|---|---|---|---|
| D3 | `03-dod-avalanche-day-of-probes.mp4` (1.9 MB) | 26.6 s (798 frames) | `Allied Resolve.mp3` (first use; Mournful March and Menace of War were used Nov 10; md5 8721535e47a7592fc9bb85bab14209a9) | -15.0 / -3.7 dBFS | f0e88b8525eab531e8ab8fadd642ba89 |

## Why stills

There are **no Nov 11 video captures** under `dod-gc/playtests/videos/`. Today's work is a long `ava-play99` / `ava-worlddl` stills series (`final.png` per probe, plus notes). Late Nov 10 videos after the 18:39 Garand and team-menu shorts were checked and are not usable as a second motion short (see skipped).

## Cut

Sources are `playtests/screenshots/<run>/final.png`. Claims follow the probe sticky notes and what's actually visible in the frame. Wall marks are mentioned in later notes (p103–p105) but are not obvious in the stills, so they are not claimed on screen.

| t | Still | Note (sticky) | On screen |
|---|---|---|---|
| 0–3.0 | p85 11:42 | VM chromakey fringe punch | Title: "Day of Defeat on GameCube: a day on dod_avalanche" · HUD still shows AMMO 8 |
| 3.0–5.4 | p5 00:48 | textured world + fire + class HUD | "00:48: in the map, Garand in hand" · solo listen-server probe |
| 5.4–7.6 | p25 01:39 | VM framed, garand skins 128 | "01:39: still working the rifle in the streets" |
| 7.6–10.0 | p54 02:58 | FOV/pose + WorldDL SCALE_4 | "02:58: more of the town drawing" |
| 10.0–12.4 | p80 11:27 | VM UV domain fix | "11:27: fixing the Garand's textures" |
| 12.4–15.2 | p85 11:42 | fringe punch, courtyard | "11:42: the rifle in a courtyard" |
| 15.2–17.6 | p89 11:52 | MODULATE lighting | "11:52: a lighting pass on the viewmodel" |
| 17.6–20.2 | p91 12:00 | weapon grant + decal shoot + gated shot mark | "12:00: a fire path runs" · "HUD shows AMMO 7" (visible in-frame; earlier stills show AMMO 8) |
| 20.2–23.2 | p104 12:32 | Sticky GX wall marks; WorldDL hull0 keep | "12:32: latest probe of the morning" · "still unfinished · not a full match" |
| 23.2–26.6 | p104 + end card | n/a | "Still in development, not on hardware yet" + gamecubehalf.life |

## Skipped / why

- Late Nov 10 videos after D1/D2 (ava-fullbsp, ava-playable27/28, hlsdk-mp-tour, local-create-match): most sampled frames are PowerPC crash dumps or a black AMD Radeon debug overlay. `ava-playable28` and `ava-fullbsp26` show the Garand in a nearly static wall-and-sky stare, which overlaps D1 and isn't enough motion for a second short.
- Nov 11 crash stills (e.g. p9, p43, p48b): not used. This short is not a crash story, so no one-frame crash flashes.
- p18 (white void), p61 (badly scaled mesh), p99 (green debug overlay): not used.
- Intermediate viewmodel pose/skin stills (dozens of play99 runs): too similar; the short keeps a handful of visible steps.
- "Sticky wall marks" (p103–p105): not clearly visible in the frames, so not captioned as something you can see.
- A second DoD short was not cut. The usable new material is this stills day, and the leftover video is too thin to pad.

## Caveats

- Dolphin emulator stills, not real hardware, not a verified full match. "Fire path" means the 12:00 probe ran the shoot/decal path and the HUD ticked to AMMO 7; it is not footage of a player firing in a match.
- No speed/perf or retail-parity claims.
- Rights/Content ID: Allied Resolve is from `dod-gc/social-media/music`, one of Tim's "original" tracks, but generation and commercial-use terms are not verified here. Footage shows Valve Day of Defeat assets as dev-log material.

## QA

1080x1920, 30/1, h264 + aac 48 kHz stereo. blackdetect d=0.1 pix_th=0.10: 0 runs. blackframe 98%: 0 frames. ebur128 I = -15.0 LUFS, true peak -3.7 dBFS.

## Drafts

**YouTube Short title:** Day of Defeat on GameCube: A Day on dod_avalanche (in development)

**YouTube description:**
A day's worth of dod_avalanche probes on my Day of Defeat GameCube port, captured in the Dolphin emulator. It's not on real hardware yet, and it isn't finished. These are stills from overnight and this morning, not a live match. Just after midnight the map was already drawing with the Garand in hand. Late morning was spent fixing the rifle's textures and lighting. A 12:00 fire-path probe ticks the HUD down to AMMO 7. The 12:32 still is the latest from this morning. A full match isn't verified yet.

Project updates and source-code link: https://gamecubehalf.life

#Shorts #DayOfDefeat #GameCube #Homebrew #GameDev

**Instagram Reel caption:** same paragraph and link, then
#DayOfDefeat #GameCube #Homebrew #GameDev
