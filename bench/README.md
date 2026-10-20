# bench

计时入口。计时程序本身放在各交付包里（`packages/<平台>/shared/bench_core_mult.c`、
`shared/bench_kem.c`），这里只是统一入口，避免同一份实现出现两套代码。

```text
bench/
  run_polymul.sh    多项式乘法单次调用 A/B      -> 论文表 1
  run_kem.sh        KEM 分阶段与端到端 A/B      -> 论文表 2
  run_sessions.sh   五组 ABBA/BAAB 会话级复测   -> 表 1、表 2 的置信区间
  frequency.sh      锁定/还原 CPU 频率（set、restore 需要 sudo）
  ablation.sh       单因素消融
```

`PLATFORM` 取 `a53`、`a76`、`x1c`，默认 `a53`；`CPU_PIN` 指定绑定到哪个逻辑核
（各包的 `platform.conf` 给出该平台建议的核号）。

```bash
PLATFORM=a76 CPU_PIN=6 sudo -E bash bench/frequency.sh set   # 先锁频
PLATFORM=a76 CPU_PIN=6 bash bench/run_polymul.sh             # 表 1
PLATFORM=a76 CPU_PIN=6 bash bench/run_kem.sh                 # 表 2
PLATFORM=a76 CPU_PIN=6 sudo -E bash bench/frequency.sh restore
```

## 测量协议

固定值写在 `packages/<平台>/protocol.conf` 里，三个平台一致：

| 项 | 值 |
|---|---|
| 时钟 | `CLOCK_MONOTONIC_RAW`（不可用时退回 `CLOCK_MONOTONIC`） |
| 多项式乘法 | 预热 200 次，每轮 2000 次调用，100 轮，`ABBA` 配对顺序 |
| KEM | 每轮预热 100 次、1000 次调用，5 组会话 |
| 随机种子 | `BASE_SEED=20260901`（基线与优化版用同一串输入） |
| 频率 | governor 设为 `performance`，可选锁到 `cpuinfo_max_freq` |
| 亲和性 | `taskset -c $CPU_PIN` 绑定单核 |

每轮取中位数，跨轮给均值与 95% 置信区间；论文里的数字都是**单核、锁频**下的挂钟时间。

**注意**：WSL + QEMU 只用于功能验证，计时在目标板卡上完成（见 `LOCAL_VALIDATION.md`）。

## 输出

结果落在包内的 `results/` 下，每次运行一个目录：

```text
results/
  polymul_<tag>_<stamp>/    summary.csv  summary.txt  <n>_<impl>_raw.csv  build_metadata.txt
  kem_<tag>_<stamp>/        同上
  session_01 .. session_05/ 每个会话一组的 summary.csv、summary.txt
  summary_sessions.csv|txt  五组会话的聚合
  ablation_<stamp>/         summary.csv、summary.txt 与各变体的原始 CSV
```

`packages/<平台>/summarize.py` 把 `results/` 汇总成论文用的表格。

## 结果

多项式乘法单次调用（论文表 1，`-O3`，编译器版本见 [`results/README.md`](../results/README.md)）：

| 平台 | 基线/ms | 优化/ms | 加速比（倍） | 95% 置信区间 |
|---|---|---|---|---|
| Cortex-A53 | 0.33915 | 0.09225 | 3.67631 | [3.67499, 3.67772] |
| Cortex-A76 | 0.08056 | 0.03070 | 2.62425 | [2.62158, 2.62687] |
| Cortex-X1C | 0.05099 | 0.01494 | 3.41187 | [3.40827, 3.41541] |

KEM 各阶段（论文表 2）：

| 平台 | 阶段 | 基线/ms | 优化/ms | 加速比（倍） |
|---|---|---|---|---|
| Cortex-A53 | 密钥生成 | 15.43536 | 15.19371 | 1.01590 |
| | 封装 | 0.53999 | 0.29371 | 1.83851 |
| | 解封装 | 1.07339 | 0.58138 | 1.84630 |
| | 端到端 | 17.04914 | 16.06922 | 1.06098 |
| Cortex-A76 | 密钥生成 | 4.84611 | 4.79295 | 1.01109 |
| | 封装 | 0.13755 | 0.08491 | 1.61990 |
| | 解封装 | 0.24261 | 0.13368 | 1.81491 |
| | 端到端 | 5.22646 | 5.01173 | 1.04285 |
| Cortex-X1C | 密钥生成 | 2.12252 | 2.08754 | 1.01676 |
| | 封装 | 0.07959 | 0.04347 | 1.83107 |
| | 解封装 | 0.13969 | 0.06206 | 2.25075 |
| | 端到端 | 2.34198 | 2.19325 | 1.06781 |
