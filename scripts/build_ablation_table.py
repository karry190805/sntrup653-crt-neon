#!/usr/bin/env python3
"""Build the v3 Table 6 CSVs from the final native ablation summaries.

Input: <root>/<platform>/summary.csv, where <platform> is a53, a76, or x1c.
The summary contains core and KEM rows.  Table 6 uses only the core rows.

Table 6 has exactly four arms:
  * FAST16 single-factor: no_low73, no_fold, no_barrett
  * configuration-level: no_zs_32bit (32-bit dense path)

`no_inplace` and the old five-arm `no_zs` row are intentionally absent.
"""
import csv
import os
import sys

FREQ_MHZ = {"a76": 2352.0, "a53": 1512.0, "x1c": 2995.2}
PLATFORMS = ("a76", "a53", "x1c")
ARMS = ("no_low73", "no_fold", "no_barrett", "no_zs_32bit")
CATEGORY = {
    "no_low73": "FAST16 single-factor",
    "no_fold": "FAST16 single-factor",
    "no_barrett": "FAST16 single-factor",
    "no_zs_32bit": "configuration-level (32-bit)",
}

FIELDS = [
    "platform", "arm", "category", "arm_mean_ms", "delivered_mean_ms",
    "slowdown_percent", "speedup_vs_delivered", "ci95_low", "ci95_high",
    "p_value", "p_underflow", "rounds_faster", "rounds_compared", "derived_cycles_arm",
    "derived_cycles_delivered", "source",
    "source_tree_sha256", "binary_sha256", "raw_summary_sha256",
]


def sha256_file(path):
    import hashlib
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def binding(package_root, platform):
    """Read the final-source/build binding recorded with the raw result."""
    build = os.path.join(package_root, "provenance", "raw", "final-build", platform)
    meta = os.path.join(build, "rebuilt_build_metadata.txt")
    binaries = os.path.join(build, "rebuilt_binary_sha256.txt")
    src = ""
    if os.path.isfile(meta):
        for line in open(meta):
            if line.startswith("source_tree_sha256="):
                src = line.split("=", 1)[1].strip()
                break
    bench = ""
    if os.path.isfile(binaries):
        for line in open(binaries):
            if line.rstrip().endswith("bench_core_optimized"):
                bench = line.split()[0]
                break
    return src, bench


def find_summary(root, platform):
    candidates = [
        os.path.join(root, platform, "summary.csv"),
        os.path.join(root, "%s_summary.csv" % platform),
        os.path.join(root, platform, "ablation", "summary.csv"),
        os.path.join(root, platform, "results", "ablation_final", "summary.csv"),
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    raise FileNotFoundError("no summary.csv for %s under %s" % (platform, root))


def load(path):
    with open(path, newline="") as fh:
        return list(csv.DictReader(fh))


def main(root, outdir):
    rows = []
    package_root = os.path.dirname(os.path.abspath(outdir))
    for p in PLATFORMS:
        src = find_summary(root, p)
        src_tree, bench_binary = binding(package_root, p)
        raw_sha = sha256_file(src)
        recs = {r["arm"]: r for r in load(src)
                if r.get("kind") == "core" and r.get("arm") in ARMS}
        missing = [a for a in ARMS if a not in recs]
        if missing:
            raise SystemExit("missing core arms for %s: %s" % (p, ", ".join(missing)))
        for arm in ARMS:
            rec = recs[arm]
            arm_ms = float(rec["arm_mean_ms"])
            del_ms = float(rec["delivered_mean_ms"])
            rows.append({
                "platform": p,
                "arm": arm,
                "category": CATEGORY[arm],
                "arm_mean_ms": "%.9f" % arm_ms,
                "delivered_mean_ms": "%.9f" % del_ms,
                "slowdown_percent": "%.3f" % (100.0 * (arm_ms / del_ms - 1.0)),
                "speedup_vs_delivered": "%.6f" % float(rec["delivered_speedup"]),
                "ci95_low": "%.9f" % float(rec["ci95_low"]),
                "ci95_high": "%.9f" % float(rec["ci95_high"]),
                "p_value": ("<1e-300" if float(rec["p_value"]) == 0.0
                            else rec["p_value"]),
                "p_underflow": ("true" if float(rec["p_value"]) == 0.0
                                else "false"),
                "rounds_faster": rec["rounds_faster"],
                "rounds_compared": rec["rounds_compared"],
                "derived_cycles_arm": "%.1f" % (arm_ms * FREQ_MHZ[p] * 1000.0),
                "derived_cycles_delivered": "%.1f" % (del_ms * FREQ_MHZ[p] * 1000.0),
                "source": "provenance/raw/ablation/%s_summary.csv; 2 mirrored epochs; 400 core paired samples" % p,
                "source_tree_sha256": src_tree,
                "binary_sha256": bench_binary,
                "raw_summary_sha256": raw_sha,
            })
    with open(os.path.join(outdir, "table6_ablation.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)

    # Table 6b: factor status.  Only the three FAST16 single-factor arms plus
    # the configuration-level zero-skipping arm are measured.  In-place reuse
    # is recorded as removed, not as a measured arm.
    fields = ["factor", "category", "switch_or_mechanism", "a76_status",
              "a53_status", "x1c_status", "note"]
    structural = [
        ("11-point DFT split", "radix-11 factor of the 77-point Good-Thomas decomposition"),
        ("Shoup multiplication", "precomputed Shoup roots/scaled multipliers"),
        ("input centering", "centered coefficient representation before transforms"),
        ("block fusion", "fused blockwise pass layout"),
        ("TMVP 4 accumulators", "four-accumulator size-8 TMVP schedule"),
        ("1/77 folding", "scale by 1/77 merged into the inverse transform"),
        ("skip final reduce", "skip_final_reduce path selected by the fixed schedule"),
    ]
    out = []
    for name, mech in structural:
        out.append({"factor": name, "category": "structural",
                    "switch_or_mechanism": mech + "; compiled in",
                    **{p + "_status": "not independently switchable" for p in PLATFORMS},
                    "note": "no alternate implementation or build macro"})
    switchable = [
        ("zero-skipping", "SNTRUP653_PUBLIC_ZS_MODE=0", "configuration-level (no_zs_32bit)",
         "dense 16-bit transform is not range-safe; uses the 32-bit path"),
        ("low-73 Neon", "SNTRUP653_LOW73_NEON=0", "measured FAST16 (no_low73)",
         "Neon low-73 convolution"),
        ("fused fold", "SNTRUP653_FINAL_FOLD_NEON=0", "measured FAST16 (no_fold)",
         "final_fold + encode fusion"),
    ]
    for name, mech, status, note in switchable:
        out.append({"factor": name, "category": "switchable",
                    "switch_or_mechanism": mech,
                    **{p + "_status": status for p in PLATFORMS},
                    "note": note})
    out.append({"factor": "in-place reuse", "category": "switchable",
                "switch_or_mechanism": "SNTRUP653_USE_INPLACE_BUFFERS=0",
                **{p + "_status": "removed: no FAST16 non-in-place path" for p in PLATFORMS},
                "note": "not a single-factor FAST16 arm; the old 32-bit arm was configuration-level"})
    out.append({"factor": "Barrett reduction (extra)", "category": "switchable",
                "switch_or_mechanism": "USE_BARRETT_REDUCE=0",
                **{p + "_status": "measured FAST16 (no_barrett)" for p in PLATFORMS},
                "note": "same-path % q reducer fallback; measures fallback cost, not a delivered optimization and not a 32-bit fallback"})
    with open(os.path.join(outdir, "table6b_factor_status.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        w.writerows(out)
    print("wrote table6_ablation.csv and table6b_factor_status.csv")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
