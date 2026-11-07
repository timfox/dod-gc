#!/usr/bin/env bash
# Dual Dolphin peer join via Broadband Adapter (IPC).
# Same-machine L2 without TapServer (guest TX was silent on TapServer) or
# BuiltIn host-UDP port remap.
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

# EXIDeviceType::EthernetIPC = 14
EXI_IPC=14

STAMP="$(date +%Y%m%d-%H%M%S)"
ROUTE="dual-ipc-join"
SHOT_DIR="$DODGC_SHOTS/${STAMP}-${ROUTE}"
NOTE="$DODGC_NOTES/${STAMP}-${ROUTE}.md"
HUSER="${XDG_RUNTIME_DIR:-/tmp}/dodgc-ipc-host-${STAMP}"
CUSER="${XDG_RUNTIME_DIR:-/tmp}/dodgc-ipc-client-${STAMP}"
HLOG="/tmp/dodgc-ipc-host-${STAMP}.log"
CLOG="/tmp/dodgc-ipc-client-${STAMP}.log"
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

HPID= CPID=
cleanup() {
	[[ -n "${CPID:-}" ]] && kill "$CPID" 2>/dev/null || true
	[[ -n "${HPID:-}" ]] && kill "$HPID" 2>/dev/null || true
	[[ -n "${CPID:-}" ]] && wait "$CPID" 2>/dev/null || true
	[[ -n "${HPID:-}" ]] && wait "$HPID" 2>/dev/null || true
}
trap cleanup EXIT

# Only prior dod-gc IPC profiles / this repo's tap hub — never bare dolphin-emu.
dodgc_pkill_own_dolphin "${XDG_RUNTIME_DIR:-/tmp}/dodgc-ipc-"
dodgc_pkill_own_tap_hub
sleep 1

echo "==> BBA IPC (EXI=$EXI_IPC) hostISO=$HOST_ISO"
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

JOIN=0
PROGRESS=0
SENDTO=0
SIGNON=0
for _ in $(seq 1 90); do
	if rg -a -q 'peer connect queue|peer resend path|sendto fd=' "$CLOG" 2>/dev/null; then
		PROGRESS=1
	fi
	if rg -a -q 'sendto fd=' "$CLOG" 2>/dev/null; then
		SENDTO=1
	fi
	# Peer evidence: ca_connected after remote accept, or host BSS seat for .3.
	if rg -a -q 'peer OOB handled state=2|Connected to |Connection accepted by|CL_ServerInfo|signon [1-9]' "$CLOG" 2>/dev/null; then
		JOIN=1
		break
	fi
	if rg -a -q 'skips local auto-connect' "$HLOG" 2>/dev/null \
		&& rg -a -q 'client frames BSS slot=0' "$HLOG" 2>/dev/null; then
		JOIN=1
		break
	fi
	if rg -a -q '10\.0\.0\.3.*connected|SV_ConnectClient|connection accepted' "$HLOG" 2>/dev/null; then
		JOIN=1
		break
	fi
	if rg -a -q 'server is full' "$HLOG" "$CLOG" 2>/dev/null; then
		echo "==> peer rejected: server is full" >&2
	fi
	if rg -a -q 'FATAL|out of memory|Mem_FreeBlockBig' "$CLOG" 2>/dev/null; then
		break
	fi
	sleep 1
done

# After accept, wait for hard signon (ca_active). Soft Serverdata alone is not play.
SOFT_SIGNON=0
HARD_SIGNON=0
if [[ "$JOIN" -eq 1 ]]; then
	echo "==> join=$JOIN — waiting for peer ca_active / SignonReply 2"
	for _ in $(seq 1 180); do
		# Hard only: real in-game (do NOT treat local MAP READY / Game started as done).
		if rg -a -q 'CL_SignonReply: 2|G159 ca_active|G159 live GX present ca_active|SCR_EndLoadingPlaque|cls\.state = ca_active|state=ca_active' "$CLOG" 2>/dev/null; then
			HARD_SIGNON=1
			SOFT_SIGNON=1
			break
		fi
		if rg -a -q 'serverdata spawn shortcut|CL_SignonReply: 1|Serverdata packet received' "$CLOG" 2>/dev/null; then
			SOFT_SIGNON=1
		fi
		if rg -a -q 'FATAL|out of memory|Mem_FreeBlockBig|mem FAIL subsystem=Client Edicts' "$CLOG" 2>/dev/null; then
			break
		fi
		sleep 1
	done
	# Extra settle only after hard signon evidence.
	if [[ "$HARD_SIGNON" -eq 1 ]]; then
		sleep 8
	elif [[ "$SOFT_SIGNON" -eq 1 ]]; then
		echo "==> soft Serverdata only — holding 45s more for SignonReply 2" >&2
		for _ in $(seq 1 45); do
			if rg -a -q 'CL_SignonReply: 2|G159 ca_active|G159 live GX present ca_active|SCR_EndLoadingPlaque' "$CLOG" 2>/dev/null; then
				HARD_SIGNON=1
				break
			fi
			sleep 1
		done
	fi
else
	sleep 6
fi
SIGNON=$HARD_SIGNON
echo "==> soft_signon=$SOFT_SIGNON hard_signon=$HARD_SIGNON (playable=$SIGNON)"

cp -f "$HLOG" "$SHOT_DIR/host-dolphin.log"
cp -f "$CLOG" "$SHOT_DIR/client-dolphin.log"

{
	echo "# Dual BBA IPC join — ${STAMP}"
	echo
	echo "- Host ISO: \`$HOST_ISO\`"
	echo "- Client ISO: \`$CLIENT_ISO\`"
	echo "- EXI: EthernetIPC ($EXI_IPC)"
	echo "- host_ready=$HOST_READY progress=$PROGRESS sendto=$SENDTO join=$JOIN soft_signon=$SOFT_SIGNON hard_signon=$HARD_SIGNON"
	echo
	echo "## Host OSReport keys"
	echo '```'
	tr -d '\000' < "$HLOG" | rg -n 'net ready|Server IPv4|listenserver|MAP READY|Game started|Connect|challenge|client connected|SV_|FATAL|out of memory|sendto|NET_Queue|IPC|Failed to send|client frames BSS|skip local' | head -120 || true
	echo '```'
	echo
	echo "## Client OSReport keys"
	echo '```'
	tr -d '\000' < "$CLOG" | rg -n 'screen init minimal|net ready|peer connect|peer resend|peer connected|peer client progs|deferred client|Serverdata|signon|CL_SignonReply|G159|SCR_End|Connecting|Connected|challenge|sendto|rejected|timeout|FATAL|out of memory|gcnetip|ClientFrame connect|spawn shortcut|heap mark stage=host_main' | head -200 || true
	echo '```'
} | tee "$NOTE"

echo "==> note: $NOTE"
echo "==> shots: $SHOT_DIR"
# Playable bar: join + hard signon (ca_active / SignonReply 2).
[[ "$JOIN" -eq 1 && "$HARD_SIGNON" -eq 1 ]]
