#!/usr/bin/env bash
# Solo local listen server: Main → New game → Create Game → Done (no clients expected).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

ISO="${1:-$DODGC_OUT/dodgc-menu.iso}"
[[ -f "$ISO" ]] || ISO="$DODGC_ISO"
[[ -f "$ISO" ]] || { echo "error: ISO not found" >&2; exit 1; }

DOLPHIN=""
if [[ -x "$DODGC_ENGINE/3rdparty/dolphin/build/Binaries/dolphin-emu" ]]; then
	DOLPHIN="$DODGC_ENGINE/3rdparty/dolphin/build/Binaries/dolphin-emu"
elif command -v dolphin-emu >/dev/null 2>&1; then
	DOLPHIN="$(command -v dolphin-emu)"
else
	echo "error: Dolphin not found" >&2
	exit 2
fi

STAMP="$(date +%Y%m%d-%H%M%S)"
SAFE_ROUTE="local-create-match"
SHOT_DIR="$DODGC_SHOTS/${STAMP}-${SAFE_ROUTE}"
USER_DIR="${XDG_RUNTIME_DIR:-/tmp}/dodgc-creatematch-${STAMP}"
LOG="/tmp/dodgc-creatematch-${STAMP}.log"
PIPE="$USER_DIR/Pipes/gcpad"
mkdir -p "$SHOT_DIR" "$USER_DIR/Config" "$USER_DIR/Pipes" "$DODGC_NOTES" "$DODGC_VIDEOS"

PROFILE="${XDG_DATA_HOME:-$HOME/.local/share}/dod-gc/dolphin"
[[ -f "$PROFILE/Config/GFX.ini" ]] && cp -f "$PROFILE/Config/GFX.ini" "$USER_DIR/Config/"

# Offline solo match — no BBA EXI needed.
cat > "$USER_DIR/Config/Dolphin.ini" <<'EOF'
[Core]
CPUCore = 1
CPUThread = True
DSPHLE = True
FastDiscSpeed = True
MMU = True
AccurateCPUCache = True
SIDevice0 = 6
SlotB = 255
SerialPort1 = 255
[DSP]
Muted = False
Volume = 100
[Interface]
ConfirmStop = False
[Display]
KeepWindowOnTop = False
[Input]
BackgroundInput = True
EOF

cat > "$USER_DIR/Config/Logger.ini" <<'EOF'
[Logs]
BOOT = True
CORE = True
OSREPORT = True
OSREPORT_HLE = True
[Options]
Verbosity = 4
WriteToConsole = True
WriteToFile = True
WriteToWindow = False
EOF

[[ -f "$USER_DIR/Config/GFX.ini" ]] || cat > "$USER_DIR/Config/GFX.ini" <<'EOF'
[Settings]
Backend = OGL
ShowFPS = True
EOF

rm -f -- "$PIPE"
mkfifo "$PIPE"
cat > "$USER_DIR/Config/GCPadNew.ini" <<'EOF'
[GCPad1]
Device = Pipe/0/gcpad
Buttons/A = `Button A`
Buttons/B = `Button B`
Buttons/X = `Button X`
Buttons/Y = `Button Y`
Buttons/Z = `Button Z`
Buttons/Start = `Button START`
Main Stick/Up = `Axis MAIN Y -`
Main Stick/Down = `Axis MAIN Y +`
Main Stick/Left = `Axis MAIN X -`
Main Stick/Right = `Axis MAIN X +`
Main Stick/Modifier = SHIFT
Main Stick/Calibration = 100.00 141.42 100.00 141.42 100.00 141.42 100.00 141.42
C-Stick/Up = `Axis C Y -`
C-Stick/Down = `Axis C Y +`
C-Stick/Left = `Axis C X -`
C-Stick/Right = `Axis C X +`
C-Stick/Modifier = CTRL
C-Stick/Calibration = 100.00 141.42 100.00 141.42 100.00 141.42 100.00 141.42
Triggers/L = `Button L`
Triggers/R = `Button R`
D-Pad/Up = `Button D_UP`
D-Pad/Down = `Button D_DOWN`
D-Pad/Left = `Button D_LEFT`
D-Pad/Right = `Button D_RIGHT`
[GCPad2]
Device = XInput2/0/Virtual core pointer
[GCPad3]
Device = XInput2/0/Virtual core pointer
[GCPad4]
Device = XInput2/0/Virtual core pointer
EOF

export EGL_PLATFORM=x11 QT_QPA_PLATFORM=xcb
unset WAYLAND_DISPLAY
: "${DISPLAY:=:0}"
export DISPLAY

REC_PY="$SCRIPT_DIR/record-session.py"
SINK=
SINK_MOD=
REC_PID=
DPID=
PAD_FD=
cleanup() {
	[[ -n "${PAD_FD:-}" ]] && eval "exec ${PAD_FD}>&-" 2>/dev/null || true
	[[ -n "${DPID:-}" ]] && kill "$DPID" 2>/dev/null || true
	[[ -n "${DPID:-}" ]] && wait "$DPID" 2>/dev/null || true
	[[ -n "${REC_PID:-}" ]] && { timeout 90 tail --pid="$REC_PID" -f /dev/null 2>/dev/null || true; }
	[[ -n "${SINK_MOD:-}" ]] && python3 "$REC_PY" sink-destroy "$SINK_MOD" || true
	rm -f -- "$PIPE" 2>/dev/null || true
}
trap cleanup EXIT

echo "==> local Create Game ISO=$ISO"
rm -f "$LOG"
exec 3<>"$PIPE"
PAD_FD=3

if command -v ffmpeg >/dev/null 2>&1 && command -v pactl >/dev/null 2>&1; then
	SINK="dodgccreate$$"
	if SINK_MOD="$(python3 "$REC_PY" sink-create "$SINK")"; then
		export PULSE_SINK="$SINK"
	else
		SINK=; SINK_MOD=
	fi
fi

"$DOLPHIN" -u "$USER_DIR" -e "$ISO" >"$LOG" 2>&1 &
DPID=$!

if [[ -n "$SINK_MOD" ]]; then
	python3 "$REC_PY" record \
		--pid "$DPID" --sink "$SINK" --route "$SAFE_ROUTE" --disc "$ISO" \
		--window-timeout 90 &
	REC_PID=$!
fi

START_TS=$(date +%s)
READY=0
while kill -0 "$DPID" 2>/dev/null; do
	elapsed=$(( $(date +%s) - START_TS ))
	if rg -a -q 'gc_menu_ready set 1|retail menu steam background ready' "$LOG" 2>/dev/null; then
		READY=1
		break
	fi
	if rg -a -q 'FATAL|fatal (message|breadcrumb)|MISSING GAME DATA' "$LOG" 2>/dev/null; then
		echo "==> fatal before menu" >&2
		cp -f "$LOG" "$SHOT_DIR/dolphin.log"
		exit 1
	fi
	(( elapsed >= 90 )) && break
	sleep 1
done
(( READY )) || { echo "==> menu not ready" >&2; cp -f "$LOG" "$SHOT_DIR/dolphin.log"; exit 1; }
sleep 3

python3 - "$SHOT_DIR" "$PIPE" "$LOG" "$DPID" <<'PY'
import os, sys, time, subprocess
from pathlib import Path

shot_dir = Path(sys.argv[1])
pipe_path = Path(sys.argv[2])
log_path = Path(sys.argv[3])
emulator_pid = int(sys.argv[4])

def find_gc_window():
    out = subprocess.check_output(["xwininfo", "-root", "-tree"], text=True, errors="replace")
    candidates = []
    for line in out.splitlines():
        if '("dolphin-emu" "GameCube")' not in line:
            continue
        parts = line.split()
        if not parts or not parts[0].startswith("0x"):
            continue
        wid = int(parts[0], 16)
        try:
            prop = subprocess.check_output(
                ["xprop", "-id", hex(wid), "_NET_WM_PID"],
                text=True, stderr=subprocess.DEVNULL)
        except subprocess.CalledProcessError:
            continue
        if f"= {emulator_pid}" not in prop:
            continue
        rank = 2 if "640x480" in line or "854x480" in line else 1 if "800x600" in line else 0
        candidates.append((rank, wid))
    candidates = [candidate for candidate in candidates if candidate[0] > 0]
    return max(candidates)[1] if candidates else None

def grab(name):
    wid = find_gc_window()
    if wid is None:
        print(f"no window for {name}", flush=True)
        return
    dest = shot_dir / f"{name}.png"
    subprocess.run(["import", "-window", hex(wid), str(dest)], check=False)
    print(f"shot {dest.name}", flush=True)

def pad(*lines, hold=0.12, gap=0.55):
    with open(pipe_path, "a", buffering=1) as f:
        for line in lines:
            f.write(line + "\n")
            f.flush()
        time.sleep(hold)
        for line in lines:
            if line.startswith("PRESS "):
                f.write(line.replace("PRESS ", "RELEASE ", 1) + "\n")
        f.flush()
    time.sleep(gap)

def log_has(pat: str) -> bool:
    try:
        data = log_path.read_bytes()
    except OSError:
        return False
    return pat.encode() in data.replace(b"\x00", b"")

with open(pipe_path, "a", buffering=1) as f:
    f.write("SET MAIN 0.5 0.5\nSET C 0.5 0.5\n")
    f.flush()

grab("01-main")
# New game is top item → open Create Game
pad("PRESS A", hold=0.18, gap=1.8)
grab("02-creategame")

# Default map row is Random (index 0) → Begin picks a real map.
# Focus Done (OK) — Create Game layout: Done is typically default / confirm with A.
# If focus is on maps table, move to Done via D-pad or just press A if Done is focused.
# Try: A to activate Done (many GC menus leave Done as default after open).
pad("PRESS A", hold=0.2, gap=2.0)
grab("03-after-ok")

# Wait for listen-server / map markers (solo — no clients).
deadline = time.time() + 75
markers = (
    b"Spawn Server",
    b"map loaded",
    b"MAP READY",
    b"Game started",
    b"direct map ready",
    b"Server spawned",
    b"fatal message",
    b"fatal breadcrumb",
    b"FATAL",
    b"out of memory",
)
last = b""
while time.time() < deadline:
    try:
        raw = log_path.read_bytes().replace(b"\x00", b"")
    except OSError:
        raw = b""
    if raw != last:
        for m in markers:
            if m in raw and m not in last:
                print(f"marker: {m.decode()}", flush=True)
        last = raw
        if b"FATAL" in raw or b"fatal message" in raw or b"fatal breadcrumb" in raw or b"out of memory" in raw:
            break
        if b"Game started" in raw or b"MAP READY" in raw or b"map loaded" in raw:
            # Server activation precedes client sign-on and the first playable
            # frame. Keep Dolphin alive long enough to capture the actual
            # world/HUD transition rather than the transient loading panel.
            time.sleep(12)
            break
    time.sleep(0.5)

grab("04-final")
print("tour done", flush=True)
PY

cp -f "$LOG" "$SHOT_DIR/dolphin.log"
NOTE="$DODGC_NOTES/${STAMP}-${SAFE_ROUTE}.md"
{
	echo "# Solo local Create Game — ${STAMP}"
	echo
	echo "- ISO: \`$ISO\`"
	echo "- Route: \`$SAFE_ROUTE\`"
	echo "- Expectation: solo listen server (no other connection)"
	echo "- Shots: \`playtests/screenshots/${STAMP}-${SAFE_ROUTE}/\`"
	echo
	echo "## OSReport keys"
	echo '```'
	tr -d '\000' < "$LOG" | rg -n 'maps\.lst|CreateGame|Spawn Server|map loaded|MAP READY|Game started|maxplayers|FATAL|fatal message|fatal breadcrumb|out of memory|Server IPv4|listen' | head -80 || true
	echo '```'
} | tee "$NOTE"

echo "==> note: $NOTE"
echo "==> shots: $SHOT_DIR"
ls -lh "$SHOT_DIR" | head -20
