#!/usr/bin/env python3
"""Join the reviewer-facing core slices and verify the production source hash."""
from pathlib import Path
import hashlib
import sys

root = Path(__file__).parents[1]
parts = sorted((root / "src").glob("[0-9][0-9]_*.c"))
data = b"".join(p.read_bytes() for p in parts)
if len(sys.argv) > 1:
    expected = Path(sys.argv[1]).read_bytes()
    if data != expected:
        raise SystemExit("joined source does not match the reference file")
print(f"{len(parts)} slices, {len(data)} bytes, sha256={hashlib.sha256(data).hexdigest()}")
