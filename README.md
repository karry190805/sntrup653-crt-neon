# sntrup653-crt-neon

面向 sntrup653 的单项式因子 CRT 多项式乘法及 AArch64 NEON 实现。本仓库是论文的配套代码，包含三个平台（Cortex-A53 / A76 / X1C）的自包含交付包，以及核心实现的**分块索引**。

- 核心源文件：`packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c`（1434 行）
- 三个平台包中的该文件逐字节相同，哈希见各包的 `SOURCE_MANIFEST.sha256`
- 分块与论文小节的对应关系见 [CODE_MAP.md](CODE_MAP.md)

## 目录

```text
packages/
  sntrup653-crt-cortex-a53/    Cortex-A53 交付包（KICKPI K2B，Allwinner H618）
  sntrup653-crt-cortex-a76/    Cortex-A76 交付包（Radxa ROCK 5B+，RK3588）
  sntrup653-crt-cortex-x1c/    Cortex-X1C 交付包（Radxa Dragon Q8B，Snapdragon 8cx Gen 3）
tools/
  blocks.json                  分块定义（唯一数据源）
  make_links.py                生成 CODE_MAP.md 与论文 LaTeX 片段
paper/
  code_map.tex                 代码分块索引表（可直接 \input）
  code_availability.tex        代码可用性段落
CODE_MAP.md                    分块索引（Markdown）
```

每个包内部结构一致：

```text
optimized/     本文实现（NEON 版多项式乘法 + KEM 胶合层）
baseline/      PQClean Round 3 的 sntrup653 纯 C 实现（对照基线）
shared/        计时、正确性、端到端测试与自检程序
*.sh, *.py     构建、运行、检查与汇总脚本
VERIFICATION.md / RANGE_AND_SECURITY.md / LOCAL_VALIDATION.md   验证与适用边界说明
```

## 快速开始

在 ARM64 Linux 上（以 A76 为例，其余两个平台把目录名换掉即可）：

```bash
cd packages/sntrup653-crt-cortex-a76
sudo bash ./run_on_target.sh
```

依赖（未安装过时执行一次）：

```bash
sudo apt-get update && sudo apt-get install -y --no-install-recommends \
    clang gcc binutils util-linux coreutils python3 unzip
```

完整流程结束时应出现

```text
compiler=gcc RESULT=PASS combos=5 unique_hashes=1
compiler=clang RESULT=PASS combos=5 unique_hashes=1
multicompiler=PASS compilers=2 combos=10 unique_hashes=1
target_campaign=PASS
```

## 结果

多项式乘法单次调用（论文表 1）：

| 平台 | 基线/ms | 优化/ms | 加速比 |
|---|---|---|---|
| Cortex-A53 | 0.33915 | 0.09225 | 3.68× |
| Cortex-A76 | 0.08056 | 0.03070 | 2.62× |
| Cortex-X1C | 0.05099 | 0.01494 | 3.41× |

KEM 端到端（一次密钥生成 + 封装 + 解封装，论文表 2）：1.06×（A53）、1.04×（A76）、1.07×（X1C）。各项的 95% 置信区间与逐阶段数据见论文。

## 代码分块

`CODE_MAP.md` 把核心源文件切成 19 块（B01–B19），每块给出论文小节、主要函数/宏、行号，以及 GitHub 行号永久链接，例如十六维加权卷积主核：

```text
https://github.com/<OWNER>/<REPO>/blob/<COMMIT>/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L984-L1025
```

链接固定到某个提交，代码以后再变也不会失效。更新方法：

```bash
python tools/make_links.py --owner <账号> --repo <仓库名> --ref <提交>
```

脚本会重写 `CODE_MAP.md`、`paper/code_map.tex` 与 `paper/code_availability.tex`；分块边界只在 `tools/blocks.json` 里维护。

## 验证状态与边界

- 三个平台均在目标板卡上完成编译、正确性与计时，结果为 `target_campaign=PASS`。
- 正确性：`ORACLE_MODE=all ORACLE_CASES=20000` 的随机用例、`CORE_EQUIV_TESTS=100000` 的核心等价测试、`KEM_TRANSCRIPT_CASES=1000` 的端到端会话，以及 GCC/Clang 在 `-O0 -O1 -O2 -O3 -Os` 下的一致哈希。
- 本地（WSL + QEMU）只做功能验证，**没有**做原生计时与原生时序筛查；这两项以板卡结果为准。详见各包 `LOCAL_VALIDATION.md`。
- 本项目**不做形式化验证**（见各包 `VERIFICATION.md` 第 9 节），常数时间性质也未作证明，只给出范围与安全边界说明（`RANGE_AND_SECURITY.md`）。

## 许可

各包的 `optimized/LICENSE` 与 `baseline/LICENSE` 均为 Public Domain（基线代码来自 PQClean）。
