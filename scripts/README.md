# scripts

仓库级的自检脚本，只需要 python3，不需要 ARM 板卡。板卡上的编译、正确性与计时仍由各交付包自己的脚本完成（见 `../bench/`）。

## 用法

```bash
bash scripts/test.sh
```

它做两件事：

1. `tools/join.py` —— 把 `src/` 下的 19 个分块文件去掉注释头后拼接，与 `packages/.../crypto_core_multsntrup653.c` 比对 SHA-256；
2. `scripts/check_testcases.py` —— 用交付包里的纯 Python oracle 重算 `testcases/` 的每个用例，并重放 1971 个用例的哈希序列。

## testcases

`testcases/fixed_00.in … fixed_11.out` 是 12 组固定用例，与 `shared/test_endtoend_oracle.c` 的前 12 个 fixed case 完全相同：

```text
# fixed_03: f, g in Z_4621[x]/(x^653 - x - 1), centred
f 1 1 1 ...
g 1 1 1 ...
```

`.out` 是 `f*g` 在 `x^653 = x + 1` 下折回后的 653 个中心化系数。用例覆盖了两个零输入、全 1、全 ±q/2、单个 `x^652`、以及会触发高位折回的 `x^652 * x^580` 等边界情形。

重新生成：

```bash
python3 scripts/gen_testcases.py
```

## 板卡侧的分工

| 检查 | 命令（包内） |
|---|---|
| C 核心 == Python oracle（1971 用例，`check_oracle_hash.py` 强制哈希）与 == 基线（100000 用例） | `bash run_correctness.sh` |
| GCC/Clang × 5 个优化等级一致哈希 | `bash run_on_target.sh` |
| 计时 | 见 `../bench/` |
