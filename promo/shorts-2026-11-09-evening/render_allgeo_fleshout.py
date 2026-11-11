#!/usr/bin/env python3
"""Render one Day of Defeat vertical Short from late-evening all-geo stills.

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

FFMPEG = "ffmpeg"
FFPROBE = "ffprobe"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
OUT_DIR = Path("/home/tim/Desktop/dod-gc/promo/shorts-2026-11-09-evening")
WORK = Path("/tmp/dod-allgeo-build")
REVIEW = OUT_DIR / "review" / "03-allgeo-fleshout-v1"
END_COLOR = "0x2d3927"
END_DUR = 3.0
SCREENSHOTS = Path("/home/tim/Desktop/dod-gc/playtests/screenshots")
MUSIC = Path("/home/tim/Desktop/dod-gc/social-media/music/Menace of War.mp3")
MUSIC_IN = 2.5  # swell already present; first ~2.5s is a quieter intro
OUT_MP4 = OUT_DIR / "03-allgeo-fleshout-v1.mp4"
MANIFEST = OUT_DIR / "03-allgeo-fleshout-v1-manifest.json"

# Two distinct same-camera Caen stills. efx-hold dropped as near-duplicate
# of unused evening Short 1 Caen sunset plate.
STILLS = [
    {
        "file": SCREENSHOTS / "20261109-225552-caen-part-wep" / "final.png",
        "folder": "20261109-225552-caen-part-wep",
        "mtime_label": "2026-11-09 10:58 PM PST",
        "duration_s": 7.0,
        "label": "2026-11-09 - dod_caen part draw still",
        "notes": (
            "Later same-camera Caen part draw. Right-wall brick is more filled "
            "than the unused evening Short 1 Caen sunset plate (~22% pixels differ >8). "
            "Faint leftover translucent bar in the upper sky."
        ),
    },
    {
        "file": SCREENSHOTS / "20261109-232523-caen-allgeo" / "final.png",
        "folder": "20261109-232523-caen-allgeo",
        "mtime_label": "2026-11-09 11:26 PM PST",
        "duration_s": 9.0,  # 7.0 equal hold + ~2.0s last-frame hold
        "label": "2026-11-09 - dod_caen all-geometry still",
        "notes": (
            "All-geometry pass from the same camera. Large missing-face white holes "
            "and a solid white leftover bar in the upper sky. Not a successful fill-in; "
            "this is the attempt. allgeo2 is a near-duplicate (0.009% pixels >8); "
            "allgeo4 is a byte-identical copy of this still."
        ),
    },
]

TITLE = "Day of Defeat on GameCube - all-geometry pass"
HOOK = "Trying to fill the geometry"
STATUS = "Dev still, Dolphin framedump - not hardware"
FOOTER1 = "Dev builds, Dolphin capture, not finished"
FOOTER2 = "Unfinished map, not a finished game"


def run(cmd, **kw):
    printable = " ".join(str(c) for c in cmd[:10])
    print("+", printable, "..." if len(cmd) > 10 else "")
    subprocess.run(cmd, check=True, **kw)


def esc_text(s: str) -> str:
    return (
        s.replace("\\", "\\\\")
        .replace(":", r"\:")
        .replace("'", r"\'")
        .replace(",", r"\,")
        .replace("%", "%%")
    )


def overlay_filter(line: str) -> str:
    common = (
        f"fontfile={FONT}:fontcolor=white:fontsize={{size}}:x=(w-text_w)/2:"
        f"shadowcolor=black@0.85:shadowx=2:shadowy=2:text='{{text}}'"
    )
    parts = [
        f"drawtext={common.format(size=36, text=esc_text(TITLE))}:y=40",
        f"drawtext={common.format(size=36, text=esc_text(HOOK)).replace('fontcolor=white', 'fontcolor=yellow')}:y=92",
        f"drawtext={common.format(size=32, text=esc_text(line))}:y=150",
        f"drawtext={common.format(size=32, text=esc_text(STATUS))}:y=196",
        f"drawtext={common.format(size=32, text=esc_text(FOOTER1))}:y=1820",
        f"drawtext={common.format(size=32, text=esc_text(FOOTER2))}:y=1870",
    ]
    return ",".join(parts)


def vf_vertical(line: str) -> str:
    # 4:3 game 1080x810 centered at y=555 over darkened blur fill. No zoom/pan.
    return (
        "[0:v]split=2[bg][fg];"
        "[bg]scale=1080:1920:force_original_aspect_ratio=increase:flags=bicubic,"
        "crop=1080:1920,boxblur=20:2,eq=brightness=-0.35:saturation=0.55,setsar=1[bg];"
        "[fg]scale=1080:810:flags=bicubic,setsar=1[fg];"
        "[bg][fg]overlay=(W-w)/2:555,fps=30,format=yuv420p,"
        + overlay_filter(line)
    )


def render_still(src: Path, dur: float, out: Path, line: str) -> float:
    out.parent.mkdir(parents=True, exist_ok=True)
    run([
        FFMPEG, "-y", "-hide_banner", "-loglevel", "error",
        "-loop", "1", "-framerate", "30", "-i", str(src),
        "-an",
        "-vf", vf_vertical(line),
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
    lines = [ln for ln in (proc.stderr or "").splitlines() if "black_start" in ln or "blackdetect" in ln.lower() and "black_" in ln]
    return "\n".join(lines) if lines else "no black_start/black_end"


def yavg(path: Path) -> float:
    proc = subprocess.run(
        [
            FFMPEG, "-hide_banner", "-loglevel", "error",
            "-i", str(path),
            "-vf", "signalstats,metadata=print:file=-",
            "-f", "null", "-",
        ],
        check=True, capture_output=True, text=True,
    )
    text = proc.stdout + proc.stderr
    for line in text.splitlines():
        if "YAVG" in line and "=" in line:
            return float(line.split("=")[-1])
    raise RuntimeError(f"no YAVG for {path}\n{text}")


def extract_frame(src: Path, t: float, dest: Path):
    run([
        FFMPEG, "-y", "-hide_banner", "-loglevel", "error",
        "-ss", f"{t:.3f}", "-i", str(src), "-frames:v", "1", str(dest),
    ])


def contact_sheet(frames: list[tuple[Path, str]], dest: Path):
    from PIL import Image, ImageDraw, ImageFont

    imgs = [(Image.open(p).convert("RGB"), lab) for p, lab in frames]
    # scale each to width 360 for a vertical short contact
    tw = 360
    thumbs = []
    for im, lab in imgs:
        th = int(im.height * tw / im.width)
        thumbs.append((im.resize((tw, th), Image.Resampling.LANCZOS), lab))
    pad, label_h, gap = 16, 36, 12
    w = tw * len(thumbs) + pad * 2 + gap * (len(thumbs) - 1)
    h = thumbs[0][0].height + pad * 2 + label_h
    sheet = Image.new("RGB", (w, h), (16, 16, 16))
    dr = ImageDraw.Draw(sheet)
    font = ImageFont.truetype(FONT, 18)
    x = pad
    for th, lab in thumbs:
        sheet.paste(th, (x, pad + label_h))
        dr.text((x + 4, pad + 6), lab, fill=(255, 255, 200), font=font)
        x += tw + gap
    dest.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(dest, quality=92)
    print("wrote contact sheet", dest)


def main():
    if shutil.which(FFMPEG) is None:
        sys.exit("ffmpeg not found")
    if not Path(FONT).exists():
        sys.exit(f"font missing: {FONT}")
    if not MUSIC.exists():
        sys.exit(f"music missing: {MUSIC}")
    for s in STILLS:
        if not s["file"].exists():
            sys.exit(f"still missing: {s['file']}")

    if WORK.exists():
        shutil.rmtree(WORK)
    WORK.mkdir(parents=True, exist_ok=True)
    REVIEW.mkdir(parents=True, exist_ok=True)
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    slices: list[Path] = []
    for i, s in enumerate(STILLS, 1):
        out = WORK / f"s{i}_{s['folder']}.mp4"
        render_still(s["file"], s["duration_s"], out, s["label"])
        slices.append(out)

    end = WORK / "endcard.mp4"
    render_endcard(end)
    slices.append(end)

    dur = concat_and_music(slices, MUSIC, OUT_MP4, MUSIC_IN)
    digest = sha256_16(OUT_MP4)
    probe = ffprobe_json(OUT_MP4)
    black = blackdetect(OUT_MP4)

    # Review frames: first still early, second still early, second still late, end card
    t_s1 = 0.40
    t_s2 = 7.0 + 0.40
    t_s2_late = 7.0 + 8.20
    t_end = 7.0 + 9.0 + 1.20
    frames = [
        (REVIEW / "01-wep-t0.40.png", t_s1, "1 wep t0.40"),
        (REVIEW / "02-allgeo-t7.40.png", t_s2, "2 allgeo t7.40"),
        (REVIEW / "03-allgeo-hold-t15.20.png", t_s2_late, "2 hold t15.20"),
        (REVIEW / "04-endcard-t17.20.png", t_end, "end t17.20"),
    ]
    frame_meta = []
    for dest, t, lab in frames:
        extract_frame(OUT_MP4, t, dest)
        y = yavg(dest)
        frame_meta.append({"file": str(dest), "t": t, "label": lab, "yavg_fullframe": y})
        print(f"review {dest.name} t={t:.2f} YAVG={y:.2f}")

    contact = REVIEW / "contact-sheet.jpg"
    contact_sheet([(p, lab) for p, t, lab in frames], contact)

    now = datetime.now(ZoneInfo("America/Los_Angeles"))
    manifest = {
        "created": now.isoformat(),
        "created_label": now.strftime("%Y-%m-%d %-I:%M %p %Z").replace("PDT", "PT").replace("PST", "PT"),
        "folder": str(OUT_DIR),
        "spec": "1080x1920 30fps H.264+AAC; 4:3 game centered at 1080x810 (y=555) over darkened blur fill; olive end card 3.0s; static stills only (no Ken Burns/zoom/pan)",
        "status": "local review only — not published, scheduled, or uploaded",
        "music_rights": "Tracks are Suno instrumentals tagged artist=gopexllc, lyrics=[Instrumental], comment='made with suno; created=2026-11-07'. No license/readme beside the files. Rights were not independently verified for commercial social posting.",
        "short": {
            "file": str(OUT_MP4),
            "title_overlay": TITLE,
            "hook_overlay": HOOK,
            "duration_s": dur,
            "bytes": OUT_MP4.stat().st_size,
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
                "file": str(MUSIC),
                "track": "Menace of War",
                "in_point_s": MUSIC_IN,
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
                for s in STILLS
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
            "visual": [
                "Caen-only cut. Flash allgeo framedumps failed (crash dumps or empty black 176-byte files).",
                "Static stills, no Ken Burns / zoom / pan / punch-in.",
                "No AMD/Mesa Video Info OSD, no magenta debug, no fatal/error/reset screens, no Half-Life missing-data menu, no Valve intro, no non-DoD menus in the used stills.",
                "A leftover bar remains in the upper sky: faint/translucent on the part-draw still, a solid white rectangle on the all-geometry still. Not cropped away.",
                "The all-geometry still has large missing-face white holes. The sequence does not cleanly show geometry filling in; the all-geo pass opened holes relative to the earlier part draw.",
                "Typography matches this morning's approved finals and the unused evening Shorts (DejaVu Sans Bold, four top lines, two footers, olive end card). End card is 3.0s as specified for this cut.",
                "No performance or hardware claims. Overlays state Dolphin framedump, dev/unfinished build.",
                "No source game audio (stills have none). Music is an unused original WW2-sounding track from the dod-gc social-media/music folder, not a Valve/Lost Coast soundtrack.",
            ],
        },
        "dropped": [
            {"folder": "20261109-220513-caen-part-leanfix", "reason": "No final.png (dolphin.log only)."},
            {"folder": "20261109-220544-caen-part-efx-hold", "reason": "Visually usable same-camera Caen still, but near-duplicate of unused evening Short 1 Caen sunset plate (mean abs 2.37, only 6.85% pixels differ >8). Not a genuinely new allgeo capture."},
            {"folder": "20261109-220917-caen-part-seed", "reason": "GameCube fatal/reset stack dump plus AMD Radeon Video Info overlay."},
            {"folder": "20261109-221552-caen-part-final", "reason": "No final.png (dolphin.log only)."},
            {"folder": "20261109-221659-caen-efx-confirm", "reason": "Empty folder."},
            {"folder": "20261109-224556-caen-part-emit", "reason": "Half-Life 'MISSING GAME DATA ST OR BOOT DISC ISO' error screen."},
            {"folder": "20261109-230207-caen-part-9mm", "reason": "First-person 9mm weapon / interior view. Not a geo flesh-out still; different subject and camera."},
            {"folder": "20261109-232943-caen-allgeo2", "reason": "Near-duplicate of caen-allgeo (0.009% pixels differ >8). Kept the earlier allgeo still."},
            {"folder": "20261109-233141-flash-allgeo2", "reason": "GameCube fatal/reset stack dump plus AMD Radeon Video Info overlay."},
            {"folder": "20261109-233401-flash-allgeo3", "reason": "Byte-identical to flash-allgeo2 (same crash dump)."},
            {"folder": "20261109-233635-flash-allgeo4", "reason": "176-byte 1-bit black frame, YAVG=0."},
            {"folder": "20261109-233841-caen-allgeo4", "reason": "Byte-identical to caen-allgeo (sha256 73b03f6ea8ee90ad)."},
            {"folder": "20261109-234008-flash-allgeo5", "reason": "176-byte 1-bit black frame, YAVG=0 (same hash as other empty Flash stills)."},
            {"folder": "20261109-234149-flash-dense7-check", "reason": "Visually usable Flash village still, but it is essentially the same plate as unused evening Short 1 flash village hold, and it is a dense7 recheck rather than an allgeo flesh-out capture. Flash allgeo attempts all failed."},
            {"folder": "20261109-234320-flash-bisect-oldspat", "reason": "176-byte 1-bit black frame, YAVG=0."},
            {"folder": "20261109-234440-flash-allgeo6", "reason": "176-byte 1-bit black frame, YAVG=0."},
            {"folder": "20261109-234614-flash-bisect-olddol", "reason": "176-byte 1-bit black frame, YAVG=0."},
            {"folder": "20261109-234756-flash-allgeo7", "reason": "176-byte 1-bit black frame, YAVG=0."},
        ],
        "inventory_notes": {
            "no_folders_after_234756": True,
            "no_20261110_folders": True,
            "no_extra_framedumps": "Every inventoried folder from 20261109-220513 onward contained at most final.png plus dolphin.log. No additional framedumps.",
            "camera": "Both used stills share the same Caen camera as each other (and as unused evening Short 1 Caen shots). The new information is the later right-wall fill, then the all-geometry pass with missing faces.",
            "geo_filling_in": "Only a modest right-wall brick fill from the evening sunset plate to the 22:58 part-wep still. The 23:26 allgeo still does not continue that fill — it opens large white holes. Caption should not claim the geometry was successfully fleshed out.",
        },
        "not_touched": [
            "Unused evening Shorts 01-caen-flash-maps-v1.mp4 and 02-capfaces-flash-avalanche-v1.mp4 were not uploaded, scheduled, replaced, or deleted.",
            "No posts deleted, unpublished, updated, uploaded, or scheduled.",
        ],
        "renderer": str(OUT_DIR / "render_allgeo_fleshout.py"),
        "review_frames": str(REVIEW),
        "contact_sheet": str(contact),
    }
    MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"DONE {OUT_MP4} {dur:.3f}s bytes={OUT_MP4.stat().st_size} sha256_16={digest}")
    print(f"MANIFEST {MANIFEST}")


if __name__ == "__main__":
    main()
