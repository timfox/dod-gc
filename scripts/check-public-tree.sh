#!/usr/bin/env bash
# This repository is public and holds the game only. Work that is not part of the game (private
# production tooling, its data, docs and outputs) lives in a separate private repository and must
# never land here, not even in one commit of a branch that later deletes it, because a merge keeps
# that commit in the public history.
#
#   scripts/check-public-tree.sh                  check the files tracked at HEAD
#   scripts/check-public-tree.sh --range A..B     also check every file any commit in A..B adds or edits
#
# Game-side support is allowed (src/dod_film*, tests/dod_film_test.c, spectator and camera fly
# control scripts): the rules below name the private paths exactly, not the word "film".
# Exit 1 and list the offending paths when a rule is broken. Run by .github/workflows/ci.yml.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

RANGE=""
while (( $# )); do
	case "$1" in
		--range) RANGE="${2:?--range needs A..B}"; shift ;;
		-h|--help) sed -n '2,12p' "$0"; exit 0 ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done

# Paths that belong to the private repository (keep in step with the block at the end of .gitignore).
PRIVATE_PATHS='^(film/|tools/film/|out/film/|docs/FILM_PIPELINE\.md$|scripts/film-capture\.sh$|tests/test_film[^/]*\.py$|link-into-dodgc\.sh$)'
# Text that only appears when private tooling is referenced from game files.
PRIVATE_TEXT='tools/film/|FILM_PIPELINE|film-capture\.sh|film-gc|filmgc|documentary'
# Files allowed to name the rules above.
TEXT_EXEMPT=(.gitignore scripts/check-public-tree.sh tests/test_check_public_tree.py .github/workflows/ci.yml)

fail=0

# CI logs of a public repository are public: name the rule a path broke and how many files, never
# the private file names or their text (run it locally to see them: DODGC_GUARD_VERBOSE=1).
report() {
	if [[ "${DODGC_GUARD_VERBOSE:-0}" == 1 ]]; then
		sed 's/^/  /' >&2
	else
		sed -E 's#^(film/|tools/film/|out/film/).*#\1#; s#^(tests/)test_film[^/]*\.py$#\1test_film*.py#' \
			| sort | uniq -c | awk '{printf "  %s (%d file%s)\n", $2, $1, $1 == 1 ? "" : "s"}' >&2
	fi
}

bad_tree="$(git ls-files | grep -E "$PRIVATE_PATHS" || true)"
if [[ -n "$bad_tree" ]]; then
	echo "error: private (non-game) files are tracked in this public repository:" >&2
	report <<<"$bad_tree"
	fail=1
fi

exempt_args=()
for f in "${TEXT_EXEMPT[@]}"; do exempt_args+=(":(exclude)$f"); done
bad_text="$(git grep -nIiE "$PRIVATE_TEXT" -- . "${exempt_args[@]}" || true)"
if [[ -n "$bad_text" ]]; then
	echo "error: game files reference private tooling (keep comments and docs game-only):" >&2
	if [[ "${DODGC_GUARD_VERBOSE:-0}" == 1 ]]; then
		cut -c1-200 <<<"$bad_text" | sed 's/^/  /' >&2
	else
		cut -d: -f1,2 <<<"$bad_text" | sed 's/^/  /' >&2
	fi
	fail=1
fi

if [[ -n "$RANGE" ]]; then
	bad_hist="$(git log --format='%h' --name-only --diff-filter=ACMR "$RANGE" -- \
		| awk 'NF && !/^[0-9a-f]{7,}$/' | grep -E "$PRIVATE_PATHS" | sort -u || true)"
	if [[ -n "$bad_hist" ]]; then
		echo "error: commits in $RANGE add private (non-game) files; deleting them later is not enough," >&2
		echo "  the commits would stay in the public history. Drop them from the branch instead:" >&2
		report <<<"$bad_hist"
		fail=1
	fi
fi

if (( fail )); then
	echo "Private production work belongs in its own private repository, not here." >&2
	exit 1
fi
echo "public tree check: ok${RANGE:+ (and commits in $RANGE)}"
