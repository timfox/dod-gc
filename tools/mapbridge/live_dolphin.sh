#!/usr/bin/env bash
# Start YOUR Dolphin on the live disc with its OSReport log in a known place, so the watcher (and the Maya panel) can tell
# when the game has really reloaded. usage: live_dolphin.sh [iso]
ISO="${1:-/home/tim/gsbridge/live_sync.iso}"; UD="$HOME/gsbridge/dolphin-user"
mkdir -p "$UD/Config" "$UD/Logs"
[ -f "$UD/Config/Logger.ini" ] || printf '[Options]\nWriteToFile = True\nVerbosity = 5\n[Logs]\nOSREPORT = True\n' > "$UD/Config/Logger.ini"
# copy your normal profile once (controller mapping, video settings)
[ -f "$UD/Config/GCPadNew.ini" ] || cp "$HOME/.local/share/dod-gc/dolphin/Config/GCPadNew.ini" "$UD/Config/" 2>/dev/null
EGL_PLATFORM=x11 QT_QPA_PLATFORM=xcb env -u WAYLAND_DISPLAY \
  /home/tim/xash3d-gc-hl1/3rdparty/dolphin/build/Binaries/xash-gc-dolphin -u "$UD" -e "$ISO"
