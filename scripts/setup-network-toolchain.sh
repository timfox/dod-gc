#!/usr/bin/env bash
# Build native GameCube Ethernet drivers in a private devkitPro overlay.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BASE="${DODGC_SYSTEM_DEVKITPRO:-/opt/devkitpro}"
DEST="${DODGC_NETWORK_TOOLCHAIN:-$ROOT/out/network-toolchain}"
OGC_REV=30134d2f98b1c0d65023089b8cd4ac9de3cbe2c7
FAT_REV=b3f286561f4d2e0495f07af5c0952aba1524cdcd
[[ -x "$BASE/devkitPPC/bin/powerpc-eabi-gcc" ]] || { echo "missing devkitPPC under $BASE" >&2; exit 1; }
mkdir -p "$DEST/sources" "$DEST/devkitpro/libogc2/gamecube/lib"
for entry in devkitPPC tools portlibs; do
	[[ -e "$BASE/$entry" ]] && ln -sfn "$BASE/$entry" "$DEST/devkitpro/$entry"
done
checkout() {
	local url=$1 dir=$2 rev=$3
	if [[ ! -d "$dir/.git" ]]; then
		git clone "$url" "$dir"
	fi
	[[ "$(git -C "$dir" rev-parse HEAD)" == "$rev" ]] || git -C "$dir" checkout --detach "$rev"
}
checkout https://github.com/extremscorner/libogc2.git "$DEST/sources/libogc2" "$OGC_REV"
checkout https://github.com/extremscorner/libfat.git "$DEST/sources/libfat" "$FAT_REV"
export DEVKITPRO="$DEST/devkitpro" DEVKITPPC="$BASE/devkitPPC"
export PATH="$BASE/tools/bin:$DEVKITPPC/bin:$PATH"
# libogc2's unused MP3 player wrappers need the libmad declarations to build.
# They are not linked by this port. libmad itself must match libogc2 if used.
export C_INCLUDE_PATH="${DODGC_LIBMAD_INCLUDE:-$BASE/libogc/include}${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}"
make -C "$DEST/sources/libogc2" cube -j"${DODGC_BUILD_JOBS:-4}"
cp -a "$DEST/sources/libogc2/include/." "$DEVKITPRO/libogc2/gamecube/include/"
cp "$DEST/sources/libogc2/lib/cube/"*.a "$DEVKITPRO/libogc2/gamecube/lib/"
cp "$DEST/sources/libogc2/gamecube_rules" "$DEVKITPRO/libogc2/"
cp "$DEST/sources/libogc2/"*_license.txt "$DEVKITPRO/libogc2/"
make -C "$DEST/sources/libfat" include/libfatversion.h
make -C "$DEST/sources/libfat/libogc2" PLATFORM=gamecube BUILD=gamecube_release -j"${DODGC_BUILD_JOBS:-4}"
cp "$DEST/sources/libfat/libogc2/gamecube/lib/libfat.a" "$DEVKITPRO/libogc2/gamecube/lib/"
cp "$DEST/sources/libfat/include/"*.h "$DEVKITPRO/libogc2/gamecube/include/"
{
	printf 'libogc2=%s\nlibfat=%s\n' "$OGC_REV" "$FAT_REV"
	"$DEVKITPPC/bin/powerpc-eabi-gcc" --version | head -1
	sha256sum "$DEVKITPRO/libogc2/gamecube/lib/libbba.a"
} > "$DEST/versions.txt"
echo "Native network toolchain ready: $DEVKITPRO"
