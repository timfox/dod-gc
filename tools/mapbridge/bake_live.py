#!/usr/bin/env python3
"""Bake the engine's own per-map render packs for the live map (the shipping path, not the lean fallback):
    wt_<map>.gct   world textures as GX CMPR (+mips)          (build-gamecube-disc.py: build_world_texture_packs)
    wd_<map>.gcd   world display lists (+ _sb static brush list)  (bake_worlddl_goldsrc.py: bake)
    wd_<map>_lf.gcd light faces                                 (bake_worlddl_goldsrc.py: bake_lightfaces)
usage: bake_live.py <bsp> <outdir> [mapname=dod_live]   -> outdir/media/*"""
import importlib.util, os, sys, tempfile, shutil
from pathlib import Path
ENGINE = Path(os.environ.get("GC_BAKE_ENGINE", "/home/tim/Desktop/mapbridge-engine"))   # current master scripts (sb + lightmap packs)
CONTENT = Path("/home/tim/Desktop/dod-gc/content")

def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path); m = importlib.util.module_from_spec(spec); sys.modules[name] = m
    spec.loader.exec_module(m); return m

def bake(bsp, outdir, name="dod_live"):
    bsp, outdir = Path(bsp), Path(outdir); media = outdir / "media"; media.mkdir(parents=True, exist_ok=True)
    disc = _load("bgd", ENGINE / "scripts" / "build-gamecube-disc.py"); wd = _load("bwd", ENGINE / "scripts" / "bake_worlddl_goldsrc.py")
    src = Path(tempfile.mkdtemp(prefix="bl_src_")); (src / "maps").mkdir()
    try:
        shutil.copy(bsp, src / "maps" / (name + ".bsp"))
        skip = set(os.environ.get("BAKE_SKIP_WADS", "").split(","))      # test hook: pretend these WADs are unavailable to the baker
        for w in (CONTENT / "dod").glob("*.wad"):
            if w.name not in skip: (src / w.name).symlink_to(w)           # map WADs resolve like the real build
        for w in (CONTENT / "valve").glob("*.wad"):
            if not (src / w.name).exists(): (src / w.name).symlink_to(w)
        (src / "media").mkdir()
        disc.build_world_texture_packs(src, outdir, Path(tempfile.mkdtemp(prefix="bl_cache_")), {name})
        out = media / ("wd_%s.gcd" % name)
        wd.bake(src / "maps" / (name + ".bsp"), out); wd.bake_lightfaces(src / "maps" / (name + ".bsp"), out.with_name(out.stem + "_lf.gcd"))
    finally: shutil.rmtree(src, ignore_errors=True)
    drop = [n for n in os.environ.get("BAKE_DROP_TEX", "").split(",") if n]       # test hook: make these textures "missing" in the pack
    if drop and (media / ("wt_%s.gct" % name)).exists():
        import struct
        pth = media / ("wt_%s.gct" % name); b = bytearray(pth.read_bytes()); cnt = struct.unpack_from(">I", b, 4)[0]
        for i in range(cnt):
            nm = bytes(b[12 + i * 28:12 + i * 28 + 16]).split(b"\0")[0].decode("latin-1").lower()
            if nm in [d.lower() for d in drop]: b[12 + i * 28:12 + i * 28 + 16] = b"zz_dropped".ljust(16, b"\0")
        pth.write_bytes(bytes(b))
    return sorted(p for p in media.iterdir())

if __name__ == "__main__":
    files = bake(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "dod_live")
    for f in files: print("%-28s %8d bytes" % (f.name, f.stat().st_size))
