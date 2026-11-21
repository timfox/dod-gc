#!/usr/bin/env bash
# The packaged final disc: every stock DoD map plus every Maya map in the Map Manager project, with the project's map cycle
# (mapcycle.txt), the Create Game map list (maps.lst), the retail HUD, the engine's full render packs, and the build stamp (repo and
# submodule commits) on the disc and next to the ISO.  usage: build_release_disc.sh [dol] [out.iso]
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
G="$HOME/gsbridge"; [ -f "$G/config.env" ] && . "$G/config.env"
DOL="${1:-${GS_DOL:-$G/dodgc_live.dol}}"; OUT="${2:-$G/out/dodgc-release.iso}"
ENGINE="${DODGC_ENGINE:-/home/tim/Desktop/mapbridge-engine}"; HLSDK="${DODGC_HLSDK:-/home/tim/Desktop/hlsdk-mapbridge}"
DODGC="${DODGC_TREE:-/home/tim/Desktop/dod-gc-mapbridge}"      # the dod-gc checkout whose scripts/build-info are used (the stamp records it)
CONTENT=/home/tim/Desktop/dod-gc/content
[ -f "$DOL" ] || { echo "missing DOL $DOL"; exit 1; }
mkdir -p "$G/out" "$G/release"; W="$G/release/work"; rm -rf "$W"; mkdir -p "$W/maps" "$W/map"
echo "==> project"
python3 - <<PY
import sys; sys.path.insert(0, "$HERE"); import mapproject as mp, json
d = mp.load(); d, _ = mp.preload_stock(d); mp.save(d)
errs = mp.problems(d)
if errs: print("project warnings:", "; ".join(errs))
mp.export(d, "$W/export")
json.dump([m for m in d["maps"]], open("$W/project.json", "w"))
PY
echo "==> compile the Maya maps"
python3 - <<PY > "$W/mayamaps.txt"
import json, os
for m in json.load(open("$W/project.json")):
    if m["scene"] and os.path.exists(m["scene"]) and not os.path.exists(os.path.join("$CONTENT/dod/maps", m["name"] + ".bsp")): print(m["name"], m["scene"])
PY
while read -r NAME SCENE; do
  [ -n "$NAME" ] || continue
  MAPF=""
  if [ -f "$G/maps/$NAME.map" ]; then MAPF="$G/maps/$NAME.map"
  elif [ "$NAME" = "dod_live" ] && [ -f "${GS_WORK:-$HOME/.cache/gsbridge}/live.map" ]; then MAPF="${GS_WORK:-$HOME/.cache/gsbridge}/live.map"
  else
    echo "no exported .map for $NAME: open $SCENE in Maya 2027 and save once (every save keeps ~/gsbridge/maps/$NAME.map). The host has no batch Maya to export it for you."; exit 1
  fi
  if ! grep -q "^( " "$MAPF"; then echo "    $NAME: map has no brushes, skipped ($MAPF)"; continue; fi
  if [ -n "$SCENE" ] && [ -f "$SCENE" ] && [ "$SCENE" -nt "$MAPF" ]; then echo "    WARNING: $SCENE is newer than its exported $MAPF: open it in Maya and save so the disc gets the latest edits"; fi
  echo "    $NAME  ($MAPF)"
  python3 "$HERE/compile_map.py" "$MAPF" "$W/maps/$NAME.bsp" || exit 1
done < "$W/mayamaps.txt"
echo "==> stage"
D="$W/dod"; cp -as "$CONTENT/dod" "$D" 2>/dev/null || { mkdir -p "$D"; cp -as "$CONTENT/dod/." "$D/"; }
rm -f "$D/dod"        # the retail install symlink inside content/dod
for b in "$W"/maps/*.bsp; do [ -e "$b" ] || continue; n=$(basename "$b" .bsp); rm -f "$D/maps/$n.bsp"; cp "$b" "$D/maps/$n.bsp"; cp "$CONTENT/dod/maps/dod_caen.txt" "$D/maps/$n.txt" 2>/dev/null || true; done
cp "$W/export/mapcycle.txt" "$D/mapcycle.txt"
python3 - <<PY
import json, os
ms = json.load(open("$W/project.json")); on_disc = set(f[:-4] for f in os.listdir("$D/maps") if f.endswith(".bsp"))
cyc = [l.strip() for l in open("$D/mapcycle.txt") if l.strip() in on_disc]       # the rotation only names maps that are on this disc
open("$D/mapcycle.txt", "w").write("".join(c + "\n" for c in cyc))
with open("$D/maps.lst", "w") as f:
    for m in ms:
        b = m["bsp"] or m["name"]
        if b in on_disc and not m["parent"]: f.write('%s "%s"\n' % (b, m["title"] or b))
PY
printf 'mp_teamplay 1\nmp_teamlist "Allies;Axis"\nmp_defaultteam 0\nmapcyclefile mapcycle.txt\n' > "$D/game.cfg"
export DODGC_ENGINE="$ENGINE" DODGC_HLSDK="$HLSDK"
( cd "$DODGC" && bash scripts/build-info.sh --short ) > "$D/build-info.txt" 2>/dev/null || echo "build-info unavailable" > "$D/build-info.txt"
( cd "$DODGC" && bash scripts/build-info.sh ) > "$OUT.build-info.txt" 2>&1 || true
python3 "${DODGC_SCRIPTS_FALLBACK:-/home/tim/Desktop/dod-gc/scripts}/stage_valve_fallback.py" --dod "$D" --valve "$CONTENT/valve" > "$W/valve.log" 2>&1 || tail -3 "$W/valve.log"
echo "==> disc (bakes wt/wd/lf/sb for every map: first run takes a few minutes, later runs are cached)"
DODGC_ALLOW_MISSING_PROPPACK=1 TMPDIR="${XASH3D_GC_ISO_CACHE:-/tmp}" python3 "$ENGINE/scripts/build-gamecube-disc.py" --dol "$DOL" --data "$D" --gamedir dod \
  --extra-data "valve=$CONTENT/valve" --no-auto-extra-gamedirs --output "$OUT" > "$W/disc.log" 2>&1 || { tail -8 "$W/disc.log"; exit 1; }
echo "==> done: $OUT ($(du -h "$OUT" | cut -f1)), $(wc -l < "$D/maps.lst") maps listed, rotation: $(tr '\n' ' ' < "$D/mapcycle.txt")"
echo "==> stamp: $OUT.build-info.txt"; cat "$D/build-info.txt"
