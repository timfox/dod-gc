#!/usr/bin/env bash
# Rootless virtual LAN for several Dolphin instances + a local DoD GC master (no sudo).
#
#   scripts/multi-dolphin-net.sh start|stop|status|curl PATH
#
# Inside a private user+net namespace (unshare -Urn) this makes a tap "gw0" = 10.0.0.1/24, runs
# scripts/dodgc_master.py on 10.0.0.1:8080 and scripts/dolphin-tap-hub.py, which fans Ethernet
# frames between every Dolphin using SerialPort1 = EthernetTapServer (EXI type 11, unix socket
# $DODGC_TAP_PATH) and the gw0 tap. Guests are 10.0.0.2.. with gateway/master at 10.0.0.1.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RUN="${DODGC_NET_RUN:-${XDG_RUNTIME_DIR:-/tmp}/dodgc-multi-net}"
TAP_PATH="${DODGC_TAP_PATH:-$RUN/dolphin-tap}"
MASTER_PY="${DODGC_MASTER_PY:-${DODGC_ENGINE:-$SCRIPT_DIR/../.deps/xash3d-gc}/scripts/dodgc_master.py}"
mkdir -p "$RUN"
case "${1:-}" in
start)
	[[ -f "$RUN/ns.pid" ]] && kill -0 "$(cat "$RUN/ns.pid")" 2>/dev/null && { echo "already running"; exit 0; }
	rm -f "$TAP_PATH"
	setsid unshare -Urn bash -c '
		set -e
		ip link set lo up
		ip tuntap add dev gw0 mode tap
		ip addr add 10.0.0.1/24 dev gw0
		ip link set gw0 up
		python3 "'"$MASTER_PY"'" --bind 10.0.0.1 --port 8080 --allow-private >"'"$RUN"'/master.log" 2>&1 &
		exec python3 "'"$SCRIPT_DIR"'/dolphin-tap-hub.py" --path "'"$TAP_PATH"'" --tap gw0 --verbose >"'"$RUN"'/hub.log" 2>&1
	' >"$RUN/ns.log" 2>&1 &
	echo $! >"$RUN/ns.pid"; sleep 2
	echo "net up: tap socket $TAP_PATH, master http://10.0.0.1:8080 (inside the namespace), logs in $RUN" ;;
stop)
	[[ -f "$RUN/ns.pid" ]] && pkill -P "$(cat "$RUN/ns.pid")" 2>/dev/null || true
	pid=$(cat "$RUN/ns.pid" 2>/dev/null || true); [[ -n "$pid" ]] && pkill -f "unshare -Urn" -P 1 2>/dev/null || true
	for p in $(pgrep -f "$RUN/dolphin-tap|--path $TAP_PATH" || true); do kill "$p" 2>/dev/null || true; done
	pkill -f "dodgc_master.py --bind 10.0.0.1" 2>/dev/null || true
	rm -f "$RUN/ns.pid" "$TAP_PATH"; echo stopped ;;
status) tail -3 "$RUN/hub.log" 2>/dev/null; tail -3 "$RUN/master.log" 2>/dev/null ;;
curl)
	hubpid=$(pgrep -f "dolphin-tap-hub.py --path $TAP_PATH" | head -1)
	nsenter --preserve-credentials -U -n -t "$hubpid" curl -s "http://10.0.0.1:8080${2:-/v1/servers}"; echo ;;
*) sed -n 2,9p "$0"; exit 2 ;;
esac
