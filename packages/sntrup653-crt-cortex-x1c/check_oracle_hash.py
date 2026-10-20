#!/usr/bin/env python3
"""check_oracle_hash.py — compare the C oracle stream against the Python model.

shared/test_endtoend_oracle.c prints

    cases_hash=<16 hex digits>

for its first 1971 cases; oracle_reference.py computes the same aggregate from
the mathematical definition alone.  This script enforces equality, so a core
that silently changes its output cannot pass even if the run is otherwise
self-consistent.

usage: check_oracle_hash.py EXPECTED_FILE LOG_FILE [LOG_FILE ...]
"""
import re
import sys

# The lookbehind matters: the C test also prints all_cases_hash=..., whose
# tail would otherwise be read as a second, always-different cases_hash and
# turn every run into a spurious MISMATCH.
HEX = re.compile(r"(?<![0-9A-Za-z_])cases_hash=([0-9a-fA-F]{16})")


def read_expected(path):
    with open(path, encoding="ascii") as fh:
        text = fh.read()
    m = re.search(r"fnv1a64=([0-9a-fA-F]{16})", text)
    if not m:
        raise SystemExit("error: %s has no fnv1a64= line" % path)
    n = re.search(r"cases=(\d+)", text)
    return m.group(1).lower(), (int(n.group(1)) if n else None)


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__.strip())
    expected_path, logs = argv[0], argv[1:]
    expected, cases = read_expected(expected_path)

    seen = []
    for path in logs:
        with open(path, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        seen.extend(m.group(1).lower() for m in HEX.finditer(text))

    if not seen:
        print("oracle_hash=ABSENT (no cases_hash line in %s)" % " ".join(logs))
        print("note: cases_hash is printed only when the fixed and the axis")
        print("      cases run together, so ORACLE_MODE=fixed omits it;")
        print("      callers may treat exit code 2 as a skip, not a failure.")
        return 2

    print("oracle_reference_cases=%s oracle_reference_hash=%s" % (cases, expected))
    bad = 0
    for i, got in enumerate(seen):
        if got == expected:
            print("cases_hash[%d]=%s MATCH" % (i, got))
        else:
            print("cases_hash[%d]=%s MISMATCH (expected %s)" % (i, got, expected))
            bad += 1

    if bad:
        print("oracle_hash_check=FAIL mismatches=%d" % bad)
        return 1
    print("oracle_hash_check=PASS streams=%d" % len(seen))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
