#!/usr/bin/env python3
"""Film pipeline: episode spec -> camera paths -> capture -> finished film.

    tools/film/film.py plan     film/episodes/ep01.json            # .cam paths, session cfgs, timeline, script
    tools/film/film.py boards   film/episodes/ep01.json            # storyboard sheets drawn from the camera paths
    tools/film/film.py animatic film/episodes/ep01.json            # previs frames from the waypoint map, then assemble
    tools/film/film.py assemble film/episodes/ep01.json [--captures DIR] [--burn-subs]

Work goes to out/film/<episode id>/ (override with --out). The plan is what the capture script
(scripts/film-capture.sh) puts on the disc; the assembler reads the frames that capture brings back.
See docs/FILM_PIPELINE.md.
"""
import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.film import actors, campath, clearance, shots, stamp, voice  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
W, H = 1920, 1080
FONT_SERIF = "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"
FONT_SERIF_B = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"
FONT_SANS = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONT_SANS_B = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
WORDS_PER_SEC = 2.4  # unhurried documentary read
PREROLL = 2.0        # seconds held on key 0 before each shot (lets bots and props settle)

GRADES = {
    "clean": "",
    # muted, warm, soft contrast, light grain and vignette: reads as archive without faking archive footage
    "archival": "eq=contrast=1.06:saturation=0.55:gamma=0.97,colorbalance=rs=0.05:gs=0.02:bs=-0.06,"
                "noise=alls=7:allf=t,vignette=PI/5",
    "monochrome": "hue=s=0,eq=contrast=1.12:gamma=0.95,noise=alls=9:allf=t,vignette=PI/4.5",
}
ASPECTS = {"16:9": 16 / 9, "2.39": 2.39, "1.85": 1.85}


# --- episode and timeline -------------------------------------------------------------------------------
def load_episode(path):
    ep = json.loads(Path(path).read_text())
    for need in ("id", "title", "sequence"):
        if need not in ep:
            raise ValueError(f"episode needs {need!r}")
    if not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", ep["id"]):
        raise ValueError("episode id: lowercase letters, digits, - and _")
    return ep


LINE_GAP = 0.6       # pause between narration lines that have no "at" of their own


def _rendered(voice_dir, line_id):
    """A line tools/film/voice.py rendered, and its real length."""
    p = Path(voice_dir) / f"{line_id}.wav" if voice_dir else None
    if p and p.exists():
        return str(p.resolve()), voice.wav_duration(p)
    return None, None


def narration_lines(seg, voice_dir=None):
    """Narration with times: a line's "at" (seconds into the segment) or right after the line before it.
    A line with no "audio" of its own uses the WAV voice.py rendered for it, and that WAV's length."""
    out, t = [], 0.8
    for k, n in enumerate(seg.get("narration", [])):
        audio, real = (n.get("audio"), None) if n.get("audio") else _rendered(voice_dir, f"{seg.get('id')}_{k + 1:02d}")
        dur = n.get("dur") or real or max(1.6, len(n["text"].split()) / WORDS_PER_SEC + 0.4)
        at = float(n["at"]) if "at" in n else t
        if out and at < out[-1]["at"] + out[-1]["dur"]:
            print(f"warning: {seg.get('id')}: \"{n['text'][:40]}...\" starts before the line before it ends",
                  file=sys.stderr)
        out.append({"at": round(at, 2), "dur": round(dur, 2), "text": n["text"], "audio": audio})
        t = at + dur + LINE_GAP
    return out


def adr_lines(seg, voice_dir=None):
    """Dialogue, callouts and walla placed in the segment, with the rendered WAV where there is one."""
    out = []
    for a in voice.expand_adr(seg):
        audio, real = _rendered(voice_dir, a["id"])
        out.append({"id": a["id"], "at": round(a["at"], 2), "dur": round(real or voice.estimate(a), 2),
                    "text": a["text"], "subtitle": a.get("subtitle"), "audio": audio, "gain": a["gain"],
                    "pan": a["pan"]})
    return out


def build_timeline(ep, root=ROOT, voice_dir=None):
    """Resolve every shot's camera path and lay the sequence on one clock."""
    fps = int(ep.get("fps", 30))
    graphs, anchors = {}, ep.get("anchors", {})
    segs, t, ids = [], 0.0, set()
    for i, item in enumerate(ep["sequence"]):
        if "card" in item:
            c = dict(item["card"])
            seg = {"kind": "card", "id": c.get("id", f"card{i:02d}"), "dur": float(c.get("dur", 5.0)), "card": c}
        elif "shot" in item:
            s = item["shot"]
            sid = s["id"]
            if not re.fullmatch(r"[a-z0-9][a-z0-9_]*", sid):
                raise ValueError(f"shot id {sid!r}: lowercase letters, digits and _ (it becomes a console arg)")
            mapname = s["map"]
            if mapname not in graphs:
                graphs[mapname] = shots.MapGraph.load(mapname, root)
            path = shots.build(s["camera"], graphs[mapname], anchors.get(mapname, {}))
            cast = actors.build(s.get("actors"), graphs[mapname], anchors.get(mapname, {}), f"{sid}: ")
            seg = {"kind": "shot", "id": sid, "map": mapname, "dur": path.duration, "path": path, "shot": s,
                   "frames": int(round(path.duration * fps)) + 1, "cast": cast}
        else:
            raise ValueError(f"sequence item {i}: needs \"card\" or \"shot\"")
        if seg["id"] in ids:
            raise ValueError(f"duplicate id {seg['id']!r}")
        ids.add(seg["id"])
        src = item.get("shot") or item.get("card")
        seg["start"] = round(t, 3)
        seg["narration"] = narration_lines(src, voice_dir)
        seg["adr"] = adr_lines(src, voice_dir)
        seg["lower_third"] = src.get("lower_third")
        seg["chapter"] = src.get("chapter")
        for n in seg["narration"]:
            if n["at"] + n["dur"] > seg["dur"] + 0.05:
                print(f"warning: {seg['id']}: narration \"{n['text'][:40]}...\" runs {n['at'] + n['dur'] - seg['dur']:.1f}s"
                      " past the end of the segment", file=sys.stderr)
        for a in seg["adr"]:
            if a["at"] + a["dur"] > seg["dur"] + 0.05:
                print(f"warning: {seg['id']}: {a['id']} \"{a['text'][:40]}\" runs"
                      f" {a['at'] + a['dur'] - seg['dur']:.1f}s past the end of the segment", file=sys.stderr)
        segs.append(seg)
        t += seg["dur"]
    return {"fps": fps, "segments": segs, "duration": round(t, 3), "graphs": graphs}


def sessions(timeline):
    """Shots grouped by map, in film order: one boot captures one map's shots back to back."""
    out = {}
    for s in timeline["segments"]:
        if s["kind"] == "shot":
            out.setdefault(s["map"], []).append(s)
    return out


def _tc(t, sep=","):
    ms = int(round(t * 1000))
    return f"{ms // 3600000:02d}:{ms // 60000 % 60:02d}:{ms // 1000 % 60:02d}{sep}{ms % 1000:03d}"


def write_plan(ep, timeline, out):
    out.mkdir(parents=True, exist_ok=True)
    cine = out / "cine"
    if cine.exists():
        shutil.rmtree(cine)
    cine.mkdir()
    fps = timeline["fps"]
    for s in timeline["segments"]:
        if s["kind"] == "shot":
            (cine / f"{s['id']}.cam").write_text(campath.dumps(
                s["path"], f"{ep['id']} {s['id']} on {s['map']}: {s['dur']:.2f} s, {s['frames']} frames at {fps}"))
            if s["cast"]:
                (cine / f"{s['id']}.act").write_text(actors.dumps(
                    s["cast"], f"{ep['id']} {s['id']} on {s['map']}: {len(s['cast'])} actor(s)"))

    # one listenserver.cfg block per map: play its shots in order, each chained from the last one's end
    for mapname, group in sessions(timeline).items():
        lines = [f"// film {ep['id']}: {len(group)} shot(s) on {mapname} (tools/film/film.py plan)",
                 f"cine_fps {fps}", "cine_hud 0", "cine_viewmodel 0", "cine_stamp 1", "gc_hud_off 1"]
        lines += ep.get("server_cmds", {}).get(mapname, [])
        for i, s in enumerate(group):
            nxt = f"film_{i + 1}" if i + 1 < len(group) else "film_end"
            cast = f"film_cast {s['id']} {PREROLL:g}" if s["cast"] else "film_cast_clear"
            lines.append(f'alias film_{i} "{cast}; cine_onend {nxt}; cine_play {s["id"]} {PREROLL:g}"')
        lines.append(f'alias film_end "film_cast_clear; echo FILM SESSION DONE {mapname}"')
        lines.append("film_0")
        (out / f"film-{mapname}.cfg").write_text("\n".join(lines) + "\n")
        # the sides whose player models the map has to precache for the actors
        sides = sorted({a["team"] for s in group for a in s["cast"]})
        (cine / f"cast-{mapname}.txt").write_text("".join(f"{t}\n" for t in sides))

    # subtitles, chapters, narration script
    cues = []
    for s in timeline["segments"]:
        cues += [(s["start"] + ln["at"], ln["dur"], ln["text"]) for ln in s["narration"]]
        cues += [(s["start"] + a["at"], a["dur"], a["subtitle"]) for a in s["adr"] if a.get("subtitle")]
    srt = [f"{n}\n{_tc(a)} --> {_tc(a + d)}\n{text}\n" for n, (a, d, text) in enumerate(sorted(cues), 1)]
    (out / "subtitles.srt").write_text("\n".join(srt))

    meta = [";FFMETADATA1", f"title={ep['title']}"]
    for s in timeline["segments"]:
        if s["chapter"]:
            meta += ["[CHAPTER]", "TIMEBASE=1/1000", f"START={int(s['start'] * 1000)}",
                     f"END={int((s['start'] + s['dur']) * 1000)}", f"title={s['chapter']}"]
    (out / "chapters.txt").write_text("\n".join(meta) + "\n")

    script = [f"# {ep['title']}: narration script", "",
              f"{ep.get('logline', '')}", "",
              f"Running time {timeline['duration']:.1f} s at {fps} fps. Record each line as its own WAV, name it as shown,",
              "and set the shot's narration `audio` to it. Timing is where the line starts in the film.", ""]
    for s in timeline["segments"]:
        script.append(f"## {s['id']} ({_tc(s['start'], '.')}, {s['dur']:.1f} s)"
                      + (f" on `{s['map']}`" if s['kind'] == 'shot' else ""))
        if s["shot" if s["kind"] == "shot" else "card"].get("note"):
            script.append(f"_{s['shot' if s['kind'] == 'shot' else 'card']['note']}_")
        for k, line in enumerate(s["narration"]):
            script.append(f"- `{_tc(s['start'] + line['at'], '.')}` **{s['id']}_{k + 1:02d}.wav** ({line['dur']:.1f} s): {line['text']}")
        for a in s["adr"]:
            script.append(f"  - ADR `{_tc(s['start'] + a['at'], '.')}` **{a['id']}.wav** ({a['dur']:.1f} s): _{a['text']}_"
                          + (f" ({a['subtitle']})" if a.get("subtitle") else ""))
        script.append("")
    (out / "narration-script.md").write_text("\n".join(script))

    plan = {"episode": ep["id"], "fps": fps, "duration": timeline["duration"], "preroll": PREROLL,
            "sessions": {m: [s["id"] for s in g] for m, g in sessions(timeline).items()},
            "segments": [{k: v for k, v in s.items() if k not in ("path", "shot", "card")} for s in timeline["segments"]]}
    (out / "plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    return plan


# --- capture frames: split a session's dumps into shots -------------------------------------------------
def _natural(p):
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", p.name)]


def read_session(frame_dir):
    """[(frame_no, path)] in dump order for every stamped image; unstamped (preroll, menus) are dropped."""
    from PIL import Image
    files = sorted((p for p in Path(frame_dir).iterdir() if p.suffix.lower() in (".png", ".tga", ".jpg", ".bmp")),
                   key=_natural)
    out = []
    for p in files:
        with Image.open(p) as im:
            f = stamp.read(im)
        if f is not None:
            out.append((f, p))
    return out


def split_runs(stamped):
    """Consecutive stamped frames -> one run per shot. A new shot starts when the frame number goes backwards."""
    runs, cur, last = [], [], -1
    for f, p in stamped:
        if cur and f < last:
            runs.append(cur)
            cur = []
        cur.append((f, p))
        last = f
    if cur:
        runs.append(cur)
    return runs


def frame_list(run, expected):
    """One image per film frame 0..expected-1: duplicates dropped, a missed frame repeats the one before."""
    first = {}
    for f, p in run:
        first.setdefault(f, p)
    out, prev, missing = [], None, 0
    for f in range(expected):
        p = first.get(f)
        if p is None:
            missing += 1
            p = prev if prev is not None else first[min(first)]
        out.append(p)
        prev = p
    return out, missing


# --- rendering helpers ----------------------------------------------------------------------------------
def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stderr[-4000:])
        raise SystemExit(f"command failed: {' '.join(cmd[:6])} ...")


def _font(path, size):
    from PIL import ImageFont
    return ImageFont.truetype(path, size)


def _wrap(draw, text, font, maxw):
    lines = []
    for para in text.split("\n"):
        line = ""
        for w in para.split():
            t = (line + " " + w).strip()
            if draw.textlength(t, font=font) <= maxw:
                line = t
            else:
                lines.append(line)
                line = w
        lines.append(line)
    return lines


def render_card(card, path):
    """Title / chapter / dedication card: serif type on near-black."""
    from PIL import Image, ImageDraw
    im = Image.new("RGB", (W, H), (10, 10, 9))
    d = ImageDraw.Draw(im)
    style = card.get("style", "title")
    y = H * (0.36 if style == "title" else 0.40)
    if card.get("kicker"):
        f = _font(FONT_SANS, 30)
        k = card["kicker"].upper()
        d.text(((W - d.textlength(k, font=f)) / 2, y - 70), k, font=f, fill=(196, 170, 120))
    if card.get("title"):
        f = _font(FONT_SERIF_B if style == "title" else FONT_SERIF, 76 if style == "title" else 58)
        for ln in _wrap(d, card["title"], f, W - 360):
            d.text(((W - d.textlength(ln, font=f)) / 2, y), ln, font=f, fill=(236, 232, 222))
            y += f.size * 1.25
    if card.get("body"):
        y += 30
        f = _font(FONT_SERIF, 36)
        for ln in _wrap(d, card["body"], f, W - 520):
            d.text(((W - d.textlength(ln, font=f)) / 2, y), ln, font=f, fill=(200, 196, 186))
            y += 52
    if card.get("footer"):
        f = _font(FONT_SANS, 24)
        yy = H - 120
        for ln in _wrap(d, card["footer"], f, W - 400):
            d.text(((W - d.textlength(ln, font=f)) / 2, yy), ln, font=f, fill=(140, 136, 128))
            yy += 34
    im.save(path)


def render_lower_third(text, path):
    from PIL import Image, ImageDraw
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    lines = text.split("\n")
    f1, f2 = _font(FONT_SERIF_B, 44), _font(FONT_SANS, 30)
    x, y = 120, H - 250
    wmax = max([d.textlength(lines[0], font=f1)] + [d.textlength(ln, font=f2) for ln in lines[1:]])
    d.rectangle([x - 28, y - 18, x + wmax + 36, y + 62 + 42 * (len(lines) - 1)], fill=(0, 0, 0, 150))
    d.rectangle([x - 28, y - 18, x - 22, y + 62 + 42 * (len(lines) - 1)], fill=(196, 170, 120, 255))
    d.text((x, y), lines[0], font=f1, fill=(240, 236, 226, 255))
    for i, ln in enumerate(lines[1:]):
        d.text((x, y + 62 + 42 * i), ln, font=f2, fill=(210, 206, 196, 255))
    im.save(path)


def _video_args(fps):
    return ["-r", str(fps), "-c:v", "libx264", "-preset", "medium", "-crf", "18", "-pix_fmt", "yuv420p"]


def card_clip(seg, fps, work):
    png = work / f"{seg['id']}.png"
    render_card(seg["card"], png)
    out = work / f"{seg['id']}.mp4"
    d = seg["dur"]
    fade = min(1.0, d / 4)
    run(["ffmpeg", "-v", "error", "-y", "-loop", "1", "-framerate", str(fps), "-t", f"{d:.3f}", "-i", str(png),
         "-vf", f"fade=t=in:st=0:d={fade},fade=t=out:st={d - fade:.3f}:d={fade},format=yuv420p",
         "-frames:v", str(int(round(d * fps))), *_video_args(fps), str(out)])
    return out


def shot_clip(seg, frames, fps, aspect, grade, work, src_size):
    """Image sequence -> letterboxed, graded clip with its lower third."""
    seq = work / f"seq_{seg['id']}"
    if seq.exists():
        shutil.rmtree(seq)
    seq.mkdir()
    ext = frames[0].suffix.lower()
    for i, p in enumerate(frames):
        os.symlink(os.path.abspath(p), seq / f"{i:06d}{ext}")
    sw, sh = src_size
    cell = math.ceil(stamp.cell_size(sw)) + 2           # stamp rows, kept out of the picture
    ch = min(sh - 2 * cell, int(sw / aspect)) // 2 * 2   # picture height inside the source
    cw = sw // 2 * 2
    if sh - 2 * cell < int(sw / aspect):                 # source too wide: trim the sides instead
        cw = int(ch * aspect) // 2 * 2
    y0 = (sh - ch) // 2
    ph = int(round(W / aspect)) // 2 * 2
    vf = [f"crop={cw}:{ch}:{(sw - cw) // 2}:{y0}", f"scale={W}:{ph}:flags=lanczos", "setsar=1"]
    if GRADES.get(grade):
        vf.append(GRADES[grade])
    vf.append(f"pad={W}:{H}:0:{(H - ph) // 2}:black")
    d = seg["dur"]
    inputs = ["-framerate", str(fps), "-i", str(seq / f"%06d{ext}")]
    if seg.get("lower_third"):
        lt = work / f"{seg['id']}_lt.png"
        render_lower_third(seg["lower_third"], lt)
        t0, t1 = min(1.0, d * 0.15), min(d - 0.5, 1.0 + 5.0)
        fc = (f"[0:v]{','.join(vf)}[v];[1:v]format=rgba,fade=t=in:st={t0}:d=0.6:alpha=1,"
              f"fade=t=out:st={max(t0 + 0.6, t1 - 0.6):.2f}:d=0.6:alpha=1[lt];[v][lt]overlay=0:0:shortest=1,format=yuv420p[o]")
        inputs += ["-loop", "1", "-framerate", str(fps), "-t", f"{d + 0.5:.3f}", "-i", str(lt)]
        filt = ["-filter_complex", fc, "-map", "[o]"]
    else:
        filt = ["-vf", ",".join(vf) + ",format=yuv420p"]
    out = work / f"{seg['id']}.mp4"
    run(["ffmpeg", "-v", "error", "-y", *inputs, *filt, "-frames:v", str(len(frames)), *_video_args(fps), str(out)])
    return out


def build_audio(ep, timeline, plan_dir, work):
    """Narration and ADR at their times over an optional music bed. The bed ducks under narration and,
    less, under ADR; ADR ducks a little under narration so the voice-over always reads."""
    dur = timeline["duration"]
    inputs, nar, adr = [], [], []

    def add(path, at, chain, bucket, what):
        p = Path(path)
        p = p if p.is_absolute() else (plan_dir / p)
        if not p.exists():
            print(f"warning: {what} audio missing: {p}", file=sys.stderr)
            return
        ms = int(round(at * 1000))
        i = inputs.count("-i")
        inputs.extend(["-i", str(p)])
        bucket.append(f"[{i}:a]aresample=48000,{chain},adelay={ms}|{ms}[{'n' if bucket is nar else 'a'}{len(bucket)}]")

    for s in timeline["segments"]:
        for line in s["narration"]:
            if line.get("audio"):
                add(line["audio"], s["start"] + line["at"], "pan=stereo|c0=c0|c1=c0", nar, "narration")
        for a in s.get("adr", []):
            if a.get("audio"):
                g = 10 ** (a["gain"] / 20)
                left, right = g * min(1.0, 1 - a["pan"]), g * min(1.0, 1 + a["pan"])
                add(a["audio"], s["start"] + a["at"], f"pan=stereo|c0={left:.3f}*c0|c1={right:.3f}*c0", adr, "ADR")
    music = ep.get("music")
    out = work / "audio.wav"
    base = ["ffmpeg", "-v", "error", "-y"]
    if not nar and not adr and not music:
        run(base + ["-f", "lavfi", "-t", f"{dur:.3f}", "-i", "anullsrc=r=48000:cl=stereo", str(out)])
        return out
    fc = nar + adr

    def bus(labels, prefix, name):
        if labels:
            fc.append("".join(f"[{prefix}{i}]" for i in range(len(labels)))
                      + f"amix=inputs={len(labels)}:normalize=0,apad[{name}]")
            return name
        return None

    voice_bus, adr_bus = bus(nar, "n", "voice"), bus(adr, "a", "adr")
    if music:
        mp = Path(music["file"]) if Path(music["file"]).is_absolute() else plan_dir / music["file"]
        mi = inputs.count("-i")
        inputs += ["-stream_loop", "-1", "-i", str(mp)]
        fc.append(f"[{mi}:a]aresample=48000,volume={music.get('gain', 0.35)},"
                  f"afade=t=out:st={max(0, dur - 4):.2f}:d=4[bed]")
    # every bus the bed and ADR key off has to be split once per use
    keys = {"voice": (["adr"] if adr_bus else []) + (["bed"] if music else []),
            "adr": ["bed"] if music else []}
    for name, uses in (("voice", keys["voice"]), ("adr", keys["adr"])):
        if (voice_bus if name == "voice" else adr_bus) and uses:
            fc.append(f"[{name}]asplit={len(uses) + 1}[{name}_m]" + "".join(f"[{name}_k{u}]" for u in uses))
        elif (voice_bus if name == "voice" else adr_bus):
            fc.append(f"[{name}]anull[{name}_m]")
    final = []
    if adr_bus:
        if voice_bus:
            fc.append("[adr_m][voice_kadr]sidechaincompress=threshold=0.05:ratio=3:attack=30:release=400[adr_d]")
            final.append("[adr_d]")
        else:
            final.append("[adr_m]")
    if voice_bus:
        final.append("[voice_m]")
    if music:
        bed = "bed"
        for name in ("voice", "adr"):
            if name in keys and "bed" in keys[name] and (voice_bus if name == "voice" else adr_bus):
                ratio = 8 if name == "voice" else 3
                fc.append(f"[{bed}][{name}_kbed]sidechaincompress=threshold=0.03:ratio={ratio}:attack=40:release=600[{bed}_{name}]")
                bed = f"{bed}_{name}"
        final.append(f"[{bed}]")
    if len(final) > 1:
        fc.append("".join(final) + f"amix=inputs={len(final)}:normalize=0[mix]")
    else:
        fc.append(f"{final[0]}anull[mix]")
    run(base + inputs + ["-filter_complex", ";".join(fc), "-map", "[mix]", "-t", f"{dur:.3f}", "-ac", "2", str(out)])
    return out


def assemble(ep, timeline, plan_dir, captures, out_path, burn_subs=False):
    from PIL import Image
    fps = timeline["fps"]
    aspect = ASPECTS[ep.get("aspect", "16:9")]
    grade = ep.get("grade", "clean")
    work = plan_dir / "work"
    work.mkdir(exist_ok=True)
    shot_frames, report = {}, []
    for mapname, group in sessions(timeline).items():
        frame_dir = Path(captures) / mapname
        if not frame_dir.is_dir():
            raise SystemExit(f"no captured frames for {mapname}: {frame_dir}")
        runs = split_runs(read_session(frame_dir))
        if len(runs) < len(group):
            raise SystemExit(f"{mapname}: {len(runs)} stamped run(s) in {frame_dir}, plan has {len(group)} shot(s)")
        if len(runs) > len(group):
            print(f"warning: {mapname}: {len(runs)} runs for {len(group)} shots; using the last {len(group)}",
                  file=sys.stderr)
            runs = runs[-len(group):]
        for seg, r in zip(group, runs):
            frames, missing = frame_list(r, seg["frames"])
            frames = frames[:int(round(seg["dur"] * fps))]  # the last key's frame is the next segment's first
            dups = len(r) - len({f for f, _ in r})
            report.append(f"{seg['id']}: {seg['frames']} frames, {len(r)} dumped, {dups} duplicate(s) dropped,"
                          f" {missing} missed and filled")
            shot_frames[seg["id"]] = frames
    clips = []
    for s in timeline["segments"]:
        if s["kind"] == "card":
            clips.append(card_clip(s, fps, work))
        else:
            fr = shot_frames[s["id"]]
            with Image.open(fr[0]) as im:
                size = im.size
            clips.append(shot_clip(s, fr, fps, aspect, grade, work, size))
    lst = work / "clips.txt"
    lst.write_text("".join(f"file '{c.resolve()}'\n" for c in clips))
    video = work / "video.mp4"
    run(["ffmpeg", "-v", "error", "-y", "-f", "concat", "-safe", "0", "-i", str(lst), "-c", "copy", str(video)])
    audio = build_audio(ep, timeline, plan_dir, work)
    srt = plan_dir / "subtitles.srt"
    cmd = ["ffmpeg", "-v", "error", "-y", "-i", str(video), "-i", str(audio), "-i", str(plan_dir / "chapters.txt")]
    has_subs = srt.exists() and srt.stat().st_size > 0
    if has_subs and not burn_subs:
        # every input before any output option: ffmpeg reads options as belonging to the next -i or output
        cmd += ["-i", str(srt)]
    if burn_subs and has_subs:
        # libass sizes in a 288-line space: about 37 px type at 1080, sitting low (in the bar when letterboxed)
        style = "FontName=DejaVu Serif,FontSize=10,PrimaryColour=&H00E8ECF0,OutlineColour=&H90000000,BorderStyle=1,Outline=0.8,Shadow=0,MarginV=10,MarginL=40,MarginR=40"
        cmd += ["-vf", f"subtitles={srt}:force_style='{style}'", *_video_args(fps)]
    else:
        cmd += ["-c:v", "copy"]
    if has_subs and not burn_subs:
        cmd += ["-map", "3:s", "-c:s", "mov_text", "-metadata:s:s:0", "language=eng"]
    cmd += ["-map", "0:v", "-map", "1:a", "-map_metadata", "2", "-map_chapters", "2",
            "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", "-t", f"{timeline['duration']:.3f}", str(out_path)]
    run(cmd)
    (plan_dir / "assembly-report.txt").write_text("\n".join(report) + "\n")
    return report


# --- animatic: previs frames from the waypoint map ------------------------------------------------------
class TopDown:
    """The map from above, drawn from its waypoints (no game assets needed)."""

    def __init__(self, graph, size, margin=40):
        from PIL import Image, ImageDraw
        (x0, y0, z0), (x1, y1, z1) = graph.bounds()
        self.size, self.margin = size, margin
        span = max(x1 - x0, y1 - y0) + 400
        self.scale = (min(size) - 2 * margin) / span
        self.cx, self.cy = (x0 + x1) / 2, (y0 + y1) / 2
        self.img = Image.new("RGB", size, (18, 20, 19))
        d = ImageDraw.Draw(self.img)
        for i, (x, y, z) in enumerate(graph.nodes):
            px, py = self.px((x, y))
            for to, _k, _c in graph.adj[i]:
                if to > i:
                    qx, qy = self.px(graph.nodes[to][:2])
                    d.line([px, py, qx, qy], fill=(44, 48, 44), width=1)
        for x, y, z in graph.nodes:
            px, py = self.px((x, y))
            h = (z - z0) / max(1.0, z1 - z0)
            col = (int(70 + 150 * h), int(96 + 70 * h), int(70 + 30 * (1 - h)))
            r = max(1.5, 22 * self.scale)
            d.ellipse([px - r, py - r, px + r, py + r], fill=col)

    def px(self, p):
        return (self.size[0] / 2 + (p[0] - self.cx) * self.scale, self.size[1] / 2 - (p[1] - self.cy) * self.scale)


def preview_session(timeline, mapname, group, out_dir, fps, aspect=16 / 9, size=(960, 720)):
    """Frames for one map, laid out like a capture session: preroll (unstamped) then each shot, stamped."""
    from PIL import Image, ImageDraw
    out_dir.mkdir(parents=True, exist_ok=True)
    for p in out_dir.glob("*.png"):
        p.unlink()
    W4, H4 = size
    band = (int((H4 - W4 / aspect) / 2) + 6, int((H4 + W4 / aspect) / 2) - 6)  # what survives the letterbox crop
    graph = timeline["graphs"][mapname]
    top = TopDown(graph, (int((band[1] - band[0]) * 1.05), band[1] - band[0] - 24))
    f_big, f_small, f_mono = _font(FONT_SERIF_B, 26), _font(FONT_SANS, 17), _font("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 15)
    n = 0
    for seg in group:
        path = seg["path"]
        route = [top.px(campath.sample(path, path.duration * i / 200)[0][:2]) for i in range(201)]
        pre = int(PREROLL * fps)
        for f in range(-pre, seg["frames"]):
            t = max(0.0, f / fps)
            o, a, fov = campath.sample(path, t)
            im = Image.new("RGB", size, (0, 0, 0))
            im.paste(top.img, (14, band[0] + 12))
            d = ImageDraw.Draw(im)
            ox, oy = 14, band[0] + 12
            d.line([(x + ox, y + oy) for x, y in route], fill=(205, 170, 90), width=2)
            cx, cy = top.px(o[:2])
            cx, cy = cx + ox, cy + oy
            half = math.radians(fov / 2)
            yaw = math.radians(a[1])
            L = 70
            p1 = (cx + L * math.cos(yaw - half), cy - L * math.sin(yaw - half))
            p2 = (cx + L * math.cos(yaw + half), cy - L * math.sin(yaw + half))
            d.polygon([(cx, cy), p1, p2], fill=(240, 230, 200), outline=(255, 255, 255))
            for (fx, fy, _fz, pose), act in zip(actors.figures(seg["cast"], t), seg["cast"]):
                ax, ay = top.px((fx, fy))
                ax, ay = ax + ox, ay + oy
                col = (120, 150, 80) if act["team"] == "allies" else (150, 150, 160)
                r = 3 if pose in actors.LOW else 4
                d.ellipse([ax - r, ay - r, ax + r, ay + r], fill=col, outline=(20, 20, 20))
            d.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], fill=(220, 60, 50))
            # side panel
            px0 = 14 + top.size[0] + 24
            y = band[0] + 18
            d.text((px0, y), seg["id"], font=f_big, fill=(236, 232, 222))
            y += 40
            kind = seg["shot"]["camera"]["type"]
            for ln in (f"{mapname} · {kind}", f"t {t:5.2f} / {seg['dur']:.2f} s", f"fov {fov:4.1f}",
                       f"pitch {a[0]:5.1f}  yaw {a[1]:6.1f}", f"eye {o[0]:.0f} {o[1]:.0f} {o[2]:.0f}"):
                d.text((px0, y), ln, font=f_mono if ln[0] in "tfpe" else f_small, fill=(200, 196, 186))
                y += 24
            y += 10
            note = seg["shot"].get("note", "")
            for ln in _wrap(d, note, f_small, W4 - px0 - 16)[:5]:
                d.text((px0, y), ln, font=f_small, fill=(170, 166, 156))
                y += 22
            d.text((px0, band[1] - 34), "PREVIS · waypoint map, not game render", font=f_small, fill=(196, 170, 120))
            if f >= 0:
                stamp.draw(im, f)
            im.save(out_dir / f"framedump_{n}.png", compress_level=1)
            n += 1
            # an emulator sometimes shows a frame twice: do the same now and then so assembly is exercised
            if f >= 0 and f % 97 == 13:
                im.save(out_dir / f"framedump_{n}.png", compress_level=1)
                n += 1
    return n


# --- CLI -----------------------------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=("plan", "check", "boards", "animatic", "assemble"))
    ap.add_argument("episode")
    ap.add_argument("--out", help="work directory (default out/film/<episode id>)")
    ap.add_argument("--captures", help="captured frames, one folder per map (default <out>/captures)")
    ap.add_argument("--burn-subs", action="store_true", help="burn narration subtitles into the picture")
    ap.add_argument("--output", help="finished film path")
    ap.add_argument("--bsp", action="append", default=[], metavar="MAP=PATH",
                    help="a map's .bsp for the clearance check (default: $FILM_BSP_DIR, content/dod/maps)")
    args = ap.parse_args(argv)

    ep = load_episode(args.episode)
    out = Path(args.out) if args.out else ROOT / "out" / "film" / ep["id"]
    timeline = build_timeline(ep, voice_dir=out / "voice")
    # no camera through walls or soldiers: plan and check stop on a problem, previews only warn
    problems, unchecked = clearance.check(timeline, bsps=dict(b.split("=", 1) for b in args.bsp))
    for line in clearance.describe(problems):
        print(f"clearance: {line}", file=sys.stderr)
    for m in unchecked:
        print(f"clearance: {m}.bsp not found: shots on {m} are checked against the soldiers only "
              f"(set FILM_BSP_DIR or --bsp {m}=PATH)", file=sys.stderr)
    if args.command == "check":
        print(f"clearance: {sum(len(v) for v in problems.values())} problem(s) in {len(problems)} shot(s)"
              + (f"; not checked against geometry: {', '.join(unchecked)}" if unchecked else ""))
        return 1 if problems else 0
    if problems and args.command == "plan":
        print("error: camera paths clip (above); fix the shots or move the cameras", file=sys.stderr)
        return 1
    plan = write_plan(ep, timeline, out)
    print(f"plan: {len(plan['segments'])} segments, {plan['duration']:.1f} s, sessions "
          + ", ".join(f"{m} ({len(v)} shots)" for m, v in plan["sessions"].items()) + f" -> {out}")
    if args.command == "plan":
        return 0
    if args.command == "boards":
        from tools.film import board
        pages = board.render_sheets(ep, timeline, out, ep.get("anchors", {}))
        (out / "shot-list.md").write_text(board.shot_list(ep, timeline))
        print("\n".join(f"storyboard: {p}" for p in pages))
        return 0
    if args.command == "animatic":
        captures = out / "previs"
        for mapname, group in sessions(timeline).items():
            n = preview_session(timeline, mapname, group, captures / mapname, timeline["fps"],
                                ASPECTS[ep.get("aspect", "16:9")])
            print(f"previs {mapname}: {n} frames")
        dest = Path(args.output) if args.output else out / f"{ep['id']}-animatic.mp4"
    else:
        captures = Path(args.captures) if args.captures else out / "captures"
        dest = Path(args.output) if args.output else out / f"{ep['id']}.mp4"
    report = assemble(ep, timeline, out, captures, dest, burn_subs=args.burn_subs)
    print("\n".join(report))
    print(f"film: {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
