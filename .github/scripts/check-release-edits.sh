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
# captured first: a failing git diff inside the for list would not stop the script (an unknown base must fail)
files=$(git diff --name-only --diff-filter=M "$base" "$head" -- 'synths/*/releases/*.md') || {
  echo "check-release-edits: git diff $base $head failed" >&2
  exit 1
}
for f in $files; do
  git show "$base:$f" > "$tmp/before.md"
  git show "$head:$f" > "$tmp/after.md"
  if ! out=$(node "$root/packages/content/src/cli/check-release-edits.ts" "$tmp/before.md" "$tmp/after.md" 2>&1); then
    echo "$f: $out"
    status=1
  fi
done
exit $status
