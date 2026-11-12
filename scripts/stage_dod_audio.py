#!/usr/bin/env python3
"""Stage Day of Defeat audio for GameCube lean discs.

Completes:
  - main-menu music (media/gamestartup.wav) + UI click WAVs
  - per-map soundscape / ambient / message WAVs from BSP entity lumps
  - optional core gameplay SFX packs (weapons, player, ui, …)

Resolves case mismatches and Valve basedir fallbacks. For a few retail-missing
BSP names, stages a close substitute under the expected path so ambient_generic
still plays something.
"""

from __future__ import annotations

import argparse
import re
import shutil
import struct
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

WAV_IN_MESSAGE = re.compile(r'"message"\s*"([^"\\]+\.wav)"', re.I)
WAV_IN_NOISE = re.compile(r'"(noise\d?)"\s*"([^"\\]+\.wav)"', re.I)
WAV_ANY = re.compile(r'([A-Za-z0-9_./\\-]+\.wav)', re.I)

# Expected path → substitute relative to sound roots (first hit wins).
SUBSTITUTES: dict[str, list[str]] = {
    "ambience/ambient_spring.wav": ["ambience/ambient_summer.wav"],
    "ambience/Opera.wav": ["ambience/dod_jagd_radio.wav", "ambience/radio_morse.wav"],
    "ambience/bombnhit.wav": ["ambience/artyboom.wav", "ambience/artyhit1.wav"],
    "ambience/frenchcountry.wav": ["ambience/ambient_summer.wav", "ambience/birds9.wav"],
    "ambience/merd_strike.wav": ["ambience/artyhit1.wav", "ambience/artyboom.wav"],
    "ambience/moo.wav": ["ambience/birds9.wav", "ambience/jagdbirds.wav"],
    # Path typo in some BSPs: file lives under debris/
    "ambience/bustmetal2.wav": ["debris/bustmetal2.wav"],
}

MENU_UI_WAVS = [
    "launch_upmenu1.wav",
    "launch_dnmenu1.wav",
    "launch_select1.wav",
    "launch_select2.wav",
    "launch_glow1.wav",
    "launch_deny1.wav",
    "launch_deny2.wav",
]

CORE_SFX_DIRS = [
    "weapons",
    "player",
    "common",
    "ui",
    "items",
    "doors",
    "buttons",
    "debris",
    "object",
]

# CapFaces listen: a full weapons/ tree (500+ WAVs) hung ISO9660 boot scans.
# Keep the combat set tip-safe and small enough for WorldDL listen.
CORE_SFX_COMBAT = [
    "weapons/garand_shoot.wav",
    "weapons/kar_shoot.wav",
    "weapons/mp40_shoot.wav",
    "weapons/thompson_shoot.wav",
    "weapons/colt_shoot.wav",
    "weapons/luger_shoot.wav",
    "weapons/pl_gun1.wav",
    "weapons/pl_gun2.wav",
    "weapons/pl_gun3.wav",
    "player/pl_step1.wav",
    "player/pl_step2.wav",
    "player/pl_step3.wav",
    "player/pl_step4.wav",
    "player/pl_shell1.wav",
    "buttons/button10.wav",
    "common/wpn_select.wav",
    "common/wpn_hudoff.wav",
]


def normalize_sound_path(raw: str) -> str:
    name = raw.replace("\\", "/").lstrip("/")
    if name.lower().startswith("sound/"):
        name = name[6:]
    return name


def extract_bsp_wavs(bsp: Path) -> set[str]:
    data = bsp.read_bytes()
    offset, length = struct.unpack_from("<II", data, 4)
    ents = data[offset : offset + length].decode("latin1", "ignore")
    refs: set[str] = set()
    for m in WAV_IN_MESSAGE.finditer(ents):
        refs.add(normalize_sound_path(m.group(1)))
    for m in WAV_IN_NOISE.finditer(ents):
        refs.add(normalize_sound_path(m.group(2)))
    for m in WAV_ANY.finditer(ents):
        refs.add(normalize_sound_path(m.group(1)))
    return {r for r in refs if r.lower().endswith(".wav") and ".." not in Path(r).parts}


def build_index(roots: list[Path]) -> dict[str, Path]:
    """Map lowercased relative path → first real file across roots."""
    index: dict[str, Path] = {}
    for root in roots:
        if not root.is_dir():
            continue
        for f in root.rglob("*"):
            if not f.is_file():
                continue
            rel = f.relative_to(root).as_posix()
            key = rel.lower()
            index.setdefault(key, f)
            # Also index by basename for last-resort lookups.
            index.setdefault(f"*/{f.name.lower()}", f)
    return index


def resolve_source(
    rel: str,
    index: dict[str, Path],
    roots: list[Path],
    *,
    allow_substitute: bool = True,
) -> tuple[Path | None, str]:
    """Return (source_file, how) for a sound relative path."""
    key = rel.lower()

    for root in roots:
        cand = root / rel
        if cand.is_file():
            return cand, "exact"
        parent = root / Path(rel).parent
        name = Path(rel).name.lower()
        if parent.is_dir():
            for f in parent.iterdir():
                if f.is_file() and f.name.lower() == name:
                    return f, "case"

    if key in index:
        return index[key], "index"

    if allow_substitute:
        subs = list(SUBSTITUTES.get(rel, []))
        # case-insensitive substitute table lookup
        for k, vals in SUBSTITUTES.items():
            if k.lower() == key:
                subs.extend(vals)
        for sub in dict.fromkeys(subs):  # preserve order, unique
            src, how = resolve_source(sub, index, roots, allow_substitute=False)
            if src is not None:
                return src, f"substitute:{sub}"

    base = f"*/{Path(rel).name.lower()}"
    if base in index:
        return index[base], "basename"

    return None, "missing"


def lean_dest(root: Path, rel: str) -> Path:
    """Destination under the lean dir, always lowercase so entity refs like
    Garand_shoot.wav and core staging of garand_shoot.wav never produce two
    case-twin files (the disc builder rejects those as CASE_MISMATCH)."""
    return root / rel.lower()


def copy_file(src: Path, dst: Path) -> int:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    return src.stat().st_size


def stage_menu_audio(dod: Path, valve: Path, lean: Path) -> dict:
    media_dst = lean / "media"
    media_dst.mkdir(parents=True, exist_ok=True)
    report = {"music": None, "ui": [], "missing_ui": []}

    mp3 = dod / "media" / "gamestartup.mp3"
    wav_out = media_dst / "gamestartup.wav"
    if mp3.is_file():
        if shutil.which("ffmpeg") is None:
            raise SystemExit("error: ffmpeg is required to stage DoD main-menu music")
        subprocess.run(
            [
                "ffmpeg",
                "-v",
                "error",
                "-y",
                "-i",
                str(mp3),
                "-ar",
                "22050",
                "-ac",
                "2",
                "-c:a",
                "pcm_s16le",
                str(wav_out),
            ],
            check=True,
        )
        report["music"] = {
            "path": "media/gamestartup.wav",
            "bytes": wav_out.stat().st_size,
            "from": "media/gamestartup.mp3",
        }

    search_roots = [
        dod / "media",
        valve / "media",
        valve / "sound" / "common",
        dod / "sound" / "common",
    ]
    for name in MENU_UI_WAVS:
        src = None
        for root in search_roots:
            cand = root / name
            if cand.is_file():
                src = cand
                break
            if root.is_dir():
                for f in root.iterdir():
                    if f.is_file() and f.name.lower() == name.lower():
                        src = f
                        break
            if src:
                break
        if src is None:
            report["missing_ui"].append(name)
            continue
        size = copy_file(src, media_dst / name)
        report["ui"].append({"path": f"media/{name}", "bytes": size, "from": str(src)})
    return report


def stage_map_sounds(
    maps: list[Path],
    sound_roots: list[Path],
    lean_sound: Path,
) -> dict:
    index = build_index(sound_roots)
    per_map: dict[str, dict] = {}
    staged: dict[str, dict] = {}
    missing: dict[str, list[str]] = defaultdict(list)

    for bsp in maps:
        refs = sorted(extract_bsp_wavs(bsp))
        entry = {"refs": refs, "staged": [], "missing": [], "substituted": []}
        for rel in refs:
            if rel in staged:
                entry["staged"].append(rel)
                continue
            src, how = resolve_source(rel, index, sound_roots)
            if src is None:
                entry["missing"].append(rel)
                missing[rel].append(bsp.stem)
                continue
            size = copy_file(src, lean_dest(lean_sound, rel))
            staged[rel] = {"bytes": size, "from": str(src), "how": how}
            entry["staged"].append(rel)
            if how.startswith("substitute"):
                entry["substituted"].append({"ref": rel, "how": how, "from": str(src)})
        per_map[bsp.stem] = entry

    total = sum(v["bytes"] for v in staged.values())
    return {
        "maps": per_map,
        "staged_unique": len(staged),
        "staged_bytes": total,
        "missing_unique": sorted(missing.keys()),
        "missing_detail": {k: v for k, v in missing.items()},
        "files": staged,
    }


def lean_tipsafe_wav(src: Path, dest: Path, max_seconds: float) -> int:
    """CapFaces listen keeps a 16 KiB memopt WAV ceiling — retail DoD shoots
    are 60–120 KiB @ 22 kHz. Emit 11.025 kHz mono s16 clips that fit."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    if shutil.which("ffmpeg") is None:
        return copy_file(src, dest)
    cmd = [
        "ffmpeg",
        "-v",
        "error",
        "-y",
        "-i",
        str(src),
        "-t",
        f"{max_seconds:.3f}",
        "-ar",
        "11025",
        "-ac",
        "1",
        "-c:a",
        "pcm_s16le",
        str(dest),
    ]
    try:
        subprocess.run(cmd, check=True)
    except (OSError, subprocess.CalledProcessError):
        return copy_file(src, dest)
    return dest.stat().st_size if dest.is_file() else 0


def stage_core_sfx(
    dod_sound: Path,
    lean_sound: Path,
    valve_sound: Path | None = None,
    lean_media: Path | None = None,
) -> dict:
    copied = []
    total = 0
    roots = [dod_sound]
    if valve_sound and valve_sound.is_dir():
        roots.append(valve_sound)
    index = build_index(roots)
    media_root = lean_media if lean_media is not None else lean_sound.parent / "media"

    for rel in CORE_SFX_COMBAT:
        src, how = resolve_source(rel, index, roots)
        if src is None:
            continue
        dest = lean_dest(lean_sound, rel)
        max_s = 0.45 if rel.startswith("weapons/") else 0.30
        size = lean_tipsafe_wav(src, dest, max_s)
        # G278: deep sound/* often misses on libogc ISO9660 — also stage a
        # shallow media/sfx_<basename>.wav the engine can open during CapFaces.
        base = Path(rel).name
        media_dest = media_root / f"sfx_{base.lower()}"
        if dest.is_file():
            media_root.mkdir(parents=True, exist_ok=True)
            shutil.copy2(dest, media_dest)
        copied.append(f"{rel} ({how})")
        total += size
    return {
        "files": len(copied),
        "bytes": total,
        "dirs": ["combat-minimal", "media/sfx_*"],
        "list": copied,
    }


def write_manifest(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    import json

    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dod", type=Path, required=True, help="content/dod root")
    ap.add_argument("--valve", type=Path, required=True, help="content/valve root")
    ap.add_argument("--lean", type=Path, required=True, help="lean dod output root")
    ap.add_argument(
        "--map",
        action="append",
        default=[],
        help="map stem or .bsp path (repeatable). Omit with --all-maps.",
    )
    ap.add_argument("--all-maps", action="store_true", help="stage audio for every dod_*.bsp")
    ap.add_argument(
        "--core-sfx",
        action="store_true",
        help="also stage weapons/player/ui/items/doors/buttons/debris/object/common",
    )
    ap.add_argument(
        "--menu-only",
        action="store_true",
        help="only stage main-menu music + UI clicks",
    )
    ap.add_argument(
        "--manifest",
        type=Path,
        default=None,
        help="write JSON inventory (default: lean/audio-manifest.json)",
    )
    args = ap.parse_args()

    dod = args.dod.resolve()
    valve = args.valve.resolve()
    lean = args.lean.resolve()
    lean.mkdir(parents=True, exist_ok=True)

    menu = stage_menu_audio(dod, valve, lean)
    print(
        f"menu music: {'OK' if menu['music'] else 'SKIP'} | "
        f"UI wavs staged={len(menu['ui'])} missing={menu['missing_ui']}"
    )

    payload: dict = {"menu": menu, "maps": None, "core_sfx": None}

    if not args.menu_only:
        maps: list[Path] = []
        if args.all_maps:
            maps = sorted((dod / "maps").glob("dod_*.bsp"))
        for m in args.map:
            p = Path(m)
            if not p.suffix:
                p = dod / "maps" / f"{m}.bsp"
            if not p.is_file():
                # allow absolute / relative bsp
                alt = dod / "maps" / Path(m).name
                if alt.suffix != ".bsp":
                    alt = dod / "maps" / f"{Path(m).stem}.bsp"
                p = alt
            if not p.is_file():
                print(f"error: map not found: {m}", file=sys.stderr)
                return 2
            maps.append(p.resolve())

        if maps:
            # de-dupe preserving order
            seen = set()
            uniq = []
            for m in maps:
                if m in seen:
                    continue
                seen.add(m)
                uniq.append(m)
            maps = uniq
            sound_roots = [dod / "sound", valve / "sound"]
            result = stage_map_sounds(maps, sound_roots, lean / "sound")
            payload["maps"] = result
            print(
                f"map soundscapes: maps={len(maps)} unique_wavs={result['staged_unique']} "
                f"({result['staged_bytes'] / 1048576:.2f} MiB) "
                f"missing={len(result['missing_unique'])}"
            )
            if result["missing_unique"]:
                print("  still missing: " + ", ".join(result["missing_unique"]))
            for stem, info in result["maps"].items():
                subs = len(info["substituted"])
                miss = len(info["missing"])
                print(
                    f"  {stem}: refs={len(info['refs'])} staged={len(info['staged'])} "
                    f"subs={subs} miss={miss}"
                )

        if args.core_sfx:
            core = stage_core_sfx(dod / "sound", lean / "sound", valve / "sound")
            payload["core_sfx"] = core
            print(
                f"core SFX: files={core['files']} ({core['bytes'] / 1048576:.2f} MiB) "
                f"dirs={','.join(core['dirs'])}"
            )

    manifest = args.manifest or (lean / "audio-manifest.json")
    write_manifest(manifest, payload)
    print(f"manifest: {manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
