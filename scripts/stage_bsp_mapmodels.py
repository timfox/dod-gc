#!/usr/bin/env python3
"""Stage only the studio MDLs a BSP entity lump references.

GameCube tip OOMs when the whole models/mapmodels/ tree (~160 files) is dumped
onto an interactive disc: FS_Rescan searchpaths eat MEM1 before Valve intro /
main-menu music. World textures already stream via ARAM + a small MEM1 bounce;
map props must follow the same laziness — disc only ships what this map needs,
and the engine PRECACHE/Mod_ForName's them at map load (not at boot).

Hard gates (override only for deliberate probes):
  DODGC_MAX_MAPMODELS   default 32   — refuse absurd BSP ref sets
  DODGC_MAX_MAPMODEL_BYTES default 4MiB — refuse packing a whole prop farm
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import sys
from pathlib import Path


DEFAULT_MAX_MODELS = 32
DEFAULT_MAX_BYTES = 4 * 1024 * 1024


def bsp_model_refs(bsp_path: Path) -> list[str]:
	raw = bsp_path.read_bytes()
	off, length = struct.unpack_from("<ii", raw, 4)
	ents = raw[off : off + length].decode("latin1", "replace")
	refs = set(re.findall(r'"model"\s+"(models/[^"]+)"', ents, flags=re.I))
	refs.update(
		m.decode("latin1")
		for m in re.findall(br'"model"\s+"(models/[^"]+)"', raw, flags=re.I)
	)
	out = []
	for r in refs:
		rel = r.replace("\\", "/").lstrip("/")
		name = rel.rsplit("/", 1)[-1]
		if name.startswith("*"):
			continue  # brush submodel
		if not rel.lower().startswith("models/"):
			continue
		out.append(rel)
	return sorted(set(out))


def resolve(src_root: Path, rel: str) -> Path | None:
	src = src_root / rel
	if src.is_file():
		return src
	models_root = src_root / "models"
	want = rel.split("/", 1)[-1].lower()
	if not models_root.is_dir():
		return None
	for cand in models_root.rglob("*"):
		if not cand.is_file():
			continue
		if str(cand.relative_to(models_root)).replace("\\", "/").lower() == want:
			return cand
	return None


def human(n: int) -> str:
	if n >= 1024 * 1024:
		return f"{n / (1024 * 1024):.1f}M"
	if n >= 1024:
		return f"{n / 1024:.0f}K"
	return f"{n}B"


def stage(
	bsp: Path,
	src_root: Path,
	lean_root: Path,
	*,
	max_models: int,
	max_bytes: int,
	dry_run: bool = False,
) -> dict:
	refs = bsp_model_refs(bsp)
	if len(refs) > max_models:
		raise SystemExit(
			f"error: {bsp.name} references {len(refs)} studio MDLs "
			f"(cap {max_models}). Refusing to stage a prop dump onto the Cube disc. "
			f"Fix the BSP or raise DODGC_MAX_MAPMODELS only for a deliberate probe."
		)

	mapmodels_dir = lean_root / "models" / "mapmodels"
	if not dry_run:
		mapmodels_dir.mkdir(parents=True, exist_ok=True)
		# Drop leftovers from a prior map so searchpaths cannot accumulate.
		for leftover in mapmodels_dir.glob("*"):
			if leftover.is_file():
				leftover.unlink()

	copied: list[str] = []
	missing: list[str] = []
	used = 0
	for rel in refs:
		src = resolve(src_root, rel)
		if src is None:
			missing.append(rel)
			continue
		sz = src.stat().st_size
		if used + sz > max_bytes:
			raise SystemExit(
				f"error: mapmodel budget exceeded packing {rel} "
				f"({human(used + sz)} > {human(max_bytes)}). "
				f"Cap is DODGC_MAX_MAPMODEL_BYTES — do not dump the prop farm."
			)
		if not dry_run:
			dst = lean_root / rel
			dst.parent.mkdir(parents=True, exist_ok=True)
			dst.write_bytes(src.read_bytes())
		used += sz
		copied.append(rel)

	# Refuse accidental whole-tree copies that bypassed this helper.
	if not dry_run and mapmodels_dir.is_dir():
		on_disc = [p for p in mapmodels_dir.iterdir() if p.is_file()]
		allowed = {Path(r).name.lower() for r in copied}
		strays = [p.name for p in on_disc if p.name.lower() not in allowed]
		if strays:
			raise SystemExit(
				f"error: lean mapmodels/ has non-BSP props after stage: {strays[:8]}. "
				f"Only BSP-referenced MDLs may ship (lazy at map load)."
			)
		if len(on_disc) > max_models:
			raise SystemExit(
				f"error: lean mapmodels/ has {len(on_disc)} files (cap {max_models})"
			)

	if missing:
		for rel in missing:
			print(f"missing map model: {rel}", file=sys.stderr)
		raise SystemExit(1)

	for rel in copied:
		print(f"staged map model: {rel}")
	print(
		f"staged mapmodels for {bsp.stem}: {len(copied)}/{len(refs)} "
		f"BSP-referenced MDLs ({human(used)}; lazy PRECACHE at map load)"
	)
	return {
		"bsp": str(bsp),
		"refs": refs,
		"copied": copied,
		"bytes": used,
		"max_models": max_models,
		"max_bytes": max_bytes,
	}


def main() -> int:
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument("bsp", type=Path)
	ap.add_argument("src_root", type=Path, nargs="?", help="content/dod (or valve) root")
	ap.add_argument("lean_root", type=Path, nargs="?", help="lean disc staging root")
	ap.add_argument(
		"--max-models",
		type=int,
		default=int(os.environ.get("DODGC_MAX_MAPMODELS", DEFAULT_MAX_MODELS)),
	)
	ap.add_argument(
		"--max-bytes",
		type=int,
		default=int(os.environ.get("DODGC_MAX_MAPMODEL_BYTES", DEFAULT_MAX_BYTES)),
	)
	ap.add_argument("--dry-run", action="store_true")
	ap.add_argument("--list-only", action="store_true", help="print BSP refs and exit")
	args = ap.parse_args()

	if not args.bsp.is_file():
		raise SystemExit(f"error: missing BSP {args.bsp}")
	if args.list_only:
		for rel in bsp_model_refs(args.bsp):
			print(rel)
		return 0
	if args.src_root is None or args.lean_root is None:
		raise SystemExit("error: src_root and lean_root are required unless --list-only")
	stage(
		args.bsp,
		args.src_root,
		args.lean_root,
		max_models=args.max_models,
		max_bytes=args.max_bytes,
		dry_run=args.dry_run,
	)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
