#!/usr/bin/env bash
# CapFaces spawn atlas: every dod_* map × Axis/Allied spawn eye (still + short video).
# Requires out/dodgc.dol with gamecube.cfg dumpeye → -gcdumpeye support.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

# CapFaces tip: BBA BSS steals ~200 KiB MEM1 and regenerates the magenta tip
# panel even after zip/searchpaths recovery. Networking is deferred.
export XASH_GAMECUBE_LINK_BBA="${XASH_GAMECUBE_LINK_BBA:-0}"

ATLAS="$DODGC_ROOT/playtests/map-spawn-atlas"
SPAWNS="$ATLAS/spawns.json"
DURATION="${ATLAS_DURATION:-40}"
MAP_FILTER="${1:-}"
SIDES_FILTER="${2:-}"

[[ -f "$SPAWNS" ]] || { echo "missing $SPAWNS" >&2; exit 1; }
[[ -f "$DODGC_DOL" ]] || { echo "missing $DODGC_DOL" >&2; exit 1; }

mkdir -p "$ATLAS"/{screenshots,videos,notes,isos}

CACHE="${XASH3D_GC_ISO_CACHE:-/mnt/datasets/dodgc-iso-build}"
LEAN="${DODGC_LEAN_DOD:-$CACHE/dod-menu-min}"
MAP_SRC="$CACHE/dod-lean/maps"
WORKLIST="$ATLAS/worklist.tsv"

python3 - "$SPAWNS" "$MAP_FILTER" "$SIDES_FILTER" "$WORKLIST" <<'PY'
import json, sys
from pathlib import Path
spawns = json.loads(Path(sys.argv[1]).read_text())
mf, sf, out = sys.argv[2], sys.argv[3], Path(sys.argv[4])
EYE_Z = 28
rows = []
for map_name, info in sorted(spawns.items()):
    if mf and map_name != mf:
        continue
    for side in ("allies", "axis"):
        if sf and side != sf:
            continue
        ent = info.get(side)
        if not ent:
            continue
        o, a = ent["origin"], ent["angles"]
        yaw = a[1] if len(a) > 1 else 0.0
        eye = f"{o[0]:.0f} {o[1]:.0f} {o[2]+EYE_Z:.0f} -8 {yaw:.0f}"
        rows.append(f"{map_name}\t{side}\t{eye}")
out.write_text("\n".join(rows) + ("\n" if rows else ""))
print(f"worklist {len(rows)} jobs → {out}")
PY

# Per-map lean ISO: only that map's BSP + its own .wad (no foreign map WADs,
# no all-BSP lean tree). Shared lean with every dod_*.wad reintroduced the
# searchpaths tip regression atlas magenta'd on.
build_one() {
	local map="$1" side="$2" eye="$3"
	local iso_out="$ATLAS/isos/${map}-${side}.iso"
	local route="atlas-${map}-${side}"

	mkdir -p "$ATLAS/isos" "$ATLAS/screenshots" "$ATLAS/videos" "$ATLAS/notes"
	echo "==> atlas job $map $side eye=($eye)"
	if ! bash "$SCRIPT_DIR/build-lean-iso.sh" --menu-only --listen "$map" \
		--world-render --skip-startup-vids --dumpeye "$eye" \
		>/tmp/dodgc-atlas-iso-${map}-${side}.log 2>&1; then
		echo -e "${map}\t${side}\tFAIL iso" | tee -a "$ATLAS/results.tsv"
		tail -15 /tmp/dodgc-atlas-iso-${map}-${side}.log
		return 1
	fi
	cp -f "$DODGC_ROOT/out/dodgc-menu.iso" "$iso_out"

	echo "==> probe $route eye=($eye)"
	bash "$SCRIPT_DIR/probe-playtest.sh" --route "$route" --duration "$DURATION" \
		--ready-regex 'CapFaces filter drawn=|disc boot override dumpeye|FATAL|Guru|Sys_Error' \
		"$iso_out" 2>&1 | tee "/tmp/dodgc-atlas-probe-${map}-${side}.log" | tail -25

	local shot vid drawn
	shot=$(find "$DODGC_ROOT/playtests/screenshots" -maxdepth 1 -type d -name "*${route}*" | sort | tail -1 || true)
	drawn="none"
	if [[ -n "${shot:-}" && -f "$shot/final.png" ]]; then
		mkdir -p "$ATLAS/screenshots/$map"
		cp -f "$shot/final.png" "$ATLAS/screenshots/$map/${side}.png"
		[[ -f "$shot/dolphin.log" ]] && cp -f "$shot/dolphin.log" "$ATLAS/notes/${map}-${side}.log"
		drawn=$(rg -a -o 'CapFaces filter drawn=[0-9]+' "$shot/dolphin.log" 2>/dev/null | tail -1 || echo "no-drawn")
	fi
	vid=$(find "$DODGC_ROOT/playtests/videos" -name "*${route}*.mp4" | sort | tail -1 || true)
	if [[ -n "${vid:-}" ]]; then
		mkdir -p "$ATLAS/videos/$map"
		cp -f "$vid" "$ATLAS/videos/$map/${side}.mp4"
	fi
	echo -e "${map}\t${side}\t${drawn}" | tee -a "$ATLAS/results.tsv"
}

pkill -9 -f dolphin-emu 2>/dev/null || true
sleep 1

echo -e "map\tside\tresult" >"$ATLAS/results.tsv"
while IFS=$'\t' read -r map side eye; do
	[[ -n "$map" ]] || continue
	build_one "$map" "$side" "$eye" || true
done <"$WORKLIST"

echo "==> Atlas complete → $ATLAS"
find "$ATLAS/screenshots" -name '*.png' | wc -l
cat "$ATLAS/results.tsv"
