# Ghidra headless: apply function signatures from ghidra_types.h prototypes, BY ADDRESS.
# Run this as its OWN analyzeHeadless pass (before ghidra_rename.py). Signatures applied in a
# dedicated apply-only pass persist to the project DB and then survive ghidra_rename.py's full
# regen; applying them inline within that mega-regen silently fails to persist (some later step
# reverts same-run applies). Idempotent.
import os, re
from ghidra.app.util.cparser.C import CParserUtils
from ghidra.app.cmd.function import ApplyFunctionSignatureCmd
from ghidra.program.model.symbol import SourceType

# Per-firmware inputs come from the environment (set by regen.sh via tools/fwenv.sh);
# defaults target the flagship 2N-update3202MT so the script also works when run standalone.
# Derived from REPO_ROOT (exported by fwenv.sh), not __file__ (undefined in Ghidra's Jython).
_DEF = os.path.join(os.environ.get("REPO_ROOT", ""), "2N-update3202MT")
NAMES = os.environ.get("NAMES_CSV", _DEF + "/input/names.csv")
TYPES = os.environ.get("TYPES_H",   _DEF + "/input/ghidra_types.h")
fm = currentProgram.getFunctionManager()
st = currentProgram.getSymbolTable()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()

N2A = {}
for line in open(NAMES):
    line = line.strip()
    if line and "," in line:
        a, n = line.split(",", 1)
        try: N2A[n] = int(a, 16)
        except: pass

hdr = open(TYPES).read()

# Create enums FIRST so signatures below can use enum types as parameter/return types (the DB
# change persists into the later ghidra_rename.py pass on the same work project). Mirrors the enum
# block in ghidra_rename.py; idempotent (REPLACE). Same "enum Name : short { M = 0xNN, };" syntax.
from ghidra.program.model.data import (EnumDataType, CategoryPath, DataTypeConflictHandler)
_dtm = currentProgram.getDataTypeManager()
_ENUM_SZ = {"char":1,"short":2,"int":4,"long":4}
_ne = 0
for em in re.finditer(r'enum\s+(\w+)\s*(?::\s*(?:unsigned\s+|signed\s+)?(char|short|int|long)\s*)?\{(.*?)\}\s*;', hdr, re.S):
    ed = EnumDataType(CategoryPath("/"), em.group(1), _ENUM_SZ.get(em.group(2) or "int", 4))
    for mm in re.finditer(r'(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)', em.group(3)):
        v = int(mm.group(2), 16) if mm.group(2).lower().startswith("0x") else int(mm.group(2))
        try: ed.add(mm.group(1), v)
        except: pass
    _dtm.addDataType(ed, DataTypeConflictHandler.REPLACE_HANDLER); _ne += 1
print("enums created (apply_sigs):", _ne)

applied = fail = 0
for m in re.finditer(r'(?m)^[A-Za-z_][\w \*]*?([A-Za-z_]\w*)\s*\([^;{}]*\)\s*;', hdr):
    proto = m.group(0).strip(); nm = m.group(1)
    addr = N2A.get(nm)
    if addr is None:
        syms = list(st.getGlobalSymbols(nm))
        addr = syms[0].getAddress().getOffset() if syms else None
    if addr is None:
        print("  no-addr:", nm); fail += 1; continue
    aa = af.getAddress(addr)
    if fm.getFunctionAt(aa) is None:
        print("  no-func:", nm, hex(addr)); fail += 1; continue
    fd = CParserUtils.parseSignature(None, currentProgram, proto)
    if fd is None:
        print("  parse-fail:", nm); fail += 1; continue
    ApplyFunctionSignatureCmd(aa, fd, SourceType.USER_DEFINED).applyTo(currentProgram)
    if fm.getFunctionAt(aa).getSignatureSource() == SourceType.USER_DEFINED:
        applied += 1
    else:
        print("  NOT-stuck:", nm, hex(addr)); fail += 1
print("SIGS APPLIED: %d (fail %d)" % (applied, fail))
