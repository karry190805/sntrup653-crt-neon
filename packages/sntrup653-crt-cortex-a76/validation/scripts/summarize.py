#!/usr/bin/env python3
"""Summarise one native validation run.

The overall status is derived from the per-step records, not from a single
subtest.  A failed required step is FAIL; a documented NOT_RUN step is PARTIAL;
only a run with every required step passing is PASS.
"""
import csv
import re
import sys
from pathlib import Path

OUT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "results" / "validation"
META = {}
for line in (OUT / "metadata.txt").read_text().splitlines():
    if "=" in line:
        k, v = line.split("=", 1)
        META[k] = v
PLATFORM = META.get("platform", "?")
EXEC = META.get("execution", "UNVERIFIED")


def text(path):
    try:
        return Path(path).read_text(errors="replace")
    except FileNotFoundError:
        return ""


def steps():
    out = {}
    for line in text(OUT / "steps.txt").splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def parse_contract():
    s = text(OUT / "contract" / "output.txt")
    m = re.search(r"contract_summary cases_per_class=(\d+) mismatches=(\d+) low73_max_abs=(\d+) low73_bias=(\d+) low73_max_plus_bias=(\d+) int32_safe=(\d+)", s)
    if not m:
        return None
    ok = "contract_validation=PASS" in s and int(m.group(2)) == 0 and int(m.group(6)) == 1
    return {
        "platform": PLATFORM, "execution": EXEC, "classes": 6,
        "cases_per_class": int(m.group(1)), "mismatches": int(m.group(2)),
        "low73_max_abs": int(m.group(3)), "low73_bias": int(m.group(4)),
        "low73_max_plus_bias": int(m.group(5)), "int32_safe": int(m.group(6)),
        "result": "PASS" if ok else "FAIL",
    }


def parse_compiler():
    out = {"platform": PLATFORM, "execution": EXEC}
    for key in ("hash", "core", "math", "kem"):
        s = text(OUT / "compiler" / key / "results.txt")
        out[key + "_pass"] = len(re.findall(r"RESULT=PASS", s))
        out[key + "_total"] = 10
    out["result"] = "PASS" if all(out[k + "_pass"] == 10 for k in ("hash", "core", "math", "kem")) else "FAIL"
    return out


def parse_variants():
    s = text(OUT / "variants" / "results.txt")
    m = re.search(r"variants_total=(\d+)\s+variants_passed=(\d+)\s+variants_failed=(\d+)\s+unique_hashes=(\d+)", s)
    ms = text(OUT / "variant_math" / "results.txt")
    math_pass = len(re.findall(r"^variant-.*RESULT=PASS", ms, re.M))
    if not m:
        return None
    return {
        "platform": PLATFORM, "execution": EXEC,
        "variants_total": int(m.group(1)), "variants_passed": int(m.group(2)),
        "variants_failed": int(m.group(3)), "unique_hashes": int(m.group(4)),
        "math_variants_passed": math_pass,
        "result": "PASS" if int(m.group(3)) == 0 and int(m.group(4)) == 1 and math_pass == int(m.group(1)) else "FAIL",
    }


def parse_flip(path):
    s = text(path / "summary.txt")
    m = re.search(r"single_bit_flip batches=(\d+) bits_per_ct=(\d+) total=(\d+) accepted=(\d+) rejected=(\d+) nonzero_rc=(\d+)", s)
    if not m:
        return None
    ok = "single_bit_flip=PASS" in s and "transcript_identical=1" in s
    return {
        "platform": PLATFORM, "execution": EXEC, "batches": int(m.group(1)),
        "bits_per_ct": int(m.group(2)), "total_flips": int(m.group(3)),
        "accepted": int(m.group(4)), "rejected": int(m.group(5)),
        "nonzero_rc": int(m.group(6)), "transcript_identical": 1 if ok else 0,
        "result": "PASS" if ok else "FAIL",
    }


def write_csv(name, rows, fields):
    (OUT / "summary").mkdir(exist_ok=True)
    with open(OUT / "summary" / name, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        for r in rows:
            if r:
                w.writerow(r)


def main():
    st = steps()
    ct = parse_contract()
    cm = parse_compiler()
    vv = parse_variants()
    fl = parse_flip(OUT / "single_bit_flip")
    fa = parse_flip(OUT / "single_bit_flip_asan")
    asan_status = st.get("single_bit_flip_asan", "NOT_RUN")
    if asan_status.startswith("PASS"):
        asan_status = "PASS"
    elif asan_status.startswith("FAIL"):
        asan_status = "FAIL"
    else:
        asan_status = "NOT_RUN"

    write_csv("contract.csv", [ct],
              ["platform", "execution", "classes", "cases_per_class", "mismatches",
               "low73_max_abs", "low73_bias", "low73_max_plus_bias", "int32_safe", "result"])
    write_csv("compiler_matrix.csv", [cm],
              ["platform", "execution", "hash_pass", "hash_total", "core_pass", "core_total",
               "math_pass", "math_total", "kem_pass", "kem_total", "result"])
    write_csv("variants.csv", [vv],
              ["platform", "execution", "variants_total", "variants_passed", "variants_failed",
               "unique_hashes", "math_variants_passed", "result"])
    write_csv("single_bit_flip.csv", [fl],
              ["platform", "execution", "batches", "bits_per_ct", "total_flips",
               "accepted", "rejected", "nonzero_rc", "transcript_identical", "result"])
    write_csv("single_bit_flip_asan.csv", [fa],
              ["platform", "execution", "batches", "bits_per_ct", "total_flips",
               "accepted", "rejected", "nonzero_rc", "transcript_identical", "result"])

    required = ["host_check", "contract", "compiler_hash", "compiler_core",
                "compiler_math", "compiler_kem", "variants", "variant_math",
                "single_bit_flip", "summarize"]
    failed = [k for k in required if st.get(k, "").startswith("FAIL")]
    notrun = [k for k, v in st.items() if v.startswith("NOT_RUN")]
    if failed:
        overall = "FAIL"
    elif asan_status != "PASS" or notrun:
        overall = "PARTIAL"
    else:
        overall = "PASS"

    L = []
    L.append("# Native validation summary — %s" % PLATFORM)
    L.append("")
    L.append("Execution: **%s**; board `%s`; host `%s`; CPU pin `%s`; MIDR `%s`; source tree `%s`." % (
        EXEC, META.get("board_model", "?"), META.get("hostname", "?"),
        META.get("cpu_pin", "?"), META.get("midr", "?"),
        META.get("source_tree_sha256", "?")))
    L.append("")
    L.append("Overall status: **%s**.  The sanitizer-subset status is **%s**." % (overall, asan_status))
    L.append("")
    L.append("## Contract / range")
    L.append("")
    if ct:
        L.append("| Classes | Cases/class | Mismatches | Low-73 max | +bias | int32 safe | Result |")
        L.append("|---:|---:|---:|---:|---:|---|---|")
        L.append("| %d | %d | %d | %d | %d | %d | %s |" % (
            ct["classes"], ct["cases_per_class"], ct["mismatches"], ct["low73_max_abs"],
            ct["low73_max_plus_bias"], ct["int32_safe"], ct["result"]))
    else:
        L.append("Contract output missing or unparsable.")
    L.append("")
    L.append("## Compiler matrix")
    L.append("")
    L.append("| Hash | Core equiv | Math fp | KEM transcript | Result |")
    L.append("|---:|---:|---:|---:|---|")
    L.append("| %d/%d | %d/%d | %d/%d | %d/%d | %s |" % (
        cm["hash_pass"], cm["hash_total"], cm["core_pass"], cm["core_total"],
        cm["math_pass"], cm["math_total"], cm["kem_pass"], cm["kem_total"], cm["result"]))
    L.append("")
    L.append("## Build variants")
    L.append("")
    if vv:
        L.append("| Variants | Passed | Unique hashes | Math variants | Result |")
        L.append("|---:|---:|---:|---:|---|")
        L.append("| %d | %d | %d | %d | %s |" % (
            vv["variants_total"], vv["variants_passed"], vv["unique_hashes"],
            vv["math_variants_passed"], vv["result"]))
    else:
        L.append("Variant output missing or unparsable.")
    L.append("")
    L.append("## Single-bit ciphertext flips")
    L.append("")
    if fl:
        L.append("| Batches | Total flips | Accepted | Rejected | Nonzero rc | Identical | Result |")
        L.append("|---:|---:|---:|---:|---:|---|---|")
        L.append("| %d | %d | %d | %d | %d | %d | %s |" % (
            fl["batches"], fl["total_flips"], fl["accepted"], fl["rejected"],
            fl["nonzero_rc"], fl["transcript_identical"], fl["result"]))
    else:
        L.append("Single-bit-flip output missing or unparsable.")
    L.append("")
    L.append("## Sanitizer subset")
    L.append("")
    if fa:
        L.append("ASan/UBSan subset: %d flips, accepted %d, rejected %d, identical %d, **%s**." % (
            fa["total_flips"], fa["accepted"], fa["rejected"], fa["transcript_identical"], fa["result"]))
    else:
        L.append("ASan/UBSan subset: **%s**." % asan_status)
        if asan_status == "NOT_RUN":
            L.append("The original campaign sanitizer result is a separate result and is not a result from this harness.")
    L.append("")
    L.append("## Step log")
    L.append("")
    L.append("| Step | Result |")
    L.append("|---|---|")
    for k in sorted(st):
        L.append("| %s | %s |" % (k, st[k]))
    L.append("")
    L.append("These are functional checks; they are not performance measurements and do not insert counters into the timing binary.")
    (OUT / "validation_summary.md").write_text("\n".join(L) + "\n")
    (OUT / "status.txt").write_text(
        "validation=%s\nasan_subset=%s\nsource_tree_sha256=%s\n" % (
            overall, asan_status, META.get("source_tree_sha256", "?")))
    print("wrote", OUT / "summary")


if __name__ == "__main__":
    main()
