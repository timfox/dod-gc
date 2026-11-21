#!/usr/bin/env bash
# Build a dev disc with the live slots: dod/maps/dod_live.bsp (3 MiB, zero padded) and
# dod/livemap.txt (2048 B), listen-boot on dod_live, gc_livemap 1. No baked CapFaces/wd:
# the engine renders the live BSP. usage: build_slot_disc.sh <engine-dol> <out.iso> [seed.bsp]
set -euo pipefail
DOL="${1:?dol}"; OUT="${2:?iso}"; SEED="${3:-$HOME/.cache/gsbridge/live.bsp}"
S=/home/tim/Desktop/dod-gc/scripts; ENGINE="${DODGC_ENGINE:-/run/media/tim/Expansion1/xash3d-gc}"; BUILDER="${GC_DISC_BUILDER_ROOT:-/home/tim/Desktop/mapbridge-engine}"   # root whose build/gc-wd-cache the disc builder uses
W=$(mktemp -d /tmp/mb-slot.XXXX); C=$W/content_dod
mkdir -p $C/maps; for f in /home/tim/Desktop/dod-gc/content/dod/*; do b=$(basename $f); [ $b = maps ] || ln -s $f $C/$b; done
for f in /home/tim/Desktop/dod-gc/content/dod/maps/*; do ln -s $f $C/maps/$(basename $f); done
python3 - "$SEED" "$C/maps/dod_live.bsp" <<'PY'
import sys; d=open(sys.argv[1],'rb').read(); open(sys.argv[2],'wb').write(d+b'\0'*((3<<20)-len(d)))
PY
cp /home/tim/Desktop/dod-gc/content/dod/maps/dod_caen.txt $C/maps/dod_live.txt
export DODGC_CONTENT_DOD=$C DODGC_STAGE_WORLDDL=0 DODGC_ENGINE="$ENGINE"
source $S/env.sh
R=$W/repack; mkdir -p $R/dod $R/valve
DODGC_LEAN_DOD=$R/dod DODGC_LEAN_VALVE=$R/valve bash $S/stage-lean-dod.sh --map dod_live --full-map
printf '%-2048s' "L 0 dod_live" > $R/dod/livemap.txt
# Render packs for the live map (fixed-size slots, rewritten in place on every push): world textures, world display lists, light faces
mkdir -p $R/dod/media; BK=$W/bake; python3 /home/tim/Desktop/dod-gc/tools/mapbridge/bake_live.py "$SEED" $BK dod_live >/dev/null
for pair in "wt_dod_live.gct:2097152" "wd_dod_live.gcd:1048576" "wd_dod_live_lf.gcd:1048576" "wd_dod_live_sb.gcd:65536"; do
  f=${pair%%:*}; sz=${pair##*:}; cp $BK/media/$f $R/dod/media/$f; truncate -s $sz $R/dod/media/$f
done
printf 'video prog\nlistenserver dod_live\ngcworldrender\ngcplayerinput\nnohud\n' > $R/dod/gamecube.cfg
printf 'gc_livemap 1\n' >> $R/dod/listenserver.cfg
GC_SLOT_PAD="dod_live:wd=1048576:lf=1048576:sb=65536" TMPDIR="${XASH3D_GC_ISO_CACHE:-/tmp}" python3 "$BUILDER/scripts/build-gamecube-disc.py" --dol "$DOL" --data $R/dod --gamedir dod \
  --extra-data "valve=$R/valve" --no-auto-extra-gamedirs --skip-startup-vids --output "$OUT"
python3 /home/tim/Desktop/dod-gc/tools/mapbridge/isoslot.py find "$OUT" /xash3d/dod/maps/dod_live.bsp
python3 /home/tim/Desktop/dod-gc/tools/mapbridge/isoslot.py find "$OUT" /xash3d/dod/livemap.txt
rm -rf $W
