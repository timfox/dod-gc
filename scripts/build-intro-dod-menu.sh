#!/usr/bin/env bash
# Same HL1 engine/game code + same HL1 intro, then Day of Defeat menu.
# Produces out/dodgc.dol (HL1SDK archives, gamedir=dod) and out/dodgc.iso.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/env.sh"

dodgc_require_content

ENGINE_HLSDK_VALVE="$DODGC_ENGINE/OUT/hlsdk-gamecube/valve"
ENGINE_HLSDK_DOD="$DODGC_ENGINE/OUT/hlsdk-gamecube/dod"
ENGINE_DOL="$DODGC_ENGINE/OUT/bin/dodgc.dol"

if [[ ! -s "$ENGINE_HLSDK_VALVE/dlls/libhl_gamecube_ppc.a" ]]; then
	echo "error: missing HL1 HLSDK archives at $ENGINE_HLSDK_VALVE" >&2
	echo "  build HL1 first: (cd engine && scripts/hlsdk-gamecube-build.sh)" >&2
	exit 1
fi

echo "==> bootstrap DoD HLSDK install from HL1 valve archives (same code)"
mkdir -p "$ENGINE_HLSDK_DOD/dlls" "$ENGINE_HLSDK_DOD/cl_dlls"
rsync -a "$ENGINE_HLSDK_VALVE/dlls/" "$ENGINE_HLSDK_DOD/dlls/"
rsync -a "$ENGINE_HLSDK_VALVE/cl_dlls/" "$ENGINE_HLSDK_DOD/cl_dlls/"
# Keep export wrappers available for the dod install tree.
for gc_exp in gamecube_server_exports.inc gamecube_client_exports.inc; do
	src="$DODGC_ENGINE/scripts/gamecube-exports/$gc_exp"
	[[ -f "$src" ]] || continue
	case "$gc_exp" in
		gamecube_server_exports.inc) dest="$ENGINE_HLSDK_DOD/dlls/$gc_exp" ;;
		*) dest="$ENGINE_HLSDK_DOD/cl_dlls/$gc_exp" ;;
	esac
	cp -f "$src" "$dest"
done

echo "==> link dodgc.dol (XASH_GAMEDIR=dod, HL1SDK code)"
cd "$DODGC_ENGINE"
XASH3D_GC_SKIP_DISC_BUILD=1 \
HLSDK_GAMECUBE_GAMEDIR=dod \
	bash scripts/build-gamecube.sh --game-profile=dod

[[ -f "$ENGINE_DOL" ]] || { echo "error: missing $ENGINE_DOL" >&2; exit 1; }
cp -f "$ENGINE_DOL" "$DODGC_DOL"
[[ -f "$DODGC_ENGINE/OUT/bin/gamecube-handoff.txt" ]] && \
	cp -f "$DODGC_ENGINE/OUT/bin/gamecube-handoff.txt" "$DODGC_HANDOFF"

echo "==> stage DoD retail + HL1 intro media → $XASH3D_GC_ISO_CACHE"
BUILD_DATA="$XASH3D_GC_ISO_CACHE/dod"
BUILD_VALVE="$XASH3D_GC_ISO_CACHE/valve"
mkdir -p "$BUILD_DATA" "$BUILD_DATA/media"
rsync -a --delete \
	--exclude 'SAVE' --exclude 'logs' --exclude '*.tmp' \
	"$DODGC_CONTENT_DOD/" "$BUILD_DATA/"

# Same intro as HL1: Sierra/Valve startup vids + logo strip.
rsync -a \
	"$DODGC_CONTENT_VALVE/media/valve.avi" \
	"$DODGC_CONTENT_VALVE/media/logo.avi" \
	"$DODGC_CONTENT_VALVE/media/StartupVids.txt" \
	"$BUILD_DATA/media/" 2>/dev/null || true
# Fallback StartupVids if missing.
if [[ ! -f "$BUILD_DATA/media/StartupVids.txt" ]]; then
	printf '%s\n' 'media/valve.avi' > "$BUILD_DATA/media/StartupVids.txt"
fi
# Prefer DoD logo_game; also expose as logo.tga for GC menu baker.
if [[ -f "$BUILD_DATA/resource/logo_game.tga" && ! -f "$BUILD_DATA/resource/logo.tga" ]]; then
	cp -f "$BUILD_DATA/resource/logo_game.tga" "$BUILD_DATA/resource/logo.tga"
fi
# Borrow HL1 background layout tiles so the GC menu baker has a real sheet,
# then overlay DoD title art (logo_game already staged).
if [[ ! -f "$BUILD_DATA/resource/BackgroundLayout.txt" \
	&& -f "$DODGC_CONTENT_VALVE/resource/BackgroundLayout.txt" ]]; then
	cp -f "$DODGC_CONTENT_VALVE/resource/BackgroundLayout.txt" \
		"$BUILD_DATA/resource/BackgroundLayout.txt"
	mkdir -p "$BUILD_DATA/resource/background"
	rsync -a "$DODGC_CONTENT_VALVE/resource/background/" \
		"$BUILD_DATA/resource/background/" || true
fi

# valve/ basedir sibling for fallbacks DoD liblist expects.
if [[ -d "$DODGC_CONTENT_VALVE" ]]; then
	mkdir -p "$BUILD_VALVE"
	rsync -a --delete \
		--exclude 'SAVE' --exclude 'logs' --exclude '*.tmp' \
		"$DODGC_CONTENT_VALVE/" "$BUILD_VALVE/"
fi

tmp_iso="$XASH3D_GC_ISO_CACHE/dodgc-intro-menu-$$.iso"
trap 'rm -f "$tmp_iso"' EXIT

echo "==> package $DODGC_ISO (gamedir=dod, play cfg, HL1 intro preserved)"
TMPDIR="$XASH3D_GC_ISO_CACHE" \
	python3 "$DODGC_ENGINE/scripts/build-gamecube-disc.py" \
		--dol "$DODGC_DOL" \
		--output "$tmp_iso" \
		--data "$BUILD_DATA" \
		--gamedir dod \
		--play

publish_tmp="$DODGC_ISO.tmp.$$"
cp --reflink=auto "$tmp_iso" "$publish_tmp"
mv -f "$publish_tmp" "$DODGC_ISO"
trap - EXIT
rm -f "$tmp_iso"

{
	echo "dod-gc intro→DoD-menu disc"
	echo "built_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "iso=$DODGC_ISO"
	echo "iso_bytes=$(wc -c < "$DODGC_ISO" | tr -d ' ')"
	echo "dol=$DODGC_DOL"
	echo "code=HL1SDK-valve-archives"
	echo "gamedir=dod"
	echo "intro=valve.avi+logo.avi from HL1"
	echo "engine_commit=$(dodgc_engine_sha)"
	echo "dodgc_commit=$(dodgc_repo_sha)"
} | tee "$DODGC_OUT/dodgc-iso-handoff.txt"

echo "==> ready: $DODGC_ISO"
echo "    next: scripts/playtest.sh --shot --note 'HL1 intro then DoD menu'"
