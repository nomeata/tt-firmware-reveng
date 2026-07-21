# tools/fwenv.sh -- single source of truth for per-firmware paths.
#
# Pick which firmware to operate on with the FW env var (a path under the repo root,
# default 2N-update3202MT, the flagship variant). Everything else is DERIVED from it,
# so the whole pipeline is retargeted by one variable:
#
#   FW=2N-update3202MT tools/fetch_firmware.py   # obtain + verify + unpack the firmware
#   FW=2N-update3202MT tools/make_base.sh        # one-time Ghidra auto-analysis base
#   FW=2N-update3202MT tools/regen.sh            # apply names/types -> out/decomp/
#
# Shell scripts:  source "$(dirname "$0")/fwenv.sh"
# The Ghidra (Jython) scripts read the SAME values from the environment; regen.sh exports
# NAMES_CSV / TYPES_H / NANDBOOT_BIN / DECOMP_OUT / LOADER_BASE below.
#
# Layout per firmware ($FW_DIR):
#   firmware.json       manifest: .upd URL + checksum + loader_base (committed)
#   data/               git-ignored: the .upd and the blobs extracted from it by
#                       tools/fetch_firmware.py (PROG.bin, nandboot.bin, ...)
#   input/names.csv     addr,name  (our RE database, committed)
#   input/ghidra_types.h  structs + signatures + docstrings + enums  (committed)
#   out/decomp/   git-ignored: generated named decompilation (regen.sh output)
#   ghidra/base|work    git-ignored: Ghidra projects (make_base.sh / regen.sh)

FWENV_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
REPO_ROOT="$(cd "$FWENV_DIR/.." && pwd)"

: "${FW:=2N-update3202MT}"
case "$FW" in
  /*) FW_DIR="$FW" ;;
  *)  FW_DIR="$REPO_ROOT/$FW" ;;
esac

PROG_BIN="$FW_DIR/data/PROG.bin"
PROG_NAME="$(basename "$PROG_BIN")"          # ghidra program name == imported file's basename
NANDBOOT_BIN="$FW_DIR/data/nandboot.bin"
NAMES_CSV="$FW_DIR/input/names.csv"
TYPES_H="$FW_DIR/input/ghidra_types.h"
DECOMP_OUT="$FW_DIR/out/decomp"
GHIDRA_BASE="$FW_DIR/ghidra/base"
GHIDRA_WORK="$FW_DIR/ghidra/work"

# LOADER_BASE: the Ghidra image base for this FW's PROG.bin. It MUST equal the true runtime
# base so Ghidra addresses == runtime addresses == pen RAM dumps. PROG.bin is a FLAT load:
# file offset F maps to LOADER_BASE + F. It is authored in the manifest (firmware.json);
# read it from there, falling back to 0x08000000. Override per-run with LOADER_BASE=... .
if [ -z "${LOADER_BASE:-}" ]; then
  LOADER_BASE="$(python3 -c "import json,sys; print(json.load(open(sys.argv[1])).get('loader_base','0x08000000'))" "$FW_DIR/firmware.json" 2>/dev/null || echo 0x08000000)"
fi

# PIPELINE_2N_HAL: the flagship 2N-update3202MT carries a block of hardcoded bootrom-HAL /
# data-global / statechart names inside ghidra_rename.py. Only apply them for that variant;
# every other variant is driven purely by its own input/ files. (This is the one bit of
# variant-specific data still baked into the shared script.)
case "$FW" in
  */2N-update3202MT|2N-update3202MT) : "${PIPELINE_2N_HAL:=1}" ;;
  *)                                 : "${PIPELINE_2N_HAL:=0}" ;;
esac

# Friendly nudge: the pipeline needs the extracted firmware, which is git-ignored.
if [ ! -f "$PROG_BIN" ]; then
  echo "note: $PROG_BIN not found -- run 'FW=$FW tools/fetch_firmware.py' first" >&2
fi

export FW FW_DIR REPO_ROOT PROG_BIN PROG_NAME NANDBOOT_BIN NAMES_CSV TYPES_H DECOMP_OUT GHIDRA_BASE GHIDRA_WORK LOADER_BASE PIPELINE_2N_HAL
