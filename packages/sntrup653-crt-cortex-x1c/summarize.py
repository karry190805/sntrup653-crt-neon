#!/usr/bin/env python3
"""summarize.py — statistics for the sntrup653 CRT release package.

Subcommands
    summarize.py kem       OUT.csv  RAW.csv  [RAW.csv ...]
    summarize.py polymul   OUT.csv  RAW.csv  [RAW.csv ...]
    summarize.py sessions  OUT.csv  SESSION_DIR [SESSION_DIR ...]
    summarize.py ablation  OUT.csv  ABLATION_DIR

Conventions
    kem/polymul/sessions: speedup = mean(baseline) / mean(optimized)
                          -> > 1 means the delivered core is faster.
    ablation: delivered_speedup = mean(arm) / mean(delivered)
              -> > 1 means the removed optimization has a positive effect.
    verdict = POSITIVE | NEGATIVE | NEUTRAL, from a block-bootstrap CI95 of the
              per-round paired ratio and a two-sided sign test.
    Rounds from different arms use the same seed, so round i is the same KEM
    input in every arm. Ratios are therefore input-paired, not simultaneous.

Standard library only: the boards run a bare python3.
"""
import csv
import glob
import math
import os
import random
import re
import sys

BOOT_ITERS = 4000
BOOT_BLOCKS = 40
BOOT_SEED = 0x653771

KEM_METRICS = ["keypair_ms", "encaps_ms", "decaps_ms", "total_ms", "e2e_ms"]
POLYMUL_METRICS = ["ms_per_call"]
LABEL = {
    "keypair_ms": "keygen",
    "encaps_ms": "encaps",
    "decaps_ms": "decaps",
    "total_ms": "total(3 ops)",
    "e2e_ms": "e2e(wall)",
    "ms_per_call": "polymul",
}


# ---------------------------------------------------------------- helpers
def mean(values):
    return sum(values) / len(values)


def median(values):
    data = sorted(values)
    n = len(data)
    if n == 0:
        return float("nan")
    mid = n // 2
    return data[mid] if n % 2 else 0.5 * (data[mid - 1] + data[mid])


def pct(values, fraction):
    data = sorted(values)
    if not data:
        return float("nan")
    if len(data) == 1:
        return data[0]
    pos = (len(data) - 1) * fraction
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return data[lo]
    return data[lo] * (hi - pos) + data[hi] * (pos - lo)


def phi(z):
    return 0.5 * (1.0 + math.erf(z / math.sqrt(2.0)))


def block_bootstrap_ci(values, iters=BOOT_ITERS, blocks=BOOT_BLOCKS, seed=BOOT_SEED):
    """Percentile CI95 of the mean, resampling contiguous blocks.

    Blocking keeps short-range drift (thermal, scheduling) inside a block
    instead of pretending the rounds are independent.
    """
    n = len(values)
    if n == 0:
        return float("nan"), float("nan")
    if n == 1:
        return values[0], values[0]
    b = max(1, min(blocks, n))
    chunks = [values[i * n // b:(i + 1) * n // b] for i in range(b)]
    chunk_means = [mean(c) for c in chunks if c]
    rng = random.Random(seed)
    draws = []
    m = len(chunk_means)
    for _ in range(iters):
        s = 0.0
        for _ in range(m):
            s += chunk_means[rng.randrange(m)]
        draws.append(s / m)
    return pct(draws, 0.025), pct(draws, 0.975)


def sign_test(ratios, center=1.0):
    """Two-sided sign test, normal approximation with continuity correction."""
    k = sum(1 for r in ratios if r > center)
    n = sum(1 for r in ratios if r != center)
    if n == 0:
        return float("nan"), k, 0
    z = (k - 0.5 - n / 2.0) / math.sqrt(n / 4.0)
    return 2.0 * (1.0 - phi(abs(z))), k, n


def verdict(ci_low, ci_high):
    if ci_low > 1.0:
        return "POSITIVE"
    if ci_high < 1.0:
        return "NEGATIVE"
    return "NEUTRAL"


def paired_stats(ref, cand):
    """ref/cand per round: >1 means `cand` is faster."""
    n = min(len(ref), len(cand))
    ratios = [ref[i] / cand[i] for i in range(n) if cand[i] > 0]
    ci_low, ci_high = block_bootstrap_ci(ratios)
    p, k, m = sign_test(ratios)
    return {
        "n": n,
        "mean_ratio": mean(ratios) if ratios else float("nan"),
        "ci95_low": ci_low,
        "ci95_high": ci_high,
        "p_value": p,
        "rounds_faster": k,
        "rounds_compared": m,
        "verdict": verdict(ci_low, ci_high),
    }


def load_raw(paths, metrics):
    """-> {(impl, metric): [value by round index]}"""
    out = {}
    for path in paths:
        rows = []
        with open(path, newline="") as handle:
            for row in csv.DictReader(handle):
                rows.append(row)
        rows.sort(key=lambda r: int(r.get("round", 0)))
        for row in rows:
            impl = row["implementation"]
            for metric in metrics:
                if metric in row and row[metric] != "":
                    out.setdefault((impl, metric), []).append(float(row[metric]))
    return out


def write_csv(path, rows, fields):
    directory = os.path.dirname(path)
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(path, "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    print("summary_csv=%s" % path)


def head(label):
    print("=" * 78)
    print(label)
    print("=" * 78)


# ---------------------------------------------------------------- kem / polymul
def cmd_ab(kind, output, inputs):
    metrics = KEM_METRICS if kind == "kem" else POLYMUL_METRICS
    values = load_raw(inputs, metrics)
    present = {impl for impl, _ in values}
    missing = {"baseline", "optimized"} - present
    if missing:
        raise SystemExit("missing implementations: %s (got %s)" % (sorted(missing), sorted(present)))

    rows = []
    for impl in ("baseline", "optimized"):
        for metric in metrics:
            data = values.get((impl, metric))
            if not data:
                continue
            rows.append({
                "implementation": impl,
                "metric": metric,
                "n": len(data),
                "mean": mean(data),
                "median": median(data),
                "p95": pct(data, 0.95),
                "min": min(data),
                "max": max(data),
            })
    fields = ["implementation", "metric", "n", "mean", "median", "p95", "min", "max"]
    write_csv(output, rows, fields)

    head("%s: raw summary (ms)" % kind.upper())
    print("implementation,metric,n,mean,median,p95,min,max")
    for row in rows:
        print("%s,%s,%d,%.9f,%.9f,%.9f,%.9f,%.9f" % (
            row["implementation"], row["metric"], row["n"], row["mean"],
            row["median"], row["p95"], row["min"], row["max"]))

    head("%s: speedup = mean(baseline) / mean(optimized); >1 means the CRT core wins" % kind.upper())
    print("metric,mean_baseline,mean_optimized,speedup,ci95_low,ci95_high,p_value,"
          "rounds_faster/compared,verdict")
    verdicts = []
    for metric in metrics:
        base = values.get(("baseline", metric))
        opt = values.get(("optimized", metric))
        if not base or not opt:
            continue
        st = paired_stats(base, opt)
        speedup = mean(base) / mean(opt)
        verdicts.append("%s=%s" % (LABEL.get(metric, metric), st["verdict"]))
        print("%s,%.9f,%.9f,%.9f,%.9f,%.9f,%.3g,%d/%d,%s" % (
            metric, mean(base), mean(opt), speedup, st["ci95_low"], st["ci95_high"],
            st["p_value"], st["rounds_faster"], st["rounds_compared"], st["verdict"]))
    if kind == "kem" and ("baseline", "e2e_ms") in values and ("baseline", "total_ms") in values:
        head("kem: measurement bookkeeping")
        for impl in ("baseline", "optimized"):
            extra = mean(values[(impl, "e2e_ms")]) - mean(values[(impl, "total_ms")])
            print("%s: e2e - (keygen+encaps+decaps) = %.9f ms  (clock overhead, %.3f%% of e2e)"
                  % (impl, extra, 100.0 * extra / mean(values[(impl, "e2e_ms")])))
    print("verdicts=" + ",".join(verdicts))


# ---------------------------------------------------------------- sessions
def cmd_sessions(output, session_dirs):
    if len(session_dirs) != 5 or len(set(os.path.realpath(s) for s in session_dirs)) != 5:
        raise SystemExit("the paper summary requires exactly five distinct sessions")
    first = session_dirs[0]
    for session in session_dirs:
        status_path = os.path.join(session, "FINAL_STATUS.txt")
        if os.path.exists(os.path.join(session, "FAILED.txt")) or not os.path.exists(status_path):
            raise SystemExit("incomplete or failed session: " + session)
        with open(status_path) as handle:
            status = handle.read()
        for needle in ("wallclock_session=PASS", "functional_equivalence=PASS"):
            if needle not in status:
                raise SystemExit("incomplete or failed session: " + session)
        for name in ("protocol.conf", "platform.conf", "frequency_before.txt",
                     "frequency_after.txt", "compiler_macros.txt"):
            with open(os.path.join(session, name), "rb") as handle:
                current = handle.read()
            with open(os.path.join(first, name), "rb") as handle:
                reference = handle.read()
            if current != reference:
                raise SystemExit("mixed configuration: " + os.path.join(session, name))

    def build_config(path):
        with open(path) as handle:
            return [line for line in handle.read().splitlines()
                    if not line.startswith("generated_at_utc=")]

    for session in session_dirs[1:]:
        if build_config(os.path.join(session, "build_metadata.txt")) != \
                build_config(os.path.join(first, "build_metadata.txt")):
            raise SystemExit("mixed build configuration: " + session)

    metrics = None
    sessions = []
    for session in session_dirs:
        groups = {}
        for part in ("first", "second"):
            path = os.path.join(session, "kem_" + part, "summary.csv")
            if not os.path.exists(path):
                continue
            with open(path, newline="") as handle:
                for row in csv.DictReader(handle):
                    impl, metric, value = row.get("implementation"), row.get("metric"), row.get("mean")
                    if impl in ("baseline", "optimized") and value:
                        groups.setdefault(metric, {}).setdefault(impl, []).append(float(value))
        if not groups:
            raise SystemExit("no usable summary.csv in " + session)
        if metrics is None:
            metrics = sorted(groups)
        per_metric = {}
        for metric, impls in groups.items():
            values = impls.get("baseline", []) + impls.get("optimized", [])
            if len(impls.get("baseline", [])) != 2 or len(impls.get("optimized", [])) != 2:
                raise SystemExit("expected two sub-runs per metric in %s: %s" % (session, metric))
            if not all(math.isfinite(v) and v > 0 for v in values):
                raise SystemExit("invalid timing in " + session)
            base = mean(impls["baseline"])
            opt = mean(impls["optimized"])
            per_metric[metric] = (base, opt, base / opt)
        sessions.append(per_metric)

    rows = []
    head("sessions: session-level paired speedup (unit = one paired run directory)")
    print("bootstrap=session ratios; percentile; resamples=10000; blocks=session count")
    for metric in metrics:
        ratios = [s[metric][2] for s in sessions if metric in s]
        if not ratios:
            continue
        print()
        print("metric=%s" % metric)
        print("session,baseline_mean,optimized_mean,speedup")
        for session, per_metric in zip(session_dirs, sessions):
            if metric not in per_metric:
                continue
            base, opt, ratio = per_metric[metric]
            print("%s,%.9f,%.9f,%.9f" % (os.path.basename(session.rstrip("/")), base, opt, ratio))
        pairs = [per_metric[metric] for per_metric in sessions if metric in per_metric]
        rng = random.Random(BOOT_SEED)
        draws = []
        for _ in range(10000):
            draws.append(mean([ratios[rng.randrange(len(ratios))] for _ in ratios]))
        ci_low, ci_high = pct(draws, 0.025), pct(draws, 0.975)
        st = sign_test(ratios)
        print("baseline_mean,%.9f" % mean([p[0] for p in pairs]))
        print("optimized_mean,%.9f" % mean([p[1] for p in pairs]))
        print("session_mean_speedup,%.9f" % mean(ratios))
        print("session_median_speedup,%.9f" % median(ratios))
        print("session_bootstrap_ci95,%.9f,%.9f" % (ci_low, ci_high))
        print("session_verdict,%s" % verdict(ci_low, ci_high))
        rows.append({
            "metric": metric,
            "sessions": len(ratios),
            "baseline_mean": mean([p[0] for p in pairs]),
            "optimized_mean": mean([p[1] for p in pairs]),
            "mean_speedup": mean(ratios),
            "median_speedup": median(ratios),
            "ci95_low": ci_low,
            "ci95_high": ci_high,
            "p_value": st[0],
            "verdict": verdict(ci_low, ci_high),
        })
    write_csv(output, rows, ["metric", "sessions", "baseline_mean", "optimized_mean",
                             "mean_speedup", "median_speedup", "ci95_low", "ci95_high",
                             "p_value", "verdict"])


# ---------------------------------------------------------------- ablation
def cmd_ablation(output, roots):
    if len(roots) != 1 or not os.path.isdir(roots[0]):
        raise SystemExit("ablation requires exactly one result directory")
    root = roots[0]
    pattern = re.compile(r"^(core|kem)_(\d+)_(.+)_raw\.csv$")
    groups = {}
    for path in sorted(glob.glob(os.path.join(root, "*_raw.csv"))):
        match = pattern.match(os.path.basename(path))
        if not match:
            continue
        kind, sequence, arm = match.groups()
        metrics = POLYMUL_METRICS if kind == "core" else KEM_METRICS
        values = load_raw([path], metrics)
        if len({impl for impl, _ in values}) != 1:
            raise SystemExit("mixed implementations in " + path)
        for metric in metrics:
            candidates = [data for (impl, name), data in values.items()
                          if name == metric]
            if len(candidates) != 1 or not candidates[0]:
                raise SystemExit("missing metric %s in %s" % (metric, path))
            data = candidates[0]
            if not all(math.isfinite(value) and value > 0 for value in data):
                raise SystemExit("invalid timing in " + path)
            groups.setdefault((kind, arm, metric), []).append(
                (int(sequence), data))

    arms = sorted({arm for _, arm, _ in groups})
    if "all" not in arms:
        raise SystemExit("ablation data has no delivered 'all' arm")
    expected = {"all", "baseline", "no_inplace", "no_zs", "no_low73", "no_fold"}
    missing = expected - set(arms)
    if missing:
        raise SystemExit("missing ablation arms: %s" % sorted(missing))

    def merged(kind, arm, metric):
        epochs = groups.get((kind, arm, metric), [])
        return [value for _, data in sorted(epochs) for value in data], len(epochs)

    rows = []
    head("ABLATION: delivered_speedup = mean(arm) / mean(delivered)")
    print("kind,arm,metric,epochs,samples,arm_mean_ms,delivered_mean_ms,"
          "delivered_speedup,ci95_low,ci95_high,p_value,rounds_faster/compared,verdict")
    arm_order = ["all", "baseline", "no_inplace", "no_zs", "no_low73", "no_fold"]
    # Report any extra arms present in the data (e.g. the opt-in no_barrett)
    # after the required ones instead of silently dropping them.
    arm_order += [arm for arm in arms if arm not in arm_order]
    for kind, metrics in (("core", POLYMUL_METRICS), ("kem", KEM_METRICS)):
        for arm in arm_order:
            for metric in metrics:
                arm_data, epochs = merged(kind, arm, metric)
                delivered, delivered_epochs = merged(kind, "all", metric)
                if not arm_data or not delivered:
                    raise SystemExit("incomplete ablation data for %s/%s/%s" %
                                     (kind, arm, metric))
                if len(arm_data) != len(delivered):
                    raise SystemExit("unpaired ablation sample counts for %s/%s/%s" %
                                     (kind, arm, metric))
                st = paired_stats(arm_data, delivered)
                speedup = mean(arm_data) / mean(delivered)
                row = {
                    "kind": kind,
                    "arm": arm,
                    "metric": metric,
                    "epochs": epochs,
                    "delivered_epochs": delivered_epochs,
                    "samples": len(arm_data),
                    "arm_mean_ms": mean(arm_data),
                    "delivered_mean_ms": mean(delivered),
                    "delivered_speedup": speedup,
                    "ci95_low": st["ci95_low"],
                    "ci95_high": st["ci95_high"],
                    "p_value": st["p_value"],
                    "rounds_faster": st["rounds_faster"],
                    "rounds_compared": st["rounds_compared"],
                    "verdict": st["verdict"],
                }
                rows.append(row)
                print("%s,%s,%s,%d,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.3g,%d/%d,%s" % (
                    kind, arm, metric, epochs, len(arm_data), mean(arm_data),
                    mean(delivered), speedup, st["ci95_low"], st["ci95_high"],
                    st["p_value"], st["rounds_faster"], st["rounds_compared"],
                    st["verdict"]))
    fields = ["kind", "arm", "metric", "epochs", "delivered_epochs", "samples",
              "arm_mean_ms", "delivered_mean_ms", "delivered_speedup", "ci95_low",
              "ci95_high", "p_value", "rounds_faster", "rounds_compared", "verdict"]
    write_csv(output, rows, fields)
    print("ablation_summary=PASS arms=%d rows=%d" % (len(arm_order), len(rows)))


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__.strip())
    kind, output, rest = sys.argv[1], sys.argv[2], sys.argv[3:]
    if kind in ("kem", "polymul"):
        cmd_ab(kind, output, rest)
    elif kind == "sessions":
        cmd_sessions(output, rest)
    elif kind == "ablation":
        cmd_ablation(output, rest)
    else:
        raise SystemExit(__doc__.strip())


if __name__ == "__main__":
    main()
