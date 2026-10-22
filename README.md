# sntrup653-crt-neon

本仓库是论文《面向 sntrup653 的单项式因子 CRT 多项式乘法及 AArch64 NEON 实现》（尚未发表）的配套代码，包含三个平台（Cortex-A53 / A76 / X1C）的自包含交付包，以及按算法层次切开的代码分块视图。

## 两种视角，同一份代码

| 用途 | 位置 |
|---|---|
| 编译、复现表 1 与表 2 | `packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c`（1433 行；A76、X1C 两个包内为同名文件，三份逐字节一致） |
| 阅读、在论文里引用 | `src/01_params.c` … `src/19_entry.c`（19 个文件，阅读顺序见 [`CODE_MAP.md`](CODE_MAP.md)） |

`src/` 是上面那份源文件的逐字节切分，只在每块前加一段注释头；`tools/join.py` 去掉注释头拼回原文件并比对 SHA-256。分块不做成独立编译单元，拆分会改变内联与代码布局，表 1、表 2 就不再对应仓库里的代码。

引用时用发布标签下的整文件链接，行号不会漂移，例如 [`src/14_w16_kernel.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/14_w16_kernel.c)。分块边界在 `tools/blocks.json`；每块的输入输出、是否在生产路径上，见 [`CODE_MAP.md`](CODE_MAP.md)。

### 论文表格从哪里来

| 论文 | 入口 | 证据文件 |
|---|---|---|
| 表 1 多项式乘法 | `bench/run_polymul.sh` | `results/<平台>/polymul_ab/summary.txt` |
| 表 2 KEM 分阶段与端到端 | `bench/run_sessions.sh` | `results/<平台>/summary_sessions.txt` |

表 2 以五个 ABBA/BAAB 会话为复测单位，取 `summary_sessions.csv` 的会话聚合值，不是单次运行的均值。

`bench/run_kem.sh` 是单次 KEM 分阶段诊断，用来快速看各阶段耗时，不生成表 2。

`run_sessions.sh` 的每个会话里也会跑多项式乘法（`session_0X/polymul_first|second`），用于同一会话内的漂移对照；表 1 的数字与置信区间仍取 `run_polymul.sh` 的 `polymul_ab/summary.txt`。

## 目录

```text
packages/    三个平台的交付包（Cortex-A53 / A76 / X1C），各自自包含
src/         分块视图 B01–B19（由 split.py 生成，勿手改）
tools/       分块定义与切分、还原、索引脚本
bench/       三个平台的计时脚本（测试程序在各包 shared/ 里）
scripts/     自检脚本与固定测例
results/     已提交的测量输出（表 1、表 2 与消融的来源）
CODE_MAP.md  分块索引、阅读顺序与数据流
```

三个平台分别是 KICKPI K2B（Allwinner H618）、Radxa ROCK 5B+（RK3588）和 Radxa Dragon Q8B（Snapdragon 8cx Gen 3）。

每个交付包内部结构一致：

```text
optimized/     本文实现（NEON 多项式乘法与 KEM 接口层）
baseline/      PQClean Round 3 的 sntrup653 纯 C 实现（对照基线）
shared/        计时、正确性、端到端测试与自检程序
*.sh, *.py     构建、运行、检查与汇总脚本
VERIFICATION.md / RANGE_AND_SECURITY.md / LOCAL_VALIDATION.md   验证与适用边界说明
```

根目录 `results/` 保存投稿时提交的测量证据。运行脚本时结果写进 `packages/<平台>/results/`，那是新生成的目录，会随运行时间与主机状态变化。两者不是同一个目录。

## 三种跑法

### A. 没有 ARM 板卡：校验代码与测例

```bash
bash scripts/test.sh
```

只需要 python3。它校验分块能否拼回原文件、12 组固定测例，以及 1971 个用例的 oracle 哈希序列。它不编译 AArch64 代码，也不产生计时数字。

### B. 有目标板卡：只跑正确性

```bash
cd packages/sntrup653-crt-cortex-a76
CPU_PIN=6 bash ./run_correctness.sh
```

覆盖内部构造自检、基线与优化核心逐字节对比、一阶原理 oracle、KEM 转录对比、并发首调用与栈预算。其余两个平台换目录名即可。

### C. 有目标板卡：复现论文性能表

```bash
cd packages/sntrup653-crt-cortex-a76
sudo CPU_PIN=6 bash ./set_performance.sh        # 锁定频率，状态写入 results/cpu6_performance_state.txt
CPU_PIN=6 bash ./run_sessions.sh                # 五组会话，得到表 2
sudo bash ./restore_performance.sh results/cpu6_performance_state.txt
```

需要 root：脚本要锁定频率并绑定测量核。依赖（未安装过时执行一次）：

```bash
sudo apt-get update && sudo apt-get install -y --no-install-recommends \
    clang gcc binutils util-linux coreutils python3 unzip
```

完整流程（编译、正确性、计时与打包）仍是一条命令：

```bash
sudo QUICK=1 bash ./run_on_target.sh --no-pack   # 短流程，几分钟，先确认环境
sudo bash ./run_on_target.sh                      # 完整流程
```

完整流程结束时应出现

```text
compiler=gcc RESULT=PASS combos=5 unique_hashes=1
compiler=clang RESULT=PASS combos=5 unique_hashes=1
multicompiler=PASS compilers=2 combos=10 unique_hashes=1
target_campaign=PASS
```

包内 `SOURCE_MANIFEST.sha256` 覆盖全部文件，进入包目录执行 `sha256sum -c SOURCE_MANIFEST.sha256` 可确认交付内容未被改动。

## 结果

多项式乘法单次调用（论文表 1，单核、锁频）：

| 平台 | 编译器 | 基线/ms | 优化/ms | 加速比（倍） |
|---|---|---|---|---|
| Cortex-A53 | GCC 14.2.0 | 0.33915 | 0.09225 | 3.68 |
| Cortex-A76 | GCC 12.2.0 | 0.08056 | 0.03070 | 2.62 |
| Cortex-X1C | GCC 15.2.0 | 0.05099 | 0.01494 | 3.41 |

KEM 端到端（一次密钥生成 + 封装 + 解封装，论文表 2，单核、锁频）：1.06 倍（A53）、1.04 倍（A76）、1.07 倍（X1C）。各项的 95% 置信区间与逐阶段数据见论文。

逐轮原始数据、五组会话复测和消融结果在 `results/`，文件与表格的对应关系见 [`results/README.md`](results/README.md)。

## 计时

```bash
PLATFORM=a76 CPU_PIN=6 sudo -E bash bench/frequency.sh set   # 锁定频率
PLATFORM=a76 CPU_PIN=6 bash bench/run_polymul.sh             # 表 1
PLATFORM=a76 CPU_PIN=6 bash bench/run_sessions.sh            # 表 2
PLATFORM=a76 CPU_PIN=6 bash bench/run_kem.sh                 # 可选，单次诊断
PLATFORM=a76 CPU_PIN=6 sudo -E bash bench/frequency.sh restore
```

协议与逐平台数字见 [`bench/README.md`](bench/README.md)。

## 验证范围

三个平台都在目标板卡上完成编译、正确性、编译器一致性与计时，结果为 `target_campaign=PASS`。

- 正确性：随机用例 `ORACLE_MODE=all ORACLE_CASES=20000`，核心等价测试 `CORE_EQUIV_TESTS=100000`，端到端会话 `KEM_TRANSCRIPT_CASES=1000`。
- 编译器一致性：GCC 与 Clang 在 `-O0 -O1 -O2 -O3 -Os` 下给出同一份哈希。
- WSL + QEMU 用于功能验证，计时数据取自目标板卡，细节见各包 `LOCAL_VALIDATION.md`。

这里的证据是测试与测量。形式化验证和常数时间证明不在本工作范围内，算法的适用范围与安全边界见 `RANGE_AND_SECURITY.md`。

## 许可

各包的 `optimized/LICENSE` 与 `baseline/LICENSE` 均为 Public Domain（基线代码来自 PQClean）。
