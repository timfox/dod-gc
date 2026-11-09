#!/usr/bin/env bash
# Copy tipfix flash Swiss pack onto a mounted FAT32 SD root.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HW="${DODGC_HW_ROOT:-/run/media/tim/Expansion1/DOD-GC-HW}"
DEST="${1:-}"
if [[ -z "$DEST" ]]; then
  echo "Usage: $0 <fat32-sd-mount>" >&2
  echo "Example: $0 /run/media/tim/EXTRA" >&2
  exit 2
fi
[[ -d "$DEST" ]] || { echo "missing mount: $DEST" >&2; exit 1; }
[[ -f "$HW/dodgc.dol" && -f "$HW/dodgc.iso" ]] || { echo "missing pack at $HW" >&2; exit 1; }
mkdir -p "$DEST/apps/dod-gc" "$DEST/xash3d/dod/logs" "$DEST/xash3d/dod/save"
cp -f "$HW/dodgc.dol" "$DEST/boot.dol"
cp -f "$HW/dodgc.dol" "$DEST/dodgc.dol"
cp -f "$HW/dodgc.dol" "$DEST/apps/dod-gc/boot.dol"
cp -f "$HW/dodgc.iso" "$DEST/dodgc.iso"
cp -f "$HW/dodgc.iso" "$DEST/dodgc-flash-solo.iso"
cp -f "$HW/xash3d/dod/gamecube.cfg" "$DEST/xash3d/dod/gamecube.cfg"
cp -f "$HW/DOD-GC-HW.txt" "$DEST/DOD-GC-HW.txt"
cp -f "$HW/STAGED.txt" "$DEST/STAGED.txt"
[[ -f "$HW/README-CUBE.txt" ]] && cp -f "$HW/README-CUBE.txt" "$DEST/README-CUBE.txt"
if [[ -f "$ROOT/out/swiss-dod-stage/apps/dod-gc/meta.xml" ]]; then
	cp -f "$ROOT/out/swiss-dod-stage/apps/dod-gc/meta.xml" "$DEST/apps/dod-gc/meta.xml"
fi
sync
echo "Staged tip CapFaces Swiss pack -> $DEST"
echo "Boot: Swiss -> apps/dod-gc/boot.dol (or dodgc.iso)"
echo "Collect: $DEST/xash3d/dod/logs/swiss-evidence.txt"
echo "Alt flash whole card: sudo dd if=$ROOT/out/dodgc-swiss-flash-solo.img of=/dev/sdX bs=4M status=progress conv=fsync"
