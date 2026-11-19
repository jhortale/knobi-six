#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# bundle_exports: the plugin binaries (AU, VST3) export none of the core's symbols (the firmware's non-static functions,
# kc_*): only the plugin's entry points. $1 the core's static library, then the binaries.
set -e
core=$1; shift
syms=$(mktemp)
nm -g --defined-only "$core" 2>/dev/null | awk 'NF >= 3 { print $3 }' | sort -u > "$syms"
n=$(wc -l < "$syms" | tr -d ' ')
[ "$n" -gt 100 ] || { echo "bundle_exports: only $n symbols in $core?"; rm -f "$syms"; exit 1; }
bad=0
for b in "$@"; do
    exp=$(mktemp)
    nm -gU "$b" | awk 'NF >= 3 { print $3 }' | sort -u > "$exp"
    both=$(comm -12 "$syms" "$exp" | wc -l | tr -d ' ')
    echo "bundle_exports: $(basename "$b"): $(wc -l < "$exp" | tr -d ' ') exported, $both of the core's $n"
    comm -12 "$syms" "$exp" | head -5
    [ "$both" -eq 0 ] || bad=1
    rm -f "$exp"
done
rm -f "$syms"
exit $bad
