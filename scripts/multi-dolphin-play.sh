#!/usr/bin/env bash
# Launch 1 host + N clients as separate Dolphin instances on the virtual LAN from multi-dolphin-net.sh.
#   scripts/multi-dolphin-play.sh HOST_ISO CLIENT_ISO [CLIENT_ISO ...]
# Each instance: own -u user dir, own MAC, SerialPort1 = EthernetTapServer. Logs in $RUN/<name>.log.
# Env: DOLPHIN (binary), DODGC_NET_RUN, DODGC_TAP_PATH, MDT_STAMP (name tag), MDT_LOCK=0 to skip ceres lock.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RUN="${DODGC_NET_RUN:-${XDG_RUNTIME_DIR:-/tmp}/dodgc-multi-net}"
TAP_PATH="${DODGC_TAP_PATH:-$RUN/dolphin-tap}"
DOLPHIN="${DOLPHIN:-$HOME/xash3d-gc-hl1/3rdparty/dolphin/build/Binaries/xash-gc-dolphin}"
STAMP="${MDT_STAMP:-$(date +%Y%m%d-%H%M%S)}"
[[ $# -ge 1 ]] || { sed -n 2,6p "$0"; exit 2; }
[[ -S "$TAP_PATH" ]] || { echo "start scripts/multi-dolphin-net.sh start first" >&2; exit 1; }
QUEUE=/tmp/dolphin-ceres.queue; LOCK=/tmp/dolphin-ceres.lock; ME="multi-dolphin online test"
if [[ "${MDT_LOCK:-1}" == 1 ]]; then
	# ceres rule: queue first, only the head of the queue may take the lock, hold < 4 min, wait 2 min before re-queueing.
	grep -qxF "$ME" "$QUEUE" 2>/dev/null || echo "$ME" >> "$QUEUE"
	until [[ "$(head -1 "$QUEUE" 2>/dev/null)" == "$ME" ]] && mkdir "$LOCK" 2>/dev/null; do echo "waiting in $QUEUE / $LOCK"; sleep 15; done
	sed -i "\|^$ME\$|d" "$QUEUE"
	echo "$ME (pid $$)" > "$LOCK/owner"; echo $$ > "$LOCK/pid"; date +%s > "$LOCK/start"
fi
# Hold the lock for one run under 4 minutes (override with MDT_MAX_SECS for unlocked runs).
( sleep "${MDT_MAX_SECS:-230}"; kill $$ 2>/dev/null ) &
PIDS=()
cleanup() {
	for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null || true; done
	[[ "${MDT_LOCK:-1}" == 1 ]] && { rm -f $LOCK/owner $LOCK/pid $LOCK/start; rmdir $LOCK 2>/dev/null || true; }
}
trap cleanup EXIT INT TERM
export EGL_PLATFORM=x11 QT_QPA_PLATFORM=xcb DISPLAY="${DISPLAY:-:0}"; unset WAYLAND_DISPLAY
i=0
for iso in "$@"; do
	name=$([[ $i == 0 ]] && echo host || echo "client$i")
	u="$RUN/user-$name"; mkdir -p "$u/Config"
	cat > "$u/Config/Dolphin.ini" <<EOF
[Core]
CPUCore = 1
CPUThread = True
DSPHLE = True
FastDiscSpeed = True
MMU = True
SIDevice0 = 6
SlotB = 255
SerialPort1 = 11
BBA_TAPSERVER_DESTINATION = $TAP_PATH
BBA_MAC = 00:09:bf:00:00:0$((i+2))
[DSP]
Muted = True
Volume = 0
[Interface]
ConfirmStop = False
EOF
	printf '[Logs]\nBOOT = True\nOSREPORT = True\nOSREPORT_HLE = True\nSP1 = True\n[Options]\nVerbosity = 4\nWriteToFile = True\nWriteToConsole = True\n' > "$u/Config/Logger.ini"
	printf '[Settings]\nBackend = OGL\nShowFPS = True\n' > "$u/Config/GFX.ini"
	stdbuf -oL -eL "$DOLPHIN" -u "$u" -v OGL -e "$iso" >"$RUN/$name.log" 2>&1 &
	PIDS+=($!); echo "$name pid=$! iso=$iso ip=10.0.0.$((i+2)) user=$u"
	[[ $i == 0 ]] && sleep "${MDT_HOST_HEAD_START:-60}"
	i=$((i+1))
done
echo "running; Ctrl-C (or kill $$) to stop only these instances"
wait
