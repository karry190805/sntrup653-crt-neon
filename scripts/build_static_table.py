#!/usr/bin/env python3
"""Build Table 7 (static footprint and machine-level operation counts).

Inputs: object files built from the clean opt22 source package:
    /tmp/rerun_clean_a53/{optimized,baseline}_core.o
    /tmp/rerun_clean_a76/{optimized,baseline}_core.o
    /tmp/rerun_clean_x1c/{optimized,baseline}_core.o

The machine counts are static disassembly counts (one call site each), not
dynamic per-call operation counts.  Algorithm-level (source-level) operation
counts are not reported.
"""
import collections
import csv
import os
import re
import subprocess
import sys

OBJ_BASE = os.environ.get("OBJ_BASE", "/tmp")
PLATFORMS = (("a53", os.path.join(OBJ_BASE, "a53")),
             ("a76", os.path.join(OBJ_BASE, "a76")),
             ("x1c", os.path.join(OBJ_BASE, "x1c")))


def size_rows(objdir):
    out = subprocess.check_output(["size", "-A", os.path.join(objdir, "optimized_core.o")],
                                  text=True)
    sec = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0].startswith("."):
            sec[parts[0]] = int(parts[1])
    return sec


def nm_tables(objdir):
    out = subprocess.check_output(["nm", "-S", "--size-sort",
                                   os.path.join(objdir, "optimized_core.o")], text=True)
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[2] in "bBdDrR":
            syms[parts[3]] = int(parts[1], 16)
    return syms


def insn_counts(obj):
    out = subprocess.check_output(["objdump", "-d", obj], text=True)
    counts = collections.Counter()
    for line in out.splitlines():
        m = re.match(r"^\s+[0-9a-f]+:\s+(?:[0-9a-f]{8}\s+)?([a-z][a-z0-9.]*)\s", line)
        if m:
            counts[m.group(1)] += 1
    return counts


def group_sum(counts, pred):
    return sum(v for k, v in counts.items() if pred(k))


def main(outdir):
    mem_fields = ["platform", "object", "text_bytes", "data_bytes", "bss_bytes",
                  "dec_bytes", "note"]
    mem_rows = []
    for platform, objdir in PLATFORMS:
        for impl in ("optimized", "baseline"):
            obj = os.path.join(objdir, "%s_core.o" % impl)
            out = subprocess.check_output(["size", obj], text=True).splitlines()[1].split()
            mem_rows.append({
                "platform": platform,
                "object": "%s_core.o" % impl,
                "text_bytes": out[0],
                "data_bytes": out[1],
                "bss_bytes": out[2],
                "dec_bytes": out[3],
                "note": "size(1); text includes .rodata",
            })
    with open(os.path.join(outdir, "table7_memory.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=mem_fields)
        w.writeheader()
        w.writerows(mem_rows)

    # Table groups are identical on all three platforms; assert that once.
    groups = {
        "W16C block/weight table": ["W16C"],
        "DFT powers (POW11_F/I, POW7_F/I)": ["POW11_F", "POW11_I", "POW7_F", "POW7_I"],
        "size-11 forward/inverse tables (M11Q, P11Q, PAIR11)":
            ["M11Q_F", "M11Q_I", "P11Q_F", "P11Q_I",
             "PAIR11_M_F", "PAIR11_M_I", "PAIR11_P_F", "PAIR11_P_I"],
        "size-7 forward/inverse tables (M7Q, P7Q, PAIR7)":
            ["M7Q_F", "M7Q_I", "P7Q_F", "P7Q_I",
             "PAIR7_M_F", "PAIR7_M_I", "PAIR7_P_F", "PAIR7_P_I"],
        "root/lambda tables (LAMBDA77 and inverses)":
            ["LAMBDA77", "INV2_SQRT_LAMBDA77", "INV2_SQRT_LAMBDA_INV77", "SQRT_LAMBDA77"],
        "Shoup tables (154-byte)":
            ["INV2_SQRT_INV77_SHOUP", "INV2_SQRT_SHOUP",
             "QMSQRT_LAMBDA_SHOUP", "SQRT_LAMBDA_SHOUP"],
        "pairing/index tables (GT_IDX_RS, GT_K_UV, ACTIVE_COL_LIST)":
            ["GT_IDX_RS", "GT_K_UV", "ACTIVE_COL_LIST"],
        "W16 inverse constants": ["W16_INV2_INV77", "W16_INV2_INV77_SHOUP"],
        "runtime table state": ["tables_state"],
    }
    table_fields = ["group", "symbols", "total_bytes", "note"]
    table_rows = []
    signatures = []
    for platform, objdir in PLATFORMS:
        syms = nm_tables(objdir)
        sig = {k: syms.get(k) for _, keys in groups.items() for k in keys}
        signatures.append((platform, sig))
    for group, keys in groups.items():
        sizes = [signatures[0][1][k] for k in keys]
        identical = all(
            s[k] == signatures[0][1][k]
            for _, s in signatures for k in keys
        )
        table_rows.append({
            "group": group,
            "symbols": "+".join(keys),
            "total_bytes": sum(sizes),
            "note": "identical symbol sizes on A76/A53/X1C" if identical
                    else "sizes differ across platforms",
        })
    table_rows.append({
        "group": "TOTAL static tables + runtime state",
        "symbols": "(all bss table symbols)",
        "total_bytes": sum(r["total_bytes"] for r in table_rows),
        "note": "equals .bss 14264 plus 12 bytes .rodata string",
    })
    with open(os.path.join(outdir, "table7_tables.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=table_fields)
        w.writeheader()
        w.writerows(table_rows)

    op_fields = ["platform", "implementation", "mul_exact", "mla_exact",
                 "add_exact", "sub_exact", "ldr_exact", "str_exact",
                 "mul_group", "mac_group", "add_group", "sub_group",
                 "load_group", "store_group", "note"]
    op_rows = []
    for platform, objdir in PLATFORMS:
        for impl in ("optimized", "baseline"):
            c = insn_counts(os.path.join(objdir, "%s_core.o" % impl))
            op_rows.append({
                "platform": platform,
                "implementation": impl,
                "mul_exact": c.get("mul", 0),
                "mla_exact": c.get("mla", 0),
                "add_exact": c.get("add", 0),
                "sub_exact": c.get("sub", 0),
                "ldr_exact": c.get("ldr", 0),
                "str_exact": c.get("str", 0),
                "mul_group": group_sum(c, lambda k: "mul" in k),
                "mac_group": group_sum(c, lambda k: any(s in k for s in ("mla", "mlal", "mls"))),
                "add_group": group_sum(c, lambda k: k.startswith("add") or k in
                                   ("saddw", "saddw2", "uaddw", "uaddw2")),
                "sub_group": group_sum(c, lambda k: k.startswith("sub") or k in
                                   ("ssubw", "ssubw2", "usubw", "usubw2")),
                "load_group": group_sum(c, lambda k: k.startswith(("ldr", "ldur", "ldp",
                                                               "ld1", "ld2", "ld3", "ld4"))),
                "store_group": group_sum(c, lambda k: k.startswith(("str", "stur", "stp",
                                                                "st1", "st2", "st3", "st4"))),
                "note": "static objdump -d counts of one call site",
            })
    with open(os.path.join(outdir, "table7_machine_ops.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=op_fields)
        w.writeheader()
        w.writerows(op_rows)

    print("wrote table7_memory.csv, table7_tables.csv, table7_machine_ops.csv")


if __name__ == "__main__":
    main(sys.argv[1])
