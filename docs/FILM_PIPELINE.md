# Film pipeline: WW2 documentary and cut scenes

Short films, cut scenes and long-form documentary episodes rendered in the xash3d-gc engine with this
repo's Day of Defeat content. An episode is a JSON file; the tools turn it into camera paths the engine
plays, capture the frames in Dolphin, and assemble a finished film with title cards, lower thirds,
narration subtitles, chapters and an audio bed.

```
film/episodes/<ep>.json ──plan──▶ out/film/<ep>/cine/*.cam + film-<map>.cfg + subtitles.srt + narration-script.md
                         ──animatic──▶ previs frames from the waypoint map ──▶ <ep>-animatic.mp4
                         ──film-capture.sh──▶ Dolphin, one boot per map ──▶ captures/<map>/*.png ──assemble──▶ <ep>.mp4
```

## Pieces

| Where | What |
|---|---|
| xash3d-gc `engine/client/cl_cine.c`, `cine_path.c` | `cine_play <shot>`: keyframed camera owns the view, HUD and weapon hidden, game time locked to `cine_fps`, frame-number stamp. See xash3d-gc `docs/CINEMATIC_CAMERA.md`. |
| `tools/film/campath.py` | The `.cam` format and its sampling, mirrored from the engine so previs matches the real move. |
| `tools/film/shots.py` | Camera moves from a few numbers: `dolly` (along the walkable route between two points, from `data/waypoints/<map>.wpt`), `orbit`, `crane`, `still` (with optional zoom push), `pan`, and raw `keys`. |
| `tools/film/film.py` | `plan`, `animatic`, `assemble`. |
| `tools/film/stamp.py` | Reads the engine's frame stamp off captured images. |
| `scripts/film-capture.sh` | Plan, build a listen disc per map with `build-lean-iso.sh --film`, run Dolphin with frame dumping until the engine logs `FILM SESSION DONE`, then assemble. |
| `film/episodes/` | Episodes. `ep01-omaha.json` is the sample: Omaha Beach on 6 June 1944, six shots on `dod_charlie`. |

## Episode format

```json
{
  "id": "ep01-omaha", "title": "...", "fps": 30,
  "aspect": "2.39",            // 16:9, 1.85 or 2.39 (letterboxed in a 1920x1080 frame)
  "grade": "archival",         // clean, archival (muted, warm, grain, vignette) or monochrome
  "music": {"file": "music/bed.wav", "gain": 0.35},          // optional; ducks under narration
  "anchors": {"dod_charlie": {"shingle": [0, -384], "sea": [-352, -3420, -250]}},
  "server_cmds": {"dod_charlie": ["dod_bots 8"]},             // console lines before the shots
  "sequence": [
    {"card": {"id": "title", "dur": 7, "style": "title", "kicker": "...", "title": "...", "body": "...", "chapter": "Title"}},
    {"shot": {"id": "s02_beach", "map": "dod_charlie", "chapter": "The landing",
              "lower_third": "Omaha Beach\nNormandy · 6 June 1944",
              "camera": {"type": "dolly", "start": "waterline", "end": "shingle", "height": 24, "duration": 19},
              "narration": [{"text": "...", "audio": "vo/s02_beach_01.wav"}]}}
  ]
}
```

- Points are `[x, y]` (z from the nearest waypoint), `[x, y, z]`, an anchor name, or `{"node": N}`. Waypoint
  z is a standing player's origin; eye height is 28 above it.
- Narration lines follow one another with a short pause; give a line `"at"` to place it yourself. `plan`
  warns when a line runs past its shot, and `narration-script.md` lists every line with its timecode and the
  WAV name to record it as.
- Shot ids become console arguments: lowercase letters, digits, `_`.

## Making an episode

1. **Block it out.** Write the sequence, then `tools/film/film.py animatic film/episodes/<ep>.json`. The
   animatic draws each shot's camera on a top-down map of the waypoints with the timing, cards, lower thirds
   and burned subtitles, so pacing and narration can be judged before anything is captured. It needs no game
   assets and runs anywhere.
2. **Refine moves in game (optional).** On a desktop or Dolphin session, noclip to a spot and run
   `cine_mark <t>`; each call appends a key for the current view to `cine/marks.cam`. Paste keys into a
   shot as `{"type": "keys", "keys": [[t, x, y, z, pitch, yaw, roll, fov, "smooth"], ...]}`.
3. **Capture.** `scripts/film-capture.sh film/episodes/<ep>.json`. Each map boots once and plays its shots
   back to back (each shot's end runs the next one). Locked game time means a slow emulator makes the
   capture longer, not the film choppier, so `FILM_IR=3` (1920x1440) is fine. Captures land in
   `out/film/<ep>/captures/<map>/`.
4. **Assemble.** Done by the capture script, or `tools/film/film.py assemble <ep>.json [--burn-subs]`. The
   assembler splits each map's dumps into shots where the stamped frame number restarts, keeps one image
   per number, fills any frame the emulator skipped with the one before, crops the stamp away with the
   letterbox, grades, and cuts in cards and lower thirds. Soft subtitles and chapters ride in the MP4;
   `--burn-subs` burns them in instead. `assembly-report.txt` lists dropped and filled frames per shot.

## Content standards

These films are about real events in which real people died. Narration states only what the cited sources
support, with numbers given as estimates where the sources do. Each episode lists its `sources`, opens with
a note that the scenes are game recreations and not archive footage, and closes with a dedication. No
gore beyond what the game shows, no glorification, no invented personal stories presented as real.

Footage uses Valve's Day of Defeat assets from a legally owned copy; check the publishing terms before
releasing anything outside the project.

## Status and next steps

- Done: engine camera and time lock, planning, previs animatic, Dolphin capture script, assembly, sample
  episode ep01 with an animatic.
- Needs a run on the capture machine: the engine change has host tests but has not been built for the
  Cube or run in Dolphin yet. First capture should check the stamp reads back (`assembly-report.txt`) and
  that the camera shows on the CapFaces/WorldDL route (`GC_RenderNewGameWorldFrames`).
- Next: actors (scripted bot moves and TAS pad scripts per shot), recorded narration, a music bed, game
  ambience captured per shot, and more maps (`dod_avalanche`, `dod_flash` have waypoints today; other maps
  need `tools/gen_waypoints.py` run against their BSP).
