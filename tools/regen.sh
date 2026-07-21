#!/usr/bin/env bash
# PURE regen:  $FW/out/decomp/ = f( $FW/data/PROG.bin [via $FW/ghidra/base],
#                                         $FW/input/ghidra_types.h, $FW/input/names.csv )
# Fresh throwaway project from the pristine base each run -> deterministic, no accumulated state.
# ATOMIC + FAIL-LOUD: decompile into decomp.new; swap into place ONLY on verified completion,
# so a crashed run can never leave stale/partial output silently.
# Retarget with FW=... (see tools/fwenv.sh). Default FW=2N-Update3202.
set -uo pipefail
HERE="$(dirname "$0")"
source "$HERE/fwenv.sh"
[ -d "$GHIDRA_BASE/tiptoi_base.rep" ] || { echo "no base for FW=$FW -- run 'FW=$FW $HERE/make_base.sh' first"; exit 1; }
HA=ghidra-analyzeHeadless
rm -rf "$GHIDRA_WORK"; mkdir -p "$(dirname "$GHIDRA_WORK")"; cp -r "$GHIDRA_BASE" "$GHIDRA_WORK"
find "$GHIDRA_WORK" -name '*.lock' -delete 2>/dev/null || true

# apply_sigs.py / ghidra_rename.py read these from the environment (see tools/fwenv.sh).
# LOADER_BASE lets ghidra_rename.py shift its hardcoded PROG-space labels to match the base.
export NAMES_CSV TYPES_H NANDBOOT_BIN LOADER_BASE PIPELINE_2N_HAL

echo "[1/2] signatures ..."
sig=$($HA "$GHIDRA_WORK" tiptoi_base -process "$PROG_NAME" -noanalysis -postScript apply_sigs.py -scriptPath "$HERE" 2>&1)
echo "$sig" | grep -E 'SIGS APPLIED' || { echo "FAIL: apply_sigs"; echo "$sig" | tail; exit 1; }

echo "[2/2] names/structs/docstrings/globals + decompile -> decomp.new ..."
rm -rf "$DECOMP_OUT.new"
out=$(DECOMP_OUT="$DECOMP_OUT.new" $HA "$GHIDRA_WORK" tiptoi_base -process "$PROG_NAME" -noanalysis -postScript ghidra_rename.py -scriptPath "$HERE" 2>&1)
echo "$out" | grep -E 'renamed firmware|built structs|applied docstrings|typed globals|DECOMP_NAMED' || true
if echo "$out" | grep -q 'DECOMP_NAMED DONE'; then
  rm -rf "$DECOMP_OUT"
  mv "$DECOMP_OUT.new" "$DECOMP_OUT"
  echo "OK -> $DECOMP_OUT ($(ls "$DECOMP_OUT"/*.c | wc -l) files)"
else
  echo "FAIL: decompile incomplete -- decomp left UNTOUCHED"; echo "$out" | tail -15
  rm -rf "$DECOMP_OUT.new"; exit 1
fi
