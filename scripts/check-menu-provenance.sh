#!/usr/bin/env bash
# The main menu must come from the mainui commit the engine pins, and no build may carry the retired
# "Enable Steam Networking" Create Game checkbox (it lives on a stale mainui branch).
#   check-menu-provenance.sh --engine DIR   the engine's 3rdparty/mainui is at the commit the engine records
#                                            and its sources do not contain the string
#   check-menu-provenance.sh --dol FILE     the linked DOL does not contain the string
# Exit 1 with a message when a check fails. DODGC_ALLOW_STALE_MAINUI=1 skips the engine checks.
set -euo pipefail
BAD="Enable Steam Networking"
fail=0
while (( $# )); do
	case "$1" in
		--engine)
			E="${2:?}"; shift 2
			[[ "${DODGC_ALLOW_STALE_MAINUI:-0}" == 1 ]] && continue
			if [[ -d "$E/3rdparty/mainui" ]]; then
				want=$(git -C "$E" ls-tree HEAD 3rdparty/mainui 2>/dev/null | awk '{print $3}')
				have=$(git -C "$E/3rdparty/mainui" rev-parse HEAD 2>/dev/null || true)
				if [[ -n "$want" && "$want" != "$have" ]]; then
					echo "error: $E/3rdparty/mainui is at ${have:-unknown}, the engine pins $want." >&2
					echo "  fix: git -C $E submodule update --init 3rdparty/mainui" >&2
					fail=1
				fi
				if grep -rIl --include='*.cpp' --include='*.h' --include='*.res' --include='*.txt' -F -- "$BAD" "$E/3rdparty/mainui" 2>/dev/null | head -3 | grep -q .; then
					echo "error: mainui sources under $E contain \"$BAD\" (stale mainui branch)." >&2
					fail=1
				fi
			fi ;;
		--dol)
			F="${2:?}"; shift 2
			if grep -aqF -- "$BAD" "$F"; then
				echo "error: $F contains \"$BAD\": the menu was built from a stale mainui." >&2
				fail=1
			fi ;;
		*) echo "usage: $0 [--engine DIR] [--dol FILE]" >&2; exit 2 ;;
	esac
done
exit $fail
