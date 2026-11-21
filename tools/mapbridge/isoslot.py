#!/usr/bin/env python3
"""Patch a file inside an ISO9660/GameCube disc image IN PLACE, same size.

Why: our dodgc*.iso keeps game data in an ISO9660 tree the engine reads with raw
DVD sector reads (iso9660_gamecube.c; one-sector cache). Dolphin serves the .iso
from the host file, so overwriting a file's extent while the game runs is seen on
the next read. The directory record (size) never changes -> the file is a fixed
"slot": we pad the new content with zeros up to the slot size.

    isoslot.py find  disc.iso /xash3d/dod/maps/dod_live.bsp      # -> lba size
    isoslot.py write disc.iso /xash3d/dod/maps/dod_live.bsp new.bsp
"""
import os, re, subprocess, sys

def find(iso, path):
    out = subprocess.run(["xorriso", "-indev", iso, "-find", path, "-exec", "report_lba", "--"],
                         capture_output=True, text=True).stdout
    m = re.search(r"File data lba:\s+\d+ ,\s+(\d+) ,\s+(\d+) ,\s+(\d+) ,", out)  # start lba, blocks, bytes
    if not m: raise SystemExit("not found in image: " + path + "\n" + out[-300:])
    lba, _blocks, size = int(m.group(1)), int(m.group(2)), int(m.group(3))
    return lba, size

def write(iso, path, src, lba_size=None, max_size=None):
    lba, size = lba_size or find(iso, path)
    data = open(src, "rb").read()
    if len(data) > size: raise SystemExit("%s is %d bytes, slot is %d" % (src, len(data), size))
    data += b"\0" * (size - len(data))
    fd = os.open(iso, os.O_WRONLY)
    try:
        os.pwrite(fd, data, lba * 2048); os.fsync(fd)
    finally: os.close(fd)
    return lba, size

if __name__ == "__main__":
    if sys.argv[1] == "find": print(*find(sys.argv[2], sys.argv[3]))
    elif sys.argv[1] == "write": print(*write(sys.argv[2], sys.argv[3], sys.argv[4]))
