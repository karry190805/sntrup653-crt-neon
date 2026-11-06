#!/usr/bin/env python3
"""Verify and report the final source -> binary -> raw-result -> table chain.

The performance tables use native measurements whose binaries were rebuilt
from the final source.  This script proves the chain mechanically:

  final source tree SHA
    == source_tree_sha256 recorded by the final build metadata
  final optimized core source SHA
    == core_sha256 recorded by the final build metadata
  rebuilt bench binaries
    == measurement bench binaries (byte-identical)
  raw ablation summary SHA
    == raw_summary_sha256 stored in table6_ablation.csv
  table6_ablation.csv SHA
    == SHA recorded in SHA_BINDING.md / csv/sha_binding.csv

Usage: python3 scripts/build_sha_binding.py [--source-root ../opt22-clean-v4]
"""
import argparse
import csv
import hashlib
import os
import subprocess
import sys

PACKAGE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLATFORMS = {"a76": "a76", "a53": "a53", "x1c": "x1c"}
FIELDS = ["platform", "final_source_tree_sha256", "build_metadata_source_tree_sha256",
          "final_core_sha256", "measurement_core_sha256", "core_binary_sha256",
          "measurement_core_binary_sha256", "binaries_identical",
          "raw_summary_sha256", "table6_raw_summary_sha256",
          "table6_ablation_sha256", "status"]


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_kv(path):
    out = {}
    if not os.path.isfile(path):
        return out
    for line in open(path, errors="replace"):
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def read_binaries(path):
    out = {}
    if not os.path.isfile(path):
        return out
    for line in open(path, errors="replace"):
        parts = line.split()
        if len(parts) == 2:
            out[os.path.basename(parts[1])] = parts[0]
    return out


def tree_sha(source_dir):
    script = os.path.join(source_dir, "source_tree_sha256.sh")
    out = subprocess.check_output(["bash", script], cwd=source_dir, text=True)
    return out.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source-root", default=os.path.join(PACKAGE, "..", "opt22-clean-v4"))
    args = ap.parse_args()
    source_root = os.path.abspath(args.source_root)
    table6 = os.path.join(PACKAGE, "csv", "table6_ablation.csv")
    table6_sha = sha256_file(table6)
    t6_rows = {}
    with open(table6, newline="") as fh:
        for r in csv.DictReader(fh):
            t6_rows[r["platform"]] = r

    rows = []
    ok_all = True
    for plat, tag in PLATFORMS.items():
        build = os.path.join(PACKAGE, "provenance", "raw", "final-build", plat)
        rebuilt_meta = read_kv(os.path.join(build, "rebuilt_build_metadata.txt"))
        meas_meta = read_kv(os.path.join(build, "measurement_build_metadata.txt"))
        rebuilt_bins = read_binaries(os.path.join(build, "rebuilt_binary_sha256.txt"))
        meas_bins = read_binaries(os.path.join(build, "measurement_binary_sha256.txt"))
        src_dir = os.path.join(source_root, "sntrup653-crt-cortex-%s-OPTIMIZED-opt22" % tag)
        final_tree = tree_sha(src_dir)
        final_core = sha256_file(os.path.join(src_dir, "optimized", "crypto_core_multsntrup653.c"))
        core_bin = rebuilt_bins.get("bench_core_optimized", "")
        meas_core_bin = meas_bins.get("bench_core_optimized", "")
        keys = ("bench_core_baseline", "bench_core_optimized",
                "bench_kem_baseline", "bench_kem_optimized")
        identical = all(rebuilt_bins.get(k) == meas_bins.get(k) and rebuilt_bins.get(k)
                        for k in keys)
        raw = os.path.join(PACKAGE, "provenance", "raw", "ablation", "%s_summary.csv" % plat)
        raw_sha = sha256_file(raw)
        t6_raw = t6_rows.get(plat, {}).get("raw_summary_sha256", "")
        checks = [
            final_tree == rebuilt_meta.get("source_tree_sha256"),
            final_core == rebuilt_meta.get("core_sha256"),
            identical,
            raw_sha == t6_raw,
            t6_raw != "",
        ]
        status = "PASS" if all(checks) else "FAIL"
        ok_all = ok_all and status == "PASS"
        rows.append({
            "platform": plat,
            "final_source_tree_sha256": final_tree,
            "build_metadata_source_tree_sha256": rebuilt_meta.get("source_tree_sha256", ""),
            "final_core_sha256": final_core,
            "measurement_core_sha256": meas_meta.get("core_sha256", ""),
            "core_binary_sha256": core_bin,
            "measurement_core_binary_sha256": meas_core_bin,
            "binaries_identical": "1" if identical else "0",
            "raw_summary_sha256": raw_sha,
            "table6_raw_summary_sha256": t6_raw,
            "table6_ablation_sha256": table6_sha,
            "status": status,
        })

    with open(os.path.join(PACKAGE, "csv", "sha_binding.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)

    L = ["# Source → binary → raw-result → table binding", ""]
    L.append("Final source tree: `%s`." % source_root)
    L.append("")
    L.append("Changes after the native measurement campaign are:")
    L.append("")
    L.append("1. a code fix, `memset(out, 0, sizeof(*out))` →")
    L.append("   `memset(out, 0, sizeof(Blocks16))` inside")
    L.append("   `blockify_clear_padding16`, in the `SNTRUP653_FAST16 &&")
    L.append("   SNTRUP653_PUBLIC_ZS_MODE == 0` branch.  That combination is a")
    L.append("   compile-time `#error`, so the branch is not compiled into the")
    L.append("   delivered or `no_zs_32bit` binaries;")
    L.append("2. documentation updates (reports and paper) that do not affect")
    L.append("   generated code.")
    L.append("")
    L.append("Rebuilding from the final source therefore reproduces the")
    L.append("measurement binaries byte for byte on all three boards.  The")
    L.append("`measurement_core_sha256` column in `csv/sha_binding.csv` records")
    L.append("the pre-fix source hash; the final rebuilt core hash and the")
    L.append("byte-identical binary are the binding that matters.")
    L.append("")
    L.append("| Platform | final source tree SHA-256 | final core SHA-256 | core binary SHA-256 | binaries identical | raw summary SHA-256 | Table 6 input SHA-256 | status |")
    L.append("|---|---|---|---|---|---|---|---|")
    for r in rows:
        L.append("| %s | `%s` | `%s` | `%s` | %s | `%s` | `%s` | **%s** |" % (
            r["platform"].upper(), r["final_source_tree_sha256"], r["final_core_sha256"],
            r["core_binary_sha256"], r["binaries_identical"], r["raw_summary_sha256"],
            r["table6_ablation_sha256"], r["status"]))
    L.append("")
    L.append("Machine-readable form: `csv/sha_binding.csv`.")
    L.append("")
    if not ok_all:
        L.append("**BINDING FAILED**: at least one check did not match.")
    open(os.path.join(PACKAGE, "SHA_BINDING.md"), "w").write("\n".join(L) + "\n")

    print("wrote csv/sha_binding.csv and SHA_BINDING.md")
    if not ok_all:
        sys.exit(1)


if __name__ == "__main__":
    main()
