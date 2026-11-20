#!/usr/bin/env bash
# create-local-match.sh inside one Dolphin queue/lock slot, headless on a private Xvfb display.
#   scripts/create-match-locked.sh [MENU_ISO] [MAX_SECONDS]
# Queue protocol (/tmp/dolphin-ceres.queue, /tmp/dolphin-ceres.lock): append this thread's name, wait outside the lock
# until first in line and mkdir succeeds, write pid/start/owner files, run under MAX_SECONDS (default 230, under the
# 4 minute rule), release in an exit trap, then wait 2 minutes before another run. Stills land under
# ~/Desktop/xash3d-gc/output/media/<stamp>-local-create-match/. Needs Xvfb, xwininfo, xprop and ImageMagick import.
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ISO="${1:-}"
MAX="${2:-230}"
ME="${DODGC_QUEUE_NAME:-memory-budget}"
Q=/tmp/dolphin-ceres.queue
L=/tmp/dolphin-ceres.lock
XDISP="${DODGC_XVFB_DISPLAY:-:97}"
XPID=
MPID=
HAVE_LOCK=0

cleanup() {
	[[ -n "$MPID" ]] && kill "$MPID" 2>/dev/null
	[[ -n "$XPID" ]] && kill "$XPID" 2>/dev/null
	if (( HAVE_LOCK )) && [[ -f "$L/owner" ]] && grep -q "$ME" "$L/owner" 2>/dev/null; then
		rm -f "$L/owner" "$L/pid" "$L/start"
		rmdir "$L" 2>/dev/null
	fi
	if [[ -f "$Q" ]]; then
		grep -vx "$ME" "$Q" > "$Q.$$" 2>/dev/null
		cat "$Q.$$" > "$Q" 2>/dev/null
		rm -f "$Q.$$"
	fi
}
trap cleanup EXIT INT TERM

for tool in Xvfb xwininfo xprop import; do
	command -v "$tool" >/dev/null 2>&1 || { echo "error: $tool not found" >&2; exit 2; }
done
[[ -n "$ISO" && -f "$ISO" ]] || { echo "usage: $0 MENU_ISO [MAX_SECONDS]" >&2; exit 2; }

grep -qx "$ME" "$Q" 2>/dev/null || echo "$ME" >> "$Q"
while true; do
	if [[ "$(head -n1 "$Q" 2>/dev/null)" == "$ME" ]] && mkdir "$L" 2>/dev/null; then
		HAVE_LOCK=1
		break
	fi
	sleep 15
done
echo "$ME (pid $$)" > "$L/owner"; echo $$ > "$L/pid"; date +%s > "$L/start"
grep -vx "$ME" "$Q" > "$Q.$$"; cat "$Q.$$" > "$Q"; rm -f "$Q.$$"

Xvfb "$XDISP" -screen 0 1280x1024x24 +extension GLX >/dev/null 2>&1 &
XPID=$!
sleep 2
export DISPLAY="$XDISP"
export DODGC_SHOTS="${DODGC_SHOTS:-$HOME/Desktop/xash3d-gc/output/media}"
export DODGC_DOLPHIN_BIN="${DODGC_DOLPHIN_BIN:-/home/tim/xash3d-gc-hl1/3rdparty/dolphin/build/Binaries/xash-gc-dolphin}"
timeout "$MAX" bash "$SCRIPT_DIR/create-local-match.sh" "$ISO" &
MPID=$!
wait "$MPID"
rc=$?
MPID=
echo "==> create-match-locked: create-local-match rc=$rc (lock released on exit; wait 2 minutes before the next run)"
exit "$rc"
