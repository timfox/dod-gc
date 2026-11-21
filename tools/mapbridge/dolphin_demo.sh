#!/usr/bin/env bash
# Live-reload proof in Dolphin. Queue/lock rule (ceres): append name to the queue with
# flock, wait for head of queue + free lock, mkdir lock, run < 4 min, release in EXIT trap,
# only ever kill OUR OWN Dolphin. usage: dolphin_demo.sh <slot.iso> <outdir>
set -u
ISO0="${1:?slot iso}"; OUTD="${2:?outdir}"; ME="mapbridge"
HERE="$(cd "$(dirname "$0")" && pwd)"
LOCK=/tmp/dolphin-ceres.lock; Q=/tmp/dolphin-ceres.queue
DOLPHIN="${DOLPHIN:-/home/tim/xash3d-gc-hl1/3rdparty/dolphin/build/Binaries/xash-gc-dolphin}"
mkdir -p "$OUTD"; ISO="$OUTD/run.iso"; WORK="$OUTD/work"; mkdir -p "$WORK"
cp -f "$ISO0" "$ISO"
UD=$(mktemp -d /tmp/mapbridge-ud.XXXX)
[ -d "$HOME/.local/share/dod-gc/dolphin/Config" ] && cp -r "$HOME/.local/share/dod-gc/dolphin/Config" "$UD/"
( flock 9; grep -qx "$ME" "$Q" 2>/dev/null || echo "$ME" >> "$Q" ) 9>"$Q.flk"
echo "[demo] queued at $(date +%T)"
while :; do
  [ "$(head -1 "$Q" 2>/dev/null)" = "$ME" ] && mkdir "$LOCK" 2>/dev/null && break
  sleep 5
done
echo "$ME" > "$LOCK/name"; echo $$ > "$LOCK/pid"; date +%s > "$LOCK/start"
cleanup() {
  [ -n "${DPID:-}" ] && kill "$DPID" 2>/dev/null
  [ -n "${WPID:-}" ] && kill "$WPID" 2>/dev/null
  ( flock 9; sed -i "/^$ME\$/d" "$Q" ) 9>"$Q.flk"
  rm -rf "$LOCK" "$UD"
}
trap cleanup EXIT INT TERM
echo "[demo] lock taken $(date +%T)"
shot() { W=$(xwininfo -root -tree 2>/dev/null | grep -E '"GameCube"|dolphin' | grep -v 800x600 | head -1 | awk '{print $1}')
         [ -n "$W" ] && import -window "$W" "$OUTD/$1.png" 2>/dev/null || import -window root "$OUTD/$1.png"; }
python3 "$HERE/selftest_map.py" "$WORK/live.map" 96
python3 "$HERE/watch.py" --iso "$ISO" --work "$WORK" --once   # seed pass: serial 1, same room
"$DOLPHIN" -u "$UD" -e "$ISO" >"$OUTD/dolphin.log" 2>&1 & DPID=$!
sleep "${BOOT_WAIT:-75}"; shot before
# the edit: taller pillar, saved -> watcher recompiles + patches + bumps serial
python3 "$HERE/selftest_map.py" "$WORK/live.map" 176
python3 "$HERE/watch.py" --iso "$ISO" --work "$WORK" --once
sleep "${RELOAD_WAIT:-20}"; shot after
echo "[demo] done $(date +%T)"
