#!/usr/bin/env python3
"""Record a Dolphin playtest into dod-gc/playtests/videos/ (isolated from HL1 .ai/)."""

from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = Path(os.environ.get("DODGC_VIDEOS", ROOT / "playtests" / "videos"))
INDEX = Path(os.environ.get("DODGC_PLAYTESTS", ROOT / "playtests")) / "INDEX.md"
MIN_FREE_GB = 5.0


def sh(*cmd: str, **kw):
	return subprocess.run(cmd, capture_output=True, text=True, **kw)


def alive(pid: int) -> bool:
	if not pid:
		return True
	try:
		os.kill(pid, 0)
		return True
	except OSError:
		return False


def find_render_window(timeout: float) -> str | None:
	deadline = time.time() + timeout
	while time.time() < deadline:
		tree = sh("xwininfo", "-root", "-tree").stdout
		for line in tree.splitlines():
			if '("dolphin-emu" "GameCube")' not in line or ")  800x600" in line:
				continue
			m = re.search(r"(\d+)x(\d+)\+", line)
			if m and int(m.group(1)) >= 300 and int(m.group(2)) >= 200:
				return line.split()[0]
		time.sleep(0.5)
	return None


def window_exists(win: str) -> bool:
	return sh("xwininfo", "-id", win).returncode == 0


def git_sha(path: Path) -> str:
	r = sh("git", "-C", str(path), "rev-parse", "--short=10", "HEAD")
	return (r.stdout or "").strip() or "unknown"


def probe_media(path: Path):
	r = sh(
		"ffprobe",
		"-v",
		"error",
		"-show_entries",
		"format=duration:stream=codec_type,codec_name",
		"-of",
		"json",
		str(path),
	)
	try:
		data = json.loads(r.stdout or "{}")
	except ValueError:
		return 0.0, []
	duration = float((data.get("format") or {}).get("duration") or 0)
	streams = [
		f'{s.get("codec_type")}:{s.get("codec_name")}'
		for s in data.get("streams") or []
	]
	return duration, streams


def write_index() -> None:
	rows = []
	for meta_path in sorted(OUT_DIR.glob("*.json"), reverse=True):
		try:
			meta = json.loads(meta_path.read_text(encoding="utf-8"))
		except (OSError, ValueError):
			continue
		rows.append(
			f"| {meta.get('date', '')} | `{meta.get('file', '')}` | "
			f"{meta.get('route', '')} | {meta.get('duration_s', '')}s | "
			f"{meta.get('size_mb', '')} MB | `{meta.get('commit', '')}` |"
		)
	INDEX.parent.mkdir(parents=True, exist_ok=True)
	INDEX.write_text(
		"# dod-gc playtests\n\n"
		"Screenshots: `playtests/screenshots/` · Videos: `playtests/videos/` · Notes: `playtests/notes/`\n\n"
		"| Date | File | Route | Duration | Size | Commit |\n"
		"|------|------|-------|----------|------|--------|\n"
		+ ("\n".join(rows) if rows else "| — | — | — | — | — | — |\n")
		+ "\n",
		encoding="utf-8",
	)


def sink_create(name: str) -> str:
	r = sh(
		"pactl",
		"load-module",
		"module-null-sink",
		f"sink_name={name}",
		f"sink_properties=device.description={name}",
	)
	if r.returncode != 0:
		sys.exit(f"null sink failed: {(r.stderr or '').strip()}")
	return (r.stdout or "").strip()


def sink_destroy(mod_id: str) -> None:
	if mod_id:
		sh("pactl", "unload-module", mod_id)


def cmd_record(args: argparse.Namespace) -> int:
	OUT_DIR.mkdir(parents=True, exist_ok=True)
	free_gb = shutil.disk_usage(OUT_DIR).free / 1e9
	if free_gb < MIN_FREE_GB:
		print(
			f"record-session: only {free_gb:.1f} GB free (< {MIN_FREE_GB}), not recording",
			file=sys.stderr,
		)
		return 0

	win = find_render_window(args.window_timeout)
	if not win or not alive(args.pid):
		print("record-session: no Dolphin render window, not recording", file=sys.stderr)
		return 0

	stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
	sha = git_sha(ROOT)
	base = OUT_DIR / f"{stamp}-{args.route}-{sha}"
	mkv = Path(str(base) + ".mkv")
	log = Path(str(base) + ".ffmpeg.log")

	ff = subprocess.Popen(
		[
			"ffmpeg",
			"-hide_banner",
			"-loglevel",
			"error",
			"-y",
			"-thread_queue_size",
			"512",
			"-f",
			"x11grab",
			"-draw_mouse",
			"0",
			"-framerate",
			str(args.fps),
			"-window_id",
			win,
			"-i",
			os.environ.get("DISPLAY", ":0"),
			"-thread_queue_size",
			"512",
			"-f",
			"pulse",
			"-i",
			f"{args.sink}.monitor",
			"-vf",
			"pad=ceil(iw/2)*2:ceil(ih/2)*2",
			"-c:v",
			"libx264",
			"-preset",
			"veryfast",
			"-crf",
			"30",
			"-pix_fmt",
			"yuv420p",
			"-g",
			str(args.fps * 2),
			"-c:a",
			"aac",
			"-b:a",
			"96k",
			"-f",
			"matroska",
			str(mkv),
		],
		stdin=subprocess.PIPE,
		stderr=open(log, "w", encoding="utf-8"),
	)

	started = time.time()
	stop = {"now": False}
	signal.signal(signal.SIGTERM, lambda *_: stop.__setitem__("now", True))
	signal.signal(signal.SIGINT, lambda *_: stop.__setitem__("now", True))

	while alive(args.pid) and ff.poll() is None and not stop["now"]:
		if args.pid == 0 and not window_exists(win):
			break
		time.sleep(0.5)

	if ff.poll() is None:
		try:
			ff.communicate(b"q", timeout=20)
		except subprocess.TimeoutExpired:
			ff.send_signal(signal.SIGINT)
			ff.wait(timeout=10)

	out = Path(str(base) + ".mp4")
	mkv_ok = mkv.is_file() and mkv.stat().st_size > 0
	if (
		mkv_ok
		and sh(
			"ffmpeg",
			"-hide_banner",
			"-loglevel",
			"error",
			"-y",
			"-i",
			str(mkv),
			"-c",
			"copy",
			"-movflags",
			"+faststart",
			str(out),
		).returncode
		== 0
		and out.is_file()
		and out.stat().st_size > 0
	):
		mkv.unlink(missing_ok=True)
		if log.exists() and log.stat().st_size == 0:
			log.unlink(missing_ok=True)
	elif mkv_ok:
		out = mkv
	else:
		print("record-session: capture failed, no media written", file=sys.stderr)
		return 1

	duration, streams = probe_media(out)
	size_mb = round(out.stat().st_size / 1e6, 1)
	meta = {
		"file": out.name,
		"date": datetime.datetime.fromtimestamp(started).isoformat(timespec="seconds"),
		"commit": sha,
		"engine_commit": git_sha(ROOT / "engine"),
		"route": args.route,
		"disc": args.disc,
		"duration_s": round(duration, 1),
		"streams": streams,
		"size_mb": size_mb,
		"project": "dod-gc",
	}
	Path(str(base) + ".json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
	write_index()
	print(f"record-session: wrote {out} ({size_mb} MB, {meta['duration_s']}s)")
	return 0


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	sub = parser.add_subparsers(dest="cmd", required=True)

	p_sink = sub.add_parser("sink-create")
	p_sink.add_argument("name")

	p_destroy = sub.add_parser("sink-destroy")
	p_destroy.add_argument("module_id")

	p_rec = sub.add_parser("record")
	p_rec.add_argument("--pid", type=int, required=True)
	p_rec.add_argument("--sink", required=True)
	p_rec.add_argument("--route", default="play")
	p_rec.add_argument("--disc", default="")
	p_rec.add_argument("--fps", type=int, default=30)
	p_rec.add_argument("--window-timeout", type=float, default=90.0)

	sub.add_parser("index")

	args = parser.parse_args()
	if args.cmd == "sink-create":
		print(sink_create(args.name))
		return 0
	if args.cmd == "sink-destroy":
		sink_destroy(args.module_id)
		return 0
	if args.cmd == "index":
		write_index()
		print(INDEX)
		return 0
	if args.cmd == "record":
		return cmd_record(args)
	return 2


if __name__ == "__main__":
	raise SystemExit(main())
