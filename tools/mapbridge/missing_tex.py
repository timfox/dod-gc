"""The pink/black "missing texture" checker, same pattern as the engine's (16x16, 4-texel cells). Pure python (Maya has no PIL)."""
import os, struct, zlib
PATH = os.path.expanduser("~/.cache/gsbridge/tex/_missing_checker.png")

def path():
    if not os.path.exists(PATH):
        os.makedirs(os.path.dirname(PATH), exist_ok=True)
        rows = b""
        for y in range(16):
            rows += b"\x00" + b"".join((b"\xff\x00\xff" if ((x >> 2) ^ (y >> 2)) & 1 else b"\x00\x00\x00") for x in range(16))
        def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
        open(PATH, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 16, 16, 8, 2, 0, 0, 0))
                               + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
    return PATH
