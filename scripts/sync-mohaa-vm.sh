#!/usr/bin/env bash
# Copy the MOHAA script VM from a mohaa-gc checkout into src/mohaa/.
set -euo pipefail
SRC="${1:?usage: scripts/sync-mohaa-vm.sh /path/to/mohaa-gc}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cp "$SRC/script/include/morpheus.h" "$SRC"/script/src/ms_*.c "$SRC/script/src/ms_internal.h" "$ROOT/src/mohaa/"
sha="$(git -C "$SRC" rev-parse --short HEAD)"
sed -i -E "s/at commit \`[0-9a-f]+\`/at commit \`$sha\`/" "$ROOT/src/mohaa/README.md"
echo "src/mohaa/ now matches mohaa-gc $sha"
