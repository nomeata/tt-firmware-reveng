#!/usr/bin/env python3
"""Acquire and unpack the firmware for one variant, without redistributing it.

The repository never contains the vendor firmware. This script obtains it on the
user's machine from the manifest ``<variant>/firmware.json``:

  1. locate the ``.upd`` in ``<variant>/data/`` (or download it from the manifest
     URL if it is not already there);
  2. verify its size and SHA-256 against the manifest -- this catches the common
     failure of a truncated download (some mirrors return only the first
     megabyte);
  3. unpack the blobs (``PROG.bin``, ``nandboot.bin``, ``producer.bin``,
     ``codepage.bin``) out of the ``.upd``.

The ``.upd`` is an ``ANYKA106`` container that describes its own contents, so the
blob offsets/sizes/names are read from the container -- not hard-coded here or in
the manifest. Because the whole ``.upd`` is verified by SHA-256, every blob sliced
from it is verified too.

Everything it writes lands in ``<variant>/data/``, which is git-ignored, so the
firmware and everything extracted from it stay out of the repository.

Usage:
    tools/fetch_firmware.py [VARIANT]        # default: 2N-update3202MT
    FW=2N-update3202MT tools/fetch_firmware.py
    tools/fetch_firmware.py --offline 2N-update3202MT   # never download
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_VARIANT = "2N-update3202MT"

# ANYKA106 container header (little-endian). The header is a sequence of structs;
# the offsets below are the ones this unpacker needs (verified against the vendor
# Anyka_UPD format template and real .upd files):
#   +0x18 producer{adr,size}   +0x20 NANDboot{adr,size}
#   +0x38 to_NAND{count,adr}   -> a list of 36-byte records, one per named image
# Each to_NAND record: bCompare(1) bBackup(1) binareasize(2) linkaddress(4)
#   size(4) adr(4) name(char[16]) checksum(4)  -> we read size/adr(file offset)/name.
_MAGIC = b"ANYKA106"
_PRODUCER_OFF = 0x18
_NANDBOOT_OFF = 0x20
_TONAND_OFF = 0x38
_TONAND_REC = 36


def _die(msg: str) -> "None":
    print(f"fetch_firmware: error: {msg}", file=sys.stderr)
    raise SystemExit(1)


def _int(v: "int | str") -> int:
    return v if isinstance(v, int) else int(str(v), 0)


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _resolve_variant(arg: "str | None") -> Path:
    """Accept a bare variant name, a path, or the FW env var (with or without a legacy fw/ prefix)."""
    raw = arg or os.environ.get("FW") or DEFAULT_VARIANT
    raw = raw[3:] if raw.startswith("fw/") else raw  # tolerate the old fw/<name> form
    cand = Path(raw)
    for p in (cand, REPO_ROOT / raw):
        if (p / "firmware.json").is_file():
            return p
    _die(f"no <variant>/firmware.json found for {raw!r}")
    raise AssertionError  # unreachable


def _download(urls: list[str], dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    last = None
    for url in urls:
        try:
            print(f"  downloading {url}")
            tmp = dest.with_suffix(dest.suffix + ".part")
            with urllib.request.urlopen(url) as resp, open(tmp, "wb") as out:  # noqa: S310
                while True:
                    chunk = resp.read(1 << 20)
                    if not chunk:
                        break
                    out.write(chunk)
            tmp.replace(dest)
            return
        except Exception as exc:  # noqa: BLE001 -- try the next mirror
            last = exc
            print(f"    failed: {exc}")
    _die(f"could not download {dest.name} from any URL ({last})")


def _container_blobs(data: bytes) -> "dict[str, tuple[int, int]]":
    """Read blob {name: (file_offset, size)} from the ANYKA106 container itself."""
    if data[:8] != _MAGIC:
        _die(f"not an ANYKA106 container (magic {data[:8]!r})")

    def u32(o: int) -> int:
        return struct.unpack_from("<I", data, o)[0]

    blobs: "dict[str, tuple[int, int]]" = {}
    p_adr, p_size = u32(_PRODUCER_OFF), u32(_PRODUCER_OFF + 4)
    if p_size:
        blobs["producer.bin"] = (p_adr, p_size)
    nb_adr, nb_size = u32(_NANDBOOT_OFF), u32(_NANDBOOT_OFF + 4)
    if nb_size:
        blobs["nandboot.bin"] = (nb_adr, nb_size)
    # to_NAND named images (PROG, codepage, ...)
    count, adr = u32(_TONAND_OFF), u32(_TONAND_OFF + 4)
    if 0 < count <= 64 and 0 < adr < len(data):
        for i in range(count):
            r = adr + i * _TONAND_REC
            if r + _TONAND_REC > len(data):
                break
            size = u32(r + 8)
            off = u32(r + 0xC)
            name = data[r + 0x10 : r + 0x20].split(b"\0", 1)[0].decode("latin1").strip()
            if name and size:
                blobs[f"{name}.bin"] = (off, size)
    return blobs


def main() -> None:
    ap = argparse.ArgumentParser(description="Download, verify and unpack one firmware variant.")
    ap.add_argument("variant", nargs="?", help="variant name (default: %(default)s)", default=None)
    ap.add_argument("--offline", action="store_true", help="never download; require the .upd to be present")
    args = ap.parse_args()

    fw_dir = _resolve_variant(args.variant)
    manifest = json.loads((fw_dir / "firmware.json").read_text())
    data_dir = fw_dir / "data"
    data_dir.mkdir(parents=True, exist_ok=True)

    upd_meta = manifest["upd"]
    upd_path = data_dir / upd_meta["filename"]
    print(f"variant {manifest['variant']}  ->  {data_dir}")

    # 1. obtain the .upd
    if not upd_path.exists():
        if args.offline:
            _die(f"{upd_path} not present and --offline given")
        _download(upd_meta["urls"], upd_path)
    else:
        print(f"  using existing {upd_path.name}")

    # 2. verify the .upd (size first: cheap, and the truncation tell-tale)
    size = _int(upd_meta["size"])
    actual = upd_path.stat().st_size
    if actual != size:
        _die(f".upd size {actual} != expected {size}; a truncated/partial download is the usual cause")
    sha = _sha256(upd_path)
    if sha != upd_meta["sha256"]:
        _die(f".upd sha256 {sha} != expected {upd_meta['sha256']}")
    print(f"  ok  {upd_path.name}: {actual} bytes, sha256 {sha[:16]}…")

    # 3. unpack the blobs the container describes (verified transitively via the .upd sha)
    data = upd_path.read_bytes()
    blobs = _container_blobs(data)
    if not blobs:
        _die("no blobs found in the ANYKA106 container")
    for name, (off, blob_size) in blobs.items():
        if off + blob_size > len(data):
            _die(f"{name}: container points past EOF (offset {off:#x} + {blob_size:#x}); truncated .upd?")
        (data_dir / name).write_bytes(data[off : off + blob_size])
        print(f"  ok  {name}: {blob_size} bytes @ {off:#08x}")

    print("done. blobs are in", data_dir, "(git-ignored).")


if __name__ == "__main__":
    main()
