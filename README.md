# sntrup653-crt-neon

面向 sntrup653 的单项式因子 CRT 多项式乘法及 AArch64 NEON 实现。本仓库是论文的配套代码，包含三个平台（Cortex-A53 / A76 / X1C）的自包含交付包，以及按算法层次切开的代码分块视图。

## 两种视角，同一份代码

| 用途 | 位置 |
|---|---|
| 编译、复现表 1 与表 2 | `packages/sntrup653-crt-cortex-{a53,a76,x1c}/optimized/crypto_core_multsntrup653.c`（1434 行，三个包逐字节相同） |
| 阅读、在论文里引用 | `src/01_params.c` … `src/19_entry.c`（19 个文件，对应论文 §3 各小节） |

`src/` 是上面那份源文件的原样切分，只给每块加一段注释头，不改一个字节；`tools/join.py` 去掉注释头就能拼回原文件并比对 SHA-256。之所以不做成每块一个编译单元，是因为测评二进制必须和交付包完全一致，拆开会改变内联与代码布局，表 1、表 2 的数字就不再对应仓库里的代码。

分块边界在 `tools/blocks.json`，完整索引见 `CODE_MAP.md`。链接固定到某个提交、指向整文件，例如：

```text
https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/14_w16_kernel.c
```

## 目录

```text
packages/
  sntrup653-crt-cortex-a53/    Cortex-A53 交付包（KICKPI K2B，Allwinner H618）
  sntrup653-crt-cortex-a76/    Cortex-A76 交付包（Radxa ROCK 5B+，RK3588）
  sntrup653-crt-cortex-x1c/    Cortex-X1C 交付包（Radxa Dragon Q8B，Snapdragon 8cx Gen 3）
src/                           分块视图 B01–B19（由 split.py 生成，勿手改）
tools/
  blocks.json                  分块定义
  split.py                     切分 + 自校验
  join.py                      还原校验
  make_links.py                生成 CODE_MAP.md
bench/                         三种平台的计时入口（测试程序在各包 shared/ 里）
scripts/
  test.sh                      仓库自检
  gen_testcases.py             从 Python oracle 生成 testcases/
  check_testcases.py           校验 testcases/ 与 expected_oracle_hashes.txt
  testcases/*.in, *.out        12 组固定用例
results/                       三个平台的测量输出（表 1、表 2 的来源）
CODE_MAP.md                    分块索引
```

每个交付包内部结构一致：

```text
optimized/     本文实现（NEON 多项式乘法与 KEM 接口层）
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

逐轮原始数据、五组会话复测和消融结果在 `results/`，文件与表格的对应关系见 `results/README.md`。

## 计时

```bash
PLATFORM=a76 CPU_PIN=6 sudo -E bash bench/frequency.sh set   # 锁定频率
PLATFORM=a76 CPU_PIN=6 bash bench/run_polymul.sh             # 表 1
PLATFORM=a76 CPU_PIN=6 bash bench/run_kem.sh                 # 表 2
bash bench/frequency.sh restore
```

协议与逐平台数字见 `bench/README.md`。

## 仓库自检

```bash
bash scripts/test.sh
```

只需要 python3，校验分块能否还原、固定测例与 1971 个用例的哈希序列是否与包内 Python oracle 一致。细节见 `scripts/README.md`。

## 验证状态与边界

- 三个平台均在目标板卡上完成编译、正确性与计时，结果为 `target_campaign=PASS`。
- 正确性：`ORACLE_MODE=all ORACLE_CASES=20000` 的随机用例、`CORE_EQUIV_TESTS=100000` 的核心等价测试、`KEM_TRANSCRIPT_CASES=1000` 的端到端会话，以及 GCC/Clang 在 `-O0 -O1 -O2 -O3 -Os` 下的一致哈希。
- 本地（WSL + QEMU）只做功能验证，**没有**做原生计时与原生时序筛查；这两项以板卡结果为准。详见各包 `LOCAL_VALIDATION.md`。
- 本项目**不做形式化验证**（见各包 `VERIFICATION.md` 第 9 节），常数时间性质也未作证明，只给出范围与安全边界说明（`RANGE_AND_SECURITY.md`）。

## 许可

各包的 `optimized/LICENSE` 与 `baseline/LICENSE` 均为 Public Domain（基线代码来自 PQClean）。
