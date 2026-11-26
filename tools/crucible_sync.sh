#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 David Behnke
# Merge RadioFoundry's main into a new sync branch off Crucible's main (CRUCIBLE.md, "Merging RadioFoundry").
#
#   tools/crucible_sync.sh [UPSTREAM_REF]     default upstream/main (fetched first when the remote exists)
#
# Conflicts on files Crucible deleted (upstream's workflows, issue forms) resolve by keeping them deleted; any other
# conflict stops with the list and leaves the merge for a person. Prints "sync: up to date", "sync: merged <branch>" or
# "sync: conflicts" and exits 0, 0 and 1. Pushes nothing.
set -eu
ref=${1:-upstream/main}
case "$ref" in upstream/*) git fetch -q upstream ;; esac
if git merge-base --is-ancestor "$ref" HEAD; then
	echo "sync: up to date with $ref ($(git rev-parse --short=8 "$ref"))"
	exit 0
fi
branch="sync/upstream-$(date -u +%y%m%d)-$(git rev-parse --short=8 "$ref")"
git switch -q -c "$branch"
msg="Merge RadioFoundry $(git rev-parse --short=8 "$ref") into Crucible"
if git merge -q --no-ff --no-edit -m "$msg" "$ref"; then
	echo "sync: merged $branch"
	exit 0
fi
# "deleted by us" (DU): Crucible removed it, upstream changed it. Keep it removed
git diff --name-only --diff-filter=U | while read -r f; do
	if [ "$(git status --porcelain -- "$f" | cut -c1-2)" = "DU" ]; then
		git rm -q -- "$f"
		echo "sync: kept deleted: $f"
	fi
done
left=$(git diff --name-only --diff-filter=U)
if [ -z "$left" ]; then
	git commit -q --no-edit
	echo "sync: merged $branch"
	exit 0
fi
echo "sync: conflicts in:"
echo "$left" | sed 's/^/  /'
exit 1
