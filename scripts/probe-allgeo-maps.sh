#!/usr/bin/env bash
# Probe every dodgc-<map>-fulldraw.iso to densify (or FATAL). Writes a summary table.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

DURATION="${DURATION:-85}"
SUMMARY="${DODGC_OUT}/allgeo-probe-summary.txt"
: > "$SUMMARY"

MAPS=(
	anzio avalanche caen charlie chemille donner escape falaise flash
	flugplatz forest glider jagd kalt kraftstoff merderet northbound
	saints sturm switch vicenza zalec
)

pass=0
fail=0
for stem in "${MAPS[@]}"; do
	iso="${DODGC_OUT}/dodgc-${stem}-fulldraw.iso"
	if [[ ! -f "$iso" ]]; then
		echo "SKIP $stem (no ISO)" | tee -a "$SUMMARY"
		fail=$((fail + 1))
		continue
	fi
	echo "==> probe $stem"
	note_log="/tmp/dodgc-probe-${stem}.log"
	if ! "$SCRIPT_DIR/probe-playtest.sh" \
		--route "${stem}-allgeo" \
		--duration "$DURATION" \
		--ready-regex 'densify extra=|FATAL|Host_Error' \
		--note "allgeo atlas probe $stem" \
		"$iso" >"$note_log" 2>&1; then
		echo "PROBE_ERR $stem" | tee -a "$SUMMARY"
		fail=$((fail + 1))
		continue
	fi
	note=$(rg -o 'playtests/notes/[^ ]+' "$note_log" | head -1 || true)
	shots=$(rg -o 'playtests/screenshots/[^ ]+' "$note_log" | head -1 || true)
	log=""
	[[ -n "$shots" ]] && log="${DODGC_ROOT}/${shots}/dolphin.log"
	[[ -z "$log" || ! -f "$log" ]] && log=$(ls -t "${DODGC_SHOTS}"/*"${stem}-allgeo"*/dolphin.log 2>/dev/null | head -1 || true)

	dens=$(strings "$log" 2>/dev/null | rg -o 'densify extra=[0-9]+ total=[0-9]+' | tail -1 || true)
	fatal=$(strings "$log" 2>/dev/null | rg -m1 'FATAL|Host_Error|fatal message=' || true)
	sky=$(strings "$log" 2>/dev/null | rg -m1 'sky .* loaded|map skyname early' || true)
	tex=$(strings "$log" 2>/dev/null | rg -m1 'textures empty|tip-lean n=' || true)

	final=$(ls -t "${DODGC_SHOTS}"/*"${stem}-allgeo"*/final.png 2>/dev/null | head -1 || true)
	stats="no-final"
	if [[ -n "$final" ]]; then
		stats=$(python3 - "$final" <<'PY'
import sys
from PIL import Image
im=Image.open(sys.argv[1])
px=list(im.getdata()); n=len(px)
black=sum(1 for r,g,b in px if r+g+b<30)/n
white=sum(1 for r,g,b in px if r>240 and g>240 and b>240)/n
print(f"black={black:.1%} white={white:.2%}")
PY
)
	fi

	if [[ -n "$dens" && -z "$fatal" ]]; then
		echo "PASS $stem $dens $stats" | tee -a "$SUMMARY"
		pass=$((pass + 1))
	else
		echo "FAIL $stem dens='${dens:-none}' fatal='${fatal:0:80}' $stats" | tee -a "$SUMMARY"
		fail=$((fail + 1))
	fi
done

echo "----" | tee -a "$SUMMARY"
echo "pass=$pass fail=$fail total=$((pass + fail))" | tee -a "$SUMMARY"
echo "summary: $SUMMARY"
