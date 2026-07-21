#!/usr/bin/env bash
# ONE-TIME cached base: $FW/data/PROG.bin + ARM:LE:32:v5t auto-analysis, NO manual edits.
# Pristine input to the pure regen (regen.sh copies it and applies names/types/sigs from source).
# Retarget with FW=... (see tools/fwenv.sh). Default FW=2N-Update3202.
set -uo pipefail
source "$(dirname "$0")/fwenv.sh"
echo "make_base: FW=$FW  PROG=$PROG_BIN  program-name=$PROG_NAME  LOADER_BASE=$LOADER_BASE"
[ -f "$PROG_BIN" ] || { echo "missing PROG bin: $PROG_BIN"; exit 1; }
rm -rf "$GHIDRA_BASE"; mkdir -p "$GHIDRA_BASE"
ghidra-analyzeHeadless "$GHIDRA_BASE" tiptoi_base \
  -import "$PROG_BIN" \
  -processor ARM:LE:32:v5t \
  -loader BinaryLoader -loader-baseAddr "$LOADER_BASE" \
  -analysisTimeoutPerFile 3600
