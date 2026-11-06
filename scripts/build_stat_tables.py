#!/usr/bin/env python3
"""Build the statistical paper tables for the opt22 sntrup653 CRT package.

Inputs are the raw per-round CSVs produced by the three-platform campaign.
Outputs are written to ../csv/.

No third-party modules are used (no numpy/scipy).  Statistics:
  * speedup       = mean(baseline) / mean(optimized)
  * CI95          = paired percentile bootstrap of the ratio of means
                    (10,000 resamples, fixed seed)
  * sign p        = two-sided sign test, normal approx. with continuity correction
  * paired log-t  = two-sided t test on per-round log ratios
  * f             = n_core * T_core_opt / T_kem_opt
                    (core and KEM come from different measurement batches;
                     f is an estimate, not a PMU-derived fraction)

Usage:  python3 build_stat_tables.py <campaign_root> <opt22_final_dir> <output_csv_dir>
"""
import csv
import math
import os
import random
import re
import sys

PLATFORMS = ("a76", "a53", "x1c")
FREQ_MHZ = {"a76": 2352.0, "a53": 1512.0, "x1c": 2995.2}
KEM_METRICS = ("keypair_ms", "encaps_ms", "decaps_ms", "total_ms", "e2e_ms")
CORE_CALLS = {"keypair_ms": 1, "encaps_ms": 1, "decaps_ms": 2}
BOOT_ITERS = 10000
BOOT_SEED = 0x653771


def read_raw(path):
    with open(path, newline="") as fh:
        rows = list(csv.DictReader(fh))
    out = []
    for row in rows:
        rec = {}
        for key, val in row.items():
            if key == "implementation":
                rec[key] = val
            else:
                rec[key] = float(val)
        out.append(rec)
    return out


def mean(vals):
    return sum(vals) / len(vals)


def median(vals):
    data = sorted(vals)
    n = len(data)
    mid = n // 2
    return data[mid] if n % 2 else 0.5 * (data[mid - 1] + data[mid])


def phi(z):
    return 0.5 * (1.0 + math.erf(z / math.sqrt(2.0)))


def bootstrap_ratio_ci(base, opt, rng, iters=BOOT_ITERS):
    n = len(base)
    draws = []
    for _ in range(iters):
        idx = [rng.randrange(n) for _ in range(n)]
        mb = sum(base[i] for i in idx) / n
        mo = sum(opt[i] for i in idx) / n
        draws.append(mb / mo)
    draws.sort()
    return draws[int(0.025 * iters)], draws[int(0.975 * iters) - 1]


def sign_test(base, opt):
    wins = sum(1 for b, o in zip(base, opt) if b > o)
    n = sum(1 for b, o in zip(base, opt) if b != o)
    if n == 0:
        return float("nan"), wins, n
    z = (abs(wins - n / 2.0) - 0.5) / math.sqrt(n / 4.0)
    return 2.0 * (1.0 - phi(abs(z))), wins, n


def paired_log_t(base, opt):
    diffs = [math.log(b) - math.log(o) for b, o in zip(base, opt)]
    n = len(diffs)
    m = sum(diffs) / n
    sd = math.sqrt(sum((d - m) ** 2 for d in diffs) / (n - 1))
    t = m / (sd / math.sqrt(n))
    return 2.0 * (1.0 - phi(abs(t))), t


def kem_rows(root, platform):
    kdir = os.path.join(root, "run_%s_opt22" % platform, "kem")
    # ABBA pairing: run 01 with 02, run 04 with 03.
    b = read_raw(os.path.join(kdir, "01_baseline_raw.csv")) + read_raw(
        os.path.join(kdir, "04_baseline_raw.csv"))
    o = read_raw(os.path.join(kdir, "02_optimized_raw.csv")) + read_raw(
        os.path.join(kdir, "03_optimized_raw.csv"))
    return b, o, kdir


def core_means(root, platform):
    pdir = os.path.join(root, "run_%s_opt22" % platform, "polymul")
    b = read_raw(os.path.join(pdir, "01_baseline_raw.csv")) + read_raw(
        os.path.join(pdir, "04_baseline_raw.csv"))
    o = read_raw(os.path.join(pdir, "02_optimized_raw.csv")) + read_raw(
        os.path.join(pdir, "03_optimized_raw.csv"))
    return mean([r["ms_per_call"] for r in b]), mean([r["ms_per_call"] for r in o])


def build_kem(root, outdir):
    rng = random.Random(BOOT_SEED)
    path = os.path.join(outdir, "table4_kem.csv")
    fields = [
        "platform", "metric", "n_pairs", "base_mean_ms", "opt_mean_ms",
        "speedup_mean", "speedup_median", "ci95_low", "ci95_high",
        "p_sign", "p_paired_logt", "wins", "pairs", "core_ms_base",
        "core_ms_opt", "f_base", "f_opt", "note",
    ]
    with open(path, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        for platform in PLATFORMS:
            base_rows, opt_rows, _ = kem_rows(root, platform)
            core_b, core_o = core_means(root, platform)
            for metric in KEM_METRICS:
                b = [r[metric] for r in base_rows]
                o = [r[metric] for r in opt_rows]
                mb, mo = mean(b), mean(o)
                lo, hi = bootstrap_ratio_ci(b, o, rng)
                p_sign, wins, pairs = sign_test(b, o)
                p_t, _ = paired_log_t(b, o)
                f_base = f_opt = ""
                if metric in CORE_CALLS:
                    f_base = "%.6f" % (CORE_CALLS[metric] * core_b / mb)
                    f_opt = "%.6f" % (CORE_CALLS[metric] * core_o / mo)
                note = ""
                if metric == "keypair_ms":
                    note = ("f_opt uses one core call per keygen and is an "
                            "estimate, not a PMU decomposition")
                if platform == "x1c" and metric == "encaps_ms":
                    note = ("mean inflated by one 2.506 ms outlier; "
                            "median-based speedup is 2.3318x")
                w.writerow({
                    "platform": platform,
                    "metric": metric,
                    "n_pairs": len(b),
                    "base_mean_ms": "%.9f" % mb,
                    "opt_mean_ms": "%.9f" % mo,
                    "speedup_mean": "%.6f" % (mb / mo),
                    "speedup_median": "%.6f" % (median(b) / median(o)),
                    "ci95_low": "%.6f" % lo,
                    "ci95_high": "%.6f" % hi,
                    "p_sign": "%.3e" % p_sign if p_sign == p_sign else "nan",
                    "p_paired_logt": "%.3e" % p_t if p_t == p_t else "nan",
                    "wins": wins,
                    "pairs": pairs,
                    "core_ms_base": "%.9f" % core_b,
                    "core_ms_opt": "%.9f" % core_o,
                    "f_base": f_base,
                    "f_opt": f_opt,
                    "note": note,
                })
    return path


PMU_RE = re.compile(r"([\d,]+)\s+(cycles:u|instructions:u|branch-misses:u)")


def last_pmu_block(text):
    blocks = re.findall(
        r"Performance counter stats.*?seconds time elapsed[^\n]*", text, re.S)
    return blocks[-1] if blocks else text


def build_pmu(final_dir, outdir):
    path = os.path.join(outdir, "table3_pmu.csv")
    fields = [
        "platform", "implementation", "core_calls", "cycles_per_call",
        "instructions_per_call", "ipc", "branch_miss_percent", "note",
    ]
    rows = []
    for platform in PLATFORMS:
        stats = {}
        for impl in ("baseline", "optimized"):
            text = open(os.path.join(final_dir, "pmu", "%s_%s.txt" % (platform, impl)),
                        errors="replace").read()
            block = last_pmu_block(text)
            vals = {}
            for num, label in PMU_RE.findall(block):
                vals[label] = int(num.replace(",", ""))
            stats[impl] = vals
        for impl in ("baseline", "optimized"):
            v = stats[impl]
            calls = 50000
            cyc = v["cycles:u"] / calls
            ins = v["instructions:u"] / calls
            bm = 100.0 * v["branch-misses:u"] / v["instructions:u"]
            rows.append({
                "platform": platform,
                "implementation": impl,
                "core_calls": calls,
                "cycles_per_call": "%.3f" % cyc,
                "instructions_per_call": "%.3f" % ins,
                "ipc": "%.4f" % (v["instructions:u"] / v["cycles:u"]),
                "branch_miss_percent": "%.6f" % bm,
                "note": "perf stat -r 5, cycles:u/instructions:u/branch-misses:u",
            })
    with open(path, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)
    return path


STAGE_MAP = (
    ("blockify", ("blockify_A", "blockify_B")),
    ("11-point forward DFT", ("fwd_dft11",)),
    ("7-point forward DFT", ("fwd_dft7",)),
    ("size-8 TMVP (pointwise)", ("pointwise",)),
    ("7-point inverse DFT", ("inv_dft7",)),
    ("11-point inverse DFT", ("inv_dft11",)),
    ("low-73 convolution", ("low73",)),
    ("fused fold + encode", ("final_fold", "encode")),
)


def build_stage(root, outdir):
    path = os.path.join(outdir, "table5_stage.csv")
    fields = ["platform", "stage_paper", "ns_per_call", "derived_cycles",
              "pct_of_8stage_total", "note"]
    with open(path, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        for platform in PLATFORMS:
            src = os.path.join(root, "run_%s_opt22" % platform, "stage",
                               "stage_profile_gcc.csv")
            raw = {r["stage"]: float(r["ns_per_call"])
                   for r in csv.DictReader(open(src))}
            merged = [(name, sum(raw[k] for k in keys)) for name, keys in STAGE_MAP]
            total = sum(v for _, v in merged)
            for name, ns in merged:
                note = "gcc diagnostic; marks add ~2.1-2.6% overhead"
                if name == "fused fold + encode":
                    note += "; final_fold+encode"
                if name == "blockify":
                    note += "; blockify_A+blockify_B"
                if name == "11-point forward DFT" or name == "7-point forward DFT":
                    note += "; aggregated over two passes"
                w.writerow({
                    "platform": platform,
                    "stage_paper": name,
                    "ns_per_call": "%.3f" % ns,
                    "derived_cycles": "%.2f" % (ns * FREQ_MHZ[platform] / 1000.0),
                    "pct_of_8stage_total": "%.2f" % (100.0 * ns / total),
                    "note": note,
                })
    return path


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(1)
    root, final_dir, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(outdir, exist_ok=True)
    print(build_kem(root, outdir))
    print(build_pmu(final_dir, outdir))
    print(build_stage(root, outdir))


if __name__ == "__main__":
    main()
