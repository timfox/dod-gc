#!/usr/bin/env bash
# One boot of any disc, N screenshots, under the ceres Dolphin queue/lock rule (<4 min, exit trap, only our Dolphin).
# usage: dolphin_shot.sh <iso> <outdir> <t1> [t2 ...]   (seconds after launch)
set -u
ISO="${1:?iso}"; OUTD="${2:?outdir}"; shift 2; ME="mapbridge"; LOCK=/tmp/dolphin-ceres.lock; Q=/tmp/dolphin-ceres.queue
DOLPHIN="${DOLPHIN:-/home/tim/xash3d-gc-hl1/3rdparty/dolphin/build/Binaries/xash-gc-dolphin}"
mkdir -p "$OUTD"; UD="${SHOT_UD:-$(mktemp -d /tmp/mapbridge-ud.XXXX)}"; mkdir -p "$UD/Config" "$UD/Logs"
printf "[Options]\nWriteToFile = True\nVerbosity = 5\n[Logs]\nOSREPORT = True\n" > "$UD/Config/Logger.ini"
( flock 9; grep -qx "$ME" "$Q" 2>/dev/null || echo "$ME" >> "$Q" ) 9>"$Q.flk"
echo "[shot] queued $(date +%T)"
while :; do [ "$(head -1 "$Q" 2>/dev/null)" = "$ME" ] && mkdir "$LOCK" 2>/dev/null && break; sleep 5; done
echo "$ME" > "$LOCK/name"; echo $$ > "$LOCK/pid"; date +%s > "$LOCK/start"
cleanup() { [ -n "${DPID:-}" ] && kill "$DPID" 2>/dev/null; ( flock 9; sed -i "/^$ME\$/d" "$Q" ) 9>"$Q.flk"; rm -rf "$LOCK"; [ -z "${SHOT_UD:-}" ] && rm -rf "$UD"; }
trap cleanup EXIT INT TERM
echo "[shot] lock $(date +%T)"
shot() { W=$(xwininfo -root -tree 2>/dev/null | grep -E '"GameCube"' | grep -v 800x600 | head -1 | awk '{print $1}'); [ -n "$W" ] && /usr/bin/import -window "$W" "$OUTD/$1.png"; }
EGL_PLATFORM=x11 QT_QPA_PLATFORM=xcb env -u WAYLAND_DISPLAY "$DOLPHIN" -u "$UD" -b -C Dolphin.DSP.Backend="No Audio Output" -e "$ISO" >"$OUTD/dolphin.log" 2>&1 & DPID=$!
prev=0; for t in "$@"; do sleep $((t-prev)); prev=$t; shot "t$t"; done
cp "$UD/Logs/dolphin.log" "$OUTD/osreport.log" 2>/dev/null; echo "[shot] done $(date +%T)"
