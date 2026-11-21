#!/usr/bin/env bash
# Build a real playable disc (not the dev slot disc) from the current Maya map: the map is compiled with lighting, staged with
# its WADs, retail HUD sheets and the engine's own wt/wd/lf/sb packs (baked by the disc builder), and it boots straight into it.
# The ISO runs in Dolphin and on hardware (Swiss). usage: build_hw_disc.sh [map name, default: the Maya map's name] [dol]
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
G="$HOME/gsbridge"; [ -f "$G/config.env" ] && . "$G/config.env"
WORK="${GS_WORK:-$HOME/.cache/gsbridge}"; DOL="${2:-${GS_DOL:-$G/dodgc_live.dol}}"
NAME="${1:-dod_live}"; NAME="${NAME,,}"; [[ "$NAME" == dod_* ]] || NAME="dod_$NAME"
S=/home/tim/Desktop/dod-gc/scripts; ENGINE="${DODGC_ENGINE:-/home/tim/Desktop/mapbridge-engine}"
OUT="$G/out"; mkdir -p "$OUT" "$G/hw"
[ -f "$DOL" ] || { echo "missing DOL $DOL (see DESIGN.md build recipe)"; exit 1; }
[ -f "$WORK/live.map" ] || { echo "no map exported yet: save in Maya first"; exit 1; }
W=$(mktemp -d "$G/hw/build.XXXX"); trap 'rm -rf "$W"' EXIT
echo "==> compile $NAME with lighting"
python3 "$HERE/compile_map.py" "$WORK/live.map" "$W/$NAME.bsp" || exit 1
C=$W/content_dod; mkdir -p $C/maps
for f in /home/tim/Desktop/dod-gc/content/dod/*; do b=$(basename $f); [ $b = maps ] || ln -s $f $C/$b; done
for f in /home/tim/Desktop/dod-gc/content/dod/maps/*; do ln -s $f $C/maps/$(basename $f); done
cp "$W/$NAME.bsp" "$C/maps/$NAME.bsp"; cp /home/tim/Desktop/dod-gc/content/dod/maps/dod_caen.txt "$C/maps/$NAME.txt"
export DODGC_CONTENT_DOD=$C DODGC_STAGE_WORLDDL=0 DODGC_ENGINE="$ENGINE"
source $S/env.sh
R=$W/repack; mkdir -p $R/dod $R/valve
echo "==> stage $NAME"
DODGC_LEAN_DOD=$R/dod DODGC_LEAN_VALVE=$R/valve bash $S/stage-lean-dod.sh --map "$NAME" --full-map > "$W/stage.log" 2>&1 || { tail -5 "$W/stage.log"; exit 1; }
printf 'video prog\nlistenserver %s\ngcworldrender\ngcplayerinput\nnohud\n' "$NAME" > $R/dod/gamecube.cfg
[ -f "$G/export/mapcycle.txt" ] && cp "$G/export/mapcycle.txt" $R/dod/mapcycle.txt
echo "==> disc ($ENGINE/scripts/build-gamecube-disc.py bakes wt/wd/lf/sb for the map)"
DODGC_ALLOW_MISSING_PROPPACK=1 TMPDIR="${XASH3D_GC_ISO_CACHE:-/tmp}" python3 "$ENGINE/scripts/build-gamecube-disc.py" --dol "$DOL" --data $R/dod --gamedir dod \
  --extra-data "valve=$R/valve" --no-auto-extra-gamedirs --skip-startup-vids --output "$OUT/$NAME.iso" > "$W/disc.log" 2>&1 || { tail -8 "$W/disc.log"; exit 1; }
echo "==> done: $OUT/$NAME.iso ($(du -h "$OUT/$NAME.iso" | cut -f1))"
echo "    Dolphin:  $HERE/live_dolphin.sh $OUT/$NAME.iso   (the live poll is not in this disc's config: it is a plain game)"
echo "    Hardware: copy the ISO to the SD/USB for Swiss together with $DOL as the boot DOL (see out/*-handoff.txt)"
