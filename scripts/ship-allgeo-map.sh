#!/usr/bin/env bash
# Stage one DoD map as a tip-safe fulldraw disc: lean full-map + CapFaces spat/gcp
# + sky, then build an ISO with the current tip-safe DOL.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"
dodgc_require_content

MAP="${1:?usage: ship-allgeo-map.sh dod_<map>}"
stem="${MAP%.bsp}"
stem="${stem#dod_}"
full="dod_${stem}"

ALLGEO_MEDIA="${DODGC_ALLGEO_MEDIA:-/tmp/dodgc-allgeo/out/media}"
REPACK="${DODGC_REPACK_ROOT:-/tmp}/dodgc-${stem}-repack"
OUT_ISO="${DODGC_OUT}/dodgc-${stem}-fulldraw.iso"
DOL="${DODGC_DOL}"

if [[ ! -f "$DOL" ]]; then
	echo "error: missing DOL $DOL" >&2
	exit 1
fi
if [[ ! -f "$ALLGEO_MEDIA/cf_${full}.gcp" || ! -f "$ALLGEO_MEDIA/cf_${full}_spatial.pk3" ]]; then
	echo "error: missing CapFaces spat for $full under $ALLGEO_MEDIA" >&2
	exit 1
fi

rm -rf "$REPACK"
mkdir -p "$REPACK"

echo "==> stage lean full-map $full → $REPACK"
DODGC_LEAN_DOD="$REPACK/dod" \
DODGC_LEAN_VALVE="$REPACK/valve" \
	bash "$SCRIPT_DIR/stage-lean-dod.sh" --map "$full" --full-map

mkdir -p "$REPACK/dod/media" "$REPACK/valve"

cp -f "$ALLGEO_MEDIA/cf_${full}.gcp" "$REPACK/dod/media/"
spat_src="$ALLGEO_MEDIA/cf_${full}_spatial.pk3"
case "$full" in
	dod_flash)
		[[ -f /tmp/dodgc-flash-repack/dod/media/cf_dod_flash_spatial.pk3 ]] \
			&& spat_src=/tmp/dodgc-flash-repack/dod/media/cf_dod_flash_spatial.pk3
		;;
	dod_caen)
		[[ -f /tmp/dodgc-caen-repack/dod/media/cf_dod_caen_spatial.pk3 ]] \
			&& spat_src=/tmp/dodgc-caen-repack/dod/media/cf_dod_caen_spatial.pk3
		;;
esac
cp -f "$spat_src" "$REPACK/dod/media/cf_${full}_spatial.pk3"
# Short gamedir-root alias — ISO9660/FST find flaps on long media/*_spatial.pk3
# names (escape absolute open ENOENT). Mount code tries cf_spat.pk3 next.
cp -f "$spat_src" "$REPACK/dod/cf_spat.pk3"
# Unpack spatial.txt (+ cells) as loose media/ for densify when pk3 mount misses.
python3 - "$spat_src" "$REPACK/dod/media" <<'PY'
import sys, zipfile
from pathlib import Path
src, dest = Path(sys.argv[1]), Path(sys.argv[2])
dest.mkdir(parents=True, exist_ok=True)
n = 0
with zipfile.ZipFile(src) as z:
	for name in z.namelist():
		base = Path(name).name
		if not base or base.endswith("/"):
			continue
		if not (base.endswith(".gcp") or base.endswith(".txt") or base.endswith(".gct")):
			continue
		out = dest / base.lower()
		out.write_bytes(z.read(name))
		n += 1
print(f"spat unpack: {n} cell files → {dest}")
PY
echo "gcp+spat: cf_${full}.gcp + $(basename "$spat_src") + cf_spat.pk3"

# World-texture pack when available (tip-lean covers the rest).
for wt in \
	"$REPACK/dod/media/wt_${full}.gct" \
	"/tmp/dodgc-wt-rebuild/media/wt_${full}.gct" \
	"/mnt/datasets/dodgc-iso-build/xash3d-gc-smoke-data-i_ltw5ru/dod/media/wt_${full}.gct" \
	"/tmp/dodgc-${stem}-repack/dod/media/wt_${full}.gct"
do
	if [[ -f "$wt" && "$(basename "$wt")" == "wt_${full}.gct" ]]; then
		cp -f "$wt" "$REPACK/dod/media/wt_${full}.gct"
		echo "wt: $(basename "$wt")"
		break
	fi
done
# Drop wrong-map WT leftovers from a prior stage.
shopt -s nullglob
for leftover in "$REPACK/dod/media"/wt_dod_*.gct; do
	[[ "$(basename "$leftover")" == "wt_${full}.gct" ]] && continue
	rm -f "$leftover"
	echo "removed foreign WT $(basename "$leftover")"
done
shopt -u nullglob

# valve/delta.lst — hybrid FST can false-hit dod/delta.lst.
if [[ -f "$DODGC_CONTENT_DOD/delta.lst" ]]; then
	cp -f "$DODGC_CONTENT_DOD/delta.lst" "$REPACK/dod/delta.lst"
	cp -f "$DODGC_CONTENT_DOD/delta.lst" "$REPACK/valve/delta.lst"
elif [[ -f "$DODGC_CONTENT_VALVE/delta.lst" ]]; then
	cp -f "$DODGC_CONTENT_VALVE/delta.lst" "$REPACK/dod/delta.lst"
	cp -f "$DODGC_CONTENT_VALVE/delta.lst" "$REPACK/valve/delta.lst"
fi

# CapFaces listen boot — matches proven flash/caen fulldraw discs.
cat > "$REPACK/dod/gamecube.cfg" <<EOF
video prog
listenserver ${full}
gcworldrender
gcplayerinput
EOF
echo "gamecube.cfg: listenserver ${full} + gcworldrender"

echo "==> build ISO $OUT_ISO"
TMPDIR="${XASH3D_GC_ISO_CACHE}" \
python3 "$DODGC_ENGINE/scripts/build-gamecube-disc.py" \
	--dol "$DOL" \
	--data "$REPACK/dod" \
	--gamedir dod \
	--extra-data "valve=$REPACK/valve" \
	--no-auto-extra-gamedirs \
	--skip-startup-vids \
	--output "$OUT_ISO"

echo "shipped $OUT_ISO ($(du -h "$OUT_ISO" | awk '{print $1}'))"
