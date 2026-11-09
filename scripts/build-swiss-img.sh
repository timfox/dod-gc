#!/usr/bin/env bash
# Build a FAT32 Swiss SD image for DoD CapFaces from out/ / DOD-GC-HW pack.
# Flash: sudo dd if=out/dodgc-swiss-flash-solo.img of=/dev/sdX bs=4M status=progress conv=fsync
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HW="${DODGC_HW_ROOT:-/run/media/tim/Expansion1/DOD-GC-HW}"
STAGE="${ROOT}/out/swiss-dod-stage"
IMG_OUT="${ROOT}/out/dodgc-swiss-flash-solo.img"
IMG_MIB="${SWISS_IMG_MIB:-192}"
VENV="${ROOT}/out/pyfatfs-venv"

[[ -f "$HW/dodgc.dol" && -f "$HW/dodgc.iso" ]] || {
	echo "error: missing $HW/dodgc.dol or dodgc.iso — refresh HW pack first" >&2
	exit 1
}

echo "==> stage $STAGE"
rm -rf "$STAGE"
mkdir -p "$STAGE/apps/dod-gc" "$STAGE/xash3d/dod/logs" "$STAGE/xash3d/dod/save"
cp -f "$HW/dodgc.dol" "$STAGE/apps/dod-gc/boot.dol"
cp -f "$HW/dodgc.dol" "$STAGE/boot.dol"
cp -f "$HW/dodgc.dol" "$STAGE/dodgc.dol"
cp -f "$HW/dodgc.iso" "$STAGE/dodgc.iso"
cp -f "$HW/xash3d/dod/gamecube.cfg" "$STAGE/xash3d/dod/gamecube.cfg"
[[ -f "$HW/STAGED.txt" ]] && cp -f "$HW/STAGED.txt" "$STAGE/STAGED.txt"
[[ -f "$HW/README-CUBE.txt" ]] && cp -f "$HW/README-CUBE.txt" "$STAGE/README-CUBE.txt"
cat > "$STAGE/apps/dod-gc/meta.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<app version="1">
  <name>Day of Defeat GC</name>
  <coder>dod-gc</coder>
  <version>tip-capfaces</version>
  <short_description>DoD CapFaces solo listen (Swiss)</short_description>
  <long_description>Boot apps/dod-gc/boot.dol. Loads dod_flash CapFaces. Quit Z+Start. Evidence: xash3d/dod/logs/swiss-evidence.txt</long_description>
</app>
EOF

need_mib=$IMG_MIB
stage_mib=$(( $(du -sm "$STAGE" | awk '{print $1}') + 16 ))
if (( stage_mib > need_mib )); then
	need_mib=$stage_mib
fi
echo "==> image ${need_mib}MiB → $IMG_OUT (stage=$(du -sh "$STAGE" | awk '{print $1}'))"

if [[ ! -x "$VENV/bin/python" ]]; then
	python3 -m venv "$VENV"
	"$VENV/bin/pip" install -q 'setuptools<81' 'pyfatfs>=1.0.0' 'fs>=2.4'
fi

rm -f "$IMG_OUT"
truncate -s "${need_mib}M" "$IMG_OUT"
# --mbr=y: DOS partition table so SD2SP2/SD Gecko see FAT reliably
mkfs.vfat -F 32 -n DODGC --mbr=y "$IMG_OUT" >/dev/null

"$VENV/bin/python" - <<PY
from pathlib import Path
from pyfatfs.PyFatFS import PyFatFS

stage = Path("$STAGE")
img = Path("$IMG_OUT")
fs = PyFatFS(str(img), read_only=False, preserve_case=True)

def ensure_dir(path: str) -> None:
	parts = [p for p in path.strip("/").split("/") if p]
	cur = ""
	for part in parts:
		cur = f"{cur}/{part}" if cur else part
		if not fs.exists(cur):
			fs.makedir(cur)

copied = 0
for path in stage.rglob("*"):
	rel = path.relative_to(stage).as_posix()
	if path.is_dir():
		ensure_dir(rel)
		continue
	parent = path.parent.relative_to(stage).as_posix()
	if parent != ".":
		ensure_dir(parent)
	with fs.open(rel, "wb") as out:
		out.write(path.read_bytes())
	copied += 1
fs.close()
print(f"populated files={copied}")
PY

"$VENV/bin/python" - <<PY
from pyfatfs.PyFatFS import PyFatFS
fs = PyFatFS("$IMG_OUT", read_only=True)
need = [
	"apps/dod-gc/boot.dol",
	"boot.dol",
	"dodgc.iso",
	"xash3d/dod/gamecube.cfg",
	"xash3d/dod/logs",
]
missing = [p for p in need if not fs.exists(p)]
fs.close()
if missing:
	raise SystemExit("image missing: " + ", ".join(missing))
print("verify OK:", ", ".join(need))
PY

cp -f "$IMG_OUT" "$HW/dodgc-swiss-flash-solo.img"
sha256sum "$IMG_OUT" | tee "$HW/dodgc-swiss-flash-solo.img.sha256"
ls -lh "$IMG_OUT" "$HW/dodgc-swiss-flash-solo.img"
echo "Flash: sudo dd if=$IMG_OUT of=/dev/sdX bs=4M status=progress conv=fsync"
echo "Or mount FAT and: scripts/stage-swiss-sd.sh <mount>"
