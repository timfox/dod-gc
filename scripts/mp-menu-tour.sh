#!/usr/bin/env bash
# Walk HLSDK MP main-menu items under DoD assets via Dolphin pad pipe; dump screenshots.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

ISO="${1:-$DODGC_OUT/dodgc-menu.iso}"
[[ -f "$ISO" ]] || ISO="$DODGC_ISO"
[[ -f "$ISO" ]] || { echo "error: ISO not found" >&2; exit 1; }

DOLPHIN="${DODGC_DOLPHIN_BIN:-}"
if [[ -n "$DOLPHIN" && ! -x "$DOLPHIN" ]]; then
	echo "error: DODGC_DOLPHIN_BIN is not executable: $DOLPHIN" >&2
	exit 2
elif [[ -n "$DOLPHIN" ]]; then
	:
elif [[ -x "$DODGC_ENGINE/3rdparty/dolphin/build/Binaries/dolphin-emu" ]]; then
	DOLPHIN="$DODGC_ENGINE/3rdparty/dolphin/build/Binaries/dolphin-emu"
elif command -v dolphin-emu >/dev/null 2>&1; then
	DOLPHIN="$(command -v dolphin-emu)"
else
	echo "error: Dolphin not found" >&2
	exit 2
fi

STAMP="$(date +%Y%m%d-%H%M%S)"
SHOT_DIR="$DODGC_SHOTS/${STAMP}-hlsdk-mp-tour"
USER_DIR="${XDG_RUNTIME_DIR:-/tmp}/dodgc-mptour-${STAMP}"
LOG="/tmp/dodgc-mptour-${STAMP}.log"
PIPE="$USER_DIR/Pipes/gcpad"
mkdir -p "$SHOT_DIR" "$USER_DIR/Config" "$USER_DIR/Pipes" "$DODGC_NOTES"

PROFILE="${XDG_DATA_HOME:-$HOME/.local/share}/dod-gc/dolphin"
[[ -f "$PROFILE/Config/GFX.ini" ]] && cp -f "$PROFILE/Config/GFX.ini" "$USER_DIR/Config/"

BBA_EXI="${DODGC_BBA_EXI:-12}"
cat > "$USER_DIR/Config/Dolphin.ini" <<EOF
[Core]
CPUCore = 1
CPUThread = True
DSPHLE = True
FastDiscSpeed = True
MMU = True
AccurateCPUCache = True
SIDevice0 = 6
SlotB = ${BBA_EXI}
SerialPort1 = ${BBA_EXI}
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
SP1 = True
EXI = True
[Options]
Verbosity = 4
WriteToConsole = True
WriteToFile = True
WriteToWindow = False
EOF

if [[ ! -f "$USER_DIR/Config/GFX.ini" ]]; then
	cat > "$USER_DIR/Config/GFX.ini" <<'EOF'
[Settings]
Backend = OGL
ShowFPS = True
EOF
fi

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

echo "==> MP tour ISO=$ISO (pad pipe + video)"
rm -f "$LOG"
# Open pipe RW so Dolphin's reader + our writer don't block forever.
exec 3<>"$PIPE"
PAD_FD=3

if command -v ffmpeg >/dev/null 2>&1 && command -v pactl >/dev/null 2>&1; then
	SINK="dodgcmptour$$"
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
		--pid "$DPID" --sink "$SINK" --route "hlsdk-mp-tour" --disc "$ISO" \
		--window-timeout 90 &
	REC_PID=$!
	echo "==> recording pid=$REC_PID sink=$SINK"
fi

# Wait for menu ready.
START_TS=$(date +%s)
READY=0
while kill -0 "$DPID" 2>/dev/null; do
	elapsed=$(( $(date +%s) - START_TS ))
	if rg -a -q 'gc_menu_ready set 1' "$LOG" 2>/dev/null; then
		READY=1
		break
	fi
	if rg -a -q 'FATAL|MISSING GAME DATA' "$LOG" 2>/dev/null; then
		echo "==> fatal before menu" >&2
		cp -f "$LOG" "$SHOT_DIR/dolphin.log"
		exit 1
	fi
	(( elapsed >= 90 )) && { echo "==> menu timeout" >&2; break; }
	sleep 1
done
(( READY )) || { echo "==> menu not ready" >&2; cp -f "$LOG" "$SHOT_DIR/dolphin.log"; tail -40 "$LOG"; exit 1; }
# Let CopyDisp / chrome settle.
sleep 3

python3 - "$SHOT_DIR" "$PIPE" "$USER_DIR" <<'PY'
import os, sys, time, subprocess
from pathlib import Path

shot_dir = Path(sys.argv[1])
pipe_path = Path(sys.argv[2])
user_dir = sys.argv[3]

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
        # Only accept Dolphin's native guest render window. The 800x600
        # launcher can exist before the GameCube video output is available.
        if "640x480" not in line and "854x480" not in line:
            continue
        prop = subprocess.run(
            ["xprop", "-id", hex(wid), "_NET_WM_PID"],
            text=True, capture_output=True, check=False).stdout
        try:
            owner = int(prop.split("=", 1)[1].strip())
        except (IndexError, ValueError):
            continue
        args = subprocess.run(
            ["ps", "-p", str(owner), "-o", "args="],
            text=True, capture_output=True, check=False).stdout
        if user_dir in args:
            candidates.append(wid)
    return candidates[-1] if candidates else None

def grab(name):
    wid = find_gc_window()
    if wid is None:
        print(f"no window for {name}", flush=True)
        return
    dest = shot_dir / f"{name}.png"
    subprocess.run(["import", "-window", hex(wid), str(dest)], check=False)
    print(f"shot {dest.name}", flush=True)

def pad(*lines, hold=0.12, gap=0.55):
    # Keep FIFO open for the whole tour (opened by bash fd 3); append writes.
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

# Neutral stick
with open(pipe_path, "a", buffering=1) as f:
    # Pipe axes use signed -1..1 values; zero is neutral (0.5 drifts upward).
    f.write("SET MAIN 0 0\nSET C 0 0\n")
    f.flush()

# Day of Defeat main menu: New / Find servers / Options (Quit hidden).
shots = []
grab("01-main-newgame"); shots.append("01")

pad("PRESS D_DOWN")
grab("02-main-findservers")

pad("PRESS D_DOWN")
grab("03-main-options")

# Back to New game
pad("PRESS D_UP")
pad("PRESS D_UP")
grab("04-main-newgame-again")

# Open Create Game (New game → A)
pad("PRESS A", hold=0.15, gap=1.4)
grab("05-creategame")

# Back
pad("PRESS B", hold=0.15, gap=1.2)
grab("06-back-main")

# Options / Configuration BEFORE Find servers (HTTP masterlist currently fatals)
pad("PRESS D_DOWN", gap=0.5)
pad("PRESS D_DOWN", gap=0.5)
pad("PRESS A", hold=0.15, gap=1.6)
grab("07-options")

pad("PRESS B", hold=0.15, gap=1.2)
grab("08-back-main2")

# Find servers last — may fatal on HTTP alloc until BBA/net pool is wired
pad("PRESS D_UP", gap=0.5)
pad("PRESS A", hold=0.15, gap=2.0)
grab("09-findservers")


print("tour done", flush=True)
PY

if [[ ! -f "$SHOT_DIR/05-creategame.png" ]]; then
	cp -f "$LOG" "$SHOT_DIR/dolphin.log"
	echo "==> native guest frame unavailable; Create Game tour is unverified" >&2
	exit 3
fi

cp -f "$LOG" "$SHOT_DIR/dolphin.log"
kill "$DPID" 2>/dev/null || true
wait "$DPID" 2>/dev/null || true
DPID=
[[ -n "${REC_PID:-}" ]] && { timeout 90 tail --pid="$REC_PID" -f /dev/null 2>/dev/null || true; REC_PID=; }
[[ -n "${SINK_MOD:-}" ]] && { python3 "$REC_PY" sink-destroy "$SINK_MOD" || true; SINK_MOD=; }
python3 "$REC_PY" index >/dev/null || true

NOTE="$DODGC_NOTES/${STAMP}-hlsdk-mp-tour.md"
{
	echo "# HLSDK MP tour (DoD assets) — $STAMP"
	echo
	echo "- ISO: \`$ISO\`"
	echo "- Pad: Dolphin \`Pipe/0/gcpad\`"
	echo "- Shots: \`playtests/screenshots/${STAMP}-hlsdk-mp-tour/\`"
	echo "- Video: \`playtests/videos/*-hlsdk-mp-tour-*.mkv\` (see INDEX.md)"
	echo "- Flow: New game → CreateGame, Find servers → InternetGames, Options (Quit hidden)"
	echo
	echo "## Frame hashes"
	echo '```'
	python3 - <<PY
from pathlib import Path
import hashlib
d=Path("$SHOT_DIR")
for p in sorted(d.glob("*.png")):
    print(f"{p.name}: md5={hashlib.md5(p.read_bytes()).hexdigest()[:10]}")
PY
	echo '```'
	echo
	echo "## OSReport keys"
	echo '```'
	rg -a -n "gc_menu_ready|steam background|mainui key|probe menu|CreateGame|Internet|xfb_k1|CopyDisp ok|presents=|FATAL|MISSING" "$LOG" 2>/dev/null | head -100 || true
	echo '```'
} > "$NOTE"

echo "==> note: $NOTE"
echo "==> shots: $SHOT_DIR"
ls -lh "$SHOT_DIR"
python3 - <<PY
from pathlib import Path
import hashlib
d=Path("$SHOT_DIR")
prev=None
for p in sorted(d.glob("*.png")):
    h=hashlib.md5(p.read_bytes()).hexdigest()[:10]
    same=" (DUP)" if h==prev else ""
    print(f"{p.name}: {h}{same}")
    prev=h
PY
