#!/usr/bin/env python3
"""ceres-side watcher: live.map saved from Maya -> compile -> patch the disc slot.

    watch.py --iso /tmp/gsbridge/live.iso [--work ~/.cache/gsbridge] [--vis] [--once]

Pipeline per change (stock GoldSrc formats, SDHLT = ZHLT fork, built in bin/):
    live.map --sdHLCSG--> --sdHLBSP--> [--sdHLVIS --sdHLRAD]--> live.bsp (v30)
    pad to slot size, pwrite into the ISO extent of /xash3d/dod/maps/dod_live.bsp,
    then bump the serial in the livemap.txt slot -> the engine's live-map poll
    (engine-livemap.patch) sees it and runs `map dod_live`.
Fast loop = CSG+BSP, fullbright (no RAD): ~0.1 s for a room. --vis/--rad add the
slow passes for lit builds. Never launches or kills Dolphin.
"""
import argparse, os, shutil, subprocess, sys, time, tempfile
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import isoslot, json, struct
BIN = os.path.join(HERE, "bin")
WADDIR = "/home/tim/Desktop/dod-gc/content/dod"
SLOT_BSP = "/xash3d/dod/maps/dod_live.bsp"
SLOT_TXT = "/xash3d/dod/livemap.txt"
PACKS = {"wt_dod_live.gct": 2 << 20, "wd_dod_live.gcd": 1 << 20, "wd_dod_live_lf.gcd": 1 << 20, "wd_dod_live_sb.gcd": 64 << 10}   # fixed slot sizes (media/)

def run(tool, args, cwd, log):
    t = time.time()
    r = subprocess.run([os.path.join(BIN, tool)] + args, cwd=cwd, capture_output=True, text=True)
    open(os.path.join(cwd, log), "w").write(r.stdout + r.stderr)
    return r.returncode, time.time() - t

def build(mapfile, work, vis, rad):
    b = tempfile.mkdtemp(prefix="b_", dir=work)
    try:
        shutil.copy(mapfile, os.path.join(b, "dod_live.map"))
        for w in os.listdir(WADDIR):
            if w.endswith(".wad"): os.symlink(os.path.join(WADDIR, w), os.path.join(b, w))
        times = {}
        steps = [("csg", "sdHLCSG", ["dod_live"]), ("bsp", "sdHLBSP", ["dod_live"])]
        if vis: steps.append(("vis", "sdHLVIS", ["dod_live", "-fast"]))
        if rad: steps.append(("rad", "sdHLRAD", ["dod_live", "-fast"]))
        for name, tool, args in steps:
            rc, dt = run(tool, args, b, name + ".log"); times[name] = dt
            if rc != 0:
                tail = open(os.path.join(b, name + ".log")).read()[-1200:]
                open(os.path.join(work, "last_error.txt"), "w").write("%s failed: %s" % (name, tail[-400:]))
                print("[watch] %s FAILED (rc=%d) - game keeps the previous map\n%s" % (name, rc, tail)); return None, times
        out = os.path.join(work, "live.bsp"); shutil.copy(os.path.join(b, "dod_live.bsp"), out)
        return out, times
    finally: shutil.rmtree(b, ignore_errors=True)

def status(work, **kw):
    """Machine-readable status for the Maya panel (~/.cache/gsbridge/status.json)."""
    f = os.path.join(work, "status.json")
    try: cur = json.load(open(f))
    except Exception: cur = {}
    cur.update(kw, updated=time.time())
    json.dump(cur, open(f + ".tmp", "w")); os.replace(f + ".tmp", f)

def bake_packs(bsp, work):
    """Engine render packs for the live BSP (textures, display lists, light faces) -> {name: path}; {} if the baker is unavailable."""
    out = os.path.join(work, "packs"); shutil.rmtree(out, ignore_errors=True)
    r = subprocess.run(["python3", os.path.join(HERE, "bake_live.py"), bsp, out], capture_output=True, text=True)
    if r.returncode: print("[watch] pack bake FAILED:", r.stderr[-300:]); return {}
    sb = os.path.join(out, "media", "wd_dod_live_sb.gcd")
    if os.path.exists(sb):      # the sb header records the wd size = the padded slot size
        with open(sb, "r+b") as fh: fh.seek(8); fh.write(struct.pack(">I", PACKS["wd_dod_live.gcd"]))
    return {n: os.path.join(out, "media", n) for n in PACKS if os.path.exists(os.path.join(out, "media", n))}

def main():
    global SLOT_BSP, SLOT_TXT
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", required=True); ap.add_argument("--work", default=os.path.expanduser("~/.cache/gsbridge"))
    ap.add_argument("--vis", action="store_true"); ap.add_argument("--rad", action="store_true")
    ap.add_argument("--slot-bsp", default=SLOT_BSP); ap.add_argument("--slot-txt", default=SLOT_TXT)
    ap.add_argument("--no-packs", action="store_true"); ap.add_argument("--once", action="store_true"); a = ap.parse_args()
    SLOT_BSP, SLOT_TXT = a.slot_bsp, a.slot_txt
    os.makedirs(a.work, exist_ok=True); mapfile = os.path.join(a.work, "live.map")
    slot_bsp = isoslot.find(a.iso, SLOT_BSP); slot_txt = isoslot.find(a.iso, SLOT_TXT)
    print("[watch] slots: bsp lba=%d size=%d  livemap lba=%d size=%d" % (*slot_bsp, *slot_txt))
    status(a.work, state="watching", iso=a.iso, pid=os.getpid())
    serial, last = int(time.time()) % 1000000000, 0.0  # time-based: monotonic across restarts
    while True:
        try: m = os.stat(mapfile).st_mtime
        except FileNotFoundError: m = 0
        if m and m != last:
            last = m; t0 = time.time(); status(a.work, state="compiling")
            bsp, times = build(mapfile, a.work, a.vis, a.rad)
            if not bsp: status(a.work, state="error", error=open(os.path.join(a.work, "last_error.txt")).read() if os.path.exists(os.path.join(a.work, "last_error.txt")) else "compile failed")
            if bsp:
                try:
                    packs = bake_packs(bsp, a.work) if not a.no_packs else {}
                    for n, pth in packs.items():     # packs first, BSP second, trigger last
                        try: isoslot.write(a.iso, "/xash3d/dod/media/" + n, pth, None, PACKS[n])
                        except SystemExit as e: print("[watch] pack slot", n, e)
                    isoslot.write(a.iso, SLOT_BSP, bsp, slot_bsp); serial += 1
                    tf = os.path.join(a.work, "livemap.txt"); open(tf, "w").write("L %d dod_live\n" % serial)
                    isoslot.write(a.iso, SLOT_TXT, tf, slot_txt)   # trigger LAST, after the BSP is on disc
                    status(a.work, state="pushed", serial=serial, times=times, pushed_at=time.time(), total=time.time() - t0, error="")
                    print("[watch] #%d compiled %s, patched %s in %.2fs total" % (serial,
                          " ".join("%s=%.2fs" % kv for kv in times.items()), os.path.basename(a.iso), time.time() - t0))
                except SystemExit as e: print("[watch] patch failed:", e)
            if a.once: return
        time.sleep(0.25)
if __name__ == "__main__": main()
