#!/usr/bin/env bash
# Wait for a removable FAT32 mount, stage DoD Swiss HW pack, print Cube steps.
# Does not dd. Skips known host volumes (Expansion*, Extra Stream, EFI, datasets).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HW="${DODGC_HW_ROOT:-/run/media/tim/Expansion1/DOD-GC-HW}"
TIMEOUT="${SD_WAIT_TIMEOUT:-7200}"
SKIP_RE='Expansion|Stream|Storage|Production|EFI|135A-3274|datasets'
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG="/tmp/dodgc-swiss-sd-watch-${STAMP}.log"

[[ -f "$HW/dodgc.dol" && -f "$HW/dodgc.iso" ]] || {
	echo "error: refresh HW pack first (missing $HW/dodgc.dol)" >&2
	exit 1
}

echo "==> watching for FAT SD (timeout ${TIMEOUT}s) log=$LOG"
echo "    stage: $ROOT/scripts/stage-swiss-sd.sh <mount>"
START=$(date +%s)
SEEN_BEFORE=$(ls -1 /run/media/tim 2>/dev/null | sort | tr '\n' ' ')

while true; do
	now=$(date +%s)
	if (( now - START >= TIMEOUT )); then
		echo "TIMEOUT: no SD within ${TIMEOUT}s" | tee -a "$LOG"
		exit 1
	fi
	for mount in /run/media/tim/*; do
		[[ -d "$mount" ]] || continue
		base=$(basename "$mount")
		echo "$base" | rg -q "$SKIP_RE" && continue
		# Prefer vfat/exfat under ~64GiB
		fstype=$(findmnt -no FSTYPE "$mount" 2>/dev/null || true)
		[[ "$fstype" == "vfat" || "$fstype" == "exfat" || "$fstype" == "msdos" ]] || continue
		size_k=$(df -k "$mount" 2>/dev/null | awk 'NR==2{print $2}')
		[[ -n "$size_k" ]] || continue
		# skip multi-TB host disks
		(( size_k < 70000000 )) || continue
		echo "==> candidate $mount fstype=$fstype size_k=$size_k" | tee -a "$LOG"
		bash "$ROOT/scripts/stage-swiss-sd.sh" "$mount" | tee -a "$LOG"
		echo "CUBE: Swiss → apps/dod-gc/boot.dol  (map dod_flash CapFaces)"
		echo "After quit: remount and collect $mount/xash3d/dod/logs/swiss-evidence.txt"
		echo "STAGED_MOUNT=$mount" | tee -a "$LOG"
		exit 0
	done
	sleep 3
done
