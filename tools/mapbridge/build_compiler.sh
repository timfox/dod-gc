#!/usr/bin/env bash
# Build the SDHLT (ZHLT fork) compilers into ./bin (not committed).
set -euo pipefail
H="$(cd "$(dirname "$0")" && pwd)"; T=$(mktemp -d)
git clone -q --depth 1 https://github.com/seedee/SDHLT.git "$T/s"; cd "$T/s" && mkdir b && cd b
cmake .. -DCMAKE_BUILD_TYPE=Release >/dev/null && make -j8 >/dev/null
mkdir -p "$H/bin"; cp ../tools/sd* "$H/bin/"
