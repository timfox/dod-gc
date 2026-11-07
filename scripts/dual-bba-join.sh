#!/usr/bin/env bash
# Dual Dolphin peer join via EthernetTapServer + local L2 hub.
# BuiltIn BBA cannot share guest UDP ports across two instances (host NAT remap).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

HOST_ISO="${1:-$DODGC_OUT/dodgc-netprobe.iso}"
CLIENT_ISO="${2:-$DODGC_OUT/dodgc-client.iso}"
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

# EXIDeviceType::EthernetTapServer = 11
EXI_TAP=11
TAP_PATH="${DODGC_TAP_PATH:-/tmp/dolphin-tap}"

STAMP="$(date +%Y%m%d-%H%M%S)"
ROUTE="dual-tap-join"
SHOT_DIR="$DODGC_SHOTS/${STAMP}-${ROUTE}"
NOTE="$DODGC_NOTES/${STAMP}-${ROUTE}.md"
HUSER="${XDG_RUNTIME_DIR:-/tmp}/dodgc-dual-host-${STAMP}"
CUSER="${XDG_RUNTIME_DIR:-/tmp}/dodgc-dual-client-${STAMP}"
HLOG="/tmp/dodgc-dual-host-${STAMP}.log"
CLOG="/tmp/dodgc-dual-client-${STAMP}.log"
HUB_LOG="/tmp/dodgc-tap-hub-${STAMP}.log"
mkdir -p "$SHOT_DIR" "$HUSER/Config" "$CUSER/Config" "$DODGC_NOTES"

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
SerialPort1 = ${EXI_TAP}
BBA_TAPSERVER_DESTINATION = ${TAP_PATH}
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

HPID= CPID= HUBPID=
cleanup() {
	[[ -n "${CPID:-}" ]] && kill "$CPID" 2>/dev/null || true
	[[ -n "${HPID:-}" ]] && kill "$HPID" 2>/dev/null || true
	[[ -n "${HUBPID:-}" ]] && kill "$HUBPID" 2>/dev/null || true
	[[ -n "${CPID:-}" ]] && wait "$CPID" 2>/dev/null || true
	[[ -n "${HPID:-}" ]] && wait "$HPID" 2>/dev/null || true
	[[ -n "${HUBPID:-}" ]] && wait "$HUBPID" 2>/dev/null || true
	rm -f -- "$TAP_PATH" 2>/dev/null || true
}
trap cleanup EXIT

pkill -f dolphin-emu 2>/dev/null || true
pkill -f dolphin-tap-hub.py 2>/dev/null || true
sleep 1
rm -f -- "$TAP_PATH"

python3 "$SCRIPT_DIR/dolphin-tap-hub.py" --path "$TAP_PATH" --verbose >"$HUB_LOG" 2>&1 &
HUBPID=$!
sleep 0.4
kill -0 "$HUBPID" 2>/dev/null || { echo "error: tap hub failed"; cat "$HUB_LOG"; exit 1; }

echo "==> tap hub $TAP_PATH hostISO=$HOST_ISO"
"$DOLPHIN" -u "$HUSER" -e "$HOST_ISO" >"$HLOG" 2>&1 &
HPID=$!

HOST_READY=0
for _ in $(seq 1 90); do
	if ! kill -0 "$HPID" 2>/dev/null; then
		echo "==> host Dolphin exited early" >&2
		break
	fi
	if rg -a -q 'Server IPv4 address 10\.0\.0\.2:27015|solo listenserver active|MAP READY' "$HLOG" 2>/dev/null; then
		HOST_READY=1
		break
	fi
	if rg -a -q 'FATAL|out of memory' "$HLOG" 2>/dev/null; then
		echo "==> host fatal" >&2
		break
	fi
	sleep 1
done
echo "==> host_ready=$HOST_READY"
sleep 2

echo "==> client ISO=$CLIENT_ISO"
"$DOLPHIN" -u "$CUSER" -e "$CLIENT_ISO" >"$CLOG" 2>&1 &
CPID=$!

# Observe handshake markers (strict — no false positives from argv/HPAK text).
JOIN=0
PROGRESS=0
for _ in $(seq 1 90); do
	if rg -a -q 'peer connect queue' "$CLOG" 2>/dev/null; then
		PROGRESS=1
	fi
	if rg -a -q 'Connected to|Connection accepted|SV_ConnectClient|client connected|CL_ServerInfo|signon' "$CLOG" "$HLOG" 2>/dev/null; then
		JOIN=1
		break
	fi
	if rg -a -q 'FATAL|out of memory' "$CLOG" 2>/dev/null; then
		break
	fi
	sleep 1
done
sleep 6

cp -f "$HLOG" "$SHOT_DIR/host-dolphin.log"
cp -f "$CLOG" "$SHOT_DIR/client-dolphin.log"
cp -f "$HUB_LOG" "$SHOT_DIR/tap-hub.log"

{
	echo "# Dual TapServer join — ${STAMP}"
	echo
	echo "- Host ISO: \`$HOST_ISO\`"
	echo "- Client ISO: \`$CLIENT_ISO\`"
	echo "- EXI: EthernetTapServer (11) → \`$TAP_PATH\`"
	echo "- host_ready=$HOST_READY progress=$PROGRESS join=$JOIN"
	echo
	echo "## Hub"
	echo '```'
	tail -20 "$HUB_LOG" || true
	echo '```'
	echo
	echo "## Host OSReport keys"
	echo '```'
	tr -d '\000' < "$HLOG" | rg -n 'net ready|Server IPv4|listenserver|MAP READY|Game started|Connect|challenge|client connected|SV_|FATAL|out of memory|NET_Queue' | head -100 || true
	echo '```'
	echo
	echo "## Client OSReport keys"
	echo '```'
	tr -d '\000' < "$CLOG" | rg -n 'net ready|peer connect|server |Connecting|Connected|challenge|rejected|timeout|FATAL|out of memory|gcnetip' | head -100 || true
	echo '```'
} | tee "$NOTE"

echo "==> note: $NOTE"
[[ "$JOIN" -eq 1 ]]
