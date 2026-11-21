#!/bin/sh
# Does this push or pull request touch the second core (CPU1) or what decides the firmware image? Prints true/false.
# build.yml's cpu1-emu job (~21 min) runs only then, on main, and on manual dispatch; elsewhere it is skipped, which
# satisfies main's required check. Usage: cpu1-changed.sh EVENT REF BASE   (BASE: the PR's base sha or the push's
# "before" sha; empty or all zeros means unknown, which counts as changed).
set -eu
event=$1 ref=$2 base=${3:-}
case "$event:$ref" in
  workflow_dispatch:*|push:refs/heads/main) echo true; exit 0 ;;
esac
if [ -z "$base" ] || [ "$base" = 0000000000000000000000000000000000000000 ] || ! git cat-file -e "$base^{commit}" 2>/dev/null; then
  echo true; exit 0
fi
paths='^(firmware/hal/|firmware/loader/|firmware/app\.ld$|firmware/crt0\.S$|firmware/src/(platform|cpu1_probe|engine)\.c$|firmware/src/params/globals\.[ch]$|tools/build\.py$|build\.sh$|tests/cpu1_emu/|\.github/workflows/build\.yml$|\.github/scripts/cpu1-changed\.sh$)'
if git diff --name-only "$base" HEAD | grep -Eq "$paths"; then echo true; else echo false; fi
