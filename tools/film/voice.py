#!/usr/bin/env python3
"""Voice-over and ADR: an episode's narration and dialogue lines rendered to WAVs that drop into the edit.

    tools/film/voice.py lines  film/episodes/ep01.json        # list every line with its id, voice and delivery
    tools/film/voice.py cast   film/episodes/ep01.json        # design each voice once: voice/cast/<voice>.wav
    tools/film/voice.py render film/episodes/ep01.json        # every line -> voice/<line id>.wav (cached)
    tools/film/voice.py mix    film/episodes/ep01.json        # dialogue + music on the film's clock: <ep>-dx.wav

The model is Qwen3-TTS (Apache 2.0, runs locally on a CUDA GPU). Every voice is *designed* from a written
description (VoiceDesign), never cloned from a recording of a real person. Narration then reads in that
designed voice through the Base model's clone path, so the narrator sounds the same on every line; ADR is
designed per line, so each callout gets its own delivery ("shouting over gunfire, out of breath").
`--backend placeholder` makes speech-shaped noise of the right length instead, so the timing, effects and
mix can be checked anywhere, with no GPU and no model download.

Work goes to out/film/<episode id>/voice/. `film.py animatic` and `film.py assemble` pick the WAVs up from
there by line id, take each line's real length for the timeline, and mix them. See docs/FILM_PIPELINE.md.
"""
import argparse
import fnmatch
import hashlib
import json
import math
import random
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORDS_PER_SEC = {"narration": 2.4, "adr": 2.9}

# What the designed voice reads to make its reference clip, per language.
REF_TEXT = {
    "English": "The tide was low that morning, and the light came up slowly over the grey water.",
    "German": "Das Wetter war schlecht in dieser Nacht, und der Wind kam kalt vom Meer herein.",
    "French": "Le vent soufflait fort ce matin-là, et la mer était grise jusqu'à l'horizon.",
}

# Effects for diegetic lines (ffmpeg audio filters). Radio and megaphone are built in fx_chain().
FX = {
    "dry": "",
    # shouted in the open: a little slap back off sand and steel
    "field": "highpass=f=110,acompressor=threshold=0.25:ratio=3:attack=5:release=120,"
             "aecho=0.85:0.55:38|85:0.16|0.09",
    # across the beach: thin, smeared, quieter
    "distant": "highpass=f=260,lowpass=f=2600,aecho=0.8:0.7:110|240:0.32|0.2,volume=0.8",
    # heard through ringing ears or a helmet
    "muffled": "lowpass=f=650,lowpass=f=650,aecho=0.8:0.8:60|140:0.4|0.28",
}
FX_NAMES = (*FX, "radio", "megaphone")


# --- lines ----------------------------------------------------------------------------------------------
def _src(item):
    return item.get("shot") or item.get("card")


def _hash(*parts):
    return hashlib.sha1(json.dumps(parts, sort_keys=True, ensure_ascii=False).encode()).hexdigest()


def expand_adr(src):
    """A segment's ADR, walla expanded: one dict per clip, `at` in seconds into the segment."""
    out = []
    sid = src["id"]
    for k, a in enumerate(src.get("adr", [])):
        base = {"fx": a.get("fx", "field"), "gain": float(a.get("gain", -6.0)), "language": a.get("language"),
                "delivery": a.get("delivery", ""), "take": int(a.get("take", 1))}
        if a.get("walla"):
            # a crowd of short lines spread through a window, each voice in its own place in the stereo field
            rng = random.Random(f"{sid}:{k}")
            voices, texts = list(a["voices"]), list(a["lines"])
            at0, span = float(a.get("at", 0.0)), float(a.get("dur", 6.0))
            for j, text in enumerate(texts):
                out.append({**base, "id": f"{sid}_walla{k + 1:02d}_{j + 1:02d}", "kind": "adr", "walla": True,
                            "voice": voices[j % len(voices)], "text": text,
                            "at": round(at0 + span * (j + rng.uniform(0.0, 0.8)) / len(texts), 2),
                            "pan": round(rng.uniform(-0.7, 0.7), 2),
                            "gain": base["gain"] + round(rng.uniform(-3.0, 1.0), 1), "subtitle": None})
            continue
        if "voice" not in a or "text" not in a:
            raise ValueError(f"{sid}: adr {k + 1} needs \"voice\" and \"text\" (or \"walla\")")
        out.append({**base, "id": f"{sid}_adr{k + 1:02d}", "kind": "adr", "voice": a["voice"], "text": a["text"],
                    "at": float(a.get("at", 0.0)), "pan": float(a.get("pan", 0.0)), "subtitle": a.get("subtitle")})
    return out


def episode_lines(ep):
    """Every line to render, in film order: narration (named like narration-script.md) then each shot's ADR."""
    default = ep.get("narrator", "narrator")
    out = []
    for item in ep["sequence"]:
        src = _src(item)
        if src is None:
            continue
        for k, n in enumerate(src.get("narration", [])):
            if n.get("audio"):
                continue  # recorded by hand
            out.append({"id": f"{src['id']}_{k + 1:02d}", "kind": "narration", "voice": n.get("voice", default),
                        "text": n["text"], "delivery": n.get("delivery", ""), "fx": n.get("fx", "dry"),
                        "language": n.get("language"), "take": int(n.get("take", 1))})
        out += expand_adr(src)
    voices = ep.get("voices", {})
    for ln in out:
        if ln["voice"] not in voices:
            raise ValueError(f"{ln['id']}: voice {ln['voice']!r} is not in the episode's \"voices\"")
        if ln["fx"] not in FX_NAMES:
            raise ValueError(f"{ln['id']}: fx {ln['fx']!r}: one of {', '.join(FX_NAMES)}")
        v = voices[ln["voice"]]
        ln["language"] = ln["language"] or v.get("language", "English")
        ln["mode"] = v.get("mode") or ("clone" if ln["kind"] == "narration" else "design")
        if ln["mode"] not in ("clone", "design"):
            raise ValueError(f"voice {ln['voice']!r}: mode is \"clone\" or \"design\"")
    return out


def instruct(voice, line):
    """The VoiceDesign prompt: who is speaking, then how this line is said."""
    parts = [voice["design"].strip().rstrip(".") + "."]
    if line.get("delivery"):
        parts.append(line["delivery"].strip().rstrip(".") + ".")
    elif voice.get("delivery"):
        parts.append(voice["delivery"].strip().rstrip(".") + ".")
    return " ".join(parts)


def line_key(ep, line, backend):
    v = ep["voices"][line["voice"]]
    return _hash(backend, line["mode"], v, line["text"], line["delivery"], line["language"], line["take"])


def estimate(line):
    return max(0.8, len(line["text"].split()) / WORDS_PER_SEC[line["kind"]] + 0.3)


# --- wav io ---------------------------------------------------------------------------------------------
def wav_duration(path):
    with wave.open(str(path), "rb") as w:
        return w.getnframes() / float(w.getframerate())


def read_wav(path):
    """(float samples, rate) from a 16-bit mono WAV, as the model's clone prompt takes it."""
    import numpy as np
    with wave.open(str(path), "rb") as w:
        sr, data = w.getframerate(), w.readframes(w.getnframes())
    return np.frombuffer(data, dtype="<i2").astype(np.float32) / 32768.0, sr


def write_wav(path, samples, sr):
    import numpy as np
    a = np.asarray(samples, dtype=np.float32).reshape(-1)
    peak = float(np.max(np.abs(a))) if a.size else 0.0
    if peak > 1.0:
        a = a / peak
    pcm = (a * 32767.0).astype("<i2")
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(int(sr))
        w.writeframes(pcm.tobytes())


# --- backends -------------------------------------------------------------------------------------------
class Placeholder:
    """Speech-shaped noise: syllable bursts at the line's expected pace. For timing and mix checks only."""
    name = "placeholder"
    sr = 24000

    def _speech(self, text, kind, seed):
        import numpy as np
        rng = np.random.default_rng(seed)
        dur = estimate({"text": text, "kind": kind})
        n = int(dur * self.sr)
        t = np.arange(n) / self.sr
        syl = max(1, int(len(text) / 3.2))
        env = np.zeros(n)
        for c in np.sort(rng.uniform(0.1, dur - 0.15, syl)):
            env += np.exp(-0.5 * ((t - c) / 0.045) ** 2)
        env = np.clip(env, 0, 1)
        f0 = 110 + 40 * rng.random()
        voiced = sum(np.sin(2 * np.pi * f0 * h * t) / h for h in range(1, 8))
        noise = rng.standard_normal(n) * 0.25
        return 0.3 * env * (voiced * 0.5 + noise), self.sr

    def design(self, text, language, instruct_text, seed, kind="adr"):
        return self._speech(text, kind, seed)

    def clone(self, voice_name, ref, text, language, seed, kind="narration"):
        return self._speech(text, kind, seed)


class Qwen3:
    """Qwen3-TTS through the `qwen-tts` package: VoiceDesign for designing voices, Base for reading in one."""
    name = "qwen3"
    DESIGN = "Qwen/Qwen3-TTS-12Hz-1.7B-VoiceDesign"
    BASE = "Qwen/Qwen3-TTS-12Hz-1.7B-Base"

    def __init__(self, device="cuda:0", attn=None):
        try:
            import torch
            from qwen_tts import Qwen3TTSModel
        except ImportError as e:
            raise SystemExit(f"qwen3 backend needs `pip install -U qwen-tts` (and a CUDA build of torch): {e}")
        self.torch, self.cls, self.device = torch, Qwen3TTSModel, device
        self.attn = attn
        self._design = self._base = None
        self._prompts = {}

    def _load(self, repo):
        kw = {"device_map": self.device, "dtype": self.torch.bfloat16}
        if self.attn:
            kw["attn_implementation"] = self.attn
        print(f"loading {repo} on {self.device}", file=sys.stderr)
        return self.cls.from_pretrained(repo, **kw)

    def _seed(self, seed):
        self.torch.manual_seed(seed)
        if self.torch.cuda.is_available():
            self.torch.cuda.manual_seed_all(seed)

    def design(self, text, language, instruct_text, seed, kind="adr"):
        if self._design is None:
            self._design = self._load(self.DESIGN)
        self._seed(seed)
        wavs, sr = self._design.generate_voice_design(text=text, language=language, instruct=instruct_text,
                                                      max_new_tokens=2048)
        return wavs[0], sr

    def clone(self, voice_name, ref, text, language, seed, kind="narration"):
        if self._base is None:
            self._base = self._load(self.BASE)
        if voice_name not in self._prompts:
            self._prompts[voice_name] = self._base.create_voice_clone_prompt(ref_audio=read_wav(ref["wav"]),
                                                                             ref_text=ref["text"])
        self._seed(seed)
        wavs, sr = self._base.generate_voice_clone(text=text, language=language,
                                                   voice_clone_prompt=self._prompts[voice_name],
                                                   max_new_tokens=2048)
        return wavs[0], sr


BACKENDS = {"qwen3": Qwen3, "placeholder": Placeholder}


# --- effects and levels ---------------------------------------------------------------------------------
TRIM = ("silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.05,"
        "areverse,silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.12,areverse")


def fx_chain(fx, dur):
    """filter_complex from [0:a] to [out]: trim, effect, level to -20 LUFS, 48 kHz mono."""
    head = f"[0:a]aresample=48000,{TRIM}"
    level = "loudnorm=I=-20:TP=-2:LRA=11,aresample=48000,aformat=sample_fmts=s16:channel_layouts=mono[out]"
    if fx == "radio":
        # a handheld set of the period: narrow band, overdriven, hiss under it, a squelch burst at each end
        d = dur + 0.5
        return (f"{head},adelay=200,apad=pad_dur=0.3,highpass=f=320,highpass=f=320,lowpass=f=2700,lowpass=f=2700,"
                f"acompressor=threshold=0.08:ratio=8:attack=2:release=80,volume=3,asoftclip=type=atan,"
                f"atrim=0:{d:.2f}[v];"
                f"anoisesrc=r=48000:c=pink:a=1:d={d:.2f}:s=7,highpass=f=600,lowpass=f=3400,"
                f"volume='if(lt(t,0.16)+gt(t,{d - 0.18:.2f}),0.22,0.025)':eval=frame[n];"
                f"[v][n]amix=inputs=2:normalize=0:duration=first,{level}")
    if fx == "megaphone":
        return (f"{head},highpass=f=480,lowpass=f=3200,acompressor=threshold=0.1:ratio=10,volume=4,"
                f"asoftclip=type=hard,aecho=0.8:0.4:30:0.2,{level}")
    return f"{head}{',' + FX[fx] if FX[fx] else ''},{level}"


def process(raw, out, fx):
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(raw), "-filter_complex", fx_chain(fx, wav_duration(raw)),
                    "-map", "[out]", str(out)], check=True)


# --- rendering ------------------------------------------------------------------------------------------
def cast_ref(ep, name, backend, vdir, force=False):
    """The designed reference clip for a voice: what clone-mode lines read from. Re-made only when it changes."""
    v = ep["voices"][name]
    lang = v.get("language", "English")
    text = v.get("ref_text") or REF_TEXT.get(lang, REF_TEXT["English"])
    key = _hash(backend.name, v, text)
    wav, meta = vdir / "cast" / f"{name}.wav", vdir / "cast" / f"{name}.json"
    if not force and wav.exists() and meta.exists() and json.loads(meta.read_text()).get("key") == key:
        return {"wav": wav, "text": text}
    audio, sr = backend.design(text, lang, instruct(v, {}), int(key[:8], 16), kind="narration")
    write_wav(wav, audio, sr)
    meta.write_text(json.dumps({"key": key, "voice": name, "text": text, "instruct": instruct(v, {})}, indent=2) + "\n")
    print(f"cast {name}: {wav_duration(wav):.1f} s -> {wav}")
    return {"wav": wav, "text": text}


def render(ep, vdir, backend, only=None, force=False):
    lines = episode_lines(ep)
    if only:
        lines = [ln for ln in lines if any(fnmatch.fnmatch(ln["id"], p) for p in only)]
    manifest_path = vdir / "manifest.json"
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    refs, done = {}, 0
    for ln in lines:
        v = ep["voices"][ln["voice"]]
        key = line_key(ep, ln, backend.name)
        raw, out = vdir / "raw" / f"{ln['id']}.wav", vdir / f"{ln['id']}.wav"
        entry = manifest.get(ln["id"], {})
        if force or entry.get("key") != key or not raw.exists():
            # one seed per voice and take, not per line: a designed voice keeps more of its timbre between lines
            seed = int(_hash(v, ln["take"])[:8], 16)
            if ln["mode"] == "clone":
                if ln["voice"] not in refs:
                    refs[ln["voice"]] = cast_ref(ep, ln["voice"], backend, vdir)
                audio, sr = backend.clone(ln["voice"], refs[ln["voice"]], ln["text"], ln["language"], seed,
                                          kind=ln["kind"])
            else:
                audio, sr = backend.design(ln["text"], ln["language"], instruct(v, ln), seed, kind=ln["kind"])
            write_wav(raw, audio, sr)
            entry = {"key": key}
            done += 1
        if force or entry.get("fx") != ln["fx"] or not out.exists() or out.stat().st_mtime < raw.stat().st_mtime:
            process(raw, out, ln["fx"])
            entry["fx"] = ln["fx"]
        entry.update({"voice": ln["voice"], "text": ln["text"], "dur": round(wav_duration(out), 2),
                      "backend": backend.name})
        manifest[ln["id"]] = entry
        print(f"{ln['id']:<24} {entry['dur']:5.1f} s  {ln['voice']:<10} {ln['fx']:<9} {ln['text'][:60]}")
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
    print(f"{len(lines)} line(s), {done} rendered, {len(lines) - done} cached -> {vdir}")
    return manifest


def make_backend(name, device="cuda:0", attn=None):
    if name == "qwen3":
        return Qwen3(device, attn)
    return BACKENDS[name]()


# --- CLI ------------------------------------------------------------------------------------------------
def main(argv=None):
    sys.path.insert(0, str(ROOT))
    from tools.film import film

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=("lines", "cast", "render", "mix"))
    ap.add_argument("episode")
    ap.add_argument("--out", help="work directory (default out/film/<episode id>)")
    ap.add_argument("--backend", choices=sorted(BACKENDS), default="qwen3")
    ap.add_argument("--device", default="cuda:0")
    ap.add_argument("--attn", help="attention implementation, e.g. flash_attention_2 if it is installed")
    ap.add_argument("--only", action="append", help="line ids to render (glob, repeatable), e.g. 's04_*'")
    ap.add_argument("--force", action="store_true", help="render again even when cached")
    ap.add_argument("--output", help="mix: output WAV")
    args = ap.parse_args(argv)

    ep = film.load_episode(args.episode)
    out = Path(args.out) if args.out else ROOT / "out" / "film" / ep["id"]
    vdir = out / "voice"
    if args.command == "lines":
        for ln in episode_lines(ep):
            v = ep["voices"][ln["voice"]]
            print(f"{ln['id']:<24} {ln['kind']:<9} {ln['voice']:<10} {ln['mode']:<6} {ln['fx']:<9} {ln['text']}")
            if ln["mode"] == "design":
                print(f"{'':<24} ↳ {instruct(v, ln)}")
        return 0
    if args.command in ("cast", "render"):
        backend = make_backend(args.backend, args.device, args.attn)
        if args.command == "cast":
            for name in ep.get("voices", {}):
                if not args.only or any(fnmatch.fnmatch(name, p) for p in args.only):
                    cast_ref(ep, name, backend, vdir, force=args.force)
            return 0
        render(ep, vdir, backend, args.only, args.force)
        return 0
    # mix: the dialogue on the film's clock, with the music bed if the episode has one
    timeline = film.build_timeline(ep, voice_dir=vdir)
    film.write_plan(ep, timeline, out)
    work = out / "work"
    work.mkdir(parents=True, exist_ok=True)
    mixed = film.build_audio(ep, timeline, out, work)
    dest = Path(args.output) if args.output else out / f"{ep['id']}-dx.wav"
    dest.write_bytes(mixed.read_bytes())
    print(f"mix: {timeline['duration']:.1f} s -> {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
