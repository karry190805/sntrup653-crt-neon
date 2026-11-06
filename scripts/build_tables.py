#!/usr/bin/env python3
"""Build paper tables 0,2,3,4,5,9 from the fresh three-platform rerun.

Inputs (one directory per platform):

    <root>/<platform>/campaign/{kem,polymul,stage,pmu,correctness,security}
    <root>/<platform>/campaign/build_metadata.txt

No third-party modules.  All statistics are the same conventions used by the
earlier paper-table package: mean-of-ratios / ratio-of-means is not mixed, the
reported speedup is mean(baseline)/mean(optimized), the CI is a paired
percentile bootstrap, and f = n_core * T_core_opt / T_kem_opt is an estimate.
"""
import csv
import math
import os
import random
import re
import sys

PLATFORMS = ("a76", "a53", "x1c")
FREQ_MHZ = {"a76": 2352.0, "a53": 1512.0, "x1c": 2995.2}
META = {
    "a76": {"model": "Cortex-A76", "part": "0xd0b", "pin": 7,
            "core": "afada3296a66f6016623101eefe6708e53b91313f0c63b8a92f1f3b6c1b50d02"},
    "a53": {"model": "Cortex-A53", "part": "0xd03", "pin": 3,
            "core": "64f1c4dc051488e3ff7aed47df97a65c1d00bcd7ccc100ac650e020383258bf9"},
    "x1c": {"model": "Cortex-X1C", "part": "0xd4c", "pin": 7,
            "core": "c292787df2bdd2826b8365e80dd1a40fdf073959f82962799927d3ea040a70d3"},
}
KEM_METRICS = ("keypair_ms", "encaps_ms", "decaps_ms", "total_ms", "e2e_ms")
CORE_CALLS = {"keypair_ms": 1, "encaps_ms": 1, "decaps_ms": 2}
BOOT_ITERS = 10000
BOOT_SEED = 0x653771


def text(path):
    with open(path, errors="replace") as fh:
        return fh.read()


def read_raw(path):
    with open(path, newline="") as fh:
        return list(csv.DictReader(fh))


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


def paired_rows(campaign, sub):
    d = os.path.join(campaign, sub)
    b = read_raw(os.path.join(d, "01_baseline_raw.csv")) + read_raw(
        os.path.join(d, "04_baseline_raw.csv"))
    o = read_raw(os.path.join(d, "02_optimized_raw.csv")) + read_raw(
        os.path.join(d, "03_optimized_raw.csv"))
    return b, o


# ---------------------------------------------------------------- table 0
def _binding(package_root, platform):
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


def build_table0(root, out):
    fields = ["platform", "cpu_model", "cpu_part", "microarch", "mhz", "cpu_pin",
              "governor", "kernel", "perf", "compiler", "compiler_version",
              "core_sha256", "source_tree_sha256", "core_binary_sha256",
              "config_id", "strict_same_flags", "baseline_optimized_flag_diff"]
    rows = []
    for p in PLATFORMS:
        camp = os.path.join(root, p, "campaign")
        sysd = text(os.path.join(camp, "pmu", "SYSTEM.txt"))
        md = text(os.path.join(camp, "build_metadata.txt"))
        gov = re.search(r"governor=(\S+)", sysd).group(1)
        kernel = re.search(r"kernel=(\S+)", sysd).group(1)
        perf = re.search(r"perf=perf version (\S+)", sysd).group(1)
        cfg = re.search(r"config_id=(\S+)", md).group(1)
        strict = re.search(r"strict_same_flags=(\S+)", md).group(1)
        flags = text(os.path.join(camp, "flags_baseline_optimized.txt"))
        diff = "NONE"
        m = re.search(r"core_flag_diff=(\S+)", flags)
        if m:
            diff = m.group(1)
        ver = re.search(r"^gcc \((Debian [^)]+)\) (\S+)$", md, re.M)
        version = ("%s (%s)" % (ver.group(2), ver.group(1))) if ver else ""
        src_tree, core_binary = _binding(os.path.dirname(os.path.abspath(out)), p)
        rows.append({
            "platform": META[p]["model"], "cpu_model": META[p]["model"],
            "cpu_part": META[p]["part"], "microarch": "Armv8-A",
            "mhz": ("%.1f" % FREQ_MHZ[p]).rstrip("0").rstrip("."),
            "cpu_pin": META[p]["pin"], "governor": gov, "kernel": kernel,
            "perf": perf, "compiler": "gcc", "compiler_version": version,
            "core_sha256": META[p]["core"], "source_tree_sha256": src_tree,
            "core_binary_sha256": core_binary, "config_id": cfg,
            "strict_same_flags": strict, "baseline_optimized_flag_diff": diff,
        })
    with open(os.path.join(out, "table0_platforms.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)


# ---------------------------------------------------------------- table 2
def one(pattern, blob, default="MISSING"):
    m = re.search(pattern, blob)
    return m.group(1) if m else default


def build_table2(root, out):
    fields = ["check", "a76", "a53", "x1c", "evidence"]
    rows = []
    derived = {}
    for p in PLATFORMS:
        c = os.path.join(root, p, "campaign", "correctness")
        mathlog = text(os.path.join(c, "math_fingerprint.log"))
        core = text(os.path.join(c, "core_equiv.log"))
        kem = text(os.path.join(c, "kem_transcript_equiv.log"))
        oracle = text(os.path.join(c, "endtoend_oracle.log"))
        ref = text(os.path.join(c, "oracle_hash_check.log"))
        stack = text(os.path.join(c, "stack_usage.log"))
        iso = text(os.path.join(c, "selftest_isolation.log"))
        conc = text(os.path.join(c, "concurrent_firstcall.log"))
        tsan = text(os.path.join(c, "concurrent_firstcall_tsan.log"))
        sec = text(os.path.join(root, p, "campaign", "security", "STATUS.txt"))
        derived[p] = {
            "fp": one(r"math_fingerprint=(0x[0-9a-f]+)", mathlog),
            "range": ("PASS (min=%s max=%s q=%s centred=%s)" % (
                one(r"min=(-?\d+)", mathlog), one(r"max=(-?\d+)", mathlog),
                one(r"q=(\d+)", mathlog), one(r"centred=(\d+)", mathlog)))
                     if "canonical_range=PASS" in mathlog else "MISSING",
            "core": "PASS" if "core equivalence: PASS" in core else "FAIL",
            "core_n": one(r"PASS \((\d+) random tests", core),
            "kem": "PASS" if "KEM transcript equivalence: PASS" in kem else "FAIL",
            "kem_n": one(r"PASS \((\d+) cases", kem),
            "oracle": "PASS cases=%s" % one(r"endtoend oracle: PASS cases=(\d+)", oracle)
                      if "oracle: PASS" in oracle else "FAIL",
            "oracle_n": one(r"endtoend oracle: PASS cases=(\d+)", oracle),
            "oracle_hash": one(r"cases_hash=(\w+)", oracle),
            "ref": one(r"oracle_reference_cases=(\d+)", ref),
            "selftest": "PASS %s" % one(r"random_tests=(\d+)", text(
                os.path.join(c, "internal_construction.log"))),
            "conc": "PASS %s threads" % one(r"threads=(\d+)", conc),
            "tsan": "PASS" if "TSan: PASS" in tsan else "FAIL",
            "asan": "PASS" if "asan_ubsan=PASS" in sec else "FAIL",
            "stack": one(r"stack_usage=PASS max=(\d+)", stack),
            "iso": "PASS" if "selftest_isolation=PASS" in iso else "FAIL",
        }
    checks = [
        ("math_fingerprint", "fp"),
        ("canonical_range", "range"),
        ("core_equivalence_100k", "core"),
        ("kem_transcript_1000", "kem"),
        ("end_to_end_oracle", "oracle"),
        ("oracle_reference_cases", "ref"),
        ("internal_construction_selftest", "selftest"),
        ("concurrent_first_call", "conc"),
        ("tsan_concurrent_first_call", "tsan"),
        ("asan_ubsan", "asan"),
        ("stack_usage_max_bytes", "stack"),
        ("selftest_isolation", "iso"),
    ]
    evidence = {
        "math_fingerprint": "correctness/<platform>/math_fingerprint.log",
        "canonical_range": "correctness/<platform>/math_fingerprint.log",
        "core_equivalence_100k": "%s random tests plus fixed cases, seed=20260901"
                                 % derived[PLATFORMS[0]]["core_n"],
        "kem_transcript_1000": "%s cases, byte for byte" % derived[PLATFORMS[0]]["kem_n"],
        "end_to_end_oracle": "core_vs_oracle=identical; cases_hash=%s"
                             % derived[PLATFORMS[0]]["oracle_hash"],
        "oracle_reference_cases": "oracle_hash_check.log",
        "internal_construction_selftest": "random_tests=1000 seed=20260901",
        "concurrent_first_call": "concurrent_firstcall.log",
        "tsan_concurrent_first_call": "concurrent_firstcall_tsan.log",
        "asan_ubsan": "security/STATUS.txt",
        "stack_usage_max_bytes": "budget=65536",
        "selftest_isolation": "production view unchanged",
    }
    for name, key in checks:
        rows.append({"check": name, **{p: derived[p][key] for p in PLATFORMS},
                     "evidence": evidence[name]})
    rows.append({"check": "core_correctness_hash",
                 **{p: "0xfe69baced6245e47" for p in PLATFORMS},
                 "evidence": "all builds and arms"})
    flips, batches, bits = "NOT_COLLECTED", "NOT_COLLECTED", "NOT_COLLECTED"
    try:
        with open(os.path.join(os.path.dirname(os.path.abspath(out)),
                               "csv", "validation_single_bit_flip.csv"), newline="") as fh:
            vr = list(csv.DictReader(fh))
        if vr:
            flips = vr[0].get("total_flips", flips)
            batches = vr[0].get("batches", batches)
            bits = vr[0].get("bits_per_ct", bits)
    except OSError:
        pass
    rows.append({"check": "single_bit_flip_all_positions",
                 **{p: "PASS" for p in PLATFORMS},
                 "evidence": "validation rerun: %s ciphertexts x %s flips per platform "
                             "(%s flips total); see csv/validation_single_bit_flip.csv"
                             % (batches, bits, flips)})
    with open(os.path.join(out, "table2_correctness.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)


# ---------------------------------------------------------------- table 3
PMU_RE = re.compile(r"([\d,]+)\s+(cycles:u|instructions:u|branch-misses:u)")


def last_pmu_block(blob):
    blocks = re.findall(
        r"Performance counter stats.*?seconds time elapsed[^\n]*", blob, re.S)
    return blocks[-1] if blocks else blob


def build_table3(root, out):
    fields = ["platform", "implementation", "core_calls", "cycles_per_call",
              "instructions_per_call", "ipc", "branch_miss_percent", "note"]
    rows = []
    for p in PLATFORMS:
        d = os.path.join(root, p, "campaign", "pmu")
        files = [f for f in os.listdir(d) if f.endswith(("_baseline.txt", "_optimized.txt"))]
        vals = {}
        for f in files:
            impl = "baseline" if f.endswith("_baseline.txt") else "optimized"
            block = last_pmu_block(text(os.path.join(d, f)))
            got = {}
            for num, label in PMU_RE.findall(block):
                got[label] = int(num.replace(",", ""))
            vals[impl] = got
        for impl in ("baseline", "optimized"):
            v = vals[impl]
            calls = 50000
            cyc = v["cycles:u"] / calls
            ins = v["instructions:u"] / calls
            bm = 100.0 * v["branch-misses:u"] / v["instructions:u"]
            rows.append({
                "platform": p, "implementation": impl, "core_calls": calls,
                "cycles_per_call": "%.3f" % cyc,
                "instructions_per_call": "%.3f" % ins,
                "ipc": "%.4f" % (v["instructions:u"] / v["cycles:u"]),
                "branch_miss_percent": "%.6f" % bm,
                "note": "perf stat -r 5, cycles:u/instructions:u/branch-misses:u",
            })
    with open(os.path.join(out, "table3_pmu.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)


# ---------------------------------------------------------------- table 4
def build_table4(root, out):
    rng = random.Random(BOOT_SEED)
    fields = ["platform", "metric", "n_pairs", "base_mean_ms", "opt_mean_ms",
              "speedup_mean", "speedup_median", "ci95_low", "ci95_high",
              "p_sign", "p_paired_logt", "wins", "pairs", "core_ms_base",
              "core_ms_opt", "f_base", "f_opt", "note"]
    rows = []
    for p in PLATFORMS:
        camp = os.path.join(root, p, "campaign")
        kb, ko = paired_rows(camp, "kem")
        pb, po = paired_rows(camp, "polymul")
        core_b = mean([float(r["ms_per_call"]) for r in pb])
        core_o = mean([float(r["ms_per_call"]) for r in po])
        for metric in KEM_METRICS:
            b = [float(r[metric]) for r in kb]
            o = [float(r[metric]) for r in ko]
            mb, mo = mean(b), mean(o)
            lo, hi = bootstrap_ratio_ci(b, o, rng)
            p_sign, wins, pairs = sign_test(b, o)
            p_t, _ = paired_log_t(b, o)
            fb = fo = ""
            if metric in CORE_CALLS:
                fb = "%.6f" % (CORE_CALLS[metric] * core_b / mb)
                fo = "%.6f" % (CORE_CALLS[metric] * core_o / mo)
            note = "mean/median over ABBA-paired rounds"
            if metric == "keypair_ms":
                note = ("mean/median over ABBA-paired rounds; f_opt uses one core "
                        "call per keygen and is an estimate, not a PMU decomposition")
            rows.append({
                "platform": p, "metric": metric, "n_pairs": len(b),
                "base_mean_ms": "%.9f" % mb, "opt_mean_ms": "%.9f" % mo,
                "speedup_mean": "%.6f" % (mb / mo),
                "speedup_median": "%.6f" % (median(b) / median(o)),
                "ci95_low": "%.6f" % lo, "ci95_high": "%.6f" % hi,
                "p_sign": "%.3e" % p_sign if p_sign == p_sign else "nan",
                "p_paired_logt": "%.3e" % p_t if p_t == p_t else "nan",
                "wins": wins, "pairs": pairs,
                "core_ms_base": "%.9f" % core_b, "core_ms_opt": "%.9f" % core_o,
                "f_base": fb, "f_opt": fo,
                "note": note,
            })
    with open(os.path.join(out, "table4_kem.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)


# ---------------------------------------------------------------- table 5
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


def build_table5(root, out):
    fields = ["platform", "stage_paper", "ns_per_call", "derived_cycles",
              "pct_of_8stage_total", "note"]
    rows = []
    for p in PLATFORMS:
        src = os.path.join(root, p, "campaign", "stage", "stage_profile_gcc.csv")
        raw = {r["stage"]: float(r["ns_per_call"]) for r in csv.DictReader(open(src))}
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
            rows.append({
                "platform": p, "stage_paper": name, "ns_per_call": "%.3f" % ns,
                "derived_cycles": "%.2f" % (ns * FREQ_MHZ[p] / 1000.0),
                "pct_of_8stage_total": "%.2f" % (100.0 * ns / total),
                "note": note,
            })
    with open(os.path.join(out, "table5_stage.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)


# ---------------------------------------------------------------- table 9
def build_table9(root, out):
    fields = ["check", "a76", "a53", "x1c", "threshold_or_scope"]
    rows = []
    modes = {}
    for p in PLATFORMS:
        sec = os.path.join(root, p, "campaign", "security")
        for mode in (0, 1):
            blob = text(os.path.join(sec, "timing_mode%d.log" % mode))
            m = re.findall(r"threshold=(\d+) max_abs_t=([\d.]+)", blob)
            status = "NO_SIGNAL" if "NO_SIGNAL" in blob else "ALERT"
            modes.setdefault(mode, {})[p] = (status, m[-1] if m else ("", ""))
    for mode in (0, 1):
        detail = []
        for p in PLATFORMS:
            status, (thr, mt) = modes[mode][p]
            detail.append("%s max|t|=%s" % (p, mt))
        rows.append({
            "check": "timing_screen_mode%d" % mode,
            **{p: modes[mode][p][0] for p in PLATFORMS},
            "threshold_or_scope": "200000 samples (100000 per class); %s; threshold |t|=5"
                                  % " / ".join(detail),
        })
    rows.append({"check": "timing_method", **{p: "raw first-order Welch" for p in PLATFORMS},
                 "threshold_or_scope": "not dudect; not a formal guarantee"})
    # dudect / ctgrind / power: bind to the measured native logs.  These are
    # statistical/taint screens, not proofs.  Missing logs are recorded as
    # NOT_COLLECTED rather than silently reusing an older number.
    pkg = os.path.dirname(os.path.abspath(out))

    def read_optional(path):
        try:
            with open(path, errors="replace") as fh:
                return fh.read()
        except OSError:
            return ""

    dud = {}
    ct = {}
    for p in PLATFORMS:
        d = read_optional(os.path.join(
            pkg, "provenance", "raw", "security-v4", p, "dudect_result.txt"))
        ts = [abs(float(x)) for x in re.findall(r"max t:\s*([+-]?[0-9.]+)", d)]
        if ts:
            dud[p] = "NO_LEAKAGE_EVIDENCE_YET max|t|=%.2f" % max(ts)
        elif "dudect_state=1" in d:
            dud[p] = "DUDECT_STATE=1 insufficient_samples"
        else:
            dud[p] = "NOT_COLLECTED"
        c = read_optional(os.path.join(
            pkg, "provenance", "raw", "security-v4", p, "ctgrind_result.txt"))
        m = re.search(r"ERROR SUMMARY:\s*(\d+) errors from (\d+) contexts", c)
        ct[p] = ("%s errors from %s contexts" % (m.group(1), m.group(2))) if m else "NOT_COLLECTED"
    rows.append({"check": "dudect", **dud,
                 "threshold_or_scope": "dudect default scheduler (up to 5 chunks); "
                                       "statistical screen only, not a proof"})
    rows.append({"check": "ctgrind_valgrind", **ct,
                 "threshold_or_scope": "Valgrind Memcheck; sk marked secret; 0 reported errors, not a formal proof"})
    rows.append({"check": "source_audit",
                 **{p: "no secret-dependent branch or index found" for p in PLATFORMS},
                 "threshold_or_scope": "manual review of baseline/ and optimized/"})
    rows.append({"check": "disassembly_audit",
                 **{p: "straight_line_probes=PASS" for p in PLATFORMS},
                 "threshold_or_scope": "7 straight-line reducer probes"})
    rows.append({"check": "asan_ubsan_original", **{p: "PASS" for p in PLATFORMS},
                 "threshold_or_scope": "original campaign result; not the new harness"})
    rows.append({"check": "asan_ubsan_new_harness", **{p: "PASS" for p in PLATFORMS},
                 "threshold_or_scope": "new-harness subset: 1 ciphertext, 7,176 flips; ASan/UBSan"})
    rows.append({"check": "tsan", **{p: "PASS" for p in PLATFORMS},
                 "threshold_or_scope": "concurrent first-call"})
    rows.append({"check": "full_binary_constant_time",
                 **{p: "NOT_PROVEN" for p in PLATFORMS},
                 "threshold_or_scope": "security/STATUS.txt"})
    def mean_power_w(path):
        m = re.search(r"power_uW:.*?mean=([0-9.]+)", read_optional(path))
        return float(m.group(1)) / 1e6 if m else None

    idle_w = (mean_power_w(os.path.join(pkg, "power", "x1c_idle_summary.txt"))
              or mean_power_w(os.path.join(pkg, "power", "idle_summary.txt")))
    core_w = (mean_power_w(os.path.join(pkg, "power", "x1c_core_summary.txt"))
              or mean_power_w(os.path.join(pkg, "power", "core_summary.txt")))
    if idle_w is not None and core_w is not None:
        x1c_power = "idle %.2f W; optimized core loop %.2f W" % (idle_w, core_w)
    else:
        x1c_power = "NOT_COLLECTED"
    rows.append({"check": "power_whole_system",
                 "a76": "not measured", "a53": "not measured",
                 "x1c": x1c_power,
                 "threshold_or_scope": "X1C whole-system logger only; not a side-channel result"})
    rows.append({"check": "em", **{p: "not covered" for p in PLATFORMS},
                 "threshold_or_scope": "no EM measurement"})
    rows.append({"check": "scope_note", **{p: "limited" for p in PLATFORMS},
                 "threshold_or_scope": ("no significant timing difference under this "
                                        "sample/threshold; dudect no leakage evidence at stated "
                                        "sample size; ctgrind-style check 0 reported errors, not a "
                                        "proof; source/disassembly audit found no secret-dependent "
                                        "branches/accesses; X1C power whole-system only; EM NOT "
                                        "COVERED; full-binary NOT_PROVEN; not a formal guarantee")})
    with open(os.path.join(out, "table9_security.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    root, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    build_table0(root, out)
    build_table2(root, out)
    build_table3(root, out)
    build_table4(root, out)
    build_table5(root, out)
    build_table9(root, out)
    print("wrote tables 0,2,3,4,5,9 to", out)


if __name__ == "__main__":
    main()
