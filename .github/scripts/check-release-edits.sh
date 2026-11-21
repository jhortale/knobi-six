#!/bin/sh
# A published release never changes: fails when a pull request modifies the CI-owned frontmatter (version, date, tag,
# source, builds) of an existing synths/*/releases/*.md. The notes (the body) and `current` may change; new files are
# not checked. Usage: check-release-edits.sh BASE HEAD   (commits; the checkout needs the history, fetch-depth 0).
set -eu
base=$1 head=$2
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
status=0
for f in $(git diff --name-only --diff-filter=M "$base" "$head" -- 'synths/*/releases/*.md'); do
  git show "$base:$f" > "$tmp/before.md"
  git show "$head:$f" > "$tmp/after.md"
  if ! out=$(node "$root/packages/content/src/cli/check-release-edits.ts" "$tmp/before.md" "$tmp/after.md" 2>&1); then
    echo "$f: $out"
    status=1
  fi
done
exit $status
