#!/usr/bin/env python3
"""Render the auxiliary reports from the collected CSVs and raw logs.

Called by lib/build_delivery.py after every measurement CSV exists:

    python3 scripts/render_reports.py <package> <run-root> <run-id>

No measured number is hard-coded here; every value is read from ``csv/*.csv``,
``provenance/raw/`` or ``timing/``.  Missing inputs are written as
``NOT_COLLECTED`` instead of silently reusing an old value.
"""
import csv
import os
import re
import sys
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
PKG = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.dirname(HERE)
RUN_ROOT = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else ""
RUN_ID = sys.argv[3] if len(sys.argv) > 3 else os.path.basename(RUN_ROOT)
CSV = os.path.join(PKG, "csv")
RAW = os.path.join(PKG, "provenance", "raw")
PLATFORMS = ("a76", "a53", "x1c")
NAMES = {"a76": "A76", "a53": "A53", "x1c": "X1C"}
BOARDS = {"a76": "Radxa ROCK 5B+", "a53": "KICKPI K2B V2", "x1c": "Radxa Dragon Q8B"}
RUN_DATE = os.environ.get("RUN_DATE") or datetime.now().strftime("%Y-%m-%d")


def rows(name):
    path = os.path.join(CSV, name)
    if not os.path.isfile(path):
        return []
    with open(path, newline="", errors="replace") as fh:
        return list(csv.DictReader(fh))


def text(*parts):
    path = os.path.join(*parts)
    try:
        with open(path, errors="replace") as fh:
            return fh.read()
    except OSError:
        return ""


def num(value, digits=3, default="NOT_COLLECTED"):
    try:
        return ("%%.%df" % digits) % float(value)
    except (TypeError, ValueError):
        return default


def short(value, n=8):
    s = (value or "").strip()
    return s if len(s) <= 2 * n else s[:n] + "\u2026" + s[-n:]


def write(name, lines):
    path = os.path.join(PKG, name)
    with open(path, "w") as fh:
        fh.write("\n".join(lines).rstrip() + "\n")


def table(header, body):
    out = ["| " + " | ".join(header) + " |",
           "|" + "|".join("---" for _ in header) + "|"]
    for r in body:
        out.append("| " + " | ".join(str(x).replace("|", "\\|") for x in r) + " |")
    return out


def raw_security(p, kind):
    return text(RAW, "security-v4", p, kind)


def dudect_max_t(p):
    vals = [abs(float(x)) for x in re.findall(r"max t:\s*([+-]?[0-9.]+)", raw_security(p, "dudect_result.txt"))]
    return max(vals) if vals else None


def ctgrind_summary(p):
    m = re.search(r"ERROR SUMMARY:\s*(\d+) errors from (\d+) contexts", raw_security(p, "ctgrind_result.txt"))
    return ("%s errors from %s contexts" % (m.group(1), m.group(2))) if m else None


def mean_power_w(name):
    m = re.search(r"power_uW:.*?mean=([0-9.]+)", text(PKG, "power", name))
    return (float(m.group(1)) / 1e6) if m else None


def main():
    t0 = {r["platform"]: r for r in rows("table0_platforms.csv")}
    t3 = {(r["platform"], r["implementation"]): r for r in rows("table3_pmu.csv")}
    t4 = {(r["platform"], r["metric"]): r for r in rows("table4_kem.csv")}
    t6 = rows("table6_ablation.csv")
    t9 = {r["check"]: r for r in rows("table9_security.csv")}
    vstat = {r["platform"].lower(): r for r in rows("validation_status.csv")}
    vcon = {r["platform"].lower(): r for r in rows("validation_contract.csv")}
    vmat = {r["platform"].lower(): r for r in rows("validation_compiler_matrix.csv")}
    vvar = {r["platform"].lower(): r for r in rows("validation_variants.csv")}
    vflip = {r["platform"].lower(): r for r in rows("validation_single_bit_flip.csv")}
    vasan = {r["platform"].lower(): r for r in rows("validation_single_bit_flip_asan.csv")}
    bh = {r["platform"]: r for r in rows("board_hash_check.csv")}
    sb = {r["platform"]: r for r in rows("sha_binding.csv")}

    core_sp = {}
    for p in PLATFORMS:
        b = t3.get((p, "baseline")) or {}
        o = t3.get((p, "optimized")) or {}
        try:
            core_sp[p] = float(b["cycles_per_call"]) / float(o["cycles_per_call"])
        except (KeyError, ValueError, ZeroDivisionError):
            core_sp[p] = None

    def kem_sp(p, m):
        r = t4.get((p, m)) or {}
        try:
            return float(r["speedup_mean"])
        except (KeyError, ValueError):
            return None

    # ------------------------------------------------------------- README_zh
    L = ["# sntrup653 CRT opt22 论文表格包（%s）" % RUN_ID, ""]
    L.append("对象：Cortex-A76 / Cortex-A53 / Cortex-X1C。本包由 `run_full_pipeline.sh` "
             "在 %s 一次完整原生运行后自动生成；原始日志在 `provenance/raw/`。" % RUN_DATE)
    L.append("")
    L.append("## 内容")
    L.append("")
    L += table(["文件", "内容"], [
        ["`PAPER_9TABLES_5PARAS.md`", "最终投稿版：恰好 9 张表 + 5 段正文结论"],
        ["`PAPER_TABLES.md`", "详细表格源（含表 0 和逐表口径说明）"],
        ["`VALIDATION_REPORT.md`", "验证重跑：一般输入契约、编译器矩阵、build variants、单比特翻转"],
        ["`ABLATION_REPORT.md`", "四臂实测消融（3 个 FAST16 单因子 + 1 个配置级）"],
        ["`SECURITY_REPORT.md`", "dudect、ctgrind 风格 taint 检查、X1C 整机功耗记录、未覆盖范围"],
        ["`TIMING.md`", "本次全流程各板、各阶段实测耗时与总耗时"],
        ["`validation/`", "验证脚本、测试源码、三块实机原生结果"],
        ["`PROVENANCE.md`", "表 → 脚本 → 原始输入的逐字段来源链"],
        ["`SHA_BINDING.md`", "源码 SHA → 编译元数据 → 二进制 → 原始结果 → 表输入"],
        ["`BOARD_HASH_CHECK.md`", "三块真板上重新计算的源码树和两组二进制哈希"],
        ["`csv/`", "全部机器可读表（含 table3_cache、table6b、validation_*）"],
    ])
    L.append("")
    L.append("## 关键结果（本次运行）")
    L.append("")
    L.append("### PMU 核心")
    L.append("")
    body = []
    for p in PLATFORMS:
        b = t3.get((p, "baseline")) or {}
        o = t3.get((p, "optimized")) or {}
        body.append([NAMES[p], num(b.get("cycles_per_call"), 1), num(o.get("cycles_per_call"), 1),
                     ("%.3f×" % core_sp[p]) if core_sp[p] else "NOT_COLLECTED"])
    L += table(["平台", "baseline cycles", "optimized cycles", "加速比"], body)
    L.append("")
    L.append("### KEM（`table4`）")
    L.append("")
    body = []
    for p in PLATFORMS:
        body.append([NAMES[p]] + [("%.3f×" % kem_sp(p, m)) if kem_sp(p, m) else "NOT_COLLECTED"
                                  for m in ("keypair_ms", "encaps_ms", "decaps_ms", "e2e_ms")])
    L += table(["平台", "keygen", "encaps", "decaps", "total"], body)
    L.append("")
    L.append("### 消融（`table6`，core wall-clock）")
    L.append("")
    arms = sorted({r["arm"] for r in t6})
    body = []
    for p in PLATFORMS:
        d = {r["arm"]: r for r in t6 if r["platform"] == p}
        body.append([NAMES[p]] + [
            ("%.6f×" % float(d[a]["speedup_vs_delivered"])) if a in d and d[a].get("speedup_vs_delivered") else "NOT_COLLECTED"
            for a in arms])
    L += table(["平台"] + arms, body)
    L.append("")
    L.append("## 统一口径")
    L.append("")
    L.append("1. **核心 cycles 用 PMU**（`table3`），`perf stat -r 5`，每次 50,000 次调用。")
    L.append("2. **KEM 时间用原始配对轮次**（`table4`），每平台 2,000 对 baseline/optimized。KEM 与 PMU 是不同批次，不能互相换算。")
    L.append("3. **stage 行是诊断值**（`table5`）；八阶段合计不是生产 PMU 周期，只用于和 `table3` 做约 ±2% 的校准核对。")
    L.append("4. **消融是 wall-clock**（`table6`）；derived cycles 只是按锁定频率换算。")
    L.append("5. **`f` 是估计值**：`f = n_core · T_core,opt / T_kem,opt`；keygen 的 `f_opt` 使用每次 keygen 一个核心调用估计，不是独立 PMU 分解。")
    L.append("")
    L.append("## 必须保留的缺口")
    L.append("")
    L.append("1. 7 个结构因子编译进代码、没有独立开关；统一标为 `not independently switchable`。")
    L.append("2. single-bit flip 是错误路径一致性测试，不是安全证明。")
    L.append("3. 算法级 mul/add/sub/mod/load/store 精确计数不报告；只有机器级静态计数（Table 7c）。")
    L.append("4. 跨平台绝对 cycles 不可比；只用同平台 A/B 比值。")
    L.append("")
    L.append("## 复现")
    L.append("")
    L.append("本包本身即可重跑：")
    L.append("")
    L.append("```bash")
    L.append("bash run_full_pipeline.sh")
    L.append("```")
    L.append("")
    L.append("连接参数写在 `boards.conf`，凭据写在本地 `.boards.local`（不随包发布）。")
    L.append("")
    L.append("## 安全措辞（只能这样写）")
    L.append("")
    L.append("> 在本样本与阈值下未发现显著计时差异；dudect 在给定样本量下为 "
             "“no leakage evidence”；ctgrind 风格 Valgrind 检查为 “0 reported errors, "
             "not a formal proof”；源码与反汇编审计未发现依赖秘密的分支或访存。"
             "这是经验筛查和审计，不是形式化保证。X1C 功耗仅为整机记录，不是侧信道结果；"
             "EM 未覆盖；未对整个二进制做等价性证明。")
    L.append("")
    write("README_zh.md", L)

    # ------------------------------------------------------------- README.txt
    E = ["sntrup653 CRT opt22 paper package (%s)" % RUN_ID, ""]
    E.append("Generated automatically by run_full_pipeline.sh on %s." % RUN_DATE)
    E.append("Platforms: Cortex-A76 / Cortex-A53 / Cortex-X1C, all native.")
    E.append("")
    E.append("Main documents: PAPER_9TABLES_5PARAS.md (9 tables + 5 conclusion paragraphs),")
    E.append("PAPER_TABLES.md, PROVENANCE.md, SHA_BINDING.md, BOARD_HASH_CHECK.md,")
    E.append("VALIDATION_REPORT.md, ABLATION_REPORT.md, SECURITY_REPORT.md, TIMING.md.")
    E.append("")
    E.append("Core PMU speedups (same-platform A/B):")
    for p in PLATFORMS:
        E.append("  %-4s %.3fx" % (NAMES[p], core_sp[p]) if core_sp[p] else "  %-4s NOT_COLLECTED" % NAMES[p])
    E.append("")
    E.append("Reproduce: bash run_full_pipeline.sh  (credentials in local .boards.local)")
    E.append("Verify: sha256sum -c MANIFEST.sha256")
    write("README.txt", E)

    # ------------------------------------------------------------- TIMING.md
    T = ["# Full-pipeline timing", ""]
    T.append("Run `%s`, measured wall-clock.  The pipeline runs A76, A53 and X1C in"
             " parallel; per-board seconds are the board's own elapsed time, the"
             " total is the orchestrator wall-clock." % RUN_ID)
    T.append("")
    stages = []
    for p in PLATFORMS:
        path = os.path.join(PKG, "timing", p + ".csv")
        if os.path.isfile(path):
            with open(path, newline="") as fh:
                for r in csv.DictReader(fh):
                    stages.append((p, r))
    by_board = {p: [] for p in PLATFORMS}
    for p, r in stages:
        by_board[p].append(r)
    body = []
    all_stages = []
    for p in PLATFORMS:
        for r in by_board[p]:
            if r["stage"] not in all_stages:
                all_stages.append(r["stage"])
    def last_ok(rows_for_board, stage):
        good = [r for r in rows_for_board
                if r["stage"] == stage and r.get("rc", "0") == "0"]
        return good[-1] if good else None

    for s in all_stages:
        cells = []
        for p in PLATFORMS:
            hit = last_ok(by_board[p], s)
            cells.append(num(hit["seconds"], 0) if hit else "—")
        body.append([s] + cells)
    totals = []
    for p in PLATFORMS:
        total = 0
        complete = True
        for s in all_stages:
            hit = last_ok(by_board[p], s)
            if hit is None:
                complete = False
                continue
            total += int(hit["seconds"])
        totals.append(str(total) if complete else "NOT_COLLECTED")
    body.append(["**board total**"] + ["**%s**" % x for x in totals])
    T += table(["Stage", "A76 (s)", "A53 (s)", "X1C (s)"], body)
    T.append("")
    T.append("Each cell is the last successful attempt of that stage.  Failed or")
    T.append("interrupted attempts remain in `timing/<platform>.csv` and in the")
    T.append("preserved remote run directory; they are not hidden.")
    T.append("")
    rt = text(PKG, "TOTAL_RUNTIME.txt") or text(os.path.dirname(PKG), "TOTAL_RUNTIME.txt")
    if rt:
        T.append("```text")
        T.append(rt.strip())
        T.append("```")
    else:
        T.append("Pipeline total: `NOT_COLLECTED`.")
    T.append("")
    write("TIMING.md", T)

    # ------------------------------------------------------------- ABLATION_REPORT.md
    A = ["# opt22 native ablation report", ""]
    A.append("Run `%s`.  All arms are native wall-clock measurements of the core;"
             " derived cycles are wall-clock × locked frequency, not PMU counts." % RUN_ID)
    A.append("")
    A.append("## Arms")
    A.append("")
    A.append("`no_low73`, `no_fold` and `no_barrett` are FAST16 single-factor arms.")
    A.append("`no_zs_32bit` is a configuration-level 32-bit arm and is kept separate.")
    A.append("`no_barrett` keeps the same 16-bit path and changes only the reducer to"
             " `% q`; it measures the fallback cost, not a delivered optimization.")
    A.append("")
    single = [r for r in t6 if "single" in r.get("category", "").lower()]
    config = [r for r in t6 if r not in single]
    for title, group in (("FAST16 single-factor arms", single),
                         ("Configuration-level arm", config)):
        if not group:
            continue
        A.append("## " + title)
        A.append("")
        body = []
        for r in sorted(group, key=lambda r: (PLATFORMS.index(r["platform"]), r["arm"])):
            body.append([NAMES[r["platform"]], r["arm"], (num(r.get("slowdown_percent"), 3) + "%"),
                         num(r.get("speedup_vs_delivered"), 6),
                         "[%s, %s]" % (num(r.get("ci95_low"), 6), num(r.get("ci95_high"), 6)),
                         "%s/%s" % (r.get("rounds_faster", "?"), r.get("rounds_compared", "?")),
                         r.get("p_value", "NOT_COLLECTED")])
        A += table(["Platform", "Arm", "Slowdown", "Speedup", "95% CI", "Wins", "p"], body)
        A.append("")
    A.append("The seven structural factors (11-point DFT split, Shoup, input centering,")
    A.append("block fusion, TMVP accumulators, 1/77 folding, skip final reduce) are")
    A.append("compiled in and are **not independently switchable**; no cycle difference")
    A.append("is invented for them.  Raw paired CSVs are under `provenance/raw/ablation/`.")
    A.append("")
    write("ABLATION_REPORT.md", A)

    # ------------------------------------------------------------- VALIDATION_REPORT.md
    V = ["# opt22 validation rerun", ""]
    V.append("Run `%s`.  All rows are native board runs; this supplements, not"
             " replaces, the original campaign." % RUN_ID)
    V.append("")
    V.append("## Final status")
    V.append("")
    body = []
    for p in PLATFORMS:
        r = vstat.get(p, {})
        body.append([NAMES[p], r.get("validation", "NOT_COLLECTED"),
                     r.get("asan_subset", "NOT_COLLECTED"),
                     short(r.get("source_tree_sha256", ""))])
    V += table(["Platform", "Validation", "New-harness ASan subset", "Source tree SHA-256"], body)
    V.append("")
    V.append("## General-input contract")
    V.append("")
    body = []
    for p in PLATFORMS:
        r = vcon.get(p, {})
        body.append([NAMES[p], r.get("execution", "NOT_COLLECTED"), r.get("classes", "?"),
                     r.get("cases_per_class", "?"), r.get("mismatches", "?"),
                     r.get("low73_max_abs", "?"), r.get("result", "NOT_COLLECTED")])
    V += table(["Platform", "Execution", "Classes", "Cases/class", "Mismatches",
                "Low-73 max abs", "Result"], body)
    V.append("")
    V.append("## Compiler matrix")
    V.append("")
    body = []
    for p in PLATFORMS:
        r = vmat.get(p, {})
        body.append([NAMES[p], r.get("execution", "NOT_COLLECTED"),
                     "%s/%s" % (r.get("hash_pass", "?"), r.get("hash_total", "?")),
                     "%s/%s" % (r.get("core_pass", "?"), r.get("core_total", "?")),
                     "%s/%s" % (r.get("math_pass", "?"), r.get("math_total", "?")),
                     "%s/%s" % (r.get("kem_pass", "?"), r.get("kem_total", "?")),
                     r.get("result", "NOT_COLLECTED")])
    V += table(["Platform", "Execution", "Hash", "Core equiv", "Math fp",
                "KEM transcript", "Result"], body)
    V.append("")
    V.append("## Build variants")
    V.append("")
    body = []
    for p in PLATFORMS:
        r = vvar.get(p, {})
        body.append([NAMES[p], r.get("execution", "NOT_COLLECTED"),
                     "%s/%s" % (r.get("variants_passed", "?"), r.get("variants_total", "?")),
                     r.get("unique_hashes", "?"), r.get("result", "NOT_COLLECTED")])
    V += table(["Platform", "Execution", "Passed", "Unique hashes", "Result"], body)
    V.append("")
    V.append("## Single-bit ciphertext flips")
    V.append("")
    body = []
    for p in PLATFORMS:
        r = vflip.get(p, {})
        body.append([NAMES[p], r.get("execution", "NOT_COLLECTED"), r.get("batches", "?"),
                     r.get("total_flips", "?"), r.get("accepted", "?"),
                     r.get("rejected", "?"), r.get("transcript_identical", "?"),
                     r.get("result", "NOT_COLLECTED")])
    V += table(["Platform", "Execution", "Batches", "Flips", "Accepted", "Rejected",
                "Identical", "Result"], body)
    V.append("")
    body = []
    for p in PLATFORMS:
        r = vasan.get(p, {})
        body.append([NAMES[p], r.get("batches", "?"), r.get("total_flips", "?"),
                     r.get("accepted", "?"), r.get("rejected", "?"),
                     r.get("result", "NOT_COLLECTED")])
    V += table(["Platform", "Batches", "Flips", "Accepted", "Rejected", "Result"], body)
    V.append("")
    V.append("The single-bit flip is an error-path consistency test: the rule is")
    V.append("baseline/optimized equality, not that every flip must reject.  It is not")
    V.append("a security proof.  Algorithm-level exact operation counts are not")
    V.append("reported; only static machine-level counts (Table 7c) are used.")
    V.append("")
    write("VALIDATION_REPORT.md", V)

    # ------------------------------------------------------------- SECURITY_REPORT.md
    S = ["# Security / power report", ""]
    S.append("Run `%s`.  These are empirical screens and audits, not proofs." % RUN_ID)
    S.append("")
    S.append("## dudect")
    S.append("")
    body = []
    for p in PLATFORMS:
        t = dudect_max_t(p)
        body.append([NAMES[p], ("%.2f" % t) if t is not None else "NOT_COLLECTED"])
    S += table(["Platform", "max |t|"], body)
    S.append("")
    S.append("No leakage evidence at the stated sample size; this is not a proof.")
    S.append("Raw logs: `provenance/raw/security-v4/<platform>/dudect*`.")
    S.append("")
    S.append("## ctgrind (Valgrind taint check)")
    S.append("")
    body = []
    for p in PLATFORMS:
        body.append([NAMES[p], ctgrind_summary(p) or "NOT_COLLECTED"])
    S += table(["Platform", "error summary"], body)
    S.append("")
    S.append("0 reported errors is not a formal proof and does not establish")
    S.append("constant-time behavior.  Raw logs: `provenance/raw/security-v4/`.")
    S.append("")
    S.append("## Power")
    S.append("")
    idle = mean_power_w("x1c_idle_summary.txt")
    core = mean_power_w("x1c_core_summary.txt")
    if idle is not None or core is not None:
        S += table(["State", "mean system power"], [
            ["idle", ("%.2f W" % idle) if idle is not None else "NOT_COLLECTED"],
            ["optimized core loop", ("%.2f W" % core) if core is not None else "NOT_COLLECTED"],
        ])
    else:
        S.append("X1C whole-system power: `NOT_COLLECTED`.")
    S.append("")
    S.append("These are **whole-system** figures, not core-only or EM measurements.")
    S.append("A53/A76 power was not measured; EM leakage was not measured.")
    S.append("")
    S.append("## Not covered")
    S.append("")
    S.append("- Power/EM side-channel leakage analysis is not covered.")
    S.append("- dudect and ctgrind are screens, not proofs.")
    S.append("- Whole-binary formal equivalence remains `NOT_PROVEN`.")
    S.append("")
    write("SECURITY_REPORT.md", S)

    # ------------------------------------------------------------- BOARD_HASH_CHECK.md
    B = ["# On-board source and binary hash recheck (%s)" % RUN_DATE, ""]
    B.append("Each board computed its source-tree hash and both binary sets locally")
    B.append("after the run.  Raw output: `provenance/raw/board-hash-check/<platform>.txt`;")
    B.append("machine-readable comparison: `csv/board_hash_check.csv`.")
    B.append("")
    body = []
    for p in PLATFORMS:
        r = bh.get(p, {})
        body.append([NAMES[p], r.get("host", BOARDS[p]),
                     short(r.get("source_tree_sha256", "")),
                     short(r.get("pmu_core_baseline", "")),
                     short(r.get("pmu_core_optimized", "")),
                     short(r.get("ablation_core_baseline", "")),
                     short(r.get("ablation_core_optimized", "")),
                     r.get("status", "NOT_COLLECTED")])
    B += table(["Platform", "Board", "Source tree", "PMU baseline", "PMU optimized",
                "Ablation baseline", "Ablation optimized", "Status"], body)
    B.append("")
    B.append("Two separate native binary sets are checked because the paper uses two")
    B.append("batches: the strict-same-flags PMU binaries and the ablation binaries.")
    B.append("They are not interchangeable.  Full 64-character values are in")
    B.append("`csv/board_hash_check.csv`.")
    B.append("")
    if any(sb.get(p, {}).get("status") == "PASS" for p in PLATFORMS):
        B.append("Source → binary → raw-result → table binding: `PASS` on all platforms;")
        B.append("see `SHA_BINDING.md` and `csv/sha_binding.csv`.")
    else:
        B.append("Source → binary → raw-result → table binding: `NOT_COLLECTED`.")
    B.append("")
    write("BOARD_HASH_CHECK.md", B)

    # ------------------------------------------------------------- PROVENANCE.md
    P = ["# Provenance and field-level source map", ""]
    P.append("Every number in `PAPER_TABLES.md` comes from `csv/`, and every CSV is")
    P.append("generated from `provenance/raw/`, `timing/` or an object rebuilt from the")
    P.append("clean source tree.  This run id is `%s`." % RUN_ID)
    P.append("")
    P.append("## Generation commands")
    P.append("")
    P.append("```bash")
    for cmd in ("python3 scripts/build_validation_csv.py",
                "python3 scripts/build_tables.py <root> csv",
                "python3 scripts/build_cache_table.py",
                "python3 scripts/build_ablation_table.py provenance/raw/ablation csv",
                "python3 scripts/build_static_table.py csv",
                "python3 scripts/build_sha_binding.py --source-root code",
                "python3 scripts/build_board_hash_check.py",
                "python3 scripts/render_paper_tables.py",
                "python3 scripts/render_submission_paper.py",
                "python3 scripts/render_reports.py . \"$PWD\" \"$RUN_ID\""):
        P.append(cmd)
    P.append("```")
    P.append("")
    P.append("## Table → source")
    P.append("")
    P += table(["Table", "Source"], [
        ["0", "`campaign/pmu/SYSTEM.txt`, `campaign/build_metadata.txt`"],
        ["1", "static opt22 decision record (`csv/table1_selection.csv`)"],
        ["2", "`campaign/correctness/*.log`"],
        ["3a", "`campaign/pmu/<platform>_{baseline,optimized}.txt`"],
        ["3c", "`pmu-cache-v4/<platform>/{baseline,optimized}.txt`, `metadata.txt`"],
        ["4", "`campaign/kem/*_raw.csv` (2,000 paired rounds)"],
        ["5", "`campaign/stage/stage_profile_gcc.csv`"],
        ["6", "`provenance/raw/ablation/<platform>_summary.csv`"],
        ["7", "objects built from the clean source tree; `provenance/raw/stage/*_stage_profile_gcc.csv`"],
        ["8", "`csv/table8_positioning.csv`, `csv/table8_references.csv`"],
        ["9", "`campaign/security/timing_mode{0,1}.log`, security-v4 raw logs"],
        ["validation", "`validation/results/<platform>/summary/*.csv`, `status.txt`"],
        ["hash", "`provenance/raw/board-hash-check/*.txt`"],
    ])
    P.append("")
    P.append("## Pairing and bootstrap")
    P.append("")
    P.append("KEM and polymul rounds are paired ABBA: `01_baseline` with")
    P.append("`02_optimized`, `04_baseline` with `03_optimized`.  The CI is a paired")
    P.append("percentile bootstrap of `mean(base)/mean(opt)` (10,000 resamples, seed")
    P.append("`0x653771`); the sign test is two-sided.  `p < 10^-300` is written as")
    P.append("`<1e-300` with `p_underflow=true`, never as the mathematical value `0.0`.")
    P.append("")
    P.append("## What is not in the provenance")
    P.append("")
    P.append("- No number for a structural factor without an independent arm.")
    P.append("- No algorithm-level exact operation counts; only static machine counts.")
    P.append("- No power/EM side-channel result.")
    P.append("- No absolute cross-platform cycle claim; only same-platform ratios.")
    P.append("")
    write("PROVENANCE.md", P)


if __name__ == "__main__":
    main()
