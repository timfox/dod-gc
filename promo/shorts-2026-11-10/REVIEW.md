# Social video batch: Nov 10, 2026 (review only, nothing uploaded or scheduled)

Rendered 2026-11-10 18:39–18:40 PT on ceres. All files are 1080x1920 (9:16), H.264 30 fps + AAC 48 kHz stereo, around -15 LUFS integrated with true peak at or below -1.1 dBFS.
Specs (`*.json`), `render.py`, and `specs.py` sit next to the outputs so every segment can be traced to its source file.

| # | File | Dur | Music | What it shows |
|---|---|---|---|---|
| D1 | dod-gc/promo/shorts-2026-11-10/01-dod-garand-viewmodel-bringup.mp4 | 31.1 s | DoD: Mournful March.mp3 (first use) | dod_avalanche, 17:19 to 18:25 PT runs: no rifle, then an oversized untextured Garand, wrong textures, and finally sized and textured. Three crash runs appear as one-frame flashes only. |
| D2 | dod-gc/promo/shorts-2026-11-10/02-dod-team-menu-three-builds.mp4 | 25.4 s | DoD: Menace of War.mp3 (2nd use) | dod_flash team menu over three builds: 14:36 panel with no text, 14:44 no text and a broken map, 14:52 SELECT A TEAM, then the Allies spawn view. |
| L1 | lostcoast-gc/promo/shorts-2026-11-10/01-lostcoast-one-day-of-builds.mp4 | 32.8 s | LC: Mechanical Rhythm.mp3 | 65 Lost Coast probe framedumps from 00:00 to 18:19 PT: shore and water, an untextured gun at 15:34, the textured gun at 16:03, bloom at 17:05, sun disc and fog from 17:16. |
| L2 | lostcoast-gc/promo/shorts-2026-11-10/02-lostcoast-bloom-sun-fog.mp4 | 20.8 s | LC: Mechanical Rhythm(1).mp3 | Three probe clips (17:05, 17:12, 17:16), captioned from PROGRESS.md notes: bloom too hot, then retuned, then the env_sun disc and hardware fog. |

## QA (ffprobe / blackdetect d=0.1 pix_th=0.10 / blackframe 98%)
- All 4: 1080x1920, 30/1, no black runs of 0.1 s or longer, 0 black frames, and an audio stream is present.
- D1 includes three single frames (1/30 s each) of PowerPC crash dumps (ava-playable2, ava-playable4, ava-vm5) as crash markers, per the rule.
- Excluded: the known-broken black 61 s DoD capture, the black frame of LC probe 173506, and the washed-out LC 125917 frame.

## Sources
- D1: dod-gc/playtests/videos/20261110-17*/18*-ava-*.mp4, with a frame at t=60 s from each run, plus a live 41-46.5 s segment of 20261110-182519-ava-vm15-599494ad0d.mp4. Facts come from the src/README.md note in commit a659514: "Garand viewmodel covers most of the screen" and "A complete Create Game match is not verified yet".
- D2: 20261110-143649-flash-teamgui, 144412-flash-teamgui2, and 145246-flash-teamgui3 (7e44ea2556).
- L1: lostcoast-gc/playtests/notes/dolphin-probe-20261110-*/best.png, or the last framedump when there's no best.png. The mapping is in ~/sv-work-2026-11-10/lcfull/sources.txt. 640x528 stills were center-cropped to 640x480.
- L2: notes/dolphin-probe-20261110-{170509,171240,171655}/clip.mp4 plus the 164010 and 181915 stills. Captions paraphrase PROGRESS.md.

## Not cut / why
- HL1: there's no new usable HL1 footage after the v3/v4 batch, and S1-S11 are all scheduled or published in BulkPublish.
  - The Nov 9-10 "menu-newgame" files in xash3d-gc/.ai/playtests have HL1 JSON (hl1-autohop.iso) but contain DoD Caen footage. For example, 20261110-081349 matches dod 20261110-081349-caen-props3, which looks like a recorder or window collision. Do not post these as HL1.
  - Other Nov 9 HL1 files are crash screens (magenta out-of-memory, PowerPC dumps) or a texture-test view. m2-intro-barney/c0a0e-to-c1a0-audio-proof.mp4 is black and silent.
- DoD map tour: the Nov 10 13:xx and 14:xx allgeo runs overlap the posted 04-maps-atlas-tour, and many hit missing-BSP or out-of-memory screens.
- DoD Caen/Flash props, ship, and align runs (Nov 10 03:00-08:52) look the same as the posted or existing Caen/Flash cuts.
- No longer YouTube video: the new footage is mostly static views, which isn't enough for a long-form cut.
- LC retail side-by-side: skipped. It repeats posted 04-geo-to-retail, and the retail still's source isn't verified.

## Rights / Content ID
- DoD tracks come from dod-gc/social-media/music and LC tracks from xash3d-gc/social-media (md5 matches the ~/Desktop/lostcoast-gc/promo/music copies). They are Tim's "original" tracks, but their generation and commercial-use terms aren't verified here (e.g. Suno plan terms). Content ID claims are possible if a track was registered or distributed anywhere.
- Footage is Dolphin emulator output of Valve/DoD and HL2 assets. That's fine as dev-log footage, but keep "in development, not hardware" wording.
