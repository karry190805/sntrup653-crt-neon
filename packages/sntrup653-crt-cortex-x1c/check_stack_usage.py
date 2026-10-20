#!/usr/bin/env python3
"""check_stack_usage.py — turn the collected -fstack-usage data into a budget.

build.sh already passes -fstack-usage, so every object leaves a .su file next
to it, but nothing ever inspected the numbers.  On a Cortex-A53 this matters:
the frame of the core alone is around 18 KiB, and the benchmark binds several
threads to one core, so the per-core stack high-water mark is a real constraint
on the target board.

usage: check_stack_usage.py BUDGET_BYTES SU_FILE [SU_FILE ...]

The GCC/Clang .su format is

    path:line:col:function<TAB>bytes<TAB>static|dynamic|bounded

Exit status is 0 when every function fits the budget, 1 otherwise.
"""
import os
import sys


def parse(path):
    rows = []
    with open(path, encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            line = raw.rstrip("\n")
            if not line.strip():
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                continue
            try:
                size = int(parts[1])
            except ValueError:
                continue
            kind = parts[2] if len(parts) > 2 else "unknown"
            rows.append((size, kind, parts[0], os.path.basename(path)))
    return rows


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__.strip())
    try:
        budget = int(argv[0])
    except ValueError:
        raise SystemExit("error: BUDGET_BYTES must be an integer")
    files = argv[1:]

    all_rows = []
    for path in files:
        all_rows.extend(parse(path))

    if not all_rows:
        print("stack_usage=ABSENT (no parsable .su rows)")
        return 1

    all_rows.sort(reverse=True)
    print("stack_budget_bytes=%d files=%d functions=%d" % (budget, len(files), len(all_rows)))
    print("top frames:")
    for size, kind, sig, src in all_rows[:10]:
        print("  %8d  %-8s %-28s %s" % (size, kind, sig[:28], src))

    over = [r for r in all_rows if r[0] > budget]
    if over:
        print("stack_usage=FAIL over_budget=%d" % len(over))
        for size, kind, sig, src in over[:20]:
            print("  OVER %8d  %s  (%s)" % (size, sig, src))
        return 1

    worst = all_rows[0]
    print("worst_frame_bytes=%d function=%s" % (worst[0], worst[2]))
    print("stack_usage=PASS max=%d budget=%d headroom=%d" % (worst[0], budget, budget - worst[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
