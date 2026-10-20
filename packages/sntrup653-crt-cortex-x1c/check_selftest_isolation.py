#!/usr/bin/env python3
"""check_selftest_isolation.py — check that test hooks cannot reach production.

The two exhaustive sweeps added to sntrup653_internal_selftest live wholly
inside the SNTRUP653_INTERNAL_TEST_HOOKS conditionals.  build.sh defines that
macro for exactly one binary, test_internal_construction, so the shipping core
object, the benchmark binaries and the KEM transcript binaries are compiled from
a translation unit that never sees those lines.

This script makes the claim checkable instead of merely stated.  It removes
every conditional block that the macro controls, using normal preprocessor
semantics and the package's default switch settings, and hashes the remainder.
The remainder is the production view of the core.  If that hash matches the
recorded value, no line outside the test hooks has changed.

usage: check_selftest_isolation.py [core.c]              # verify
       check_selftest_isolation.py --write [core.c]      # record
"""
import hashlib
import re
import sys

MACRO = "SNTRUP653_INTERNAL_TEST_HOOKS"
COND = re.compile(r"^\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)$")
RECORD = "expected_production_core.sha256"


def strip_macro(text, macro):
    """Return the text as the preprocessor would see it with `macro` undefined
    and every other switch at its package default."""
    out = []
    stack = []

    def live():
        return all(stack)

    for raw in text.split("\n"):
        line = raw.rstrip("\r")
        m = COND.match(line)
        if not m:
            if live():
                out.append(line)
            continue
        kind, rest = m.group(1), m.group(2).strip()
        if kind in ("ifdef", "ifndef", "if"):
            if kind == "ifdef":
                # The macro is treated as not defined: the body is dropped.
                taken = (rest != macro)
            elif kind == "ifndef":
                taken = (rest == macro)
            else:
                if macro in rest:
                    # `defined(SNTRUP653_INTERNAL_TEST_HOOKS)` is false, so the
                    # guarded branch is not compiled.
                    taken = False
                else:
                    # Every remaining switch defaults to 1 in this package, so
                    # the `#if` branch is the one that is compiled.  Both files
                    # are judged by the same rule, which is all the comparison
                    # needs.
                    taken = True
            stack.append(taken)
        elif kind == "elif":
            if not stack:
                raise SystemExit("error: #elif without #if")
            stack[-1] = False
        elif kind == "else":
            if not stack:
                raise SystemExit("error: #else without #if")
            stack[-1] = not stack[-1]
        else:  # endif
            if not stack:
                raise SystemExit("error: #endif without #if")
            stack.pop()
    if stack:
        raise SystemExit("error: unbalanced conditionals (%d left open)" % len(stack))
    return "\n".join(out)


def main(argv):
    write = "--write" in argv
    args = [a for a in argv if not a.startswith("--")]
    path = args[0] if args else "optimized/crypto_core_multsntrup653.c"

    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    view = strip_macro(text, MACRO)
    digest = hashlib.sha256(view.encode("utf-8")).hexdigest()
    kept = len(view.split("\n"))
    total = len(text.split("\n"))

    print("core_file=%s" % path)
    print("lines_total=%d lines_production_view=%d lines_removed=%d"
          % (total, kept, total - kept))
    print("production_view_sha256=%s" % digest)

    if write:
        with open(RECORD, "w", encoding="ascii", newline="\n") as fh:
            fh.write(
                "# expected_production_core.sha256\n"
                "# sha256 of optimized/crypto_core_multsntrup653.c with every\n"
                "# SNTRUP653_INTERNAL_TEST_HOOKS conditional removed, i.e. what the\n"
                "# production translation unit contains.\n"
                "# Regenerate with: python3 check_selftest_isolation.py --write\n"
                "production_view_sha256=%s\n" % digest
            )
        print("wrote %s" % RECORD)
        return 0

    try:
        with open(RECORD, encoding="ascii") as fh:
            recorded = None
            for line in fh:
                if line.startswith("production_view_sha256="):
                    recorded = line.split("=", 1)[1].strip()
    except OSError:
        print("selftest_isolation=UNRECORDED (run with --write to record)")
        return 0

    if recorded is None:
        print("selftest_isolation=UNRECORDED (%s has no digest)" % RECORD)
        return 0
    if recorded != digest:
        print("selftest_isolation=FAIL production view changed")
        print("  recorded=%s" % recorded)
        print("  current =%s" % digest)
        return 1
    print("selftest_isolation=PASS production view unchanged (%s)" % digest[:16])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
