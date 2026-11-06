#!/usr/bin/env python3
"""Build Table 3c: cache-reference / cache-miss counters (native cache batch).

Input: provenance/raw/pmu-cache-v4/<platform>/{baseline,optimized}.txt
and metadata.txt.  The binaries are the original strict-same-flags PMU
campaign binaries, i.e. the same binaries used by Table 3a/3b.

The cache batch is deliberately kept separate from the Table 3a PMU batch.
Cache-miss events are descriptive only: they are not used in any speedup or
security claim and are not compared across microarchitectures.
"""
import csv
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
PACKAGE = os.path.dirname(HERE)
RAW = os.path.join(PACKAGE, "provenance", "raw", "pmu-cache-v4")
OUT = os.path.join(PACKAGE, "csv", "table3_cache.csv")
PLATFORMS = ("a76", "a53", "x1c")
CALLS = 50000

EVENTS = ("cycles:u", "instructions:u", "branch-misses:u",
          "cache-references:u", "cache-misses:u")
RE = re.compile(r"([\d,]+)\s+(cycles:u|instructions:u|branch-misses:u|"
                r"cache-references:u|cache-misses:u)")


def last_block(blob):
    blocks = re.findall(
        r"Performance counter stats.*?seconds time elapsed[^\n]*", blob, re.S)
    return blocks[-1] if blocks else blob


def read_kv(path):
    out = {}
    for line in open(path, errors="replace"):
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def main():
    fields = [
        "platform", "implementation", "core_calls",
        "cycles_per_call", "instructions_per_call",
        "cache_references", "cache_misses",
        "cache_references_per_call", "cache_misses_per_call",
        "cache_miss_rate_percent", "governor", "cur_khz",
        "source_binary_sha256", "batch", "note",
    ]
    out_rows = []
    for p in PLATFORMS:
        d = os.path.join(RAW, p)
        meta_text = open(os.path.join(d, "metadata.txt"), errors="replace").read()
        meta = read_kv(os.path.join(d, "metadata.txt"))
        gov_match = re.search(r"governor=(\S+)", meta_text)
        cur_match = re.search(r"cur_khz=(\d+)", meta_text)
        governor = gov_match.group(1) if gov_match else meta.get("governor", "")
        cur_khz = cur_match.group(1) if cur_match else meta.get("cur_khz", "")
        for impl in ("baseline", "optimized"):
            blob = open(os.path.join(d, impl + ".txt"), errors="replace").read()
            got = {}
            for number, label in RE.findall(last_block(blob)):
                got[label] = int(number.replace(",", ""))
            missing = [e for e in EVENTS if e not in got]
            if missing:
                raise SystemExit("%s/%s missing %s" % (p, impl, ",".join(missing)))
            refs = got["cache-references:u"]
            misses = got["cache-misses:u"]
            out_rows.append({
                "platform": p,
                "implementation": impl,
                "core_calls": CALLS,
                "cycles_per_call": "%.3f" % (got["cycles:u"] / CALLS),
                "instructions_per_call": "%.3f" % (got["instructions:u"] / CALLS),
                "cache_references": refs,
                "cache_misses": misses,
                "cache_references_per_call": "%.3f" % (refs / CALLS),
                "cache_misses_per_call": "%.6f" % (misses / CALLS),
                "cache_miss_rate_percent": "%.6f" % (100.0 * misses / refs if refs else 0.0),
                "governor": governor,
                "cur_khz": cur_khz,
                "source_binary_sha256": meta.get("binary_" + impl, ""),
                "batch": "native-cache-v4",
                "note": ("perf stat -r 5, cycles:u/instructions:u/branch-misses:u/"
                         "cache-references:u/cache-misses:u; separate native cache "
                         "batch; descriptive only, not a speedup or security claim"),
            })

    with open(OUT, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        w.writerows(out_rows)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
