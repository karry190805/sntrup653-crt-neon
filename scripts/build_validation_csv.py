#!/usr/bin/env python3
"""Merge the three native validation summaries into the paper-package CSVs.

Input: validation/results/<platform>/summary/*.csv and status.txt.
Output: csv/validation_contract.csv, validation_compiler_matrix.csv,
        validation_variants.csv, validation_single_bit_flip.csv,
        validation_single_bit_flip_asan.csv, validation_status.csv.
"""
import csv
import os

PACKAGE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLATFORMS = ("a76", "a53", "x1c")
ROWS = {
    "contract": "validation_contract.csv",
    "compiler_matrix": "validation_compiler_matrix.csv",
    "variants": "validation_variants.csv",
    "single_bit_flip": "validation_single_bit_flip.csv",
    "single_bit_flip_asan": "validation_single_bit_flip_asan.csv",
}


def read_csv(path):
    with open(path, newline="") as fh:
        return list(csv.DictReader(fh))


def main():
    merged = {k: [] for k in ROWS}
    status_rows = []
    for p in PLATFORMS:
        base = os.path.join(PACKAGE, "validation", "results", p)
        for key in ROWS:
            path = os.path.join(base, "summary", key + ".csv")
            if os.path.isfile(path):
                merged[key].extend(read_csv(path))
        st = {}
        sp = os.path.join(base, "status.txt")
        if os.path.isfile(sp):
            for line in open(sp):
                if "=" in line:
                    k, v = line.split("=", 1)
                    st[k.strip()] = v.strip()
        status_rows.append({
            "platform": p.upper(),
            "validation": st.get("validation", "MISSING"),
            "asan_subset": st.get("asan_subset", "MISSING"),
            "source_tree_sha256": st.get("source_tree_sha256", ""),
        })

    for key, name in ROWS.items():
        rows = merged[key]
        if not rows:
            continue
        # Put platforms in A76, A53, X1C order.
        order = {p.upper(): i for i, p in enumerate(PLATFORMS)}
        rows.sort(key=lambda r: order.get(r.get("platform", ""), 99))
        with open(os.path.join(PACKAGE, "csv", name), "w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)

    with open(os.path.join(PACKAGE, "csv", "validation_status.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["platform", "validation", "asan_subset", "source_tree_sha256"])
        w.writeheader()
        w.writerows(status_rows)
    print("wrote validation CSVs")


if __name__ == "__main__":
    main()
