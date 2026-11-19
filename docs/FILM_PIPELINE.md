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
| `tools/film/film.py` | `plan`, `boards`, `animatic`, `assemble`. |
| `tools/film/actors.py`, `src/dod_film.c`, `src/dod_film_actor.cpp` | Actors: soldiers placed and moving in a shot (below). |
| `tools/film/voice.py` | Narration and ADR rendered with a local TTS model (Qwen3-TTS), effects, levels, and the dialogue mix (below). |
| `tools/film/foley.py` | Foley: the game's own sounds placed under each shot (below). |
| `tools/film/stamp.py` | Reads the engine's frame stamp off captured images. |
| `scripts/film-capture.sh` | Plan, build a listen disc per map with `build-lean-iso.sh --film`, run Dolphin with frame dumping until the engine logs `FILM SESSION DONE`, then assemble. |
| `film/episodes/` | Episodes. `ep01-omaha.json` is the sample: Omaha Beach on 6 June 1944, six shots on `dod_charlie`. |

## Episode format

```json
{
  "id": "ep01-omaha", "title": "...", "fps": 30,
  "aspect": "2.39",            // 16:9, 1.85 or 2.39 (letterboxed in a 1920x1080 frame)
  "grade": "archival",         // clean, archival (muted, warm, grain, vignette) or monochrome
  "music": {"file": "social-media/music/Mournful March.mp3",  // optional; ducks under narration and ADR
            "gain": 0.3, "start": 0.5, "fade_in": 3, "fade_out": 6},  // file: absolute, next to the plan, or in the repo
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

## Film grammar and storyboards

A shot's `camera` takes film language on top of its move, and a `board` block for the slate:

```json
"camera": {"type": "still", "eye": "shingle", "target": "draw_top", "height": 4, "duration": 13,
           "lens": 40, "dutch": -8, "handheld": 0.4, "seed": 6},
"board":  {"no": "6", "size": "MS", "angle": "Ground level, dutch tilt", "action": "...",
           "sound": "Ringing ears; muffled fire", "figures": ["crest"], "sea": true}
```

- `lens` / `lens_end`: focal length in mm on a 36 mm wide sensor (24 mm wide, 50 mm normal, 85 mm tight);
  a still with `lens_end` is a zoom push or pull.
- `dutch`: roll in degrees, or `[start, end]` to roll during the shot.
- `handheld`: operator shake, 0.2 steadied to 1.0 running; slow drift, not jitter, and the same every
  capture for a given `seed`.
- Low and high angles come from `height` and where the camera looks; `crane`, `dolly`, `orbit`, `pan`
  and `still` are the moves.
- `figures`: stand-ins drawn on the boards where people should be in frame (bots on the capture).

`tools/film/film.py boards <ep>.json` draws numbered storyboard sheets from the same camera paths the
engine flies: a pencil-style view projected from the map's waypoints, an A and B panel for moves, a move
arrow or zoom frame, an overhead inset, and the slate (size, angle, lens, move, duration, action, VO, SFX).
It also writes `shot-list.md`. Since the boards and the capture share a path, the film matches the boards.

## Actors

A shot's `actors` puts soldiers in it. Each is a prop wearing its side's player model (the uniform and the
gun come from the class), not a bot, so it needs no client slot: the GameCube listen server has room for one
bot, and a shot can have up to 12 actors.

```json
"actors": [
  {"id": "run", "team": "allies", "class": "assault", "squad": {"count": 4, "spacing": 64},
   "path": [{"at": [-330, -2560], "pose": "run"}, {"at": "shingle", "route": true, "pose": "crouch"}]},
  {"id": "lead", "team": "allies", "path": [{"at": [40, 330, 40], "pose": "crouch", "look": "draw_top"},
                                            {"t": 5, "pose": "crouch_walk"}, {"at": [250, 780, 280]}]}
]
```

- A stop has `at` (any camera point), a `pose` (`stand walk run crouch crouch_walk prone crawl aim crouch_aim
  fallen`), and either a `t` or nothing, in which case the actor gets there at its pose's pace (`speed`
  overrides). `hold` waits at the stop; `route: true` walks the waypoint graph; `yaw` or `look` sets the facing,
  otherwise the actor faces the way it moves. The leg to a stop moves in the previous stop's pose.
- `squad` repeats an actor in a loose file with a short stagger; `offset` shifts one actor's whole path.
- Give `[x, y, z]` where the waypoints are sparse or stacked: the game puts the feet on the floor under each
  point with a trace, but previs and boards use the z you give.
- `plan` writes `cine/<shot>.act` (format in `src/dod_film.h`) and `cine/cast-<map>.txt` (the sides to
  precache), and each shot's alias runs `film_cast <shot> <preroll>` before `cine_play`, or `film_cast_clear`
  for a shot with nobody in it. Boards draw the actors where they are at each panel (crouched and prone ones
  low) and the animatic shows them as dots.
- Pose sequences are looked up by name in the retail model (DoD: dod_walk, dod_jog, dod_crawl for the crouched
  walk, dod_crouch_idle, prone_idle, prone_forward, die_simple, stand/crouch_rifle_aim); the game logs
  `FILM ACTOR <id> pose <pose>: sequence N (<name>)` so the first capture shows which ones it found.

## Voice-over and ADR

`tools/film/voice.py` renders an episode's narration and its ADR (callouts, radio, walla) with
[Qwen3-TTS](https://github.com/QwenLM/Qwen3-TTS), run locally on a CUDA GPU. It was picked (November 2026) as
the strongest open model that can *design* a voice from a written description: no reference recording, so
no real person's voice is cloned, and each line takes a plain-language delivery ("shouting over gunfire,
out of breath"). Weights and code are Apache 2.0. It also speaks German and French for the other side and
for civilians. Chatterbox (MIT) is the runner-up but needs a reference recording for every voice;
IndexTTS-2 has finer emotion control but its licence needs an agreement for commercial use.

```json
"narrator": "narrator",
"voices": {
  "narrator": {"design": "Male documentary narrator in his late fifties, deep warm baritone, ...",
               "delivery": "Calm, grave and respectful; measured pace", "mode": "clone"},
  "sgt":      {"design": "American man in his late twenties, gravelly baritone, ..."},
  "de_obs":   {"design": "German man in his thirties, low steady voice", "language": "German"}
},
"shot": {"id": "s04_crossing", ...,
  "narration": [{"text": "..."}],
  "adr": [
    {"voice": "sgt", "at": 0.6, "text": "Keep moving! Get to the shingle!",
     "delivery": "Bellowing over machine-gun fire, out of breath", "fx": "field", "gain": -8, "pan": 0.2},
    {"walla": true, "at": 4, "dur": 9, "voices": ["pvt_a", "pvt_b"], "lines": ["Medic!", "Keep going!"],
     "delivery": "Desperate shout across open sand, far away", "fx": "distant", "gain": -19},
    {"voice": "de_obs", "at": 6.6, "text": "Da unten!", "subtitle": "Down there!", "fx": "distant"}
  ]}
```

- **Voices** are descriptions: age, register, accent, role. Keep them generic and never name or describe a
  real person. `mode: clone` (the default for narration) designs the voice once into `voice/cast/<voice>.wav`
  and reads every line in it with the Base model, so a narrator stays the same man for the whole film.
  `mode: design` (the default for ADR) designs every line, so delivery can change line to line; lines of one
  voice share a seed so the timbre mostly holds. `take: 2` on a line tries another seed.
- **ADR** lines sit at `at` seconds into their shot. `fx`: `dry`, `field` (shouted in the open), `distant`,
  `muffled` (ringing ears), `radio` (narrow band, overdrive, hiss and squelch) or `megaphone`. `gain` is dB
  in the mix, `pan` -1 to 1. Only lines with a `subtitle` get one (use it for translations); narration is
  always subtitled.
- **Walla** spreads its `lines` through `dur`, cycling the `voices`, each at its own place and level.
- Lines are named like the narration script (`s04_crossing_01`, `s04_crossing_adr01`,
  `s04_crossing_walla02_03`) and land in `out/film/<ep>/voice/`. `plan`, `animatic` and `assemble` use a
  rendered line automatically, at its real length, unless the line names its own `audio` (a recorded take
  always wins). Narration ducks the music bed hard and ADR a little; ADR ducks the bed lightly.

```
pip install -U qwen-tts               # once, in a venv with a CUDA build of torch (about 8 GB of VRAM)
tools/film/voice.py lines  film/episodes/ep01-omaha.json      # every line, voice and prompt
tools/film/voice.py cast   film/episodes/ep01-omaha.json      # audition each voice
tools/film/voice.py render film/episodes/ep01-omaha.json      # cached: re-renders only what changed
tools/film/voice.py render film/episodes/ep01-omaha.json --only 's04_*' --force
tools/film/voice.py mix    film/episodes/ep01-omaha.json      # dialogue and music on the film's clock
tools/film/film.py animatic film/episodes/ep01-omaha.json     # the animatic, now with voices
```

`--backend placeholder` renders speech-shaped noise of the right length instead, to check timing and the
mix with no GPU. The models download from Hugging Face on first use (about 9 GB for both).

## Foley from the game

`tools/film/foley.py` lays the game's own sounds under a shot: surf and wind beds, MG and rifle fire,
explosions, footsteps. A shot's `foley` list names sounds by their path under the game's sound folders
(`weapons/mg42_shoot.wav`), found in the episode's `foley_dirs` (default `content/dod/sound`, then
`content/valve/sound`, the retail files on the capture machine; never committed).

```json
"foley": [
  {"sound": "ambience/waves.wav", "loop": true, "gain": -16, "fade": 1.5},
  {"sound": ["weapons/mg42_shoot.wav", "weapons/30cal_shoot.wav"], "at": 1, "dur": 10,
   "every": [1.2, 3.0], "gain": -20, "pan": [-0.6, 0.6], "fx": "distant"},
  {"sound": "weapons/explode3.wav", "at": 6.5, "gain": -12, "fx": "distant", "rate": 0.8}
]
```

`loop` makes a bed for `dur` (default: the rest of the shot); `every: [min, max]` repeats hits at random
spacing, each picking a sound from the list, a level near `gain` and a place in `pan`'s range; `fx` takes the
voice effects; `rate` below 1 slows and deepens a sound. Cues are seeded, so a re-assemble sounds the same.
The foley track ducks under narration and leaves the music alone. A missing sound is a warning.
## Clearance: no camera through the map

The engine camera has no collision, so `film.py check <ep>.json` (and every `plan`, which the capture script
runs first) tests each shot against the map's BSP, sampled every 4 units along the path:

- **inside the world**: the eye in solid in hull 0, the geometry that is drawn;
- **too close to a surface**: drawn geometry within 16 units across or 18 up and down of the eye, where the
  near plane and a wide lens start cutting into it. Only what is drawn counts: invisible player-clip brushes
  (dod_charlie has them over the sea and along the shingle) do not stop a camera;
- **through a soldier**: the eye inside an actor's body at that moment.

Solid brush entities (`func_wall`, `func_breakable`, doors ...) count, at their spawn positions. `plan` stops
with the shot, time and position of each problem; previews (`boards`, `animatic`) only warn. The BSP is found
in `$FILM_BSP_DIR`, `content/dod/maps` or `content/valve/maps`, or passed as `--bsp dod_charlie=PATH`. Where
there is none (the retail maps are not in the repo) shots are checked against the soldiers only, and `plan`
says so.

## Making an episode

1. **Board it.** Write the sequence and run `film.py boards` to check framing shot by shot. Then `tools/film/film.py animatic film/episodes/<ep>.json`. The
   animatic draws each shot's camera on a top-down map of the waypoints with the timing, cards, lower thirds
   and burned subtitles, so pacing and narration can be judged before anything is captured. It needs no game
   assets and runs anywhere.
2. **Refine moves in game (optional).** On a desktop or Dolphin session, noclip to a spot and run
   `cine_mark <t>`; each call appends a key for the current view to `cine/marks.cam`. Paste keys into a
   shot as `{"type": "keys", "keys": [[t, x, y, z, pitch, yaw, roll, fov, "smooth"], ...]}`.
3. **Capture.** `scripts/film-capture.sh film/episodes/<ep>.json`. It stops before building anything if a
   camera path clips (above). Each map boots once and plays its shots
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

Each episode keeps a fact check beside it (`film/episodes/<ep>.factcheck.md`): every spoken line, card and
lower third with the source that supports it, and what changed. Re-check it whenever narration changes.

Music beds come from `social-media/music` (Tim's own tracks, made with Suno, kept out of git); check the
plan's commercial-use terms before publishing.

Footage uses Valve's Day of Defeat assets from a legally owned copy; check the publishing terms before
releasing anything outside the project.

## Status and next steps

- Done: engine camera and time lock, planning, previs animatic, Dolphin capture script, assembly, sample
  episode ep01 with an animatic.
- Needs a run on the capture machine: the engine change has host tests but has not been built for the
  Cube or run in Dolphin yet. First capture should check the stamp reads back (`assembly-report.txt`) and
  that the camera shows on the CapFaces/WorldDL route (`GC_RenderNewGameWorldFrames`).
- Done since: actors (props on keyed paths, previs and boards from the same samples as the game).
- Done since: voice-over and ADR rendered with Qwen3-TTS (above).
- Next: check the actors' sequences and floor placement on the first capture; a music bed, game
  ambience captured per shot, and more maps (`dod_avalanche`, `dod_flash` have waypoints today; other maps
  need `tools/gen_waypoints.py` run against their BSP).
