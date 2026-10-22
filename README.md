# sntrup653-crt-neon

sntrup653 的多项式乘法核心，AArch64 NEON 实现，配套论文《面向 sntrup653 的单项式因子 CRT 多项式乘法及 AArch64 NEON 实现》（尚未发表）。三个平台各一个自包含交付包。

## 编译

```bash
cd packages/sntrup653-crt-cortex-a76
bash ./build.sh
```

三个包里的核心源文件逐字节相同，`optimized/crypto_core_multsntrup653.c`，1433 行。

## 计时

```bash
cd packages/sntrup653-crt-cortex-a76
sudo CPU_PIN=6 LOCK_MAX_FREQ=1 bash ./set_performance.sh
CPU_PIN=6 bash ./run_polymul.sh     # 表 1，多项式乘法 A/B
CPU_PIN=6 bash ./run_sessions.sh    # 表 2，五组 ABBA/BAAB 会话
sudo bash ./restore_performance.sh results/cpu6_performance_state.txt
```

`run_kem.sh` 只做单次分阶段诊断，不生成表 2。协议与逐平台数字见 `bench/README.md`。

## 正确性

```bash
cd packages/sntrup653-crt-cortex-a76
CPU_PIN=6 bash ./run_correctness.sh
```

覆盖内部构造自检、基线与优化核心逐字节对比、一阶原理 oracle、KEM 转录对比、并发首调用与栈预算。

## 结果

多项式乘法，单核、锁频：

| 平台 | 编译器 | 基线/ms | 优化/ms | 加速比（倍） |
|---|---|---|---|---|
| Cortex-A53 | GCC 14.2.0 | 0.33915 | 0.09225 | 3.68 |
| Cortex-A76 | GCC 12.2.0 | 0.08056 | 0.03070 | 2.62 |
| Cortex-X1C | GCC 15.2.0 | 0.05099 | 0.01494 | 3.41 |

KEM 端到端（密钥生成 + 封装 + 解封装）1.06 倍（A53）、1.04 倍（A76）、1.07 倍（X1C）。逐轮数据、五组会话与消融结果在 `results/`，论文数字与文件的对应见 `results/README.md`。

## 没有板卡

```bash
bash scripts/test.sh
```

校验分块能否拼回原文件、12 组固定测例、1971 个用例的 oracle 哈希序列，只需要 python3。它不编译 AArch64 代码，也不产生计时数字。

## 代码怎么读

`src/01_params.c` … `src/19_entry.c` 是核心源文件的逐字节切分，只在每块前加注释头，`tools/join.py` 去掉注释头即可拼回并比对 SHA-256。分块只用于阅读，不做成独立编译单元，拆分会改变内联与代码布局，表 1、表 2 就不再对应仓库里的代码。

`CODE_MAP.md` 给出推荐阅读顺序、每块的输入输出、生产/参考/测试三条路径。主链是 B19 → B06 → B07 → B12 → B14 → B16 → B17 → B19，B08 是标量参考实现，B18 只在测试宏打开时编译。

引用用标签下的整文件链接，行号不会漂移，例如 [`src/14_w16_kernel.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/14_w16_kernel.c)。分块边界在 `tools/blocks.json`。

## 目录

```text
packages/    三个平台的交付包（Cortex-A53 / A76 / X1C）
src/         分块视图 B01–B19，由 split.py 生成
tools/       分块定义、切分、还原与索引
bench/       三个平台的计时入口
scripts/     自检脚本与固定测例
results/     已提交的测量证据
CODE_MAP.md  分块索引、阅读顺序与数据流
```

包内结构一致，`optimized/` 是本文实现，`baseline/` 是 PQClean Round 3 纯 C 实现，`shared/` 是测试与计时程序。

根目录 `results/` 保存投稿时提交的测量证据。运行脚本时结果写进 `packages/<平台>/results/`，会随机器状态变化，两者不是同一个目录。

## 验证范围

三个平台都在目标板卡上完成编译、正确性、编译器一致性与计时，结果为 `target_campaign=PASS`。

- 正确性。随机用例 `ORACLE_MODE=all ORACLE_CASES=20000`，核心等价测试 `CORE_EQUIV_TESTS=100000`，端到端会话 `KEM_TRANSCRIPT_CASES=1000`。
- 编译器一致性。GCC 与 Clang 在 `-O0 -O1 -O2 -O3 -Os` 下给出同一份哈希。
- WSL + QEMU 用于功能验证，计时数据取自目标板卡，细节见各包 `LOCAL_VALIDATION.md`。

这里的证据是测试与测量。形式化验证和常数时间证明不在本工作范围内，适用范围与安全边界见 `RANGE_AND_SECURITY.md`。

## 许可

各包的 `optimized/LICENSE` 与 `baseline/LICENSE` 均为 Public Domain，基线代码来自 PQClean。
