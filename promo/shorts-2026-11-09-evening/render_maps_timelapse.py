#!/usr/bin/env python3
"""Render Day of Defeat vertical Shorts from multi-map spawn-atlas stills
and the dod_donner tweak series.

Local review only — does not upload, schedule, publish, or delete anything.
Static stills only (no Ken Burns / zoom / pan / punch-in).
"""
from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo

from PIL import Image, ImageDraw, ImageFont
import numpy as np

FFMPEG = "ffmpeg"
FFPROBE = "ffprobe"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
OUT_DIR = Path("/home/tim/Desktop/dod-gc/promo/shorts-2026-11-09-evening")
WORK = Path("/tmp/dod-maps-timelapse-build")
ATLAS = Path("/home/tim/Desktop/dod-gc/playtests/map-spawn-atlas/screenshots")
SCREENSHOTS = Path("/home/tim/Desktop/dod-gc/playtests/screenshots")
MUSIC_DIR = Path("/home/tim/Desktop/dod-gc/social-media/music")
END_COLOR = "0x2d3927"
END_DUR = 3.0
STATUS = "Dev still, Dolphin framedump - not hardware"
FOOTER1 = "Dev builds, Dolphin capture, not finished"
FOOTER2 = "Unfinished map, not a finished game"

# Exact 30 fps holds: 16 frames = 0.5333s (inside 0.35-0.55), last = 2.0s
HOLD = 16 / 30
LAST = 60 / 30  # 2.0s

# -----------------------------------------------------------------------------
# Short 04 — rapid spawn-atlas tour of the 5 maps that actually loaded
# -----------------------------------------------------------------------------
TITLE_04 = "Day of Defeat on GameCube - spawn atlas"
HOOK_04 = "Five maps that loaded in the spawn pass"

def atlas_still(map_id: str, side: str, duration: float, notes: str) -> dict:
    src = ATLAS / map_id / f"{side}.png"
    label = f"2026-11-08 - {map_id} {side} spawn"
    return {
        "file": src,
        "folder": f"atlas-{map_id}-{side}",
        "mtime_label": "2026-11-08 10:28-11:00 PM PT",
        "duration_s": duration,
        "label": label,
        "notes": notes,
        "map": map_id,
        "side": side,
    }

STILLS_04 = [
    # Show order: strongest open (Avalanche), then Flash / Donner, Anzio (darkest) mid-pack, Kraftstoff close.
    atlas_still("dod_avalanche", "allies", HOLD,
                "Most complete plate in this atlas pass: stone/wood town textures, some flat tan faces and a dark doorway. Opened here so the first frame is not the dark Anzio plate."),
    atlas_still("dod_avalanche", "axis", HOLD,
                "Textured town from the axis spawn, missing faces. Distinct from allies (85.4% pixels differ >8)."),
    atlas_still("dod_flash", "allies", HOLD,
                "Autumn foliage and gold light, plus large olive-brown missing-texture rectangles. Included as part of the multi-map tour, not a Flash-only recut."),
    atlas_still("dod_flash", "axis", HOLD,
                "Darker axis courtyard/alley. Distinct from allies (85.0% pixels differ >8)."),
    atlas_still("dod_donner", "allies", HOLD,
                "Evening allies spawn: sparse tree clusters and fragments floating in a cyan/blue void. Later and much sparser than the midday allies alley used in Short 05."),
    atlas_still("dod_donner", "axis", HOLD,
                "Evening axis courtyard with gold lighting, a black missing face, and a cyan void floor. Byte-identical to 20261108-222604-tipfix-donner-cf."),
    atlas_still("dod_anzio", "allies", HOLD,
                "Untextured / under-lit brick and large planes. Darker plate (YAVG 30.9) but readable geometry. No OSD. Placed mid-pack so it is not the opening or closing frame."),
    atlas_still("dod_anzio", "axis", HOLD,
                "Sparser untextured spawn. Large unlit/black plane on the left and a cyan void. Distinct from allies (74.8% pixels differ >8)."),
    atlas_still("dod_kraftstoff", "allies", HOLD,
                "Scattered industrial buildings with missing faces and a cyan void. A dark ground bar sits along the bottom edge."),
    atlas_still("dod_kraftstoff", "axis", LAST,
                "Axis spawn of the same unfinished industrial scatter. Distinct from allies (60.1% pixels differ >8). Last game frame, 2.0s hold."),
]

MUSIC_04 = MUSIC_DIR / "Allied Resolve.mp3"
MUSIC_04_IN = 3.0  # intro is quiet at 0s; established groove by ~3s
OUT_04 = OUT_DIR / "04-maps-atlas-tour-v1.mp4"
MANIFEST_04 = OUT_DIR / "04-maps-atlas-tour-v1-manifest.json"
REVIEW_04 = OUT_DIR / "review" / "04-maps-atlas-tour-v1"

# -----------------------------------------------------------------------------
# Short 05 — Donner chronological stills with labeled viewpoint changes
# -----------------------------------------------------------------------------
TITLE_05 = "Day of Defeat on GameCube - dod_donner tweaks"
HOOK_05 = "Tweak passes, then later spawn views"

def shot(folder: str, mtime_label: str, duration: float, label: str, notes: str) -> dict:
    return {
        "file": SCREENSHOTS / folder / "final.png",
        "folder": folder,
        "mtime_label": mtime_label,
        "duration_s": duration,
        "label": label,
        "notes": notes,
    }

STILLS_05 = [
    shot(
        "20261108-130544-atlas-dod_donner-allies",
        "2026-11-08 1:05 PM PT",
        HOLD,
        "2026-11-08 - dod_donner midday allies spawn",
        "Camera A. Clean cobblestone alley with wooden walls and a blue sky. Strongest single Donner still. Different viewpoint from the later tweak and evening plates.",
    ),
    shot(
        "20261108-135622-tip-donner-fix12",
        "2026-11-08 1:56 PM PT",
        HOLD,
        "2026-11-08 - dod_donner tweak pass",
        "Camera B start. Wooden post, diamond grate, tiled roof on the left, ruined wall to the right, cyan void where the ground should be. Viewpoint change from the midday allies alley.",
    ),
    shot(
        "20261108-140624-tip-donner-fix15",
        "2026-11-08 2:06 PM PT",
        HOLD,
        "2026-11-08 - dod_donner tweak pass",
        "Camera B, 10 minutes later. Same composition (mean_abs 8.11, 17.0% pixels >8 vs fix12). Stone steps appear; the tiled roof is gone. Mixed tweak, not a clean fill-in.",
    ),
    shot(
        "20261108-143659-tip-donner-strarena",
        "2026-11-08 2:36 PM PT",
        HOLD,
        "2026-11-08 - dod_donner tweak pass",
        "Camera B, 30 minutes later. Same composition (49.0% pixels >8 vs fix15). Tiled roof returns; left wall is stone. Mixed tweak, not monotonic improvement.",
    ),
    shot(
        "20261108-223603-atlas-dod_donner-allies",
        "2026-11-08 10:36 PM PT",
        HOLD,
        "2026-11-08 - dod_donner later allies spawn",
        "Camera C. Evening atlas allies spawn: sparse tree clusters in a cyan/blue void. Not the same camera as midday allies, and much sparser — not a successful fill-in.",
    ),
    shot(
        "20261108-223646-atlas-dod_donner-axis",
        "2026-11-08 10:37 PM PT",
        LAST,
        "2026-11-08 - dod_donner later axis spawn",
        "Camera D. Evening atlas axis courtyard with gold lighting, a black missing face, and a cyan void floor. Byte-identical to tipfix-donner-cf. Last game frame, 2.0s hold.",
    ),
]

MUSIC_05 = MUSIC_DIR / "Mournful Horn Theme.mp3"
MUSIC_05_IN = 8.0  # theme is fuller here than the opening horn phrase
OUT_05 = OUT_DIR / "05-donner-tweaks-v1.mp4"
MANIFEST_05 = OUT_DIR / "05-donner-tweaks-v1-manifest.json"
REVIEW_05 = OUT_DIR / "review" / "05-donner-tweaks-v1"


def run(cmd, **kw):
    printable = " ".join(str(c) for c in cmd[:12])
    print("+", printable, "..." if len(cmd) > 12 else "")
    subprocess.run(cmd, check=True, **kw)


def esc_text(s: str) -> str:
    return (
        s.replace("\\", "\\\\")
        .replace(":", r"\:")
        .replace("'", r"\'")
        .replace(",", r"\,")
        .replace("%", "%%")
    )


def overlay_filter(title: str, hook: str, line: str) -> str:
    common = (
        f"fontfile={FONT}:fontcolor=white:fontsize={{size}}:x=(w-text_w)/2:"
        f"shadowcolor=black@0.85:shadowx=2:shadowy=2:text='{{text}}'"
    )
    parts = [
        f"drawtext={common.format(size=36, text=esc_text(title))}:y=40",
        f"drawtext={common.format(size=36, text=esc_text(hook)).replace('fontcolor=white', 'fontcolor=yellow')}:y=92",
        f"drawtext={common.format(size=32, text=esc_text(line))}:y=150",
        f"drawtext={common.format(size=32, text=esc_text(STATUS))}:y=196",
        f"drawtext={common.format(size=32, text=esc_text(FOOTER1))}:y=1820",
        f"drawtext={common.format(size=32, text=esc_text(FOOTER2))}:y=1870",
    ]
    return ",".join(parts)


def vf_vertical(title: str, hook: str, line: str) -> str:
    # 4:3 game 1080x810 centered at y=555 over darkened blur fill. No zoom/pan.
    return (
        "[0:v]split=2[bg][fg];"
        "[bg]scale=1080:1920:force_original_aspect_ratio=increase:flags=bicubic,"
        "crop=1080:1920,boxblur=20:2,eq=brightness=-0.35:saturation=0.55,setsar=1[bg];"
        "[fg]scale=1080:810:flags=bicubic,setsar=1[fg];"
        "[bg][fg]overlay=(W-w)/2:555,fps=30,format=yuv420p,"
        + overlay_filter(title, hook, line)
    )


def render_still(src: Path, dur: float, out: Path, title: str, hook: str, line: str) -> float:
    out.parent.mkdir(parents=True, exist_ok=True)
    run([
        FFMPEG, "-y", "-hide_banner", "-loglevel", "error",
        "-loop", "1", "-framerate", "30", "-i", str(src),
        "-an",
        "-vf", vf_vertical(title, hook, line),
        "-r", "30",
        "-t", f"{dur:.4f}",
        "-c:v", "libx264", "-preset", "medium", "-crf", "18",
        "-pix_fmt", "yuv420p", "-profile:v", "high", "-level", "4.0",
        "-video_track_timescale", "15360",
        str(out),
    ])
    if out.stat().st_size < 10000:
        raise RuntimeError(f"empty still slice: {out} ({out.stat().st_size} bytes)")
    return dur


def render_endcard(out: Path) -> float:
    vf = (
        f"drawtext=fontfile={FONT}:fontcolor=white:fontsize=48:"
        f"x=(w-text_w)/2:y=890:shadowcolor=black@0.85:shadowx=2:shadowy=2:"
        f"text='{esc_text('Project updates:')}',"
        f"drawtext=fontfile={FONT}:fontcolor=white:fontsize=52:"
        f"x=(w-text_w)/2:y=968:shadowcolor=black@0.85:shadowx=2:shadowy=2:"
        f"text='{esc_text('gamecubehalf.life')}'"
    )
    run([
        FFMPEG, "-y", "-hide_banner", "-loglevel", "error",
        "-f", "lavfi", "-i", f"color=c={END_COLOR}:s=1080x1920:d={END_DUR}:r=30",
        "-an", "-vf", vf,
        "-c:v", "libx264", "-preset", "medium", "-crf", "18",
        "-pix_fmt", "yuv420p", "-profile:v", "high", "-level", "4.0",
        "-video_track_timescale", "15360",
        str(out),
    ])
    return END_DUR


def concat_and_music(slices: list[Path], music: Path, out: Path, music_start: float) -> float:
    lst = WORK / (out.stem + "_concat.txt")
    with lst.open("w") as f:
        for p in slices:
            f.write(f"file '{p}'\n")
    silent = WORK / (out.stem + "_silent.mp4")
    run([
        FFMPEG, "-y", "-hide_banner", "-loglevel", "error",
        "-f", "concat", "-safe", "0", "-i", str(lst),
        "-c:v", "libx264", "-preset", "medium", "-crf", "18",
        "-pix_fmt", "yuv420p", "-r", "30",
        "-an", str(silent),
    ])
    probe = subprocess.check_output([
        FFPROBE, "-v", "error", "-show_entries", "format=duration",
        "-of", "default=nk=1:nw=1", str(silent),
    ], text=True).strip()
    dur = float(probe)
    fade_out_st = max(0.0, dur - 1.2)
    run([
        FFMPEG, "-y", "-hide_banner", "-loglevel", "error",
        "-i", str(silent),
        "-ss", f"{music_start:.3f}", "-i", str(music),
        "-filter_complex",
        f"[1:a]atrim=duration={dur:.4f},asetpts=PTS-STARTPTS,"
        f"volume=0.18,afade=t=in:st=0:d=0.4,afade=t=out:st={fade_out_st:.4f}:d=1.2,"
        f"aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo[a]",
        "-map", "0:v:0", "-map", "[a]",
        "-c:v", "libx264", "-preset", "medium", "-crf", "18",
        "-pix_fmt", "yuv420p", "-profile:v", "high", "-level", "4.0",
        "-c:a", "aac", "-b:a", "192k", "-ar", "48000", "-ac", "2",
        "-movflags", "+faststart",
        "-t", f"{dur:.4f}",
        str(out),
    ])
    return dur


def sha256_16(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()[:16]


def ffprobe_json(path: Path) -> dict:
    raw = subprocess.check_output([
        FFPROBE, "-v", "error", "-show_format", "-show_streams",
        "-of", "json", str(path),
    ], text=True)
    return json.loads(raw)


def blackdetect(path: Path) -> str:
    proc = subprocess.run(
        [
            FFMPEG, "-hide_banner", "-i", str(path),
            "-vf", "blackdetect=d=0.3:pix_th=0.10",
            "-an", "-f", "null", "-",
        ],
        check=True, capture_output=True, text=True,
    )
    hits = [ln for ln in (proc.stderr or "").splitlines() if "black_start" in ln or "black_end" in ln]
    return "no black_start/black_end" if not hits else " | ".join(hits)


def yavg(path: Path, crop=None) -> float:
    im = Image.open(path).convert("RGB")
    if crop:
        im = im.crop(crop)
    arr = np.asarray(im)
    y = 0.299 * arr[:, :, 0] + 0.587 * arr[:, :, 1] + 0.114 * arr[:, :, 2]
    return float(y.mean())


def extract_frame(src: Path, t: float, dest: Path):
    dest.parent.mkdir(parents=True, exist_ok=True)
    run([
        FFMPEG, "-y", "-hide_banner", "-loglevel", "error",
        "-ss", f"{t:.3f}", "-i", str(src), "-frames:v", "1", str(dest),
    ])


def contact_sheet(frames: list[tuple[Path, str]], dest: Path, cols: int = 4):
    imgs = [(Image.open(p).convert("RGB"), lab) for p, lab in frames]
    tw = 270
    thumbs = []
    for im, lab in imgs:
        th = int(im.height * tw / im.width)
        thumbs.append((im.resize((tw, th), Image.Resampling.LANCZOS), lab))
    pad, label_h, gap = 12, 32, 10
    rows = (len(thumbs) + cols - 1) // cols
    w = tw * cols + pad * 2 + gap * (cols - 1)
    h = rows * (thumbs[0][0].height + label_h + gap) + pad * 2
    sheet = Image.new("RGB", (w, h), (16, 16, 16))
    dr = ImageDraw.Draw(sheet)
    font = ImageFont.truetype(FONT, 16)
    for i, (th, lab) in enumerate(thumbs):
        r, c = divmod(i, cols)
        x = pad + c * (tw + gap)
        y = pad + r * (th.height + label_h + gap)
        dr.text((x + 4, y + 4), lab, fill=(255, 255, 200), font=font)
        sheet.paste(th, (x, y + label_h))
    dest.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(dest, quality=92)
    print("wrote contact sheet", dest)


def still_hold_check(a: Path, b: Path) -> str:
    A = np.asarray(Image.open(a).convert("RGB")).astype(int)
    B = np.asarray(Image.open(b).convert("RGB")).astype(int)
    d = np.abs(A - B)
    return (
        f"{a.name} vs {b.name}: mean_abs={d.mean():.2f} max={d.max()} "
        f"pct>8={(d.max(2)>8).mean()*100:.3f}% — composition unchanged if this is typical H.264 variance"
    )


def render_short(
    stills: list[dict],
    title: str,
    hook: str,
    music: Path,
    music_in: float,
    music_name: str,
    out_mp4: Path,
    manifest_path: Path,
    review_dir: Path,
    visual_notes: list[str],
    dropped: list[dict],
    inventory_notes: dict,
) -> dict:
    review_dir.mkdir(parents=True, exist_ok=True)
    work = WORK / out_mp4.stem
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True, exist_ok=True)

    slices: list[Path] = []
    t = 0.0
    starts: list[float] = []
    for i, s in enumerate(stills, 1):
        if not s["file"].exists():
            sys.exit(f"still missing: {s['file']}")
        out = work / f"s{i:02d}_{s['folder']}.mp4"
        render_still(s["file"], s["duration_s"], out, title, hook, s["label"])
        slices.append(out)
        starts.append(t)
        t += s["duration_s"]
    game_end = t
    end = work / "endcard.mp4"
    render_endcard(end)
    slices.append(end)

    dur = concat_and_music(slices, music, out_mp4, music_in)
    digest = sha256_16(out_mp4)
    probe = ffprobe_json(out_mp4)
    black = blackdetect(out_mp4)

    # One mid-hold review frame per still + end card
    frames = []
    frame_meta = []
    game_y = {}
    for i, (s, st) in enumerate(zip(stills, starts), 1):
        mid = st + min(0.25, s["duration_s"] * 0.4)
        dest = review_dir / f"{i:02d}-{s['folder']}-t{mid:.2f}.png"
        extract_frame(out_mp4, mid, dest)
        y_full = yavg(dest)
        y_game = yavg(dest, crop=(0, 555, 1080, 555 + 810))
        lab = f"{i} {s['folder']} t{mid:.2f}"
        frames.append((dest, f"{i} t{mid:.2f}"))
        frame_meta.append({"file": str(dest), "t": mid, "label": lab, "yavg_fullframe": y_full})
        game_y[dest.name] = y_game
        print(f"review {dest.name} t={mid:.2f} YAVG={y_full:.2f} game={y_game:.2f}")

    # last-game-frame late hold to confirm no Ken Burns
    last_start = starts[-1]
    late = last_start + stills[-1]["duration_s"] - 0.20
    late_dest = review_dir / f"{len(stills):02d}-hold-late-t{late:.2f}.png"
    extract_frame(out_mp4, late, late_dest)
    y_full = yavg(late_dest)
    y_game = yavg(late_dest, crop=(0, 555, 1080, 555 + 810))
    frames.append((late_dest, f"last hold t{late:.2f}"))
    frame_meta.append({"file": str(late_dest), "t": late, "label": f"last hold t{late:.2f}", "yavg_fullframe": y_full})
    game_y[late_dest.name] = y_game

    end_t = game_end + 1.20
    end_dest = review_dir / f"endcard-t{end_t:.2f}.png"
    extract_frame(out_mp4, end_t, end_dest)
    y_full = yavg(end_dest)
    y_game = yavg(end_dest, crop=(0, 555, 1080, 555 + 810))
    frames.append((end_dest, f"end t{end_t:.2f}"))
    frame_meta.append({"file": str(end_dest), "t": end_t, "label": f"end t{end_t:.2f}", "yavg_fullframe": y_full})
    game_y[end_dest.name] = y_game
    print(f"review {end_dest.name} t={end_t:.2f} YAVG={y_full:.2f}")

    contact = review_dir / "contact-sheet.jpg"
    contact_sheet(frames, contact, cols=4)

    mid_last = last_start + min(0.25, stills[-1]["duration_s"] * 0.4)
    # find the mid-hold frame of the last still
    last_mid_frame = [p for p, _ in frames if stills[-1]["folder"] in p.name][0]
    hold_note = still_hold_check(last_mid_frame, late_dest)

    now = datetime.now(ZoneInfo("America/Los_Angeles"))
    vstream = next(s for s in probe.get("streams", []) if s.get("codec_type") == "video")
    astream = next(s for s in probe.get("streams", []) if s.get("codec_type") == "audio")
    manifest = {
        "created": now.isoformat(),
        "created_label": now.strftime("%Y-%m-%d %-I:%M %p %Z").replace("PDT", "PT").replace("PST", "PT"),
        "folder": str(OUT_DIR),
        "spec": "1080x1920 30fps H.264+AAC; 4:3 game centered at 1080x810 (y=555) over darkened blur fill; olive end card 3.0s; static stills only (no Ken Burns/zoom/pan); rapid holds 16/30s (~0.533s), last game frame 2.0s",
        "status": "local review only — not published, scheduled, or uploaded",
        "music_rights": "Tracks are Suno instrumentals tagged artist=gopexllc, lyrics=[Instrumental], comment='made with suno; created=2026-11-07'. No license/readme beside the files. Rights were not independently verified for commercial social posting.",
        "short": {
            "file": str(out_mp4),
            "title_overlay": title,
            "hook_overlay": hook,
            "duration_s": dur,
            "bytes": out_mp4.stat().st_size,
            "sha256_16": digest,
            "probe": {
                "streams": [
                    {
                        "codec_name": s.get("codec_name"),
                        "codec_type": s.get("codec_type"),
                        "width": s.get("width"),
                        "height": s.get("height"),
                        "r_frame_rate": s.get("r_frame_rate"),
                        "sample_rate": s.get("sample_rate"),
                        "channels": s.get("channels"),
                    }
                    for s in probe.get("streams", [])
                ],
                "format": {
                    "duration": probe.get("format", {}).get("duration"),
                    "size": probe.get("format", {}).get("size"),
                    "bit_rate": probe.get("format", {}).get("bit_rate"),
                },
            },
            "music": {
                "file": str(music),
                "track": music_name,
                "in_point_s": music_in,
                "volume": 0.18,
                "fade_in_s": 0.4,
                "fade_out_s": 1.2,
            },
            "sources": [
                {
                    "file": str(s["file"]),
                    "folder": s["folder"],
                    "mtime_label": s["mtime_label"],
                    "edit_duration_s": s["duration_s"],
                    "label": s["label"],
                    "notes": s["notes"],
                }
                for s in stills
            ],
            "endcard": {
                "duration_s": END_DUR,
                "color": END_COLOR,
                "text": ["Project updates:", "gamecubehalf.life"],
            },
        },
        "qa": {
            "blackdetect": f"ffmpeg blackdetect=d=0.3:pix_th=0.10 — {black}",
            "review_yavg": frame_meta,
            "visual": visual_notes,
            "game_region_yavg": {
                "crop": "1080x810 at y=555",
                "frames": game_y,
                "note": "All game-region YAVG should be well above the drop threshold of 16. End-card value is the olive field plus white text in the center crop.",
            },
            "still_hold_check": hold_note,
            "ffprobe": {
                "video": f"{vstream.get('codec_name')} {vstream.get('profile', '')} {vstream.get('pix_fmt', '')} {vstream.get('width')}x{vstream.get('height')} {vstream.get('r_frame_rate')} {vstream.get('nb_frames', '')} frames".strip(),
                "audio": f"{astream.get('codec_name')} {astream.get('profile', '')} {astream.get('sample_rate')} Hz {astream.get('channel_layout', '')}".strip(),
                "duration_s": dur,
                "size_bytes": out_mp4.stat().st_size,
                "bit_rate": probe.get("format", {}).get("bit_rate"),
            },
        },
        "dropped": dropped,
        "inventory_notes": inventory_notes,
        "renderer": str(OUT_DIR / "render_maps_timelapse.py"),
        "review_frames": str(review_dir),
        "contact_sheet": str(contact),
        "not_touched": [
            "No posts deleted, unpublished, updated, uploaded, or scheduled.",
            "Unused evening Shorts 01/02/03 were not recut or uploaded.",
            "Already-posted menu, skybox, and geometry-progress Shorts were not touched.",
        ],
    }
    manifest_path.write_text(json.dumps(manifest, indent=2))
    print(f"wrote {out_mp4} dur={dur:.3f}s bytes={out_mp4.stat().st_size} sha={digest}")
    print(f"wrote {manifest_path}")
    return manifest


DROPPED_04 = [
    {"map": m, "plate": "allies+axis (byte-identical)",
     "reason": "Magenta GameCube _MEM_ALLOC OUT OF MEMORY / HALTED crash dump plus AMD Radeon Video Info overlay. Allies and axis are the same file. ~14–16 KB, ~68% magenta pixels."}
    for m in [
        "dod_caen", "dod_charlie", "dod_chemille", "dod_escape", "dod_falaise",
        "dod_flugplatz", "dod_forest", "dod_glider", "dod_jagd", "dod_kalt",
        "dod_merderet", "dod_northbound", "dod_saints", "dod_sturm", "dod_switch",
        "dod_vicenza", "dod_zalec",
    ]
] + [
    {"map": "all 22 maps", "plate": "early-morning capfaces.png (~9.3–9.5 KB) except Flash and Donner",
     "reason": "~9.4 KB failures (same class as the early tipfix/smoke stills). Only Flash (462 KB) and Donner (312 KB) capfaces are real gameplay; Flash/Avalanche capfaces already appeared in unused evening Short 02, so no capfaces tour was cut."},
    {"map": "dod_charlie", "plate": "20261109-011715-charlie-lean3 and later 0122* stills",
     "reason": "Not part of the evening spawn atlas. lean3/diag/bootcf/nodl are GameCube fatal / stack-dump screens (YAVG ~28.7, 83% black), not playable map views. No standalone Charlie short."},
    {"map": "skybox stills (jagd 044529, kraftstoff 044648, etc.)",
     "reason": "Skybox stills are off-limits — already a posted skybox Short."},
]

INVENTORY_04 = {
    "atlas_maps_total": 22,
    "maps_used": ["dod_anzio", "dod_avalanche", "dod_donner", "dod_flash", "dod_kraftstoff"],
    "maps_dropped_magenta_oom": 17,
    "plate_type": "Evening Nov 8 10:28–11:00 PM PT spawn atlas, both allies and axis. Editorial show order (Avalanche, Flash, Donner, Anzio, Kraftstoff) rather than capture order, so the dark Anzio plates are not the open or the last hold.",
    "allies_axis_identical_on_used_maps": False,
    "progress_claim": "This is a spawn-state tour, not a geometry-fill-in sequence. Several plates are sparse, untextured, or have missing faces and cyan void.",
}

VISUAL_04 = [
    "Rapid 10-still tour of the only 5 maps whose evening spawn-atlas plates actually loaded. The other 17 maps halted on a magenta _MEM_ALLOC OUT OF MEMORY dump with an AMD Radeon Video Info overlay and were skipped. Editorial order is Avalanche, Flash, Donner, Anzio, Kraftstoff (not capture order) so the first frame and last hold are not the dark Anzio plates.",
    "Consistent plate type: evening Nov 8 allies then axis spawn per map. Show order is Avalanche, Flash, Donner, Anzio, Kraftstoff. Hook states 'five maps that loaded' — does not claim all 22.",
    "Static stills, no Ken Burns / zoom / pan / punch-in. Holds are 16/30s (~0.533s); last Kraftstoff axis hold is 2.0s; olive end card is 3.0s.",
    "No AMD/Mesa Video Info OSD, no magenta debug, no fatal/error/reset screens, no Half-Life missing-data menu, no Valve intro, no non-DoD menus in the used stills.",
    "Leftover / unfinished artifacts left in: Anzio is mostly untextured and dark, with a large black plane on the axis plate. Avalanche has some flat tan faces and a dark doorway. Donner evening allies is sparse floating trees in a cyan void; Donner axis has a black missing face and a cyan void floor. Flash allies has large olive-brown missing-texture rectangles. Kraftstoff has scattered floating buildings, missing faces, a cyan void, and a dark ground bar along the bottom edge.",
    "This sequence does not show geometry filling in over time. It is a current-state spawn tour of the maps that came up.",
    "Typography matches this morning's approved finals and the unused evening Shorts (DejaVu Sans Bold, four top lines, two footers, olive end card).",
    "No performance or hardware claims. Overlays state Dolphin framedump, dev/unfinished build. No 'PC screenshot'.",
    "No source game audio (stills have none). Music is the unused original WW2-sounding track Allied Resolve, not a Valve/Lost Coast soundtrack.",
]

DROPPED_05 = [
    {"folder": "early tip-donner / tipfix / spill / pad2m / planes / floor-diag (most of 03:04–12:27)",
     "reason": "Finals are ~9.4 KB failures, 176-byte black frames, YAVG below 16 (pad2m 800x600 YAVG 12.93 and two byte-identical copies), magenta MEM_ALLOC, Dolphin error dialogs ('Couldn't open port', 'An error occurred'), or red exception/console dumps. Not usable gameplay."},
    {"folder": "20261108-111236 through 121655 and 124341-130055 tip-donner (13–14 KB, YAVG ~28, uniq ~140)",
     "reason": "Red GameCube exception / stack-dump screens, not map views. Several are near-duplicates of each other (identical 16x16 aHash)."},
    {"folder": "20261108-113527-tip-donner and 113550 (byte-identical 500x112)",
     "reason": "Odd 500x112 crop with a Dolphin 'Couldn't open port' dialog. Not a map view."},
    {"folder": "20261108-122708-tip-donner (500x286)",
     "reason": "Odd-aspect Dolphin error dialog, not a map view."},
    {"folder": "20261108-130250-tip-donner, 130434-atlas-dod_donner-axis, 144001-tip-donner-eye2, 162621-tip-donner-stickeye4",
     "reason": "A stuck inside-wall camera: half the frame is a close-up wall, with a small doorway view on the right. Textures do swap (wood vs grass), but the composition is not show-ready and is a different camera from the clean tweak series. Dropped rather than jump-cut into the short."},
    {"folder": "20261108-143846-tip-donner-eye",
     "reason": "13.7 KB exception/dump class still (YAVG 28.8, uniq 140). Byte-related to failed avalanche tip stills. Not a map view."},
    {"folder": "20261108-160045-tip-donner-libogc-draw and 160453-tip-donner-markskip",
     "reason": "Hex memory-dump / GPR screens, not map views."},
    {"folder": "20261108-222604-tipfix-donner-cf",
     "reason": "Byte-identical to the evening atlas axis plate already used as the last frame. Kept the atlas path."},
    {"folder": "skybox stills",
     "reason": "Off-limits (already a posted skybox Short)."},
]

INVENTORY_05 = {
    "donner_folders_named": 80,
    "usable_gameplay_stills_after_qa": 6,
    "cameras": {
        "A": "20261108-130544 midday allies cobblestone alley",
        "B": "fix12 → fix15 → strarena, same composition (tweak pass, mixed changes)",
        "C": "evening atlas allies, sparse trees in void",
        "D": "evening atlas axis courtyard (byte-identical to tipfix-donner-cf)",
    },
    "progress_claim": "Camera B is a real same-camera tweak series but changes are mixed (stone steps appear then a tiled roof returns; not a monotonic fill-in). Evening allies is a different camera and is sparser than midday allies — not captioned as improvement. Viewpoint changes are labeled in the map line.",
}

VISUAL_05 = [
    "Six-still chronological Donner cut with four labeled viewpoints. Camera B (fix12 / fix15 / strarena) is the only same-camera tweak series. Midday allies alley and the two evening atlas spawns are clearly labeled as different views rather than jump-cut as if they were one camera.",
    "The Camera B sequence is a tweak pass, not a successful geometry fill-in: fix15 adds stone steps and loses the tiled roof (17% pixels differ >8 vs fix12); strarena brings the tiled roof back and changes the left wall to stone (49% pixels differ >8 vs fix15). Cyan void remains where the ground should be in all three.",
    "Evening allies spawn is much sparser than the midday allies alley (70% pixels differ >8, and it is a different camera). Not captioned as progress.",
    "Static stills, no Ken Burns / zoom / pan / punch-in. Holds are 16/30s (~0.533s); last evening axis hold is 2.0s; olive end card is 3.0s.",
    "No AMD/Mesa Video Info OSD, no magenta debug, no fatal/error/reset screens, no Valve intro, no non-DoD menus in the used stills.",
    "Leftover artifacts left in: cyan void floors, a black missing face on the evening axis courtyard, sparse floating trees on the evening allies plate.",
    "Typography matches this morning's approved finals and the unused evening Shorts (DejaVu Sans Bold, four top lines, two footers, olive end card).",
    "No performance or hardware claims. Overlays state Dolphin framedump, dev/unfinished build. No 'PC screenshot'.",
    "No source game audio. Music is the unused original WW2-sounding track Mournful Horn Theme (Allied Resolve is on Short 04; Procession of Determination, Brutal Impact, and Menace of War were already used on evening Shorts 01–03).",
]


def main():
    if shutil.which(FFMPEG) is None:
        sys.exit("ffmpeg not found")
    if not Path(FONT).exists():
        sys.exit(f"font missing: {FONT}")
    for p in [MUSIC_04, MUSIC_05]:
        if not p.exists():
            sys.exit(f"music missing: {p}")

    if WORK.exists():
        shutil.rmtree(WORK)
    WORK.mkdir(parents=True, exist_ok=True)
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    print("======== SHORT 04 atlas tour ========")
    render_short(
        STILLS_04, TITLE_04, HOOK_04, MUSIC_04, MUSIC_04_IN, "Allied Resolve",
        OUT_04, MANIFEST_04, REVIEW_04, VISUAL_04, DROPPED_04, INVENTORY_04,
    )
    print("======== SHORT 05 donner tweaks ========")
    render_short(
        STILLS_05, TITLE_05, HOOK_05, MUSIC_05, MUSIC_05_IN, "Mournful Horn Theme",
        OUT_05, MANIFEST_05, REVIEW_05, VISUAL_05, DROPPED_05, INVENTORY_05,
    )
    print("done — local files only, nothing uploaded or scheduled")


if __name__ == "__main__":
    main()
