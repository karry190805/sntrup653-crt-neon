#!/usr/bin/env python3
"""Verify the on-board source and binary hash checks.

Each ``provenance/raw/board-hash-check/<platform>.txt`` file was produced by
running ``source_tree_sha256.sh`` and ``sha256sum`` on the physical board.
This script compares those board outputs with the package records:

* source tree  -> csv/sha_binding.csv final_source_tree_sha256
* PMU binaries -> provenance/raw/pmu-cache-v4/<platform>/metadata.txt
* ablation binaries -> provenance/raw/final-build/<platform>/rebuilt_binary_sha256.txt

The check is intentionally independent of the local source tree: it proves
what the board actually hashed, not what the host would have hashed.
"""
import csv
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PACKAGE = os.path.dirname(HERE)
RAW = os.path.join(PACKAGE, "provenance", "raw")
OUT = os.path.join(PACKAGE, "csv", "board_hash_check.csv")
PLATFORMS = ("a76", "a53", "x1c")
FIELDS = [
    "platform", "host", "kernel", "checked_at_utc",
    "source_tree_sha256", "expected_source_tree_sha256",
    "pmu_core_baseline", "expected_pmu_core_baseline",
    "pmu_core_optimized", "expected_pmu_core_optimized",
    "ablation_core_baseline", "expected_ablation_core_baseline",
    "ablation_core_optimized", "expected_ablation_core_optimized", "status",
]


def kv(path):
    out = {}
    if not os.path.isfile(path):
        return out
    for line in open(path, errors="replace"):
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def binaries(path):
    out = {}
    if not os.path.isfile(path):
        return out
    for line in open(path, errors="replace"):
        parts = line.split()
        if len(parts) == 2:
            out[os.path.basename(parts[1])] = parts[0]
    return out


def main():
    binding = {}
    with open(os.path.join(PACKAGE, "csv", "sha_binding.csv"), newline="") as fh:
        for r in csv.DictReader(fh):
            binding[r["platform"]] = r

    rows = []
    for p in PLATFORMS:
        board = kv(os.path.join(RAW, "board-hash-check", p + ".txt"))
        cache = kv(os.path.join(RAW, "pmu-cache-v4", p, "metadata.txt"))
        rebuilt = binaries(os.path.join(RAW, "final-build", p,
                                        "rebuilt_binary_sha256.txt"))
        expected_source = binding.get(p, {}).get("final_source_tree_sha256", "")
        checks = [
            board.get("source_tree_sha256") == expected_source and expected_source,
            board.get("pmu_core_baseline") == cache.get("binary_baseline")
            and cache.get("binary_baseline"),
            board.get("pmu_core_optimized") == cache.get("binary_optimized")
            and cache.get("binary_optimized"),
            board.get("ablation_core_baseline") == rebuilt.get("bench_core_baseline")
            and rebuilt.get("bench_core_baseline"),
            board.get("ablation_core_optimized") == rebuilt.get("bench_core_optimized")
            and rebuilt.get("bench_core_optimized"),
        ]
        rows.append({
            "platform": p,
            "host": board.get("host", ""),
            "kernel": board.get("kernel", ""),
            "checked_at_utc": board.get("checked_at_utc", ""),
            "source_tree_sha256": board.get("source_tree_sha256", ""),
            "expected_source_tree_sha256": expected_source,
            "pmu_core_baseline": board.get("pmu_core_baseline", ""),
            "expected_pmu_core_baseline": cache.get("binary_baseline", ""),
            "pmu_core_optimized": board.get("pmu_core_optimized", ""),
            "expected_pmu_core_optimized": cache.get("binary_optimized", ""),
            "ablation_core_baseline": board.get("ablation_core_baseline", ""),
            "expected_ablation_core_baseline": rebuilt.get("bench_core_baseline", ""),
            "ablation_core_optimized": board.get("ablation_core_optimized", ""),
            "expected_ablation_core_optimized": rebuilt.get("bench_core_optimized", ""),
            "status": "PASS" if all(checks) else "FAIL",
        })

    with open(OUT, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)
    print("wrote", OUT)
    for r in rows:
        print(r["platform"], r["host"], r["status"])


if __name__ == "__main__":
    main()
