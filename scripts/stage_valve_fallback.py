#!/usr/bin/env python3
"""Fill a staged DoD game directory with the Half-Life files DoD falls back on.

Retail Day of Defeat is a mod: any sprite or sound it does not ship itself is
read from valve/. A disc carries only the dod directory, so those files were
simply missing: no blood, explosion, spark or ricochet sprites, no pickup or
ammo sounds, no material impact sounds. This copies the ones a match can ask
for into the staged dod tree. A file DoD ships itself always wins (compared
without case), and nothing in the retail trees is written.

usage: stage_valve_fallback.py --dod STAGED_DOD --valve VALVE
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

# Effect sprites, in the spelling the game and engine precache them with
# (the retail files differ in case, e.g. xspark1.spr).
EFFECT_SPRITES = (
	"muzzleflash1.spr", "muzzleflash2.spr", "muzzleflash3.spr", "richo1.spr",
	"zerogxplode.spr", "WXplo1.spr", "steam1.spr", "bubble.spr",
	"bloodspray.spr", "blood.spr", "hotglow.spr", "smoke.spr", "laserdot.spr",
	"streak.spr", "xbeam1.spr", "XSpark1.spr", "muz1.spr", "laserbeam.spr",
	"explode1.spr", "mg42.spr", "lgtning.spr",
)

# Sound directories whose Half-Life files a DoD map or the game code plays:
# pickups, HUD clicks, footsteps and water, material impacts and breakage,
# explosions and ricochets, doors, buttons, lifts and map ambience.
SOUND_DIRS = (
	"items", "common", "player", "debris", "weapons", "doors", "buttons",
	"plats", "ambience", "materials",
)
SOUND_SUFFIXES = (".wav",)


def _index(root: Path) -> dict[str, Path]:
	"""Lower-cased relative path -> real path, for every file under root."""
	out: dict[str, Path] = {}
	if root.is_dir():
		for p in root.rglob("*"):
			if p.is_file():
				out[p.relative_to(root).as_posix().lower()] = p
	return out


def _dir_like(root: Path, rel: str) -> Path:
	"""root/rel, reusing the spelling of directories that already exist."""
	cur = root
	for part in Path(rel).parts:
		match = None
		if cur.is_dir():
			for child in cur.iterdir():
				if child.is_dir() and child.name.lower() == part.lower():
					match = child
					break
		cur = match if match else cur / part
	return cur


def stage(dod: Path, valve: Path) -> dict[str, int]:
	stats = {"sprites": 0, "sprite_bytes": 0, "sounds": 0, "sound_bytes": 0, "sprites_missing": 0}
	have = _index(dod)

	valve_sprites = _index(valve / "sprites")
	sprite_dir = _dir_like(dod, "sprites")
	for name in EFFECT_SPRITES:
		if f"sprites/{name.lower()}" in have:
			continue
		src = valve_sprites.get(name.lower())
		if not src:
			stats["sprites_missing"] += 1
			continue
		sprite_dir.mkdir(parents=True, exist_ok=True)
		shutil.copyfile(src, sprite_dir / name)
		stats["sprites"] += 1
		stats["sprite_bytes"] += src.stat().st_size

	valve_sound = _dir_like(valve, "sound")
	for sub in SOUND_DIRS:
		src_dir = _dir_like(valve_sound, sub)
		if not src_dir.is_dir():
			continue
		for src in sorted(src_dir.rglob("*")):
			if not src.is_file() or src.suffix.lower() not in SOUND_SUFFIXES:
				continue
			rel = src.relative_to(valve_sound).as_posix()
			if f"sound/{rel.lower()}" in have:
				continue
			dest_dir = _dir_like(dod, str(Path("sound") / Path(rel).parent))
			dest_dir.mkdir(parents=True, exist_ok=True)
			shutil.copyfile(src, dest_dir / src.name.lower())
			stats["sounds"] += 1
			stats["sound_bytes"] += src.stat().st_size
	return stats


def main() -> int:
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--dod", type=Path, required=True, help="staged dod directory (written)")
	ap.add_argument("--valve", type=Path, required=True, help="Half-Life valve directory (read only)")
	args = ap.parse_args()
	if not args.dod.is_dir():
		print(f"error: no staged dod directory at {args.dod}", file=sys.stderr)
		return 1
	if not args.valve.is_dir():
		print(f"valve fallback: no valve directory at {args.valve}, nothing staged")
		return 0
	s = stage(args.dod, args.valve)
	print(
		f"valve fallback: {s['sprites']} effect sprites ({s['sprite_bytes']} bytes, "
		f"{s['sprites_missing']} in neither tree), {s['sounds']} sounds ({s['sound_bytes'] // 1024} KB)"
	)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
