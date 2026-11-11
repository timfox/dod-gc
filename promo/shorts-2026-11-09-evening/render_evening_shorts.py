#!/usr/bin/env python3
"""Render two evening Day of Defeat vertical Shorts (local review only)."""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path

FFMPEG = "ffmpeg"
FFPROBE = "ffprobe"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
OUT_DIR = Path("/home/tim/Desktop/dod-gc/promo/shorts-2026-11-09-evening")
WORK = Path("/tmp/dod-evening-build")
END_COLOR = "0x2d3927"  # sampled from morning end card (45, 57, 39)
END_DUR = 2.5

VIDEOS = Path("/home/tim/Desktop/dod-gc/playtests/videos")
PROMO = Path("/home/tim/Desktop/dod-gc/playtests/promo")
MUSIC_DIR = Path("/home/tim/Desktop/dod-gc/social-media/music")

CAEN_LDENSE = VIDEOS / "20261109-193958-caen-ldense-878604c1db.mp4"
FLASH_NOVM = VIDEOS / "20261109-192714-flash-novm-878604c1db.mp4"
FLASH_DENSE8 = VIDEOS / "20261109-205739-flash-dense8-878604c1db.mp4"
CAP_FLASH = PROMO / "01-capfaces-flash.mp4"
CAP_AVA = PROMO / "02-capfaces-avalanche.mp4"
MUSIC_1 = MUSIC_DIR / "Procession of Determination.mp3"
MUSIC_2 = MUSIC_DIR / "Brutal Impact.mp3"


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd[:8]), "..." if len(cmd) > 8 else "")
    subprocess.run(cmd, check=True, **kw)


def esc_text(s: str) -> str:
    """Escape drawtext specials (comma separates filters)."""
    return (
        s.replace("\\", "\\\\")
         .replace(":", r"\:")
         .replace("'", r"\'")
         .replace(",", r"\,")
         .replace("%", "%%")
    )

def overlay_filter(title: str, hook: str, line: str, status: str, footer2: str) -> str:
    footer1 = "Dev builds, Dolphin capture, not finished"
    common = (
        f"fontfile={FONT}:fontcolor=white:fontsize={{size}}:x=(w-text_w)/2:"
        f"shadowcolor=black@0.85:shadowx=2:shadowy=2:text='{{text}}'"
    )
    # y positions sampled from morning geometry v3 typography
    parts = [
        f"drawtext={common.format(size=36, text=esc_text(title))}:y=40",
        f"drawtext={common.format(size=36, text=esc_text(hook)).replace('fontcolor=white', 'fontcolor=yellow')}:y=92",
        f"drawtext={common.format(size=32, text=esc_text(line))}:y=150",
        f"drawtext={common.format(size=32, text=esc_text(status))}:y=196",
        f"drawtext={common.format(size=32, text=esc_text(footer1))}:y=1820",
        f"drawtext={common.format(size=32, text=esc_text(footer2))}:y=1870",
    ]
    return ",".join(parts)


def vf_vertical(labels: dict, extra_v: str = "") -> str:
    # 4:3 game 1080x810 centered at y=555 over darkened blur fill
    chain = (
        "[0:v]split=2[bg][fg];"
        "[bg]scale=1080:1920:force_original_aspect_ratio=increase:flags=bicubic,"
        "crop=1080:1920,boxblur=20:2,eq=brightness=-0.35:saturation=0.55,setsar=1[bg];"
        "[fg]scale=1080:810:flags=bicubic,setsar=1[fg];"
        "[bg][fg]overlay=(W-w)/2:555,fps=30,format=yuv420p,"
        + overlay_filter(**labels)
    )
    if extra_v:
        chain = extra_v + chain
    return chain


def render_slice(src: Path, start: float, dur: float, out: Path, labels: dict, freeze_pad: float = 0.0):
    """Two-pass: accurate trim first, then vertical layout / optional freeze pad."""
    out.parent.mkdir(parents=True, exist_ok=True)
    total = dur + (freeze_pad if freeze_pad > 0 else 0.0)
    raw = WORK / (out.stem + "_raw.mp4")
    # Pass 1: -ss after -i for accurate in-point; single -t limits output length.
    run([
        FFMPEG, "-y", "-hide_banner", "-loglevel", "error",
        "-i", str(src),
        "-ss", f"{start:.4f}",
        "-t", f"{dur:.4f}",
        "-an",
        "-c:v", "libx264", "-preset", "ultrafast", "-crf", "16",
        "-pix_fmt", "yuv420p",
        str(raw),
    ])
    if raw.stat().st_size < 10000:
        raise RuntimeError(f"empty raw slice: {raw} ({raw.stat().st_size} bytes)")
    vf = vf_vertical(labels)
    if freeze_pad > 0:
        vf = vf + f",tpad=stop_mode=clone:stop_duration={freeze_pad:.4f}"
    run([
        FFMPEG, "-y", "-hide_banner", "-loglevel", "error",
        "-i", str(raw),
        "-an",
        "-vf", vf,
        "-r", "30",
        "-t", f"{total:.4f}",
        "-c:v", "libx264", "-preset", "medium", "-crf", "18",
        "-pix_fmt", "yuv420p", "-profile:v", "high", "-level", "4.0",
        "-video_track_timescale", "15360",
        str(out),
    ])
    if out.stat().st_size < 10000:
        raise RuntimeError(f"empty labeled slice: {out} ({out.stat().st_size} bytes)")
    return total


def render_endcard(out: Path) -> float:
    # Match morning olive end card typography
    vf = (
        f"drawtext=fontfile={FONT}:fontcolor=white:fontsize=48:"
        f"x=(w-text_w)/2:y=890:shadowcolor=black@0.85:shadowx=2:shadowy=2:"
        f"text='{esc_text('Project updates:')}',"
        f"drawtext=fontfile={FONT}:fontcolor=white:fontsize=52:"
        f"x=(w-text_w)/2:y=968:shadowcolor=black@0.85:shadowx=2:shadowy=2:"
        f"text='{esc_text('gamecubehalf.life')}'"
    )
    cmd = [
        FFMPEG, "-y", "-hide_banner", "-loglevel", "error",
        "-f", "lavfi", "-i", f"color=c={END_COLOR}:s=1080x1920:d={END_DUR}:r=30",
        "-an", "-vf", vf,
        "-c:v", "libx264", "-preset", "medium", "-crf", "18",
        "-pix_fmt", "yuv420p", "-profile:v", "high", "-level", "4.0",
        "-video_track_timescale", "15360",
        str(out),
    ]
    run(cmd)
    return END_DUR


def concat_and_music(slices: list[Path], music: Path, out: Path, music_start: float = 0.0):
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
    # fade in 0.4s, fade out last 1.2s; modest volume; no source game audio
    fade_out_st = max(0.0, dur - 1.2)
    cmd = [
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
    ]
    run(cmd)
    return dur


def main():
    if shutil.which(FFMPEG) is None:
        sys.exit("ffmpeg not found")
    WORK.mkdir(parents=True, exist_ok=True)
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    common_status = "Dev build, Dolphin emulator - not hardware"
    title1 = "Day of Defeat on GameCube - evening maps"
    hook1 = "Unfinished maps, in progress"
    foot1 = "Evening playtests, real time"

    # Short 1 — distinct Caen / Flash map views from tonight
    s1 = []
    s1.append(WORK / "s1_caen_walk.mp4")
    render_slice(
        CAEN_LDENSE, 9.00, 4.50, s1[-1],
        dict(
            title=title1, hook=hook1,
            line="2026-11-09 - dod_caen ldense - 878604c1",
            status=common_status, footer2=foot1,
        ),
    )
    s1.append(WORK / "s1_caen_sunset.mp4")
    # source only has 74.22-76.05 (~1.83s) of this later lighting; clone-pad to 4.0s
    render_slice(
        CAEN_LDENSE, 74.22, 1.83, s1[-1],
        dict(
            title=title1, hook=hook1,
            line="2026-11-09 - dod_caen later lighting - 878604c1",
            status=common_status, footer2=foot1,
        ),
        freeze_pad=2.17,
    )
    s1.append(WORK / "s1_flash_alley.mp4")
    render_slice(
        FLASH_NOVM, 7.50, 5.50, s1[-1],
        dict(
            title=title1, hook=hook1,
            line="2026-11-09 - dod_flash courtyard - 878604c1",
            status=common_status, footer2=foot1,
        ),
    )
    s1.append(WORK / "s1_flash_village.mp4")
    render_slice(
        FLASH_DENSE8, 36.09, 5.50, s1[-1],
        dict(
            title=title1, hook=hook1,
            line="2026-11-09 - dod_flash village - 878604c1",
            status=common_status, footer2=foot1,
        ),
    )
    end1 = WORK / "endcard.mp4"
    render_endcard(end1)
    s1.append(end1)
    out1 = OUT_DIR / "01-caen-flash-maps-v1.mp4"
    dur1 = concat_and_music(s1, MUSIC_1, out1)

    # Short 2 — unused Nov 8 capfaces Flash + Avalanche flythroughs
    title2 = "Day of Defeat on GameCube - map geometry"
    hook2 = "Broken geometry, still in progress"
    foot2 = "Playtest flythrough, not finished"
    s2 = []
    s2.append(WORK / "s2_flash.mp4")
    render_slice(
        CAP_FLASH, 7.40, 15.82 - 7.40, s2[-1],
        dict(
            title=title2, hook=hook2,
            line="2026-11-08 - dod_flash capfaces",
            status=common_status, footer2=foot2,
        ),
    )
    s2.append(WORK / "s2_ava_a.mp4")
    render_slice(
        CAP_AVA, 5.50, 15.25 - 5.50, s2[-1],
        dict(
            title=title2, hook=hook2,
            line="2026-11-08 - dod_avalanche capfaces",
            status=common_status, footer2=foot2,
        ),
    )
    s2.append(WORK / "s2_ava_b.mp4")
    render_slice(
        CAP_AVA, 17.52, 20.95 - 17.52, s2[-1],
        dict(
            title=title2, hook=hook2,
            line="2026-11-08 - dod_avalanche capfaces",
            status=common_status, footer2=foot2,
        ),
    )
    s2.append(end1)
    out2 = OUT_DIR / "02-capfaces-flash-avalanche-v1.mp4"
    dur2 = concat_and_music(s2, MUSIC_2, out2)

    print(f"DONE {out1} {dur1:.3f}s")
    print(f"DONE {out2} {dur2:.3f}s")


if __name__ == "__main__":
    main()
