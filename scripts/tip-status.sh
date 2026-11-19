#!/usr/bin/env bash
# Is a source checkout at the tip of the branch it tracks? Sourced by build-info.sh and build-dodgc.sh.
#
#   dodgc_tip_status DIR REF     prints one of
#       tip=yes
#       tip=NO (behind N: origin/REF is SHA)       the checkout is missing N commits of the branch
#       tip=NO (origin/REF is SHA; the checkout has diverged)
#       tip=ahead (the checkout has commits origin/REF does not)
#       tip=n/a (REF is not a branch)              a pinned commit or tag: nothing to be behind
#       tip=unknown (REASON)                       offline, no origin, ...
#   dodgc_engine_is_stale DIR REF   exit 0 when dodgc_tip_status starts with tip=NO
#
# It asks the remote (git ls-remote, DODGC_TIP_TIMEOUT seconds, default 15), so a clone that was never
# fetched is still caught: the point is that a rebuild must not silently use an old engine.

dodgc_tip_status() {
	local dir="$1" ref="$2" head remote line behind
	[[ -d "$dir/.git" ]] || { echo "tip=unknown (no git checkout)"; return 0; }
	head="$(git -C "$dir" rev-parse HEAD 2>/dev/null)" || { echo "tip=unknown (no HEAD)"; return 0; }
	if ! line="$(timeout "${DODGC_TIP_TIMEOUT:-15}" git -C "$dir" ls-remote --heads origin "refs/heads/$ref" 2>/dev/null)"; then
		echo "tip=unknown (cannot reach origin)"
		return 0
	fi
	if [[ -z "$line" ]]; then
		echo "tip=n/a ($ref is not a branch of origin)"
		return 0
	fi
	remote="${line%%[[:space:]]*}"
	if [[ "$head" == "$remote" ]]; then
		echo "tip=yes"
	elif git -C "$dir" merge-base --is-ancestor "$remote" "$head" 2>/dev/null; then
		echo "tip=ahead (the checkout has commits origin/$ref does not)"
	elif git -C "$dir" cat-file -e "$remote^{commit}" 2>/dev/null && git -C "$dir" merge-base --is-ancestor "$head" "$remote" 2>/dev/null; then
		behind="$(git -C "$dir" rev-list --count "$head..$remote")"
		echo "tip=NO (behind $behind: origin/$ref is ${remote:0:10})"
	else
		echo "tip=NO (origin/$ref is ${remote:0:10}; the checkout is behind or has diverged)"
	fi
}

dodgc_engine_is_stale() {
	[[ "$(dodgc_tip_status "$1" "$2")" == tip=NO* ]]
}
