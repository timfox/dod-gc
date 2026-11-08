#!/usr/bin/env bash
# Prove each DoD skybox on CapFaces flash: fixed-size skyname patch + 6 CMPR sides in bootstrap.
# Inject only the active sky's CMPs (all 78 tips MEM1 searchpaths).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"
export PATH="/usr/bin:/bin:/usr/local/bin:${HOME}/.local/bin:${PATH}"
export DISPLAY="${DISPLAY:-:0}"

CACHE="${XASH3D_GC_ISO_CACHE:-/mnt/datasets/dodgc-iso-build}"
LEAN="${DODGC_LEAN_DOD:-$CACHE/dod-menu-min}"
ATLAS="$DODGC_ROOT/playtests/map-spawn-atlas/skyboxes"
DURATION="${SKY_DURATION:-36}"
SKY_EYE="${SKY_EYE:-2056 336 120 -45 211}"
FLASH_ORIG="${FLASH_ORIG:-/tmp/dodgc-flash-orig.bsp}"
CMP_ALL="${CMP_ALL:-/tmp/dodgc-sky-cmp-all}"

mkdir -p "$ATLAS"/{screenshots,videos,notes}
mkdir -p "$LEAN/maps" "$LEAN/media"

# Canonical flash + tipfix DOL + lean WADs
[[ -f "$FLASH_ORIG" ]] || cp -f "$CACHE/dod-lean/maps/dod_flash.bsp" "$FLASH_ORIG"
cp -f /run/media/tim/Expansion1/xash3d-gc/OUT/bin/dodgc.dol "$DODGC_DOL"
mkdir -p /tmp/dodgc-wad-park
for w in "$LEAN"/*.wad; do
	[[ -f "$w" ]] || continue
	bn=$(basename "$w")
	case "$bn" in gfx.wad|hlbasics.wad|dod_flash.wad) ;;
		*) mv -f "$w" /tmp/dodgc-wad-park/ 2>/dev/null || true ;;
	esac
done
[[ -f "$LEAN/dod_flash.wad" ]] || cp -f "$CACHE/dod-lean/dod_flash.wad" "$LEAN/"

patch_flash_sky() {
	local sky="$1"
	python3 - "$sky" "$FLASH_ORIG" "$LEAN/maps/dod_flash.bsp" <<'PY'
import re, struct, sys
from pathlib import Path
sky, src, dst = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
raw = bytearray(src.read_bytes())
off, length = struct.unpack_from("<ii", raw, 4)
text = raw[off : off + length].split(b"\x00")[0].decode("latin1")

def sub_key(s, key, val):
    pat = re.compile(rf'("{re.escape(key)}"\s+")([^"]*)(")')
    m = pat.search(s)
    if not m:
        raise SystemExit(f"missing {key}")
    return s[: m.start(2)] + val + s[m.end(2) :]

new_text = sub_key(text, "wad", "dod_flash.wad;")
new_text = sub_key(new_text, "skyname", sky)
nb = new_text.encode("latin1")
if len(nb) > length - 1:
    raise SystemExit(f"text too long {len(nb)} > {length - 1}")
pad = (length - 1) - len(nb)
filled = nb + (b"\n" * pad) + b"\x00"
raw[off : off + length] = filled
dst.write_bytes(raw)
print(f"patched sky={sky!r} text={len(nb)} pad={pad} file={len(raw)}")
PY
}

SKIES=(ava dashnight256 dmcw grnplsnt jagd killertomato kraftstoff morningdew morningdew_ rubble sildom2 snowcliff st)

echo -e "sky\tstatus\tloaded\tmean\tbytes\tsearchpaths" >"$ATLAS/results.tsv"
pkill -9 -f dolphin-emu 2>/dev/null || true
sleep 1

for sky in "${SKIES[@]}"; do
	echo "==== SKYBOX PROOF $sky ===="
	rm -f "$LEAN/maps"/dod_*.bsp "$LEAN/media"/sky_*.cmp
	cp -f "$CMP_ALL"/sky_${sky}{rt,bk,lf,ft,up,dn}.cmp "$LEAN/media/"
	patch_flash_sky "$sky"
	echo 'dod_flash "Flash"' >"$LEAN/maps.lst"

	if ! bash "$SCRIPT_DIR/build-lean-iso.sh" --menu-only --listen dod_flash \
		--world-render --skip-startup-vids --skip-stage --dumpeye "$SKY_EYE" \
		>"/tmp/skyiso-${sky}.log" 2>&1; then
		echo -e "${sky}\tFAIL_ISO\t0\t-\t0\t-" | tee -a "$ATLAS/results.tsv"
		continue
	fi
	rg -n 'sky mirror: injected' "/tmp/skyiso-${sky}.log" | tail -1 || true

	pkill -9 -f dolphin-emu 2>/dev/null || true
	sleep 0.3
	bash "$SCRIPT_DIR/probe-playtest.sh" --route "skybox-${sky}" --duration "$DURATION" \
		--ready-regex 'sky .+ loaded|missing media/sky|CapFaces filter drawn=|FATAL' \
		"$DODGC_ROOT/out/dodgc-menu.iso" >"/tmp/skyprobe-${sky}.log" 2>&1 || true
	tail -5 "/tmp/skyprobe-${sky}.log" || true

	shot=$(ls -d "$DODGC_ROOT"/playtests/screenshots/*skybox-${sky} 2>/dev/null | sort | tail -1 || true)
	if [[ -z "${shot:-}" || ! -f "$shot/final.png" ]]; then
		echo -e "${sky}\tNO_SHOT\t0\t-\t0\t-" | tee -a "$ATLAS/results.tsv"
		continue
	fi
	cp -f "$shot/final.png" "$ATLAS/screenshots/${sky}.png"
	cp -f "$shot/dolphin.log" "$ATLAS/notes/${sky}.log"
	vid=$(ls "$DODGC_ROOT"/playtests/videos/*skybox-${sky}*.mp4 2>/dev/null | sort | tail -1 || true)
	[[ -n "${vid:-}" ]] && cp -f "$vid" "$ATLAS/videos/${sky}.mp4"

	python3 - "$sky" "$shot" "$ATLAS/results.tsv" <<'PY'
import re, sys
from pathlib import Path
from PIL import Image
import numpy as np
sky, shot, tsv = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
text = Path(shot / "dolphin.log").read_bytes().replace(b"\x00", b"").decode("latin1", errors="ignore")
im = np.array(Image.open(shot / "final.png").convert("RGB"))
mean = tuple(int(x) for x in im.mean(axis=(0, 1)).round())
loaded = bool(re.search(rf"sky {re.escape(sky)} loaded \(6 x 128x128 CMPR\)", text))
nbytes = (shot / "final.png").stat().st_size
sp = re.search(r"mem stage=searchpaths total=([0-9.]+) Kb", text)
magenta = mean[0] > 200 and mean[2] > 200 and mean[1] < 80
if magenta:
    status = "MAGENTA"
elif loaded and nbytes > 20000:
    status = "SKY_OK"
elif loaded:
    status = "LOADED_THIN"
elif f"missing media/sky_{sky}" in text:
    status = "MISSING"
else:
    status = "UNKNOWN"
spv = sp.group(1) if sp else "-"
print(f"{sky}\t{status}\t{int(loaded)}\t{mean}\t{nbytes}\t{spv}")
tsv.open("a").write(f"{sky}\t{status}\t{int(loaded)}\t{mean}\t{nbytes}\t{spv}\n")
PY
done

# restore lean media/flash defaults
cp -f "$CMP_ALL"/sky_*.cmp "$LEAN/media/" 2>/dev/null || true
cp -f "$FLASH_ORIG" "$LEAN/maps/dod_flash.bsp"
echo "==== DONE ===="
cat "$ATLAS/results.tsv"
