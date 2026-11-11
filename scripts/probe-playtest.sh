#!/usr/bin/env bash
# Timed Dolphin probe: video + milestone screenshots + OSReport note into playtests/.
# Non-interactive — suitable for progress logging during development.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

usage() {
	cat <<'EOF'
usage: scripts/probe-playtest.sh [options] [ISO]

  --route NAME       recording route label (default: probe)
  --duration SEC     max wall time before killing Dolphin (default: 45)
  --note TEXT        note body (default: auto summary from log)
  --skip-video       screenshots + note only
  --ready-regex RE   stop early once OSReport matches (default: menu ready)
  -h, --help

Artifacts land under playtests/{screenshots,videos,notes}/ — never HL1 .ai/.
EOF
}

ROUTE="probe"
DURATION=45
NOTE=""
RECORD=1
# Menu/hand-off markers only — do NOT include CopyDisp (fires during intro AVI).
READY_RE='gc_menu_ready set 1|retail menu steam background ready|FATAL|MISSING GAME DATA'
ISO="$DODGC_ISO"

while [[ $# -gt 0 ]]; do
	case "$1" in
		--route) ROUTE="${2:-probe}"; shift ;;
		--duration) DURATION="${2:-45}"; shift ;;
		--note) NOTE="${2:-}"; shift ;;
		--skip-video) RECORD=0 ;;
		--ready-regex) READY_RE="${2:-}"; shift ;;
		-h|--help) usage; exit 0 ;;
		-*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
		*) ISO="$(readlink -f "$1")" ;;
	esac
	shift
done

[[ -f "$ISO" ]] || { echo "error: ISO not found: $ISO" >&2; exit 1; }

DOLPHIN="${DODGC_DOLPHIN_BIN:-}"
if [[ -n "$DOLPHIN" && ! -x "$DOLPHIN" ]]; then
	echo "error: DODGC_DOLPHIN_BIN is not executable: $DOLPHIN" >&2
	exit 2
fi
if [[ -n "$DOLPHIN" ]]; then
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
SAFE_ROUTE="$(echo "$ROUTE" | tr -cs 'A-Za-z0-9._-' '-' | sed 's/^-//;s/-$//')"
SHOT_DIR="$DODGC_SHOTS/${STAMP}-${SAFE_ROUTE}"
USER_DIR="${XDG_RUNTIME_DIR:-/tmp}/dodgc-probe-${STAMP}"
LOG="/tmp/dodgc-probe-${STAMP}.log"
mkdir -p "$SHOT_DIR" "$USER_DIR/Config" "$DODGC_VIDEOS" "$DODGC_NOTES"

# Prefer isolated DoD Dolphin profile; seed pads from HL1 once.
PROFILE="${XDG_DATA_HOME:-$HOME/.local/share}/dod-gc/dolphin"
if [[ -f "$PROFILE/Config/GCPadNew.ini" ]]; then
	cp -f "$PROFILE/Config/GCPadNew.ini" "$USER_DIR/Config/" 2>/dev/null || true
fi
# Carry known-good GFX from last interactive profile if present.
if [[ -f "$PROFILE/Config/GFX.ini" ]]; then
	cp -f "$PROFILE/Config/GFX.ini" "$USER_DIR/Config/" 2>/dev/null || true
fi

# EXIDeviceType::EthernetBuiltIn = 12 — retail BBA is Slot B; Swiss eth2sp1 is SP1.
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
UsePanicHandlers = False
[Display]
KeepWindowOnTop = False
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

export EGL_PLATFORM=x11 QT_QPA_PLATFORM=xcb
unset WAYLAND_DISPLAY
: "${DISPLAY:=:0}"
export DISPLAY

REC_PY="$SCRIPT_DIR/record-session.py"
SINK=
SINK_MOD=
REC_PID=
DPID=

cleanup() {
	[[ -n "${DPID:-}" ]] && kill "$DPID" 2>/dev/null || true
	[[ -n "${DPID:-}" ]] && wait "$DPID" 2>/dev/null || true
	[[ -n "${REC_PID:-}" ]] && { timeout 90 tail --pid="$REC_PID" -f /dev/null 2>/dev/null || true; }
	[[ -n "${SINK_MOD:-}" ]] && python3 "$REC_PY" sink-destroy "$SINK_MOD" || true
}
trap cleanup EXIT

rm -f "$LOG"
if (( RECORD )) && command -v ffmpeg >/dev/null 2>&1 && command -v pactl >/dev/null 2>&1; then
	SINK="dodgcprobe$$"
	if SINK_MOD="$(python3 "$REC_PY" sink-create "$SINK")"; then
		export PULSE_SINK="$SINK"
	else
		SINK=; SINK_MOD=
	fi
fi

echo "==> probe $ISO route=$SAFE_ROUTE duration=${DURATION}s"
"$DOLPHIN" -u "$USER_DIR" -e "$ISO" >"$LOG" 2>&1 &
DPID=$!

if [[ -n "$SINK_MOD" ]]; then
	python3 "$REC_PY" record \
		--pid "$DPID" --sink "$SINK" --route "$SAFE_ROUTE" --disc "$ISO" \
		--window-timeout 90 &
	REC_PID=$!
fi

grab() {
	local name=$1
	local win=""
	win="$(python3 - "$DPID" <<'PY'
import subprocess, sys
pid = int(sys.argv[1])
tree = subprocess.run(["xwininfo", "-root", "-tree"], text=True, capture_output=True).stdout
candidates = []
for line in tree.splitlines():
    if '("dolphin-emu" "GameCube")' not in line and '("xash-gc-dolphin" "GameCube")' not in line:
        continue
    parts = line.split()
    if not parts or not parts[0].startswith("0x"):
        continue
    if "640x480" not in line and "854x480" not in line:
        continue
    wid = int(parts[0], 16)
    prop = subprocess.run(["xprop", "-id", hex(wid), "_NET_WM_PID"], text=True, capture_output=True).stdout
    if f"= {pid}" in prop:
        candidates.append(wid)
if candidates:
    print(hex(candidates[-1]))
PY
	)"
	if [[ -n "${win:-}" ]] && command -v import >/dev/null 2>&1; then
		import -window "$win" "$SHOT_DIR/${name}.png" 2>/dev/null || true
	else
		echo "==> no guest render window for $name (launcher windows are ignored)"
	fi
}

START_TS=$(date +%s)
READY=0
INTRO_SHOT=0
MENU_SHOT=0
while kill -0 "$DPID" 2>/dev/null; do
	now=$(date +%s)
	elapsed=$((now - START_TS))
	if (( elapsed >= DURATION )); then
		echo "==> duration reached (${DURATION}s)"
		break
	fi
	if (( ! INTRO_SHOT )) && rg -a -q "intro AVI decoded first frame|intro AVI uploaded frame=" "$LOG" 2>/dev/null; then
		sleep 0.8
		grab intro
		INTRO_SHOT=1
		echo "==> shot intro"
	fi
	if (( ! MENU_SHOT )) && rg -a -q "gc_menu_ready set 1|retail menu steam background ready" "$LOG" 2>/dev/null; then
		sleep 2
		grab menu
		sleep 1
		grab menu-2
		MENU_SHOT=1
		echo "==> shot menu"
	fi
	if (( ! READY )) && rg -a -q "$READY_RE" "$LOG" 2>/dev/null; then
		READY=1
		if rg -a -q "FATAL|MISSING GAME DATA" "$LOG" 2>/dev/null; then
			sleep 2
			grab final
			break
		fi
	fi
	# Skip-intro menu routes can finish shortly after menu chrome is up.
	if (( READY && MENU_SHOT )) && [[ "$SAFE_ROUTE" == menu || "$SAFE_ROUTE" == menu-* ]]; then
		sleep 3
		grab final
		break
	fi
	# Intro routes: hold through AVI + menu handoff, then capture and stop.
	if (( READY && MENU_SHOT && elapsed >= 22 )); then
		grab final
		break
	fi
	sleep 1
done

# Final grab if we never hit menu markers.
(( MENU_SHOT )) || grab final
kill "$DPID" 2>/dev/null || true
wait "$DPID" 2>/dev/null || true
DPID=
[[ -n "${REC_PID:-}" ]] && { timeout 90 tail --pid="$REC_PID" -f /dev/null 2>/dev/null || true; REC_PID=; }
[[ -n "${SINK_MOD:-}" ]] && { python3 "$REC_PY" sink-destroy "$SINK_MOD" || true; SINK_MOD=; }

cp -f "$LOG" "$SHOT_DIR/dolphin.log"
python3 "$REC_PY" index >/dev/null || true

# Frame stats
STATS="$(python3 - <<PY
from pathlib import Path
try:
	from PIL import Image
except ImportError:
	print('(no pillow)')
	raise SystemExit
d=Path('$SHOT_DIR')
for p in sorted(d.glob('*.png')):
	im=Image.open(p).convert('RGB')
	px=list(im.getdata()); n=len(px)
	black=sum(1 for r,g,b in px if r+g+b<16)/n
	mean=tuple(sum(c[i] for c in px)//n for i in range(3))
	print(f'{p.name}: {im.size} black={black:.1%} mean={mean}')
PY
)"

KEY_LOG="$(rg -a -n "linked gamedir|FS_Rescan|released unused|intro AVI (play|decoded|progress)|steam background|gc_menu_ready|CopyDisp ok|FATAL|MISSING|Day of Defeat|map |avail=" "$LOG" 2>/dev/null | head -60 || true)"

NOTE_FILE="$DODGC_NOTES/${STAMP}-${SAFE_ROUTE}.md"
{
	echo "# Probe ${SAFE_ROUTE} — $STAMP"
	echo
	echo "- ISO: \`$ISO\`"
	echo "- Route: \`$SAFE_ROUTE\`"
	echo "- Duration cap: ${DURATION}s"
	echo "- Repo: \`$(dodgc_repo_sha)\`"
	echo "- Engine: \`$(dodgc_engine_sha)\`"
	echo "- Shots: \`playtests/screenshots/${STAMP}-${SAFE_ROUTE}/\`"
	echo
	if [[ -n "$NOTE" ]]; then
		echo "$NOTE"
		echo
	fi
	echo "## Frame stats"
	echo '```'
	echo "$STATS"
	echo '```'
	echo
	echo "## OSReport (key)"
	echo '```'
	echo "$KEY_LOG"
	echo '```'
} > "$NOTE_FILE"

echo "==> note: $NOTE_FILE"
echo "==> shots: $SHOT_DIR"
echo "$STATS"
ls -lh "$SHOT_DIR" | head -20
