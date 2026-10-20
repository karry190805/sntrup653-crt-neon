# -*- coding: utf-8 -*-
"""校验 scripts/testcases/ 与 expected_oracle_hashes.txt。

两件事：
1. 重算每个 .in 的乘积，与 .out 比对；
2. 重放 12 个 fixed + 3*653 个 axis 用例，与 expected_oracle_hashes.txt 的
   cases 数和 FNV-1a-64 比对（C 侧 shared/test_endtoend_oracle.c 输出同一个哈希）。

只需要 python3，不需要 ARM 硬件。

用法：
    python3 scripts/check_testcases.py
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PKG = os.path.join(ROOT, "packages", "sntrup653-crt-cortex-a53")
ORACLE = os.path.join(PKG, "oracle_reference.py")
CASES = os.path.join(HERE, "testcases")


def load_oracle():
    spec = importlib.util.spec_from_file_location("oracle_reference", ORACLE)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def read_vector(path, tag):
    with open(path, encoding="ascii") as fh:
        for line in fh:
            line = line.strip()
            if line.startswith("#") or not line:
                continue
            parts = line.split()
            if parts[0] == tag:
                return [int(x) for x in parts[1:]]
    raise SystemExit("%s: missing '%s' line" % (path, tag))


def read_flat(path):
    with open(path, encoding="ascii") as fh:
        for line in fh:
            line = line.strip()
            if line.startswith("#") or not line:
                continue
            return [int(x) for x in line.split()]
    raise SystemExit("%s: empty" % path)


def main():
    orc = load_oracle()
    failures = 0
    names = sorted(n[:-3] for n in os.listdir(CASES) if n.endswith(".in"))
    for base in names:
        f = read_vector(os.path.join(CASES, base + ".in"), "f")
        g = read_vector(os.path.join(CASES, base + ".in"), "g")
        want = read_flat(os.path.join(CASES, base + ".out"))
        got = orc.oracle_product(f, g)
        ok = got == want
        failures += 0 if ok else 1
        print("%-12s %s" % (base, "PASS" if ok else "FAIL"))

    # 整个用例序列的哈希
    h, _ = orc.hash_stream()
    exp_cases = exp_hash = None
    with open(os.path.join(PKG, "expected_oracle_hashes.txt"), encoding="ascii") as fh:
        for line in fh:
            if line.startswith("cases="):
                exp_cases = int(line.split("=", 1)[1])
            elif line.startswith("fnv1a64="):
                exp_hash = int(line.split("=", 1)[1], 16)
    hash_ok = (h == exp_hash)
    failures += 0 if hash_ok else 1
    print("hash stream  cases=%d expected=%d" % (orc.HASH_CASES, exp_cases or -1))
    print("             %016x %s" % (h, "PASS" if hash_ok else "FAIL (expected %016x)" % exp_hash))

    print("result       %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
