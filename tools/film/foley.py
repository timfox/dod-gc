"""Foley from the game's own sounds: gunfire, explosions, surf, footsteps placed on the film's clock.

A shot (or card) takes a `foley` list; each cue names a game sound by its path under the sound folders
(`weapons/mg42_shoot.wav`), or a list of them to pick from:

    "foley": [
      {"sound": "ambience/waves.wav", "loop": true, "at": 0, "dur": 12, "gain": -16, "fade": 1.5},
      {"sound": ["weapons/mg42_shoot.wav", "weapons/30cal_shoot.wav"], "at": 1, "dur": 10,
       "every": [1.2, 3.0], "gain": -20, "pan": [-0.6, 0.6], "fx": "distant"},
      {"sound": "weapons/explode3.wav", "at": 6.5, "gain": -12, "fx": "distant"}
    ]

- One hit at `at`, or with `every: [min, max]` a hit every so often from `at` for `dur` seconds (each hit
  picks a sound from the list, a level a little either side of `gain`, and a place in `pan`'s range).
- `loop: true` repeats the sound for `dur` seconds (beds: surf, wind, engines); `fade` fades it in and out.
- `fx` takes the voice effects (`dry`, `distant`, `muffled`, ...); `rate` (e.g. 0.85) slows a sound down
  and lowers it, which makes small game sounds bigger and further away.
- `dur` defaults to the rest of the segment. Cues are seeded by segment and index, so they repeat exactly.

Sounds are found under the episode's `foley_dirs` (default: content/dod/sound, then content/valve/sound),
which hold the retail game's files on the capture machine; they are never copied into git. A missing sound
is a warning, so the film still assembles without it.
"""
import random
import subprocess
import sys
from pathlib import Path

DEFAULT_DIRS = ("content/dod/sound", "content/valve/sound")


def _pick(v, rng):
    return rng.choice(v) if isinstance(v, list) else v


def _span(v, rng):
    if isinstance(v, (list, tuple)):
        return rng.uniform(float(v[0]), float(v[1]))
    return float(v)


def expand(src, seg_dur):
    """A segment's foley cues as single events: {id, sound, at, dur (None = the whole sound), gain, pan, fx, rate, loop, fade}."""
    out = []
    sid = src["id"]
    for k, c in enumerate(src.get("foley", [])):
        if "sound" not in c:
            raise ValueError(f"{sid}: foley {k + 1} needs \"sound\"")
        rng = random.Random(f"{sid}:foley:{k}")
        at = float(c.get("at", 0.0))
        dur = float(c.get("dur", max(0.0, seg_dur - at)))
        base = {"fx": c.get("fx", "dry"), "rate": float(c.get("rate", 1.0)), "fade": float(c.get("fade", 0.0))}
        gain = float(c.get("gain", -14.0))
        if c.get("loop"):
            out.append({**base, "id": f"{sid}_fol{k + 1:02d}", "sound": _pick(c["sound"], rng), "at": at, "dur": dur,
                        "gain": gain, "pan": _span(c.get("pan", 0.0), rng), "loop": True})
        elif c.get("every"):
            t, j = at + rng.uniform(0, float(c["every"][0])), 0
            while t < at + dur:
                out.append({**base, "id": f"{sid}_fol{k + 1:02d}_{j + 1:03d}", "sound": _pick(c["sound"], rng),
                            "at": round(t, 3), "dur": None, "gain": gain + rng.uniform(-3.0, 2.0),
                            "pan": _span(c.get("pan", 0.0), rng), "loop": False})
                t += _span(c["every"], rng)
                j += 1
        else:
            out.append({**base, "id": f"{sid}_fol{k + 1:02d}", "sound": _pick(c["sound"], rng), "at": at,
                        "dur": c.get("dur"), "gain": gain, "pan": _span(c.get("pan", 0.0), rng), "loop": False})
    return out


def find_sound(name, dirs, root):
    p = Path(name)
    if p.is_absolute():
        return p if p.exists() else None
    for d in dirs:
        base = Path(d) if Path(d).is_absolute() else Path(root) / d
        if (base / p).exists():
            return base / p
    return None


def build_track(ep, timeline, work, root):
    """Every segment's foley mixed to one stereo 48 kHz WAV the length of the film, or None if there is none."""
    from tools.film import voice
    dirs = ep.get("foley_dirs", DEFAULT_DIRS)
    events = []
    for s in timeline["segments"]:
        for e in s.get("foley", []):
            path = find_sound(e["sound"], dirs, root)
            if path is None:
                print(f"warning: foley sound not found: {e['sound']} (in {', '.join(dirs)})", file=sys.stderr)
                continue
            events.append((s["start"] + e["at"], e, path))
    if not events:
        return None
    fdir = Path(work) / "foley"
    fdir.mkdir(parents=True, exist_ok=True)
    total = timeline["duration"]
    inputs, chains = [], []
    for i, (t, e, path) in enumerate(events):
        chain = ["aresample=48000", "aformat=channel_layouts=mono"]
        if e["rate"] != 1.0:
            chain += [f"asetrate={int(48000 * e['rate'])}", "aresample=48000"]
        fx = voice.FX.get(e["fx"], "")
        if fx:
            chain.append(fx)
        if e["dur"]:
            chain.append(f"atrim=0:{float(e['dur']):.3f}")
            if e["fade"]:
                chain.append(f"afade=t=in:d={e['fade']:.2f},afade=t=out:st={max(0, float(e['dur']) - e['fade']):.2f}:d={e['fade']:.2f}")
        g = 10 ** (e["gain"] / 20)
        left, right = g * min(1.0, 1 - e["pan"]), g * min(1.0, 1 + e["pan"])
        ms = int(round(t * 1000))
        chain += [f"pan=stereo|c0={left:.4f}*c0|c1={right:.4f}*c0", f"adelay={ms}|{ms}"]
        inputs += (["-stream_loop", "-1"] if e["loop"] else []) + ["-i", str(path)]
        chains.append(f"[{i}:a]{','.join(chain)}[f{i}]")
    # amix in groups keeps the graph small when a cue fires every second for a minute
    script = list(chains)
    labels = [f"[f{i}]" for i in range(len(events))]
    gen = 0
    while len(labels) > 1:
        nxt = []
        for j in range(0, len(labels), 32):
            grp = labels[j:j + 32]
            name = f"[m{gen}_{j}]"
            script.append("".join(grp) + f"amix=inputs={len(grp)}:normalize=0:duration=longest{name}" if len(grp) > 1
                          else f"{grp[0]}anull{name}")
            nxt.append(name)
        labels, gen = nxt, gen + 1
    script.append(f"{labels[0]}apad,atrim=0:{total:.3f}[out]")
    (fdir / "graph.txt").write_text(";\n".join(script))
    out = fdir / "foley.wav"
    subprocess.run(["ffmpeg", "-v", "error", "-y", *inputs, "-filter_complex_script", str(fdir / "graph.txt"),
                    "-map", "[out]", "-ac", "2", "-ar", "48000", str(out)], check=True)
    return out
