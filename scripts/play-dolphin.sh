#!/usr/bin/env bash
# Familiar Dolphin launcher name; build and recording options live in playtest.sh.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
exec "$SCRIPT_DIR/playtest.sh" "$@"
