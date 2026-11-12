# D6: Day of Defeat Veterans Day short (review only: nothing uploaded, scheduled, or published)

Rendered Wed 2026-11-11 ~4:25 PM PT on ceres with a copy of the Nov 11 `render.py` (same D3 look). Spec: `dod06.json`.

| File | Dur | Res | Music | LUFS I / TP | md5 |
|---|---|---|---|---|---|
| `06-dod-veterans-day.mp4` (2.1 MB) | 27.2 s (816 frames) | 1080x1920, 30/1, h264 + aac 48 kHz stereo | `Mournful Horn Theme.mp3` (first use, from `dod-gc/social-media/music`, starts at 0.5 s to skip the lead-in silence; md5 e7dd2ab4…) | -15.2 LUFS / -2.3 dBFS | b7719a183ec3b6bec62fecb2329da728 |

## What it shows / the new work

These are Dolphin emulator stills (`final.png`) from this afternoon's `ava-play99` dod_avalanche probes. All of them are from after D3's last still (p104, 12:32). They show more of the town than D3 did: interiors, an archway, a library room, alleys, and an outdoor street. The HUD shows Team Allies, the Garand viewmodel, HP 100 and AMMO 7. This is a still slideshow with a slow zoom on the opener. It is not live video and not a verified match. No movement or walking is claimed, even though some probe notes mention walk pulses.

## Cut

| t (s) | Source (`playtests/screenshots/…/final.png`) | On screen |
|---|---|---|
| 0–3.6 | 20261111-142344-ava-play99-130 | VETERANS DAY · NOV 11 / "Thank you to every veteran who served" / pill "in development · Dolphin emulator · not hardware" |
| 3.6–6.2 | 20261111-123408-ava-play99-105 (12:34) | "Day of Defeat is set in WWII. Today we remember those who really served." / pill "this afternoon's dod_avalanche stills · not live video" |
| 6.2–8.6 | 20261111-134544-ava-play99-111a (13:45) | "13:45: stone walls of the town" |
| 8.6–11.0 | 20261111-135535-ava-play99-115 (13:55) | "13:55: an archway between buildings" |
| 11.0–13.4 | 20261111-140734-ava-play99-122 (14:07) | "14:07: a room lined with bookshelves" |
| 13.4–15.8 | 20261111-142137-ava-play99-129 (14:22) | "14:22: a narrow stone alley" |
| 15.8–18.2 | 20261111-143011-ava-play99-132 (14:31) | "14:31: cobblestones down the alley" |
| 18.2–20.6 | 20261111-150737-ava-play99-145t1 (15:08) | "15:08: a dark interior hallway" |
| 20.6–23.6 | p130 (14:24) | "14:24: out on the street under blue sky" / pill "Allied team · Garand · HP 100 · AMMO 7" |
| 23.6–27.2 | p130 + end card | "Honoring all who served. Port still in development, not on hardware yet." + gamecubehalf.life |

p130 is used three times: as the opener, as its own timestamped beat, and as the end card background. Its 14:24 beat comes after 15:08 on purpose, so the short ends on the best shot. Every still carries its capture time.

## Excluded

- `playtests/videos/20261111-155151-play-1a5bcc258c.mkv`: the parent/Tim review said not to use it. Note: Dolphin was still running and the recording was still being written when this was checked (~4:15 PM PT). It ended prematurely, so this is an incomplete mux. Frames from ~0:19.3 to the end show a frozen PowerPC crash dump, and 0:00–0:02.8 is black. The first ~19 s do contain real footage: the team menu (Allies), the class menu, then ~11 s in the dod_avalanche street with the Garand and the view changing. It's held back as a possible later motion cut, if Tim wants one.
- All `ava-hl1move-1..12` finals: loading tiles or crash dumps. No HL1 movement or PM_Move claims.
- p110 (near-duplicate of p105), p124 (very dark), p146 (white doorframe glitch), and black/crash probes in between.
- No crash frames at all, not even a one-frame flash, because this is a Veterans Day piece.

## Caveats

- Dolphin emulator stills, not hardware, not a full match. No speed/perf claims.
- The Veterans Day message thanks real veterans. It doesn't present the game as a memorial or the footage as real events.
- Music rights/terms for the DoD tracks are not verified here. The footage shows Valve Day of Defeat assets as dev-log material.

## QA

ffprobe: 1080x1920, 30/1, 816 frames, 27.2 s, aac 48 kHz stereo. blackdetect d=0.1 pix_th=0.10: 0 runs. blackframe 98%: 0 frames. ebur128 I -15.2 LUFS, TP -2.3 dBFS. Contact sheet: `contact-sheet.jpg`, previews in `preview/`.
