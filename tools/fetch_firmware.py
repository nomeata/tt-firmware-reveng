#!/usr/bin/env python3
"""Acquire and unpack the firmware for one variant, without redistributing it.

The repository never contains the vendor firmware. This script obtains it on the
user's machine from the manifest ``fw/<variant>/firmware.json``:

  1. locate the ``.upd`` in ``fw/<variant>/data/`` (or download it from the
     manifest URL if it is not already there);
  2. verify its size and SHA-256 against the manifest -- this catches the common
     failure of a truncated download (some mirrors return only the first
     megabyte);
  3. slice the named blobs (``PROG.bin``, ``nandboot.bin``, ...) out of the
     ``.upd`` at the manifest offsets and verify each blob's SHA-256.

Everything it writes lands in ``fw/<variant>/data/``, which is git-ignored, so
the firmware and everything extracted from it stay out of the repository.

Usage:
    tools/fetch_firmware.py [VARIANT]        # default: 2N-update3202MT
    FW=fw/2N-update3202MT tools/fetch_firmware.py
    tools/fetch_firmware.py --offline 2N-update3202MT   # never download

Exit status is non-zero on any checksum or size mismatch.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_VARIANT = "2N-update3202MT"


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
    """Accept a bare variant name, an ``fw/<name>`` path, or the FW env var."""
    raw = arg or os.environ.get("FW") or DEFAULT_VARIANT
    cand = Path(raw)
    for p in (cand, REPO_ROOT / raw, REPO_ROOT / "fw" / raw):
        if (p / "firmware.json").is_file():
            return p
    _die(f"no fw/<variant>/firmware.json found for {raw!r}")
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


def _verify(path: Path, size: int, sha: str, what: str) -> None:
    actual_size = path.stat().st_size
    if actual_size != size:
        _die(
            f"{what}: size {actual_size} != expected {size} "
            f"({path}); a truncated/partial download is the usual cause"
        )
    actual_sha = _sha256(path)
    if actual_sha != sha:
        _die(f"{what}: sha256 {actual_sha} != expected {sha} ({path})")
    print(f"  ok  {what}: {actual_size} bytes, sha256 {sha[:16]}…")


def main() -> None:
    ap = argparse.ArgumentParser(description="Download, verify and unpack one firmware variant.")
    ap.add_argument("variant", nargs="?", help="variant name (default: %(default)s)", default=None)
    ap.add_argument("--offline", action="store_true", help="never download; require the .upd to be present")
    ap.add_argument("--force", action="store_true", help="re-extract blobs even if present and valid")
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
    _verify(upd_path, _int(upd_meta["size"]), upd_meta["sha256"], f".upd {upd_path.name}")

    # 3. slice + verify each blob
    blobs = {k: v for k, v in manifest.get("blobs", {}).items() if not k.startswith("_")}
    if not blobs:
        print("  no blob layout in manifest (work-in-progress variant); .upd verified only")
        return

    data = upd_path.read_bytes()
    for name, spec in blobs.items():
        out = data_dir / name
        off, size, sha = _int(spec["offset"]), _int(spec["size"]), spec["sha256"]
        if out.exists() and not args.force and out.stat().st_size == size and _sha256(out) == sha:
            print(f"  ok  {name}: already extracted")
            continue
        blob = data[off : off + size]
        if len(blob) != size:
            _die(f"{name}: .upd too short at offset {off:#x} (+{size:#x}); truncated .upd?")
        out.write_bytes(blob)
        _verify(out, size, sha, name)

    print("done. blobs are in", data_dir, "(git-ignored).")


if __name__ == "__main__":
    main()
