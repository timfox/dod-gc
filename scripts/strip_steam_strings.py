#!/usr/bin/env python3
"""Drop the retired Steam Networking checkbox from staged localisation files and fail if any staged file still has it.
Retail resource/*.txt files are UTF-16 (or UTF-8); a plain sed or grep does not see the UTF-16 ones.
  strip_steam_strings.py DIR [DIR ...]
"""
import sys
from pathlib import Path

PHRASE = "Enable Steam Networking"
KEY = "Valve_Enable_SteamNetworking"


def encodings(raw):
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return "utf-16"
    return "utf-8"


def strip(path):
    raw = path.read_bytes()
    enc = encodings(raw)
    text = raw.decode(enc, errors="replace")
    lines = text.splitlines(keepends=True)
    kept = [l for l in lines if KEY not in l]
    if len(kept) == len(lines):
        return False
    out = "".join(kept)
    data = out.encode("utf-16-le") if enc == "utf-16" else out.encode("utf-8")
    if enc == "utf-16":
        data = raw[:2] + data
    path.write_bytes(data)
    return True


def carries(path):
    raw = path.read_bytes()
    return PHRASE.encode() in raw or PHRASE.encode("utf-16-le") in raw


def main(dirs):
    bad = []
    for d in map(Path, dirs):
        for f in sorted(d.rglob("*")):
            if f.is_file() and not f.is_symlink() and f.suffix.lower() in (".txt", ".res", ".scr"):
                if strip(f):
                    print(f"dropped {KEY} from {f}")
    for d in map(Path, dirs):
        for f in sorted(d.rglob("*")):
            if f.is_file() and not f.is_symlink() and carries(f):
                bad.append(f)
    for f in bad:
        print(f"error: {f} still contains {PHRASE!r}", file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
