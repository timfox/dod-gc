#!/usr/bin/env python3
"""Bounded two-Cube Dolphin regression; preserves logs and ISO hashes.

Requires local-player signon on the host AND remote-player signon on the peer.
This is emulator evidence, never physical-adapter or internet evidence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("host", type=Path)
p.add_argument("client", type=Path)
p.add_argument("--timeout", type=int, default=120)
p.add_argument("--settle", type=int, default=15)
p.add_argument("--headless", action="store_true", help="only require peer signon for a server-only host")
args = p.parse_args()
dolphin = ROOT / "engine/3rdparty/dolphin/build/Binaries/dolphin-emu"
if not dolphin.is_file():
    dolphin = Path(shutil.which("dolphin-emu") or "missing-dolphin")
for artifact in (args.host, args.client, dolphin):
    if not artifact.is_file():
        p.error(f"missing {artifact}")
stamp = time.strftime("%Y%m%d-%H%M%S")
evidence = ROOT / "out" / f"network-probe-{stamp}"
evidence.mkdir()
env = dict(os.environ, EGL_PLATFORM="x11", QT_QPA_PLATFORM="xcb", DISPLAY=os.environ.get("DISPLAY", ":0"))
env.pop("WAYLAND_DISPLAY", None)
processes = []
files = []
logs = {}
profiles = {}


def launch(role, iso):
    profile = Path(tempfile.mkdtemp(prefix=f"dodgc-network-{role}-"))
    profiles[role] = str(profile)
    config = profile / "Config"
    config.mkdir()
    (config / "Dolphin.ini").write_text('''[Core]
CPUCore = 1
CPUThread = True
DSPHLE = True
FastDiscSpeed = True
MMU = True
AccurateCPUCache = True
SIDevice0 = 6
SlotB = 255
SerialPort1 = 14
[DSP]
Muted = True
Volume = 0
[Interface]
ConfirmStop = False
[Input]
BackgroundInput = True
''')
    (config / "GFX.ini").write_text("[Settings]\nBackend = OGL\nShowFPS = True\n")
    (config / "Logger.ini").write_text('''[Logs]
OSREPORT = True
OSREPORT_HLE = True
SP1 = True
EXI = True
[Options]
Verbosity = 4
WriteToConsole = True
WriteToFile = True
WriteToWindow = False
''')
    # Supply a real, controllable pad instead of enabling synthetic -gcmap input.
    pipes = profile / "Pipes"
    pipes.mkdir()
    os.mkfifo(pipes / "pad")
    (config / "GCPadNew.ini").write_text('''[GCPad1]
Device = Pipe/0/pad
Buttons/A = `Button A`
Buttons/B = `Button B`
Buttons/Start = `Button START`
Main Stick/Up = `Axis MAIN Y +`
Main Stick/Down = `Axis MAIN Y -`
Main Stick/Left = `Axis MAIN X -`
Main Stick/Right = `Axis MAIN X +`
Triggers/R = `Button R`
''')
    log = evidence / f"{role}.log"
    logs[role] = log
    stream = log.open("w")
    files.append(stream)
    proc = subprocess.Popen([str(dolphin), "-u", str(profile), "-e", str(iso.resolve())], stdout=stream, stderr=subprocess.STDOUT, env=env)
    processes.append(proc)
    return proc


def contents(role):
    return logs[role].read_text(errors="replace") if role in logs else ""


def active(role):
    return "GameCube DoD: gameplay active" in contents(role)


def fatal(role):
    return bool(re.search(r"FATAL|Host_Error|Sys_Error|out of memory|Exception.*(?:DSI|ISI)", contents(role), re.I))


def capture_windows(role, pid):
    if not shutil.which("xwininfo") or not shutil.which("import"):
        return
    tree = subprocess.run(["xwininfo", "-root", "-tree"], env=env, capture_output=True, text=True, timeout=5).stdout
    for index, line in enumerate(tree.splitlines()):
        match = re.search(r'(0x[0-9a-f]+).*".*".*\s(\d+)x(\d+)\+', line)
        if not match or int(match[2]) < 320 or int(match[3]) < 240:
            continue
        props = subprocess.run(["xprop", "-id", match[1], "_NET_WM_PID"], env=env, capture_output=True, text=True, timeout=5).stdout
        if re.search(r"=\s*" + str(pid) + r"\b", props):
            subprocess.run(["import", "-window", match[1], str(evidence / f"{role}-{index}.png")], env=env, timeout=10, check=False)


result = {"evidence": "emulator only", "profiles": profiles, "host_active": False, "client_active": False}
try:
    host = launch("host", args.host)
    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        if "Server IPv4 address" in contents("host") and (args.headless or active("host")):
            break
        if host.poll() is not None or fatal("host"):
            break
        time.sleep(1)
    print(f"Host active={active('host')}; launching peer", flush=True)
    client = launch("client", args.client)
    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        result.update(host_active=active("host"), client_active=active("client"))
        if (result["host_active"] or args.headless) and result["client_active"]:
            # Exercise both pads while gameplay and UDP traffic continue.
            for role in ("host", "client"):
                try:
                    fd = os.open(Path(profiles[role]) / "Pipes/pad", os.O_WRONLY | os.O_NONBLOCK)
                    os.write(fd, b"SET MAIN 0.5 0.8\nPRESS R\n")
                    time.sleep(1)
                    os.write(fd, b"SET MAIN 0.5 0.5\nRELEASE R\n")
                    os.close(fd)
                except OSError:
                    pass
            time.sleep(args.settle)
            for role, proc in zip(("host", "client"), processes):
                capture_windows(role, proc.pid)
            result["survived_settle"] = all(proc.poll() is None for proc in processes) and not fatal("host") and not fatal("client")
            result["moving_slots"] = sorted(set(re.findall(r"GameCube DoD: player movement slot=(\d+)", contents("host"))))
            break
        if any(proc.poll() is not None for proc in processes) or fatal("host") or fatal("client"):
            break
        time.sleep(1)
finally:
    for proc in processes:
        proc.terminate() if proc.poll() is None else None
    for proc in processes:
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    for stream in files:
        stream.close()
    result["sha256"] = {str(path.resolve()): hashlib.file_digest(path.open("rb"), "sha256").hexdigest() for path in (args.host, args.client)}
    for role, profile in profiles.items():
        shutil.copytree(Path(profile) / "Config", evidence / role / "Config", dirs_exist_ok=True)
        lines = contents(role).splitlines()
        (evidence / f"{role}-memory.log").write_text("\n".join(line for line in lines if re.search(r"heap|mem |kernel_|arena|alloc", line)) + "\n")
    (evidence / "command.txt").write_text(" ".join(__import__("sys").argv) + "\nDISPLAY=" + env["DISPLAY"] + "\nEGL_PLATFORM=x11\nQT_QPA_PLATFORM=xcb\n")
    result["dolphin"] = str(dolphin.resolve())
    result["timeout_seconds"] = args.timeout
    result["headless"] = args.headless
    (evidence / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2), flush=True)
raise SystemExit(0 if (result["host_active"] or args.headless) and result["client_active"] and result.get("survived_settle") and len(result.get("moving_slots", [])) >= (1 if args.headless else 2) else 1)
