#!/usr/bin/env python3
"""Compile a Maya-exported .map to a BSP with the same pipeline as the watcher.   compile_map.py <in.map> <out.bsp> [--vis] [--no-rad]"""
import os, sys, tempfile, shutil
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import watch
def main():
    src, out = sys.argv[1], sys.argv[2]; vis = "--vis" in sys.argv; rad = "--no-rad" not in sys.argv
    work = tempfile.mkdtemp(prefix="cm_")
    try:
        bsp, times = watch.build(src, work, vis, rad)
        if not bsp:
            print("compile FAILED: " + open(os.path.join(work, "last_error.txt")).read()); return 1
        shutil.copy(bsp, out); print("compiled %s (%s)" % (out, " ".join("%s=%.2fs" % kv for kv in times.items()))); return 0
    finally: shutil.rmtree(work, ignore_errors=True)
if __name__ == "__main__": raise SystemExit(main())
