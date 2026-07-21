#!/usr/bin/env bash
# Measure RE progress per firmware: % of functions named + docstring/struct counts.
# Reads the regenerated decompilation (out/decomp_named) and input/ghidra_types.h.
# Run 'FW=<variant> tools/regen.sh' first so out/decomp_named exists.
cd "$(dirname "$0")/.."      # repo root
for fwdir in fw/*/; do
  nm="$(basename "$fwdir")"
  docs="$fwdir/input/ghidra_types.h"
  d="$fwdir/out/decomp_named"
  [ -d "$d" ] || continue
  total=$(ls "$d"/*.c 2>/dev/null | wc -l)
  [ "$total" -gt 0 ] || continue
  named=$(grep -hoE '^// [A-Za-z_][A-Za-z0-9_]+ @' "$d"/*.c 2>/dev/null | grep -vE '// (FUN_|thunk_FUN_|_?DAT_)' | wc -l)
  ndoc=$( [ -f "$docs" ] && grep -cE '^/\* 0x[0-9a-f]+ ' "$docs" || echo 0)
  nstruct=$( [ -f "$docs" ] && grep -cE '^struct ' "$docs" || echo 0)
  awk -v n="$nm" -v a="$named" -v t="$total" -v dc="$ndoc" -v st="$nstruct" \
    'BEGIN{printf "%-18s named %4d/%4d (%5.1f%%)   docstrings %d   structs %d\n", n, a, t, 100*a/t, dc, st}'
done
