#!/usr/bin/env bash
# Launch dodgc.iso in Dolphin and record video + screenshots into playtests/.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

usage() {
	cat <<'EOF'
usage: scripts/playtest.sh [options] [ISO]

  --build          rebuild the interactive DoD disc first
  --record         record window+audio to playtests/videos/ (default)
  --no-record      skip video recording
  --duration SEC   auto-quit Dolphin after SEC (default: wait forever)
  --note TEXT      write a dated playtest note
  --shot           grab a window screenshot after Dolphin starts
  -h, --help

ISO defaults to out/dodgc-interactive.iso: Valve intro, DoD menu, then Create
Game on dod_flash with BSP world rendering enabled. Evidence never lands in the
HL1 .ai/ tree.
For timed milestone probes prefer: scripts/probe-playtest.sh
EOF
}

BUILD=0
RECORD=1
NOTE=""
SHOT=0
DURATION=0
ISO="$DODGC_OUT/dodgc-interactive.iso"

while [[ $# -gt 0 ]]; do
	case "$1" in
		--build) BUILD=1 ;;
		--record) RECORD=1 ;;
		--no-record) RECORD=0 ;;
		--duration)
			DURATION="${2:-0}"
			shift
			;;
		--note)
			NOTE="${2:-}"
			shift
			;;
		--shot) SHOT=1 ;;
		-h|--help) usage; exit 0 ;;
		-*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
		*) ISO="$(readlink -f "$1")" ;;
	esac
	shift
done

dodgc_require_content

if (( BUILD )) || [[ ! -f "$ISO" ]]; then
	[[ -f "$DODGC_DOL" ]] || bash "$SCRIPT_DIR/build-dodgc.sh" --dol-only
	DODGC_ISO="$ISO" bash "$SCRIPT_DIR/build-lean-iso.sh" --intro-only --map dod_flash --world-render
fi

if [[ ! -f "$ISO" ]]; then
	echo "error: ISO not found: $ISO" >&2
	echo "  run: scripts/build-dodgc.sh" >&2
	exit 1
fi

DOLPHIN=""
if [[ -x "$DODGC_ENGINE/3rdparty/dolphin/build/Binaries/dolphin-emu" ]]; then
	DOLPHIN="$DODGC_ENGINE/3rdparty/dolphin/build/Binaries/dolphin-emu"
elif command -v dolphin-emu >/dev/null 2>&1; then
	DOLPHIN="$(command -v dolphin-emu)"
elif command -v dolphin >/dev/null 2>&1; then
	DOLPHIN="$(command -v dolphin)"
else
	echo "error: Dolphin not found" >&2
	exit 2
fi

USER_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/dod-gc/dolphin"
mkdir -p "$USER_DIR/Config" "$DODGC_SHOTS" "$DODGC_VIDEOS" "$DODGC_NOTES"

# Carry controller mapping from HL1 profile once, then keep DoD isolated.
if [[ ! -f "$USER_DIR/Config/GCPadNew.ini" ]]; then
	for src in \
		"$HOME/.local/share/xash3d-gc/dolphin/Config/GCPadNew.ini" \
		"$HOME/.config/dolphin-emu/GCPadNew.ini"
	do
		[[ -f "$src" ]] && { cp "$src" "$USER_DIR/Config/GCPadNew.ini"; break; }
	done
fi

cat > "$USER_DIR/Config/Dolphin.ini" <<EOF
[Core]
CPUCore = 1
CPUThread = True
DSPHLE = True
FastDiscSpeed = True
MMU = True
AccurateCPUCache = True
SIDevice0 = 6
[DSP]
Muted = False
Volume = 100
[Interface]
ConfirmStop = False
[Display]
KeepWindowOnTop = False
EOF

STAMP="$(date +%Y%m%d-%H%M%S)"
if [[ -n "$NOTE" ]]; then
	NOTE_FILE="$DODGC_NOTES/${STAMP}-playtest.md"
	{
		echo "# Playtest $STAMP"
		echo
		echo "- ISO: \`$ISO\`"
		echo "- Engine: \`$(dodgc_engine_sha)\`"
		echo "- Repo: \`$(dodgc_repo_sha)\`"
		echo
		echo "$NOTE"
	} > "$NOTE_FILE"
	echo "==> note: $NOTE_FILE"
fi

export EGL_PLATFORM=x11 QT_QPA_PLATFORM=xcb
unset WAYLAND_DISPLAY

REC_PY="$SCRIPT_DIR/record-session.py"
SINK=
SINK_MOD=
REC_PID=

cleanup() {
	[[ -n "${REC_PID:-}" ]] && { timeout 120 tail --pid="$REC_PID" -f /dev/null 2>/dev/null || true; }
	[[ -n "${SINK_MOD:-}" ]] && python3 "$REC_PY" sink-destroy "$SINK_MOD" || true
}
trap cleanup EXIT

if (( RECORD )) && command -v ffmpeg >/dev/null 2>&1 && command -v pactl >/dev/null 2>&1; then
	SINK="dodgcplay$$"
	if SINK_MOD="$(python3 "$REC_PY" sink-create "$SINK")"; then
		export PULSE_SINK="$SINK"
	else
		SINK=
		SINK_MOD=
	fi
fi

echo "==> playtest $ISO (record=$([[ $RECORD == 1 ]] && echo yes || echo no))"
"$DOLPHIN" -u "$USER_DIR" -e "$ISO" &
DOLPHIN_PID=$!

if [[ -n "$SINK_MOD" ]]; then
	python3 "$REC_PY" record --pid "$DOLPHIN_PID" --sink "$SINK" --route play --disc "$ISO" &
	REC_PID=$!
fi

if (( SHOT )) && command -v import >/dev/null 2>&1; then
	(
		# The interactive disc plays the 10-second Valve intro before the menu.
		sleep 16
		SHOT_PATH="$DODGC_SHOTS/${STAMP}-dolphin.png"
		# Prefer GameCube render window over full desktop.
		WIN=$(xwininfo -root -tree 2>/dev/null | rg '\("dolphin-emu" "GameCube"\)' | rg -v '800x600' | head -1 | awk '{print $1}')
		if [[ -n "${WIN:-}" ]]; then
			import -window "$WIN" "$SHOT_PATH" 2>/dev/null || true
		else
			import -window root "$SHOT_PATH" 2>/dev/null || true
		fi
		[[ -f "$SHOT_PATH" ]] && echo "==> screenshot: $SHOT_PATH"
	) &
fi

if (( DURATION > 0 )); then
	(
		sleep "$DURATION"
		kill "$DOLPHIN_PID" 2>/dev/null || true
	) &
fi

wait "$DOLPHIN_PID" || true
python3 "$REC_PY" index >/dev/null
echo "==> evidence under $DODGC_PLAYTESTS"
