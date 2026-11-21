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
import isoslot, json, struct, threading
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

_lock = threading.Lock()
_lba = {}
def slot(iso, path):
    """find() once per slot; the extents never move."""
    if (iso, path) not in _lba: _lba[(iso, path)] = isoslot.find(iso, path)
    return _lba[(iso, path)]

def tail_dolphin(logpath, work):
    """Follow Dolphin's OSReport log (see live_dolphin.sh) and turn the engine's own lines into real reload status."""
    import re
    pos = None; started = None
    while True:
        try:
            with open(logpath, "r", errors="replace") as f:
                if pos is None: f.seek(0, 2); pos = f.tell()          # only what happens from now on
                else: f.seek(pos)
                while True:
                    line = f.readline()
                    if not line: break
                    pos = f.tell(); now = time.time()
                    m = re.search(r"livemap reload serial=(\d+)", line)
                    if m: started = now; status(work, state="game_reloading", game_serial=int(m.group(1)), reload_started=now)
                    elif re.search(r"livemap (armed|serial=)", line): status(work, game_connected=True, game_seen=now)
                    elif "MAP READY (dod_live)" in line and started:
                        status(work, state="game_reloaded", reload_seconds=round(now - started, 2), game_reloaded_at=now); started = None
                    elif "livemap restored player" in line: status(work, player_restored=True)
        except FileNotFoundError: pass
        time.sleep(0.15)

def bsp_texture_names(path):
    d = open(path, "rb").read(); off, _ = struct.unpack_from("<ii", d, 4 + 2 * 8); n = struct.unpack_from("<i", d, off)[0]
    return sorted(d[off + o:off + o + 16].split(b"\0")[0].decode("latin1").lower() for o in struct.unpack_from("<%di" % n, d, off + 4) if o >= 0)

def main():
    global SLOT_BSP, SLOT_TXT
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", required=True); ap.add_argument("--work", default=os.path.expanduser("~/.cache/gsbridge"))
    ap.add_argument("--vis", action="store_true"); ap.add_argument("--rad", action="store_true")
    ap.add_argument("--slot-bsp", default=SLOT_BSP); ap.add_argument("--slot-txt", default=SLOT_TXT)
    ap.add_argument("--dolphin-log", help="Dolphin OSReport log to follow for real 'game reloaded' status")
    ap.add_argument("--no-packs", action="store_true"); ap.add_argument("--once", action="store_true"); a = ap.parse_args()
    SLOT_BSP, SLOT_TXT = a.slot_bsp, a.slot_txt
    os.makedirs(a.work, exist_ok=True); mapfile = os.path.join(a.work, "live.map")
    slot_bsp = slot(a.iso, SLOT_BSP); slot_txt = slot(a.iso, SLOT_TXT)
    for n in PACKS: slot(a.iso, "/xash3d/dod/media/" + n)
    print("[watch] slots: bsp lba=%d size=%d  livemap lba=%d size=%d" % (*slot_bsp, *slot_txt))
    status(a.work, state="watching", iso=a.iso, pid=os.getpid(), game_connected=False)
    if a.dolphin_log: threading.Thread(target=tail_dolphin, args=(a.dolphin_log, a.work), daemon=True).start()
    serial, last = int(time.time()) % 1000000000, 0.0  # time-based: monotonic across restarts
    last_hash = None; last_tex = None; started_loop = False
    while True:
        try: m = os.stat(mapfile).st_mtime
        except FileNotFoundError: m = 0
        if m and m != last:
            last = m; t0 = time.time(); stages = {"wait": round(t0 - m, 2) if started_loop and t0 - m < 30 else 0.0}   # save -> watcher noticed (0 for the first pass after start)
            status(a.work, state="compiling", stages=stages, error="")
            bsp, times = build(mapfile, a.work, a.vis, a.rad); stages["compile"] = round(sum(times.values()), 2)
            if not bsp:
                status(a.work, state="error", error=open(os.path.join(a.work, "last_error.txt")).read() if os.path.exists(os.path.join(a.work, "last_error.txt")) else "compile failed")
            else:
                try:
                    import hashlib
                    h = hashlib.md5(open(bsp, "rb").read()).hexdigest()
                    if h == last_hash:
                        status(a.work, state="unchanged", stages=stages); print("[watch] map unchanged, nothing pushed")
                        if a.once: return
                        continue
                    texs = bsp_texture_names(bsp); skip_wt = texs == last_tex
                    t1 = time.time()
                    packs = {}
                    if not a.no_packs:
                        os.environ["BAKE_SKIP_WT"] = "1" if skip_wt else "0"
                        packs = bake_packs(bsp, a.work)
                    stages["bake"] = round(time.time() - t1, 2); t2 = time.time()
                    for n, pth in packs.items():     # packs first, BSP second, trigger last
                        try: isoslot.write(a.iso, "/xash3d/dod/media/" + n, pth, slot(a.iso, "/xash3d/dod/media/" + n), PACKS[n])
                        except SystemExit as e: print("[watch] pack slot", n, e)
                    isoslot.write(a.iso, SLOT_BSP, bsp, slot_bsp); serial += 1
                    tf = os.path.join(a.work, "livemap.txt"); open(tf, "w").write("L %d dod_live\n" % serial)
                    isoslot.write(a.iso, SLOT_TXT, tf, slot_txt)   # trigger LAST, after the BSP is on disc
                    stages["patch"] = round(time.time() - t2, 2); total = time.time() - t0; last_hash = h; last_tex = texs
                    status(a.work, state="pushed", serial=serial, times=times, stages=stages, pushed_at=time.time(), total=total, error="",
                           wt_reused=skip_wt, reload_seconds=None)
                    print("[watch] #%d %s total %.2fs%s" % (serial, " ".join("%s=%.2fs" % kv for kv in stages.items()), total, " (textures unchanged, wt kept)" if skip_wt else ""))
                except SystemExit as e: print("[watch] patch failed:", e)
            started_loop = True
            if a.once: return
        time.sleep(0.1)
if __name__ == "__main__": main()
