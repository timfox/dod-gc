#!/usr/bin/env python3
"""Bake tip-safe GameCube loading/intro plaques from DoD splash tiles.

Writes resource/gc_menu/loading.tga (+ intro.tga) at 320×240 for
GC_DrawLoadingStatus / GC_LoadLoadingBackground. Prefers the already-staged
BackgroundLayout.txt + 800_*_loading.tga sheet; falls back to a dark plate.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def parse_layout(source: Path, layout: Path) -> tuple[int, int, list[tuple[Path, int, int]]]:
	tiles: list[tuple[Path, int, int]] = []
	bg_w, bg_h = 800, 600
	for line in layout.read_text(encoding="utf-8", errors="replace").splitlines():
		parts = line.split()
		if not parts:
			continue
		if parts[0] == "resolution" and len(parts) >= 3:
			bg_w, bg_h = int(parts[1]), int(parts[2])
			continue
		if len(parts) < 4 or not parts[0].startswith("resource/background/"):
			continue
		try:
			x = int(parts[-2])
			y = int(parts[-1])
		except ValueError:
			continue
		tile = source / parts[0]
		if tile.is_file():
			tiles.append((tile, x, y))
	return bg_w, bg_h, tiles


def default_dod_tiles(source: Path) -> list[tuple[Path, int, int]]:
	"""Synthesize the retail 800×600 DoD loading sheet when layout is missing."""
	bg = source / "resource" / "background"
	if not bg.is_dir():
		return []
	# row/col letter grid matching stage-lean-dod.sh BackgroundLayout.txt
	grid = [
		("1_a", 0, 0),
		("1_b", 256, 0),
		("1_c", 512, 0),
		("1_d", 768, 0),
		("2_a", 0, 256),
		("2_b", 256, 256),
		("2_c", 512, 256),
		("2_d", 768, 256),
		("3_a", 0, 512),
		("3_b", 256, 512),
		("3_c", 512, 512),
		("3_d", 768, 512),
	]
	tiles: list[tuple[Path, int, int]] = []
	for key, x, y in grid:
		path = bg / f"800_{key}_loading.tga"
		if path.is_file():
			tiles.append((path, x, y))
	return tiles


def composite_layout(source: Path) -> "object | None":
	from PIL import Image, ImageEnhance

	candidates: list[tuple[int, int, list[tuple[Path, int, int]]]] = []
	for name in ("BackgroundLayout.txt", "HD_BackgroundLoadingLayout.txt", "HD_BackgroundLayout.txt"):
		layout = source / "resource" / name
		if not layout.is_file():
			continue
		bg_w, bg_h, tiles = parse_layout(source, layout)
		if tiles:
			candidates.append((bg_w, bg_h, tiles))
	dod = default_dod_tiles(source)
	if dod:
		candidates.insert(0, (800, 600, dod))

	for bg_w, bg_h, tiles in candidates:
		canvas = Image.new("RGB", (bg_w, bg_h), (0, 0, 0))
		pasted = 0
		for tile_path, x, y in tiles:
			try:
				canvas.paste(Image.open(tile_path).convert("RGB"), (x, y))
				pasted += 1
			except OSError:
				continue
		if pasted == 0:
			continue
		# DoD tiles are already readable; light lift only.
		boosted = ImageEnhance.Brightness(canvas).enhance(1.15)
		boosted = ImageEnhance.Contrast(boosted).enhance(1.1)
		return boosted.resize((320, 240), Image.Resampling.LANCZOS)
	return None


def bake(source: Path, out_root: Path) -> int:
	try:
		from PIL import Image, ImageDraw, ImageFilter
	except ImportError:
		print("error: Pillow required to bake loading plaques", file=sys.stderr)
		return 1

	menu_dir = out_root / "resource" / "gc_menu"
	menu_dir.mkdir(parents=True, exist_ok=True)

	bg = composite_layout(source)
	theme = "dod-tiles" if bg is not None else "fallback"
	if bg is None:
		bg = Image.new("RGB", (320, 240), (22, 24, 20))
		d = ImageDraw.Draw(bg)
		d.rectangle((0, 0, 319, 239), outline=(90, 100, 70), width=2)
		d.text((88, 100), "DAY OF DEFEAT", fill=(210, 200, 160))

	# Soft vignette + dark lower band for status/progress text.
	vignette = Image.new("L", (320, 240), 0)
	ImageDraw.Draw(vignette).ellipse((-40, -60, 360, 280), fill=230)
	vignette = vignette.filter(ImageFilter.GaussianBlur(24))
	bg = Image.composite(bg, Image.new("RGB", (320, 240), (8, 8, 8)), vignette)
	band = Image.new("RGBA", (320, 240), (0, 0, 0, 0))
	ImageDraw.Draw(band).rectangle((0, 168, 320, 240), fill=(8, 10, 8, 200))
	bg = Image.alpha_composite(bg.convert("RGBA"), band).convert("RGB")

	loading_path = menu_dir / "loading.tga"
	bg.save(loading_path, format="TGA")

	# Intro still: same art with a title strip (used if AVI Zone soft-fails).
	intro = bg.copy()
	td = ImageDraw.Draw(intro)
	td.rectangle((40, 200, 280, 228), fill=(12, 14, 10))
	td.text((92, 208), "DAY OF DEFEAT", fill=(220, 210, 170))
	intro_path = menu_dir / "intro.tga"
	intro.save(intro_path, format="TGA")

	print(f"baked gc_menu plaques ({theme}): {loading_path} {intro_path}")
	return 0


def main() -> int:
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument(
		"source",
		type=Path,
		nargs="?",
		default=ROOT / "content" / "dod",
		help="gamedir with resource/BackgroundLayout.txt + tiles",
	)
	ap.add_argument(
		"out_root",
		type=Path,
		nargs="?",
		default=None,
		help="output gamedir (default: same as source)",
	)
	args = ap.parse_args()
	out = args.out_root or args.source
	return bake(args.source, out)


if __name__ == "__main__":
	raise SystemExit(main())
