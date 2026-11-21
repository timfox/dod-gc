#!/usr/bin/env bash
# Start the ceres-side watcher for the live disc. Dolphin is started by YOU (one window,
# stays open): see the command printed below. usage: live_session.sh [iso]
ISO="${1:-/home/tim/gsbridge/live_full.iso}"; HERE="$(cd "$(dirname "$0")" && pwd)"
pkill -f "watch.py --iso $ISO" 2>/dev/null
mkdir -p ~/.cache/gsbridge
nohup python3 "$HERE/watch.py" --iso "$ISO" --work ~/.cache/gsbridge >> ~/.cache/gsbridge/watch.log 2>&1 &
echo "watcher pid $! on $ISO (log ~/.cache/gsbridge/watch.log)"
echo "Launch Dolphin on THE SAME disc (the disc must be booted fresh once):"
echo "EGL_PLATFORM=x11 QT_QPA_PLATFORM=xcb /home/tim/xash3d-gc-hl1/3rdparty/dolphin/build/Binaries/xash-gc-dolphin -e $ISO"
