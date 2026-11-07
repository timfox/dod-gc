#!/usr/bin/env bash
# Dual Dolphin BBA/IPC play session — leaves both guests running for manual play.
# Unlike dual-bba-ipc-join.sh, this does not auto-quit after signon evidence.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

HOST_ISO="${1:-$DODGC_OUT/dodgc-flash-host.iso}"
CLIENT_ISO="${2:-$DODGC_OUT/dodgc-flash-client.iso}"
[[ -f "$HOST_ISO" ]] || { echo "error: host ISO missing: $HOST_ISO" >&2; exit 1; }
[[ -f "$CLIENT_ISO" ]] || { echo "error: client ISO missing: $CLIENT_ISO" >&2; exit 1; }

DOLPHIN=""
if [[ -x "$DODGC_ENGINE/3rdparty/dolphin/build/Binaries/dolphin-emu" ]]; then
	DOLPHIN="$DODGC_ENGINE/3rdparty/dolphin/build/Binaries/dolphin-emu"
elif command -v dolphin-emu >/dev/null 2>&1; then
	DOLPHIN="$(command -v dolphin-emu)"
else
	echo "error: Dolphin not found" >&2
	exit 2
fi

EXI_IPC=14
STAMP="$(date +%Y%m%d-%H%M%S)"
HUSER="${XDG_RUNTIME_DIR:-/tmp}/dodgc-ipc-host-play-${STAMP}"
CUSER="${XDG_RUNTIME_DIR:-/tmp}/dodgc-ipc-client-play-${STAMP}"
HLOG="/tmp/dodgc-ipc-host-play-${STAMP}.log"
CLOG="/tmp/dodgc-ipc-client-play-${STAMP}.log"
mkdir -p "$HUSER/Config" "$CUSER/Config"

PROFILE="${XDG_DATA_HOME:-$HOME/.local/share}/dod-gc/dolphin"
[[ -f "$PROFILE/Config/GFX.ini" ]] && cp -f "$PROFILE/Config/GFX.ini" "$HUSER/Config/" && cp -f "$PROFILE/Config/GFX.ini" "$CUSER/Config/"

write_ini() {
	local udir=$1
	cat > "$udir/Config/Dolphin.ini" <<EOF
[Core]
CPUCore = 1
CPUThread = True
DSPHLE = True
FastDiscSpeed = True
MMU = True
AccurateCPUCache = True
SIDevice0 = 6
SlotB = 255
SerialPort1 = ${EXI_IPC}
[DSP]
Muted = True
Volume = 0
[Interface]
ConfirmStop = False
[Display]
KeepWindowOnTop = False
EOF
	cat > "$udir/Config/Logger.ini" <<'EOF'
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
	[[ -f "$udir/Config/GFX.ini" ]] || cat > "$udir/Config/GFX.ini" <<'EOF'
[Settings]
Backend = OGL
ShowFPS = True
EOF
}

write_ini "$HUSER"
write_ini "$CUSER"

export EGL_PLATFORM=x11 QT_QPA_PLATFORM=xcb
unset WAYLAND_DISPLAY
: "${DISPLAY:=:0}"
export DISPLAY

dodgc_pkill_own_dolphin "${XDG_RUNTIME_DIR:-/tmp}/dodgc-ipc-"
dodgc_pkill_own_tap_hub
sleep 1

echo "==> interactive BBA IPC host ISO=$HOST_ISO"
"$DOLPHIN" -u "$HUSER" -e "$HOST_ISO" >"$HLOG" 2>&1 &
HPID=$!

for _ in $(seq 1 90); do
	if ! kill -0 "$HPID" 2>/dev/null; then
		echo "error: host Dolphin exited early" >&2
		exit 1
	fi
	if rg -a -q 'Server IPv4 address 10\.0\.0\.2:27015|solo listenserver active|MAP READY' "$HLOG" 2>/dev/null; then
		break
	fi
	if rg -a -q 'FATAL|out of memory' "$HLOG" 2>/dev/null; then
		echo "error: host fatal — see $HLOG" >&2
		exit 1
	fi
	sleep 1
done

echo "==> host ready pid=$HPID — starting client"
"$DOLPHIN" -u "$CUSER" -e "$CLIENT_ISO" >"$CLOG" 2>&1 &
CPID=$!
echo "==> client pid=$CPID"
echo "==> logs: $HLOG / $CLOG"
echo "==> leave both Dolphin windows open to play; Ctrl-C here only stops this waiter"

# Wait forever on either guest exiting (manual close).
while kill -0 "$HPID" 2>/dev/null && kill -0 "$CPID" 2>/dev/null; do
	sleep 2
done
echo "==> a Dolphin window closed — session end"
