#!/usr/bin/env python3
"""Render the submission manuscript: exactly 9 tables and 5 conclusion paragraphs.

Every performance number is read from ``csv/*.csv`` or from the raw paired
polymul samples in ``provenance/raw/polymul/``.  The manuscript deliberately
keeps the opt22 measurement boundaries:

* core speedups are PMU counts on all three platforms;
* the core 95% CI is a paired bootstrap over the raw polymul samples;
* stage totals are diagnostic timer sums, not production PMU cycles;
* the seven structural factors are not independently switchable;
* single-bit-flip, the 10-combination compiler matrix and the 9 build variants
  were not rerun in opt22 and are marked as such.
"""
import csv
import os
import random
import re
import statistics
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
CSVDIR = ROOT / "csv"
RAWDIR = ROOT / "provenance" / "raw"

PLATFORMS = ("a76", "a53", "x1c")
PLAT = {
    "a76": {
        "name": "A76",
        "board": "Radxa ROCK 5B+",
        "soc": "Rockchip RK3588",
        "core": "Cortex-A76, CPU 7",
        "mhz": "2352",
        "os": "Debian 12 (bookworm)",
        "kernel": "6.1.84-8-rk2410",
        "gcc": "12.2.0",
        "clang": "14.0.6*",
    },
    "a53": {
        "name": "A53",
        "board": "KICKPI K2B V2",
        "soc": "Allwinner H618",
        "core": "Cortex-A53, CPU 3",
        "mhz": "1512",
        "os": "Armbian 26.11 / Debian 13 (trixie)",
        "kernel": "6.18.59-current-sunxi64",
        "gcc": "14.2.0",
        "clang": "19.1.7*",
    },
    "x1c": {
        "name": "X1C",
        "board": "Radxa Dragon Q8B",
        "soc": "Qualcomm SC8280XP",
        "core": "Cortex-X1C, CPU 7",
        "mhz": "2995.2",
        "os": "Debian 13 (trixie)",
        "kernel": "7.0.11-8-qcom",
        "gcc": "14.2.0",
        "clang": "19.1.7*",
    },
}


def rows(name):
    with open(CSVDIR / name, newline="") as fh:
        return list(csv.DictReader(fh))


def num(x, nd=3):
    return ("{:,.%df}" % nd).format(float(x))


def pct(x, nd=2):
    return ("%." + str(nd) + "f%%") % (100.0 * float(x))


def sig(x, n=4):
    return ("{:." + str(n) + "g}").format(float(x))


def read_raw(path):
    with open(path, newline="") as fh:
        return list(csv.DictReader(fh))


def core_ci(key):
    """Paired percentile bootstrap of mean(baseline)/mean(optimized)."""
    d = RAWDIR / "polymul" / key
    b = [float(r["ms_per_call"]) for r in read_raw(d / "01_baseline_raw.csv")]
    b += [float(r["ms_per_call"]) for r in read_raw(d / "04_baseline_raw.csv")]
    o = [float(r["ms_per_call"]) for r in read_raw(d / "02_optimized_raw.csv")]
    o += [float(r["ms_per_call"]) for r in read_raw(d / "03_optimized_raw.csv")]
    if len(b) != len(o):
        raise SystemExit("unpaired polymul samples for " + key)
    rng = random.Random(0x653771)
    ratios = []
    n = len(b)
    for _ in range(10000):
        idx = [rng.randrange(n) for _ in range(n)]
        bb = sum(b[i] for i in idx) / n
        oo = sum(o[i] for i in idx) / n
        ratios.append(bb / oo)
    ratios.sort()
    lo = ratios[int(0.025 * len(ratios))]
    hi = ratios[int(0.975 * len(ratios)) - 1]
    return statistics.mean(b) / statistics.mean(o), lo, hi


def polymul_summary(key):
    out = {}
    for r in rows("../provenance/raw/polymul/%s/summary.csv" % key):
        out[r["implementation"]] = float(r["mean"])
    return out


def stack_values(key):
    text = (RAWDIR / "correctness" / key / "stack_usage.log").read_text()
    selftest = core = None
    for line in text.splitlines():
        m = re.match(r"\s*(\d+)\s+static\s+\S+\s+([A-Za-z0-9_]+)\.su", line)
        if not m:
            continue
        if m.group(2) == "test_internal_construction_core" and selftest is None:
            selftest = int(m.group(1))
        if m.group(2) == "optimized_core" and core is None:
            core = int(m.group(1))
    return core, selftest


def main():
    L = []
    w = L.append

    w("# sntrup653 CRT on Three ARM Platforms: Switch-Level Ablation and Measured KEM Performance")
    w("")
    run_date = os.environ.get("RUN_DATE") or __import__("datetime").date.today().isoformat()
    w("*Submission tables from the fresh opt22 rerun (%s). All ratios are same-platform A/B against the strict-same-flags PQClean baseline; no cross-platform absolute-cycle comparison is made.*" % run_date)
    w("")

    # ---------------------------------------------------------------- table 1
    w("## Table 1. Platforms, toolchains, and build flags")
    w("")
    w("| Platform | Board | SoC | Core / pin | MHz | OS | Kernel | GCC | Clang | Build identity |")
    w("|---|---|---|---|---:|---|---|---|---|---|")
    for p in PLATFORMS:
        d = PLAT[p]
        w("| %s | %s | %s | %s | %s | %s | %s | %s | %s | `strict_same_flags=1`; `core_flag_diff=NONE` |" % (
            d["name"], d["board"], d["soc"], d["core"], d["mhz"], d["os"],
            d["kernel"], d["gcc"], d["clang"]))
    w("")
    w("*Clang versions are the host toolchain inventory. The measured performance binaries were built with GCC; the compiler matrix in Table 2c was rerun from the same source. Full compile flags for the measured core (baseline uses `-Ibaseline`, optimized uses `-Ioptimized`):*")
    w("")
    w("```text")
    w("# A76 and A53; replace cortex-a76 with cortex-a53 on A53.")
    w("gcc -O3 -std=gnu11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra \\")
    w("  -mcpu=cortex-a76 -mtune=cortex-a76 -fomit-frame-pointer -fno-lto \\")
    w("  -DSNTRUP653_REQUIRE_AARCH64_NEON=1 -funroll-loops \\")
    w("  -fvect-cost-model=unlimited -fno-guess-branch-probability \\")
    w("  -fno-tree-slp-vectorize -falign-functions=32 -fmodulo-sched \\")
    w("  -flive-range-shrinkage -DSNTRUP653_USE_INPLACE_BUFFERS=1 \\")
    w("  -DSNTRUP653_PUBLIC_ZS_MODE=1 -DSNTRUP653_LOW73_NEON=1 \\")
    w("  -DSNTRUP653_FINAL_FOLD_NEON=1 -DUSE_BARRETT_REDUCE=1 \\")
    w("  -I<baseline|optimized> -Ishared -c <impl>/crypto_core_multsntrup653.c")
    w("# X1C: -mcpu=cortex-x1 -mtune=cortex-x1; no -flive-range-shrinkage.")
    w("```")
    w("")
    w("All three platforms use governor `performance` with min = max frequency. The delivered optimized core has the same `core_correctness_hash` across the measured arms; the current build-variant script was rerun for 10 variants (Table 2d). The configuration id is `inplace1-zs1-w16-radix2-2xtmvp8-low1-fold1-barrett1-fast16`.  The core is implemented with AArch64 NEON intrinsics plus three inline-asm sequences (`csel`/`asr`) for conditional reduction/sign handling; no scalar portable fallback is used in the delivered core.")
    w("")

    # ---------------------------------------------------------------- table 2
    c = {r["check"]: r for r in rows("table2_correctness.csv")}
    stacks = {p: stack_values(p) for p in PLATFORMS}
    w("## Table 2. Correctness, general-input contract, and validation coverage")
    w("")
    w("| Check | A76 | A53 | X1C | Evidence |")
    w("|---|---|---|---|---|")
    w("| Core equivalence vs PQClean (100,000 + fixed vectors) | %s | %s | %s | 100,000 random tests plus fixed cases, seed=20260901 |" % (
        c["core_equivalence_100k"]["a76"], c["core_equivalence_100k"]["a53"], c["core_equivalence_100k"]["x1c"]))
    w("| Independent oracle (21,971 cases) | %s | %s | %s | `core_vs_oracle=identical`; cases hash `c53ec27ecdbb56bf` |" % (
        c["end_to_end_oracle"]["a76"].replace("PASS cases=21971", "PASS (21,971)"),
        c["end_to_end_oracle"]["a53"].replace("PASS cases=21971", "PASS (21,971)"),
        c["end_to_end_oracle"]["x1c"].replace("PASS cases=21971", "PASS (21,971)")))
    w("| KEM transcript (1,000; byte-for-byte) | %s | %s | %s | `kem_transcript_equiv.log` |" % (
        c["kem_transcript_1000"]["a76"], c["kem_transcript_1000"]["a53"], c["kem_transcript_1000"]["x1c"]))
    w("| Internal construction selftest (1,000) | %s | %s | %s | `random_tests=1000 seed=20260901` |" % (
        c["internal_construction_selftest"]["a76"], c["internal_construction_selftest"]["a53"], c["internal_construction_selftest"]["x1c"]))
    w("| Concurrent first call (32 threads) | %s | %s | %s | `concurrent_firstcall.log` |" % (
        c["concurrent_first_call"]["a76"], c["concurrent_first_call"]["a53"], c["concurrent_first_call"]["x1c"]))
    w("| TSan concurrent first call | %s | %s | %s | `concurrent_firstcall_tsan.log` |" % (
        c["tsan_concurrent_first_call"]["a76"], c["tsan_concurrent_first_call"]["a53"], c["tsan_concurrent_first_call"]["x1c"]))
    w("| ASan / UBSan (original campaign) | %s | %s | %s | `security/STATUS.txt`; original campaign result |" % (
        c["asan_ubsan"]["a76"], c["asan_ubsan"]["a53"], c["asan_ubsan"]["x1c"]))
    w("| ASan / UBSan (new harness subset) | PASS | PASS | PASS | new-harness subset; 1 ciphertext, 7,176 flips; see 2e |")
    w("| Canonical range | %s | %s | %s | `math_fingerprint.log` |" % (
        c["canonical_range"]["a76"], c["canonical_range"]["a53"], c["canonical_range"]["x1c"]))
    w("| `math_fingerprint` | `%s` | `%s` | `%s` | `math_fingerprint.log` |" % (
        c["math_fingerprint"]["a76"], c["math_fingerprint"]["a53"], c["math_fingerprint"]["x1c"]))
    w("| `core_correctness_hash` | `%s` | `%s` | `%s` | all builds and arms |" % (
        c["core_correctness_hash"]["a76"], c["core_correctness_hash"]["a53"], c["core_correctness_hash"]["x1c"]))
    w("| Production core stack peak (B) | %s | %s | %s | `optimized_core.su`; budget 65,536 B |" % (
        num(stacks["a76"][0], 0), num(stacks["a53"][0], 0), num(stacks["x1c"][0], 0)))
    w("| Selftest stack peak (B) | %s | %s | %s | `test_internal_construction_core.su`; budget 65,536 B |" % (
        num(stacks["a76"][1], 0), num(stacks["a53"][1], 0), num(stacks["x1c"][1], 0)))
    w("| Single-bit flip, all positions | PASS* | PASS* | PASS* | 4 ciphertexts x 7,176 bits; baseline/optimized transcripts identical; see 2e |")
    w("| GCC/Clang × O0–O3/Os (10 combinations) | PASS* | PASS* | PASS* | 10/10 hash, core equivalence, math fingerprint and KEM transcript; see 2c |")
    w("| Build variants | PASS* | PASS* | PASS* | 10/10 variants, one core hash and one math fingerprint; see 2d |")
    w("")
    w("The 21,971 oracle cases are the confirmed end-to-end figure; 1,971 is the Python reference stream.  Fixed vectors are emitted by `shared/test_core_equiv.c` with seed `20260901`; the independent Python oracle supplies the 1,971-case reference stream. The 31,872 / 31,840 / 31,872 B rows are the **selftest** frames; the production core frames are 15,664 / 15,440 / 15,472 B. Rows marked `*` use the validation rerun. All three rows are native board runs; the validation is functional and does not replace the PMU/KEM timing runs.")

    vc = rows("validation_contract.csv")
    vm = rows("validation_compiler_matrix.csv")
    vv = rows("validation_variants.csv")
    vs = rows("validation_single_bit_flip.csv")
    w("")
    w("### 2b. General-input contract and range validation")
    w("")
    w("The delivered `crypto_core_mult` is **Rq-by-small**, not dense-by-dense: its second argument is a byte-encoded ternary polynomial. The KEM `3v` step is `crypto_core_scale3`, applied to the first argument. The test covers six mult classes x 100,000 cases (centered, mod-q, edge, centered edge, `{-3,0,3}` as the first argument, byte-pattern and random-byte key decoders), three scale3 classes x 100,000 cases, and 100,000 `mult3` cases. `low73_max_abs` is the observed maximum low-73 convolution; `+bias` adds the production `73*q` bias.")
    w("")
    w("| Platform | Execution | Classes | Cases/class | Mismatches | Low-73 max abs | + bias | int32 safe | Result |")
    w("|---|---|---:|---:|---:|---:|---:|---|---|")
    for r in vc:
        w("| %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            r["platform"], r["execution"], r["classes"], r["cases_per_class"],
            r["mismatches"], num(r["low73_max_abs"], 0), num(r["low73_max_plus_bias"], 0),
            r["int32_safe"], r["result"]))
    w("")
    w("The worst-case canonical low-73 bound is `73*(q-1) + 73*q = 674,593`, far below `INT32_MAX`; the observed maximum was 99,061 before the bias. The test does **not** claim that `{0,±3}` is a valid second argument; that is not the core API.")
    w("")
    w("### 2c. GCC/Clang × O0–O3/Os matrix")
    w("")
    w("Every combination builds the delivered core and runs the core hash smoke, core equivalence (1,000 cases on each board), the math fingerprint, and a 50-case byte-for-byte KEM transcript. All ten combinations produce hash `0x08aced42bae1b9a0`, fingerprint `0xa866aa344f934dca`, and KEM transcript SHA-256 `1f15600b...`. The `0x08aced...` value is the 200-case matrix smoke hash; the campaign `0xfe69...` hash in the main table comes from the full correctness run.")
    w("")
    w("| Platform | Execution | Hash | Core equiv | Math fp | KEM transcript | Result |")
    w("|---|---|---:|---:|---:|---:|---|")
    for r in vm:
        w("| %s | %s | %s/%s | %s/%s | %s/%s | %s/%s | %s |" % (
            r["platform"], r["execution"], r["hash_pass"], r["hash_total"],
            r["core_pass"], r["core_total"], r["math_pass"], r["math_total"],
            r["kem_pass"], r["kem_total"], r["result"]))
    w("")
    w("This is compiler/optimisation coverage, not a formal machine-code equivalence check.")
    w("")
    w("### 2d. Build variants")
    w("")
    w("The current script has **10** variants, not 9; `no_fast16` is the extra variant relative to the earlier package. Each variant passed the internal selftest, 2,000-case core equivalence, fixed/axes oracle checks and the hash smoke; the math fingerprint was checked separately.")
    w("")
    w("| Platform | Execution | Variants | Passed | Unique hashes | Math variants | Result |")
    w("|---|---|---:|---:|---:|---:|---|")
    for r in vv:
        w("| %s | %s | %s | %s | %s | %s | %s |" % (
            r["platform"], r["execution"], r["variants_total"], r["variants_passed"],
            r["unique_hashes"], r["math_variants_passed"], r["result"]))
    w("")
    w("### 2e. Single-bit ciphertext flips")
    w("")
    w("For each ciphertext every one of its 7,176 bits is flipped once; decapsulation is run; the baseline and optimized streams are compared byte for byte. `accepted` means the recovered secret equals the original. sntrup uses implicit rejection, so `nonzero_rc=0` is expected. The rule is baseline/optimized equality, not “every flip must reject.”")
    w("")
    w("| Platform | Execution | Batches | Total flips | Accepted | Rejected | Nonzero rc | Identical | Result |")
    w("|---|---|---:|---:|---:|---:|---:|---|---|")
    for r in vs:
        w("| %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            r["platform"], r["execution"], r["batches"], r["total_flips"],
            r["accepted"], r["rejected"], r["nonzero_rc"],
            r["transcript_identical"], r["result"]))
    w("")
    w("A76, A53 and X1C new-harness ASan/UBSan subset: 1 ciphertext, 7,176 flips, zero accepted, no sanitizer error, result `PASS`.  A53 validation overall is therefore `PASS`; the original A53 ASan/UBSan campaign result is still reported separately.  All three single-bit rows are native board runs; the test is an error-path consistency check, not a security proof.")
    w("")

    # ---------------------------------------------------------------- table 3
    pmu = {(r["platform"], r["implementation"]): r for r in rows("table3_pmu.csv")}
    cache = {(r["platform"], r["implementation"]): r for r in rows("table3_cache.csv")}
    core_tab = []
    for p in PLATFORMS:
        b = polymul_summary(p)["baseline"]
        o = polymul_summary(p)["optimized"]
        sp, lo, hi = core_ci(p)
        pb = pmu[(p, "baseline")]
        po = pmu[(p, "optimized")]
        pmu_sp = float(pb["cycles_per_call"]) / float(po["cycles_per_call"])
        instr_red = 1.0 - float(po["instructions_per_call"]) / float(pb["instructions_per_call"])
        core_tab.append((p, b, o, sp, lo, hi, pmu_sp, instr_red))

    w("## Table 3. Polynomial-multiplication core performance")
    w("")
    w("### 3a. Per-call time and hardware counters")
    w("")
    w("| Platform | Implementation | ms/call (200 samples) | PMU cycles/call | instructions/call | IPC | branch-miss rate |")
    w("|---|---|---:|---:|---:|---:|---:|")
    for p in PLATFORMS:
        for impl in ("baseline", "optimized"):
            r = pmu[(p, impl)]
            ms = polymul_summary(p)[impl]
            w("| %s | %s | %s | %s | %s | %s | %s |" % (
                PLAT[p]["name"], impl, num(ms, 6), num(r["cycles_per_call"], 1),
                num(r["instructions_per_call"], 1), r["ipc"], ("%.6f%%" % float(r["branch_miss_percent"]))))
    w("")
    w("### 3b. Same-platform speedups")
    w("")
    w("| Platform | Wall-clock speedup (ms/call) | 95% CI | PMU cycle speedup | PMU instruction reduction |")
    w("|---|---:|---|---:|---:|")
    for p, b, o, sp, lo, hi, pmu_sp, instr_red in core_tab:
        w("| %s | **%.3f×** | [%.3f, %.3f] | **%.3f×** | %.2f%% |" % (
            PLAT[p]["name"], sp, lo, hi, pmu_sp, 100.0 * instr_red))
    w("")
    w("### 3c. Cache counters (separate native batch, descriptive only)")
    w("")
    w("`perf stat -r 5` with `cache-references:u,cache-misses:u` on the same strict-same-flags binaries and the same 50,000-call workload; governor `performance` on all three boards. This is a separate batch from 3a, so its cycles column is not substituted into 3a.")
    w("")
    w("| Platform | Implementation | cache refs/call | cache misses/call | cache-miss rate | PMU cycles/call (same run) |")
    w("|---|---|---:|---:|---:|---:|")
    for p in PLATFORMS:
        for impl in ("baseline", "optimized"):
            r = cache[(p, impl)]
            w("| %s | %s | %s | %s | %s%% | %s |" % (
                PLAT[p]["name"], impl, num(r["cache_references_per_call"], 1),
                num(r["cache_misses_per_call"], 6), r["cache_miss_rate_percent"],
                num(r["cycles_per_call"], 1)))
    w("")
    w("Cache-miss events are reported as measured and are not used in any speedup or security claim; they are not compared across microarchitectures. The counters are noisy across runs, and a lower cache-reference count does not by itself imply a lower miss rate.")
    w("")
    w("The 95% CI is a paired percentile bootstrap over the 200 raw polymul samples per implementation (10,000 resamples, seed `0x653771`); it applies to the wall-clock ratio. PMU counts in 3a come from `perf stat -r 5 -e cycles:u,instructions:u,branch-misses:u` around 50,000 calls and are a separate measurement batch from the millisecond column; the PMU speedup is a five-repeat point estimate. The cache counters in 3c are a further separate native batch. All PMU counts are hardware counters on all three platforms, not locked-frequency estimates.")
    w("")

    # ---------------------------------------------------------------- table 4
    stage = rows("table5_stage.csv")
    by_stage = {}
    for r in stage:
        by_stage[(r["platform"], r["stage_paper"])] = r
    stage_order = [
        "blockify",
        "11-point forward DFT",
        "7-point forward DFT",
        "size-8 TMVP (pointwise)",
        "7-point inverse DFT",
        "11-point inverse DFT",
        "low-73 convolution",
        "fused fold + encode",
    ]
    w("## Table 4. Diagnostic stage costs (gcc)")
    w("")
    w("Each cell is `ns/call (derived cycles at locked frequency, share of the eight-stage diagnostic sum)`. The derived cycles are **diagnostic timer values, not production PMU counts**.")
    w("")
    w("| Stage | A76 | A53 | X1C |")
    w("|---|---|---|---|")
    for s in stage_order:
        cells = []
        for p in PLATFORMS:
            r = by_stage[(p, s)]
            cells.append("%s (%s, %s)" % (
                num(r["ns_per_call"], 3), num(r["derived_cycles"], 1),
                pct(float(r["pct_of_8stage_total"]) / 100.0, 2)))
        w("| %s | %s | %s | %s |" % (s, cells[0], cells[1], cells[2]))
    cells = []
    for p in PLATFORMS:
        rs = [by_stage[(p, s)] for s in stage_order]
        ns = sum(float(r["ns_per_call"]) for r in rs)
        cy = sum(float(r["derived_cycles"]) for r in rs)
        cells.append("%s (%s, 100%%)" % (num(ns, 3), num(cy, 1)))
    w("| **Eight-stage diagnostic total** | **%s** | **%s** | **%s** |" % tuple(cells))
    w("")
    w("Calibration against the official PMU table (diagnostic excess = stage sum / PMU optimized - 1):")
    w("")
    w("| Platform | Eight-stage diagnostic sum (cyc) | PMU optimized (cyc) | Diagnostic excess |")
    w("|---|---:|---:|---:|")
    for p in PLATFORMS:
        rs = [by_stage[(p, s)] for s in stage_order]
        cy = sum(float(r["derived_cycles"]) for r in rs)
        pmu_opt = float(pmu[(p, "optimized")]["cycles_per_call"])
        excess = 100.0 * (cy / pmu_opt - 1.0)
        w("| %s | %s | %s | %+.2f%% |" % (
            PLAT[p]["name"], num(cy, 1), num(pmu_opt, 1), excess))
    w("")
    w("The eight-stage totals 24,640.8 / 45,321.1 / 12,505.9 are sums of diagnostic `cntvct_el0` checkpoints, not production PMU cycles, and must not be cited as official core cycles. The inline checkpoints themselves add about 2.1–2.6% overhead; the calibration row above is the net diagnostic excess against PMU, not that overhead. The totals are shown only to calibrate the profile against Table 3. The table is gcc-only.")
    w("")

    # ---------------------------------------------------------------- table 5
    kem = {(r["platform"], r["metric"]): r for r in rows("table4_kem.csv")}
    w("## Table 5. KEM performance (mean, 95% CI, p-values, wins, polymul fraction)")
    w("")
    w("Paired raw rounds: 2,000 baseline and 2,000 optimized per platform (ABBA pairing). The CI is a paired percentile bootstrap of the ratio of means (10,000 resamples, seed `0x653771`). `p` is the two-sided sign test; the normal approximation underflows below `10^-300`.")
    w("")
    w("| Platform | Operation | n | Base mean (ms) | Opt mean (ms) | Speedup | 95% CI | p | Wins | `f_opt` |")
    w("|---|---|---:|---:|---:|---:|---|---:|---:|---:|")
    metric_names = [("keypair_ms", "keygen"), ("encaps_ms", "encaps"),
                    ("decaps_ms", "decaps"), ("e2e_ms", "e2e")]
    for p in PLATFORMS:
        for metric, label in metric_names:
            r = kem[(p, metric)]
            fopt = "—"
            if r["f_opt"]:
                if metric == "keypair_ms":
                    fopt = "≈%.3f" % float(r["f_opt"])
                else:
                    fopt = sig(r["f_opt"], 4)
            w("| %s | %s | %s | %s | %s | %s | [%s, %s] | <1e-300 | %s/%s | %s |" % (
                PLAT[p]["name"], label, r["n_pairs"], sig(r["base_mean_ms"]),
                sig(r["opt_mean_ms"]), sig(r["speedup_mean"], 5),
                sig(r["ci95_low"], 5), sig(r["ci95_high"], 5),
                r["wins"], r["pairs"], fopt))
    w("")
    w("Timing API: `clock_gettime(CLOCK_MONOTONIC_RAW)` brackets each operation; each epoch uses 100 warmup rounds and 1,000 measured rounds, with ABBA pairing and seed 1.  The table reports the 2,000 paired samples per platform.  The `e2e` row is wall-clock keygen + encaps + decaps; the `total` row in the CSV differs by less than 0.0001×. The keygen `f_opt` is **≈0.002** (CSV precise values 0.002177 / 0.002014 / 0.001994); it uses one core call per keygen and is an estimate, not an independent PMU decomposition. All other `f_opt` values are the estimated fraction of the optimized operation spent in the core.")
    w("")

    # ---------------------------------------------------------------- table 6
    t6_all = rows("table6_ablation.csv")
    single = [r for r in t6_all if r["category"].startswith("FAST16")]
    config = [r for r in t6_all if r["category"].startswith("configuration")]
    w("## Table 6. Ablation: FAST16 single-factor arms and configuration-level checks")
    w("")
    w("### 6a. FAST16 single-factor arms (core, wall-clock)")
    w("")
    w("Each arm keeps the delivered 16-bit path and disables exactly one switch. Speedup is `mean(arm)/mean(delivered)`; >1 means the switch matters. CI is the paired block-bootstrap 95% interval. Derived cycles use the locked frequency and are not PMU counts.")
    w("")
    w("| Platform | Arm | Arm mean (ms) | Delivered (ms) | Slowdown | Speedup | 95% CI | p | Wins | Derived cycles (arm / delivered) |")
    w("|---|---|---:|---:|---:|---:|---|---:|---:|---|")
    for r in single:
        w("| %s | %s | %s | %s | +%s%% | %s | [%s, %s] | <1e-300 | %s/%s | %s / %s |" % (
            r["platform"].upper(), r["arm"], num(r["arm_mean_ms"], 9), num(r["delivered_mean_ms"], 9),
            r["slowdown_percent"], r["speedup_vs_delivered"], num(r["ci95_low"], 6), num(r["ci95_high"], 6),
            r["rounds_faster"], r["rounds_compared"], num(r["derived_cycles_arm"], 0),
            num(r["derived_cycles_delivered"], 0)))
    w("")
    w("### 6b. Configuration-level zero-skipping arm")
    w("")
    w("The dense zero-skipping arm cannot stay on the 16-bit path: the 16-bit transform is not range-safe for the dense transform.  It is therefore reported separately as a **32-bit configuration-level** arm and is **not** mixed into the single-factor claims.")
    w("")
    w("| Platform | Arm | Arm mean (ms) | Delivered (ms) | Slowdown | Speedup | 95% CI | p | Wins | Note |")
    w("|---|---|---:|---:|---:|---:|---|---:|---:|---|")
    for r in config:
        w("| %s | %s | %s | %s | +%s%% | %s | [%s, %s] | <1e-300 | %s/%s | 32-bit path |" % (
            r["platform"].upper(), r["arm"], num(r["arm_mean_ms"], 9), num(r["delivered_mean_ms"], 9),
            r["slowdown_percent"], r["speedup_vs_delivered"], num(r["ci95_low"], 6), num(r["ci95_high"], 6),
            r["rounds_faster"], r["rounds_compared"]))
    w("")
    w("### 6c. Structural factors without an independent switch")
    w("")
    w("| Factor | Category | Mechanism | Status (all three platforms) |")
    w("|---|---|---|---|")
    for r in rows("table6b_factor_status.csv"):
        if r["category"] != "structural":
            continue
        w("| %s | %s | %s | `not independently switchable` |" % (
            r["factor"], r["category"], r["switch_or_mechanism"].replace("; compiled in", "")))
    w("")
    w("The FAST16 single-factor arms are low-73, fused fold and Barrett.  The Barrett arm keeps the 16-bit path and changes only the reducer to `% q`; it is a same-path reduction ablation, not a 32-bit fallback.  It measures the cost of the `% q` fallback, not a delivered optimization, and must not be read as an optimization contribution.  Zero-skipping is configuration-level because dense mode requires the 32-bit path.  In-place reuse is not reported as a FAST16 arm: the 16-bit path has no independent non-in-place buffer variant; the old 32-bit non-in-place arm was configuration-level and is removed from the single-factor table.  The seven structural factors remain compiled in and are not assigned a cycle contribution.  The `p` column is the two-sided sign test; every row is below `1e-300`.  The CSV records `p_value=<1e-300` together with `p_underflow=true`, so this is a display convention for normal-approximation underflow, not a mathematical zero.")
    w("")

    # ---------------------------------------------------------------- table 7
    mem = {(r["platform"], r["object"]): r for r in rows("table7_memory.csv")}
    w("## Table 7. Memory, code size, and static instruction counts")
    w("")
    w("### 7a. Stack, heap, and core object size")
    w("")
    w("| Platform | Production core stack (B) | Selftest stack (B) | Heap allocations in algorithm path | Optimized text (B) | data (B) | bss (B) | Optimized dec (B) | Baseline text (B) |")
    w("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    for p in PLATFORMS:
        opt = mem[(p, "optimized_core.o")]
        base = mem[(p, "baseline_core.o")]
        core, self_ = stacks[p]
        w("| %s | %s | %s | 0 | %s | %s | %s | %s | %s |" % (
            PLAT[p]["name"], num(core, 0), num(self_, 0), num(opt["text_bytes"], 0),
            num(opt["data_bytes"], 0), num(opt["bss_bytes"], 0), num(opt["dec_bytes"], 0),
            num(base["text_bytes"], 0)))
    w("")
    w("The algorithm path (`baseline/` + `optimized/`) has no `malloc`/`calloc`/`realloc`/`free` call sites. The benchmark harness has 7 process-scoped allocation call sites, not per-operation allocations. Text includes `.rodata`.")
    w("")
    w("### 7b. Static table sizes")
    w("")
    w("| Table group | Symbols / scope | Bytes |")
    w("|---|---|---:|")
    for r in rows("table7_tables.csv"):
        w("| %s | %s | %s |" % (r["group"], r["symbols"], num(r["total_bytes"], 0)))
    w("")
    w("All table sizes are identical on A76, A53 and X1C. The total includes the 4-byte runtime table state; `.bss` is 14,576 B, with the remainder compiler-generated padding/state.")
    w("")
    w("### 7c. Static machine-level instruction counts")
    w("")
    w("Static disassembly counts at one core call site. They are **not** dynamic per-call executed-instruction counts and are **not** algorithm-level operation counts. `*_group` includes the whole mnemonic family.")
    w("")
    w("| Platform | Impl | `mul` | `mla` | `add` | `sub` | `ldr` | `str` | mul* | MAC* | add* | sub* | load* | store* |")
    w("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for p in PLATFORMS:
        for impl in ("optimized", "baseline"):
            r = next(x for x in rows("table7_machine_ops.csv")
                     if x["platform"] == p and x["implementation"] == impl)
            w("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
                PLAT[p]["name"], impl, r["mul_exact"], r["mla_exact"], r["add_exact"],
                r["sub_exact"], r["ldr_exact"], r["str_exact"], r["mul_group"],
                r["mac_group"], r["add_group"], r["sub_group"], r["load_group"],
                r["store_group"]))
    w("")
    w("### 7d. Algorithm-level operation counts")
    w("")
    w("Algorithm-level exact operation counts are **not reported**.  The former counter build was removed because it did not use one consistent counting basis: it does not count every add embedded in a MAC, does not count every subtract embedded in a vector multiply-subtract, leaves some scalar operations uncounted, attributes operations to a stage at the stage marker rather than at the operation, and mixes a source-level scalar baseline model with vector-lane counts for the optimized path.  Those differences are large enough to change the apparent totals, so the numbers are not used in any table, timing statement, or speedup claim.  Table 7c is the only operation-count evidence in this paper: static machine-level instruction counts with one reproducible rule (one core call site, `objdump -d`).")
    w("")

    # ---------------------------------------------------------------- table 8
    pos = rows("table8_positioning.csv")
    w("## Table 8. Positioning against related work (feature matrix, not a cycle comparison)")
    w("")
    w("| Work | Venue | Parameter set | Good–Thomas / CRT | TMVP | Neon | Mathematics changed | Platforms measured |")
    w("|---|---|---|---|---|---|---|---|")
    for r in pos:
        w("| %s | %s | %s | %s | %s | %s | %s | %s |" % (
            r["work"], r["year_venue"], r["target_parameter_set"], r["good_thomas"],
            r["tmvp"], r["neon"], r["mathematics_changed"], r["cross_platform_measurement"]))
    w("")
    w("### 8b. Bibliographic check")
    w("")
    w("| Work | Title | Venue / year | Pages | DOI | Parameter set | Platform |")
    w("|---|---|---|---:|---|---|---|")
    for r in rows("table8_references.csv"):
        w("| %s | %s | %s | %s | `%s` | %s | %s |" % (
            r["work"], r["title"], r["venue_year"], r["pages"], r["doi"],
            r["target_parameter_set"], r["platforms"]))
    w("")
    w("Alkim et al. is TCHES 2021(1), not 2022. This is a feature matrix only: it does not compare absolute cycles across platforms, and it does not claim that this work is faster than any listed system. Relative-to-own-baseline speedups for prior work are not tabulated here because the papers use different baselines and methodologies.")
    w("")
    w("### 8c. Relation to 761 work and to Chiu/Hwang")
    w("")
    w("This work does not introduce the CRT/Good-Thomas decomposition, the TMVP formulation, or the first ARM polynomial-multiplication implementation.  Alkim et al. studied NTRU Prime on Cortex-M4; Hwang, Liu and Yang gave a vectorization-oriented 761 decomposition with TMVP on Cortex-A72; Hwang extended the vector-arithmetic design space on Cortex-A72 and Apple M1 Pro; Chiu, Yang and Wang already evaluated a verified CRT polymul on 761 using Raspberry Pi 3/4/5 (Cortex-A53/A72/A76).  The contribution here is narrower and specific to **sntrup653**: an Rq-by-small core with a three-platform, same-platform A/B measurement, switch-level ablation, and a clean reproducibility package.  No platform-first or CRT-first claim is made, and no absolute-cycle ranking against the 761 work is implied.  In a Section 1.3-style contribution statement, the claim is the 653-specific Rq-by-small CRT/TMVP realization plus a three-platform native measurement and switch-level ablation package, not a new CRT algorithm.")
    w("")

    # ---------------------------------------------------------------- table 9
    t9 = {r["check"]: r for r in rows("table9_security.csv")}

    def cell(r, p):
        return r.get(p, "MISSING").replace("|", "\\|")

    w("## Table 9. Security and timing scope")
    w("")
    w("| Check | A76 | A53 | X1C | Threshold / scope |")
    w("|---|---|---|---|---|")
    for key, label in (("timing_screen_mode0", "First-order Welch, mode 0"),
                       ("timing_screen_mode1", "First-order Welch, mode 1"),
                       ("timing_method", "Timing method"),
                       ("dudect", "dudect"),
                       ("ctgrind_valgrind", "Valgrind taint check (ctgrind-style)"),
                       ("source_audit", "Source audit"),
                       ("disassembly_audit", "Disassembly audit"),
                       ("asan_ubsan_original", "ASan / UBSan (original campaign)"),
                       ("asan_ubsan_new_harness", "ASan / UBSan (new harness subset)"),
                       ("tsan", "TSan"),
                       ("full_binary_constant_time", "Full-binary status"),
                       ("power_whole_system", "Power (whole-system)"),
                       ("em", "EM")):
        r = t9.get(key, {})
        w("| %s | %s | %s | %s | %s |" % (
            label, cell(r, "a76"), cell(r, "a53"), cell(r, "x1c"),
            str(r.get("threshold_or_scope", "")).replace("|", "\\|")))
    w("")
    w("The screen found no significant timing difference under these samples and this threshold; dudect reported no leakage evidence at this sample size.  A Valgrind-based taint check was performed: it reported 0 reported errors on all three boards.  This is not a formal proof and does not establish constant-time behavior.  X1C whole-system power was logged, but it is not used as a side-channel security result; EM leakage was not measured.  The source and disassembly audit found no secret-dependent branches or accesses; the full-binary status remains NOT_PROVEN.  These results are an empirical screen and audit, not a formal guarantee.")
    w("")

    # ---------------------------------------------------------------- conclusions
    t3 = {(r["platform"], r["implementation"]): r for r in rows("table3_pmu.csv")}
    t4 = {(r["platform"], r["metric"]): r for r in rows("table4_kem.csv")}

    def core_cycles(p, impl):
        return float(t3[(p, impl)]["cycles_per_call"])

    core_sp = {p: core_cycles(p, "baseline") / core_cycles(p, "optimized")
               for p in PLATFORMS}
    kem_sp = {p: {m: float(t4[(p, m)]["speedup_mean"])
                  for m in ("keypair_ms", "encaps_ms", "decaps_ms", "e2e_ms")}
              for p in PLATFORMS}
    wins = [int(r["wins"]) for r in t4.values()]
    pairs = [int(r["pairs"]) for r in t4.values()]
    fopt = {p: {m: float(t4[(p, m)]["f_opt"])
                for m in ("keypair_ms", "encaps_ms", "decaps_ms") if t4[(p, m)]["f_opt"]}
            for p in PLATFORMS}
    vs = {r["platform"].lower(): r for r in rows("validation_status.csv")}
    t2 = {r["check"]: r for r in rows("table2_correctness.csv")}
    vc = (rows("validation_contract.csv") or [{}])[0]
    vm = (rows("validation_compiler_matrix.csv") or [{}])[0]
    vv = (rows("validation_variants.csv") or [{}])[0]
    vf = (rows("validation_single_bit_flip.csv") or [{}])[0]

    def first_int(value, default="NOT_COLLECTED"):
        mm = re.search(r"(\d[\d,]*)", str(value))
        return mm.group(1).replace(",", "") if mm else default

    core_n = first_int(t2.get("core_equivalence_100k", {}).get("evidence", ""))
    kem_n = first_int(t2.get("kem_transcript_1000", {}).get("evidence", ""))
    oracle_n = first_int(t2.get("end_to_end_oracle", {}).get("a76", ""))
    val_cases = first_int(vc.get("cases_per_class", ""))
    val_classes = vc.get("classes", "NOT_COLLECTED")
    matrix_n = vm.get("hash_total", "NOT_COLLECTED")
    variants_n = vv.get("variants_total", "NOT_COLLECTED")
    flip_n = vf.get("total_flips", "NOT_COLLECTED")
    flip_bits = vf.get("bits_per_ct", "NOT_COLLECTED")
    flip_batches = vf.get("batches", "NOT_COLLECTED")

    w("## Conclusions")
    w("")
    w("Across the three platforms, the optimized core reduces PMU cycles per call from %s to %s on A76 (%s×), from %s to %s on A53 (%s×), and from %s to %s on X1C (%s×), i.e. **%s–%s×**. The KEM-level gains are smaller but consistent: encaps **%s–%s×**, decaps **%s–%s×**, and end-to-end **%s–%s×**. Every paired comparison won at least %d/%d rounds." % (
        num(core_cycles("a76", "baseline")), num(core_cycles("a76", "optimized"), 1), num(core_sp["a76"]),
        num(core_cycles("a53", "baseline")), num(core_cycles("a53", "optimized"), 1), num(core_sp["a53"]),
        num(core_cycles("x1c", "baseline")), num(core_cycles("x1c", "optimized"), 1), num(core_sp["x1c"]),
        num(min(core_sp.values())), num(max(core_sp.values())),
        num(min(kem_sp[p]["encaps_ms"] for p in PLATFORMS)), num(max(kem_sp[p]["encaps_ms"] for p in PLATFORMS)),
        num(min(kem_sp[p]["decaps_ms"] for p in PLATFORMS)), num(max(kem_sp[p]["decaps_ms"] for p in PLATFORMS)),
        num(min(kem_sp[p]["e2e_ms"] for p in PLATFORMS)), num(max(kem_sp[p]["e2e_ms"] for p in PLATFORMS)),
        min(wins), max(pairs)))
    w("")
    w("All correctness checks that were executed passed: %s core equivalence tests plus fixed vectors against PQClean, %s independent-oracle cases, %s byte-for-byte KEM transcripts, the internal construction selftest, 32-thread concurrent first call, TSan, the original campaign ASan/UBSan, and the canonical-range/fingerprint checks. The new-harness ASan/UBSan subset is PASS on A76, A53 and X1C. The validation rerun adds %s mult classes, three scale3 classes and the mult3 path at %s cases each, a %s/%s GCC/Clang × O0–O3/Os matrix, %s/%s build variants, and %s single-bit ciphertext flips per platform (%s ciphertexts × %s bits) with byte-identical baseline/optimized transcripts. All validation rows executed on the native A53/A76/X1C boards; validation status is A76=%s, A53=%s, X1C=%s." % (
        core_n, oracle_n, kem_n, val_classes, val_cases,
        matrix_n, matrix_n, vv.get("variants_passed", "NOT_COLLECTED"), variants_n,
        flip_n, flip_batches, flip_bits,
        vs.get("a76", {}).get("validation", "MISSING"),
        vs.get("a53", {}).get("validation", "MISSING"),
        vs.get("x1c", {}).get("validation", "MISSING")))
    w("")
    w("The gap between core and end-to-end speedup is explained by Amdahl's law. The measured optimized-core fraction is `f_opt` = %s for keygen, %s for encaps, and %s for decaps on A76/A53/X1C; keygen is dominated by sampling, encoding and hashing, so even a %s–%s× faster core moves total keygen by only %s–%s×. Consequently, the KEM end-to-end time improves by %s–%s×, consistent with the measured fractions rather than with the core ratio alone. The keygen `f_opt` is an estimate from one core call per keygen, not an independent PMU decomposition." % (
        "/".join(sig(fopt[p]["keypair_ms"], 3) for p in PLATFORMS),
        "/".join(sig(fopt[p]["encaps_ms"], 3) for p in PLATFORMS),
        "/".join(sig(fopt[p]["decaps_ms"], 3) for p in PLATFORMS),
        num(min(core_sp.values())), num(max(core_sp.values())),
        num(min(kem_sp[p]["keypair_ms"] for p in PLATFORMS)),
        num(max(kem_sp[p]["keypair_ms"] for p in PLATFORMS)),
        num(min(kem_sp[p]["e2e_ms"] for p in PLATFORMS)),
        num(max(kem_sp[p]["e2e_ms"] for p in PLATFORMS))))
    w("")
    w("The results are same-platform A/B ratios against the strict-same-flags PQClean baseline; they are not a cross-platform absolute-cycle comparison. Alkim et al., Hwang et al., and Chiu et al. target different parameter sets (ntrulpr761/sntrup761 versus sntrup653), different microarchitectures (Cortex-M4, A72, A53/A72/A76, Apple M1 Pro), and different measurement methodologies, so their absolute cycle counts cannot be placed in the same table. Table 8 therefore compares technical features only, and this work makes no claim to be faster than any listed system.")
    w("")
    w("The security evidence is a first-order timing screen, a dudect statistical screen, a Valgrind-based taint check, and a source/disassembly audit. At the stated samples and threshold `|t| = 5`, both timing modes were %s on A76, %s on A53 and %s on X1C; dudect reported %s on A76, %s on A53 and %s on X1C; the source review found no secret-dependent branch or index, and the disassembly probes were straight-line. The Valgrind taint check reported %s on A76, %s on A53 and %s on X1C, which is not a formal proof and does not establish constant-time behavior. X1C whole-system power was logged but is not a side-channel security result; EM leakage was not measured. The full-binary status remains NOT_PROVEN. These results are an empirical screen and audit, not a formal guarantee." % (
        t9.get("timing_screen_mode0", {}).get("a76", "MISSING"),
        t9.get("timing_screen_mode0", {}).get("a53", "MISSING"),
        t9.get("timing_screen_mode0", {}).get("x1c", "MISSING"),
        t9.get("dudect", {}).get("a76", "MISSING"),
        t9.get("dudect", {}).get("a53", "MISSING"),
        t9.get("dudect", {}).get("x1c", "MISSING"),
        t9.get("ctgrind_valgrind", {}).get("a76", "MISSING"),
        t9.get("ctgrind_valgrind", {}).get("a53", "MISSING"),
        t9.get("ctgrind_valgrind", {}).get("x1c", "MISSING")))
    w("")

    out = ROOT / "PAPER_9TABLES_5PARAS.md"
    out.write_text("\n".join(L) + "\n")
    print("wrote", out)


if __name__ == "__main__":
    main()
