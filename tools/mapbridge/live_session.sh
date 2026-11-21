#!/usr/bin/env bash
# Start the ceres-side watcher for the live disc. Dolphin is started by YOU (one window,
# stays open): see the command printed below. usage: live_session.sh [iso]
ISO="${1:-/home/tim/gsbridge/live_sync.iso}"; HERE="$(cd "$(dirname "$0")" && pwd)"
pkill -f "watch.py --iso $ISO" 2>/dev/null
mkdir -p ~/.cache/gsbridge
nohup python3 "$HERE/watch.py" --iso "$ISO" --work ~/.cache/gsbridge --dolphin-log "$HOME/gsbridge/dolphin-user/Logs/dolphin.log" >> ~/.cache/gsbridge/watch.log 2>&1 &
echo "watcher pid $! on $ISO (log ~/.cache/gsbridge/watch.log)"
echo "Start Dolphin with its log where the watcher reads it (gives real \"game reloaded\" status):"
echo "$HERE/live_dolphin.sh $ISO"
