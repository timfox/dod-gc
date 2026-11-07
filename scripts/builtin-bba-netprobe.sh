#!/usr/bin/env bash
# Prove Dolphin BuiltIn BBA (EXI=12) on SP1 — same EXI locus libbba probes
# (channel 0 / device 2 = Swiss eth2sp1). Slot B alone is NOT seen by this libbba.
# Single guest: listen + netstatic; success = net ready + Server IPv4 / MAP READY.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

# EXIDeviceType::EthernetBuiltIn = 12
EXI_BUILTIN=12
MAP="${1:-stalkyard}"
ISO="${2:-$DODGC_OUT/dodgc-netprobe.iso}"
# Placement: sp1 (default, eth2sp1/libbba) | slotb (retail BBA — expected fail with current libbba)
PLACE="${DODGC_BBA_PLACE:-sp1}"

[[ -f "$ISO" ]] || {
	echo "==> building listen ISO for $MAP"
	export XASH_GAMECUBE_LINK_BBA="${XASH_GAMECUBE_LINK_BBA:-1}"
	[[ -f "$DODGC_DOL" ]] || bash "$SCRIPT_DIR/build-dodgc.sh" --dol-only --skip-hlsdk
	bash "$SCRIPT_DIR/build-lean-iso.sh" --menu-only --listen "$MAP" --net --net-ip 10.0.0.2 \
		--full-map --skip-startup-vids
	cp -f "$DODGC_OUT/dodgc-menu.iso" "$ISO"
}

DOLPHIN=""
if [[ -x "$DODGC_ENGINE/3rdparty/dolphin/build/Binaries/dolphin-emu" ]]; then
	DOLPHIN="$DODGC_ENGINE/3rdparty/dolphin/build/Binaries/dolphin-emu"
elif command -v dolphin-emu >/dev/null 2>&1; then
	DOLPHIN="$(command -v dolphin-emu)"
else
	echo "error: Dolphin not found" >&2
	exit 2
fi

STAMP="$(date +%Y%m%d-%H%M%S)"
USER_DIR="${XDG_RUNTIME_DIR:-/tmp}/dodgc-builtin-${STAMP}"
LOG="/tmp/dodgc-builtin-${STAMP}.log"
NOTE="$DODGC_NOTES/${STAMP}-builtin-bba-netprobe.md"
SHOT_DIR="$DODGC_SHOTS/${STAMP}-builtin-bba-netprobe"
mkdir -p "$USER_DIR/Config" "$SHOT_DIR" "$DODGC_NOTES"

PROFILE="${XDG_DATA_HOME:-$HOME/.local/share}/dod-gc/dolphin"
[[ -f "$PROFILE/Config/GFX.ini" ]] && cp -f "$PROFILE/Config/GFX.ini" "$USER_DIR/Config/" || true

SLOT_B=255
SERIAL_PORT1=255
PLACE_DESC=""
case "$PLACE" in
	sp1|SP1|eth2sp1)
		SERIAL_PORT1=$EXI_BUILTIN
		PLACE_DESC="SerialPort1/SP1 (eth2sp1 / libbba EXI ch0.dev2)"
		;;
	slotb|SlotB|bba)
		SLOT_B=$EXI_BUILTIN
		PLACE_DESC="Slot B (retail BBA EXI ch1 — libbba does not probe this)"
		;;
	both)
		# Cover both loci; SP1 is what libbba uses.
		SLOT_B=$EXI_BUILTIN
		SERIAL_PORT1=$EXI_BUILTIN
		PLACE_DESC="Slot B + SP1 (BuiltIn on both)"
		;;
	*)
		echo "error: unknown DODGC_BBA_PLACE=$PLACE (use sp1|slotb|both)" >&2
		exit 2
		;;
esac

cat > "$USER_DIR/Config/Dolphin.ini" <<EOF
[Core]
CPUCore = 1
CPUThread = True
DSPHLE = True
FastDiscSpeed = True
MMU = True
AccurateCPUCache = True
SIDevice0 = 6
SlotB = ${SLOT_B}
SerialPort1 = ${SERIAL_PORT1}
BBA_BUILTIN_DNS = 8.8.8.8
BBA_BUILTIN_IP =
[DSP]
Muted = True
Volume = 0
[Interface]
ConfirmStop = False
EOF
cat > "$USER_DIR/Config/Logger.ini" <<'EOF'
[Logs]
BOOT = True
OSREPORT = True
EXI = True
SP1 = True
[Options]
Verbosity = 4
WriteToFile = True
WriteToConsole = True
EOF
[[ -f "$USER_DIR/Config/GFX.ini" ]] || cat > "$USER_DIR/Config/GFX.ini" <<'EOF'
[Settings]
Backend = OGL
ShowFPS = True
EOF

export EGL_PLATFORM=x11 QT_QPA_PLATFORM=xcb
unset WAYLAND_DISPLAY
: "${DISPLAY:=:0}"
export DISPLAY

# Only prior BuiltIn probe profiles — never the interactive playtest (-u …/dod-gc/dolphin).
dodgc_pkill_own_dolphin "${XDG_RUNTIME_DIR:-/tmp}/dodgc-builtin-"
sleep 1

echo "==> BuiltIn BBA (EXI=$EXI_BUILTIN $PLACE_DESC) ISO=$ISO"
"$DOLPHIN" -u "$USER_DIR" -e "$ISO" >"$LOG" 2>&1 &
DPID=$!

READY=0
NET=0
CREATED=0
deadline=$((SECONDS + 90))
while (( SECONDS < deadline )); do
	if ! kill -0 "$DPID" 2>/dev/null; then
		break
	fi
	if rg -a -q 'Created Built in network interface' "$LOG" 2>/dev/null; then
		CREATED=1
	fi
	if rg -a -q 'net ready ip=.*\(eth2sp1/BBA\)' "$LOG" 2>/dev/null; then
		NET=1
	fi
	if rg -a -q 'Server IPv4 address|solo listenserver active|MAP READY' "$LOG" 2>/dev/null; then
		READY=1
		# Give net bring-up a few more seconds if map beat the adapter.
		if (( NET )); then
			sleep 2
			break
		fi
		sleep 8
		rg -a -q 'net ready ip=.*\(eth2sp1/BBA\)' "$LOG" 2>/dev/null && NET=1
		break
	fi
	if rg -a -q 'FATAL|out of memory|net offline stub' "$LOG" 2>/dev/null; then
		break
	fi
	sleep 1
done

kill "$DPID" 2>/dev/null || true
wait "$DPID" 2>/dev/null || true

{
	echo "# BuiltIn BBA netprobe $STAMP"
	echo
	echo "- EXI: EthernetBuiltIn ($EXI_BUILTIN) on $PLACE_DESC"
	echo "- SlotB=$SLOT_B SerialPort1=$SERIAL_PORT1"
	echo "- ISO: \`$ISO\`"
	echo "- interface_created=$CREATED net_ready=$NET listen_ready=$READY"
	echo
	echo '```'
	tr -d '\000' < "$LOG" | rg -n 'Created.*network|net ready|net bring-up|net static|Server IPv4|MAP READY|listenserver|FATAL|offline stub|BuiltIn|BBA|eth2sp1' | head -50 || true
	echo '```'
} | tee "$NOTE"

cp -f "$LOG" "$SHOT_DIR/dolphin.log" 2>/dev/null || true
echo "==> note: $NOTE"
(( NET && READY )) || exit 1
