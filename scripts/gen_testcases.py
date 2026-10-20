# -*- coding: utf-8 -*-
"""从交付包的纯 Python oracle 生成固定测例（.in/.out）。

测例序列与 shared/test_endtoend_oracle.c 的前 12 个 fixed case 相同：
    f, g  in Z_q[x]/(x^653 - x - 1), q = 4621, 中心代表元
    out   = f*g 的 653 个中心化系数

用法：
    python tools/../scripts/gen_testcases.py        # 重新生成 scripts/testcases/
    python scripts/check_testcases.py               # 校验（任何机器都能跑）
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ORACLE = os.path.join(ROOT, "packages", "sntrup653-crt-cortex-a53", "oracle_reference.py")
OUTDIR = os.path.join(HERE, "testcases")


def load_oracle():
    spec = importlib.util.spec_from_file_location("oracle_reference", ORACLE)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    orc = load_oracle()
    os.makedirs(OUTDIR, exist_ok=True)
    written = []
    for cid in range(orc.FIXED_CASES):
        f, g = orc.fixed_case(cid)
        out = orc.oracle_product(f, g)
        base = "fixed_%02d" % cid
        with open(os.path.join(OUTDIR, base + ".in"), "w", encoding="ascii", newline="\n") as fh:
            fh.write("# %s: f, g in Z_4621[x]/(x^653 - x - 1), centred\n" % base)
            fh.write("f " + " ".join(str(v) for v in f) + "\n")
            fh.write("g " + " ".join(str(v) for v in g) + "\n")
        with open(os.path.join(OUTDIR, base + ".out"), "w", encoding="ascii", newline="\n") as fh:
            fh.write("# %s: f*g, 653 centred coefficients\n" % base)
            fh.write(" ".join(str(v) for v in out) + "\n")
        written.append(base)
    print("wrote %d case pairs to %s" % (len(written), os.path.relpath(OUTDIR, ROOT)))
    print("       " + " ".join(written))


if __name__ == "__main__":
    sys.exit(main())
