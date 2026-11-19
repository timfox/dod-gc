#!/usr/bin/env bash
# Capture an episode's shots in Dolphin: one boot per map, every shot on that map played back to back by
# the engine's cinematic camera (xash3d-gc cine_play), frames dumped by Dolphin, then assembled.
#
#   scripts/film-capture.sh film/episodes/ep01-omaha.json            # plan, capture every map, assemble
#   scripts/film-capture.sh film/episodes/ep01-omaha.json --map dod_charlie --no-assemble
#
# Game time is locked to the film frame rate while a shot plays, so a slow emulator only makes the capture
# take longer, never the film choppier. Raise FILM_IR (Dolphin internal resolution, 3 = 1920x1440) for
# sharper frames at the cost of capture time. Needs an engine with cine_play (xash3d-gc docs/CINEMATIC_CAMERA.md).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

EPISODE="${1:?usage: scripts/film-capture.sh EPISODE.json [--map MAP] [--no-assemble] [--burn-subs]}"
shift
ONLY_MAP=""
ASSEMBLE=1
BURN=()
while [[ $# -gt 0 ]]; do
	case "$1" in
		--map) ONLY_MAP="${2:?--map needs a map}"; shift ;;
		--no-assemble) ASSEMBLE=0 ;;
		--burn-subs) BURN=(--burn-subs) ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done

FILM_IR="${FILM_IR:-3}"
FILM_TIMEOUT="${FILM_TIMEOUT:-3600}"
EP_ID="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["id"])' "$EPISODE")"
PLAN="$DODGC_OUT/film/$EP_ID"

python3 "$DODGC_ROOT/tools/film/film.py" plan "$EPISODE" --out "$PLAN"
mapfile -t MAPS < <(python3 -c 'import json,sys; print("\n".join(json.load(open(sys.argv[1]))["sessions"]))' "$PLAN/plan.json")

dodgc_require_content
[[ -f "$DODGC_DOL" ]] || {
	echo "==> building DOL"
	export XASH_GAMECUBE_LINK_BBA="${XASH_GAMECUBE_LINK_BBA:-1}"
	bash "$SCRIPT_DIR/build-dodgc.sh" --dol-only --skip-hlsdk
}
if ! grep -aq "cine_play" "$DODGC_DOL"; then
	echo "error: $DODGC_DOL has no cine_play: build it from an engine with the cinematic camera" >&2
	exit 1
fi

DOLPHIN=""
if [[ -x "$DODGC_ENGINE/3rdparty/dolphin/build/Binaries/dolphin-emu" ]]; then
	DOLPHIN="$DODGC_ENGINE/3rdparty/dolphin/build/Binaries/dolphin-emu"
elif command -v dolphin-emu >/dev/null 2>&1; then
	DOLPHIN="$(command -v dolphin-emu)"
else
	echo "error: Dolphin not found" >&2
	exit 2
fi
export EGL_PLATFORM=x11 QT_QPA_PLATFORM=xcb
unset WAYLAND_DISPLAY
: "${DISPLAY:=:0}"
export DISPLAY

FAIL_RE='Xash Error:|Host_Error|fatal message=|_Mem_Realloc: out of memory|cine: (no |cine/)'
for MAP in "${MAPS[@]}"; do
	[[ -n "$ONLY_MAP" && "$MAP" != "$ONLY_MAP" ]] && continue
	echo "==> $EP_ID: capturing $MAP"
	bash "$SCRIPT_DIR/build-lean-iso.sh" --menu-only --listen "$MAP" --full-map --world-render \
		--skip-startup-vids --film "$PLAN"
	ISO="$DODGC_OUT/film-$EP_ID-$MAP.iso"
	cp -f "$DODGC_OUT/dodgc-menu.iso" "$ISO"

	STAMP="$(date +%Y%m%d-%H%M%S)"
	USER_DIR="${XDG_RUNTIME_DIR:-/tmp}/dodgc-film-${STAMP}"
	DUMP="$PLAN/captures/$MAP"
	LOG="$PLAN/capture-$MAP.log"
	rm -rf "$DUMP"
	mkdir -p "$DUMP" "$USER_DIR/Config"
	cat > "$USER_DIR/Config/Dolphin.ini" <<INI
[Core]
CPUCore = 1
CPUThread = False
DSPHLE = True
FastDiscSpeed = True
MMU = True
SIDevice0 = 6
[DSP]
Muted = True
[Interface]
ConfirmStop = False
[General]
DumpPath = $DUMP
[Movie]
DumpFrames = True
DumpFramesSilent = True
INI
	cat > "$USER_DIR/Config/GFX.ini" <<INI
[Settings]
DumpFramesAsImages = True
DumpPath = $DUMP
InternalResolution = $FILM_IR
InternalResolutionFrameDumps = True
Backend = OGL
EFBCopyEnable = True
SkipEFBAccessFromCPU = False
ShowFPS = False
INI
	cat > "$USER_DIR/Config/Logger.ini" <<'INI'
[Logs]
BOOT = True
OSREPORT = True
[Options]
Verbosity = 3
WriteToFile = True
INI
	dodgc_pkill_own_dolphin "${XDG_RUNTIME_DIR:-/tmp}/dodgc-film-"
	set +e
	"$DOLPHIN" -u "$USER_DIR" -e "$ISO" >"$LOG" 2>&1 &
	DPID=$!
	set -e
	deadline=$((SECONDS + FILM_TIMEOUT))
	done_ok=0
	while (( SECONDS < deadline )) && kill -0 "$DPID" 2>/dev/null; do
		if grep -aqE "$FAIL_RE" "$LOG" "$USER_DIR"/Logs/*.log 2>/dev/null; then
			echo "error: engine failure, see $LOG" >&2
			break
		fi
		if grep -aq "FILM SESSION DONE $MAP" "$LOG" "$USER_DIR"/Logs/*.log 2>/dev/null; then
			done_ok=1
			sleep 2
			break
		fi
		sleep 5
	done
	kill "$DPID" 2>/dev/null || true
	wait "$DPID" 2>/dev/null || true
	cat "$USER_DIR"/Logs/*.log >> "$LOG" 2>/dev/null || true
	# Dolphin writes into DumpPath/Frames on some versions, or its user dir's Dump/Frames
	for fd in "$DUMP/Frames" "$USER_DIR/Dump/Frames"; do
		if [[ -d "$fd" ]]; then
			find "$fd" -maxdepth 1 -type f -name 'framedump*' -exec mv -t "$DUMP" {} +
			rmdir "$fd" 2>/dev/null || true
		fi
	done
	n=$(find "$DUMP" -maxdepth 1 -type f -name 'framedump*' | wc -l)
	grep -aoE "CINE (START|DONE|STOP) [^\r]*" "$LOG" | sort -u | sed 's/^/    /' || true
	echo "==> $MAP: session_done=$done_ok images=$n -> $DUMP"
	(( done_ok )) || { echo "error: $MAP did not finish its shots (timeout ${FILM_TIMEOUT}s or failure)" >&2; exit 1; }
	(( n > 0 )) || { echo "error: $MAP played its shots but Dolphin dumped no frames (check [Movie] DumpFrames in $USER_DIR/Config/Dolphin.ini)" >&2; exit 1; }
done

if (( ASSEMBLE )); then
	python3 "$DODGC_ROOT/tools/film/film.py" assemble "$EPISODE" --out "$PLAN" "${BURN[@]+"${BURN[@]}"}"
fi
