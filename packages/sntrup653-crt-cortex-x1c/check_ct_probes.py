#!/usr/bin/env python3
"""Check straight-line reducer probes; inventory full-core branches separately."""
import re
import sys
from pathlib import Path

text = Path(sys.argv[1]).read_text()
expected = {"audit_reduce_i64", "audit_reduce_u32", "audit_add", "audit_sub",
            "audit_center", "audit_normalize_short", "audit_normalize_fq"}
found = set()
current = None
bad = []
for line in text.splitlines():
    label = re.match(r"^\s*[0-9a-f]+ <([^>]+)>:", line)
    if label:
        current = label[1]
        if current in expected:
            found.add(current)
    insn = re.match(r"^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(\S+)", line)
    if insn and current in expected:
        op = insn[1]
        if op in {"b", "bl", "blr", "br", "cbz", "cbnz", "tbz", "tbnz"} or op.startswith("b."):
            bad.append((current, line.strip()))
if found != expected or bad:
    print("straight_line_probes=FAIL missing=%s branches=%s" % (sorted(expected - found), bad))
    sys.exit(1)
print("straight_line_probes=PASS count=7")
print("full_binary_constant_time=NOT_PROVEN; inspect core disassembly and timing results")
