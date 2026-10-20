# results

三个平台的原始测量输出，数字没有改过。论文表 1、表 2 就是从这里取的。

## 来源

| 平台 | 板卡 | 运行目录 | 日期 | 编译器 |
|---|---|---|---|---|
| `a53` | KICKPI K2B（Allwinner H618，Cortex-A53） | `run_20261017T055830Z_iEPYsZ` | 2026-10-17 | GCC 14.2.0 |
| `a76` | Radxa ROCK 5B+（RK3588，Cortex-A76） | `run_20261017T055653Z_mYX8Vg` | 2026-10-17 | GCC 12.2.0 |
| `x1c` | Radxa Dragon Q8B（Snapdragon 8cx Gen 3，Cortex-X1C） | `run_20261017T055418Z_S2oKKj` | 2026-10-17 | GCC 15.2.0 |

编译参数、配置 ID、core 的 SHA-256 见各平台的 `build_metadata.txt`。

## 文件

| 路径 | 内容 | 对应 |
|---|---|---|
| `<平台>/polymul_ab/` | 多项式乘法 A/B：`summary.csv`、`summary.txt` 与 4 份逐轮原始数据 | 表 1 |
| `<平台>/summary_sessions.csv/txt` | 五组 ABBA/BAAB 会话复测的聚合 | 表 2 |
| `<平台>/session_0X/` | 每个会话四段计时的摘要 | 表 2 的置信区间 |
| `<平台>/kem_ab/` | KEM 单次运行的分阶段 A/B | 对照 |
| `<平台>/ablation/` | 单因素消融 | §4.3 |
| `<平台>/FINAL_STATUS.txt` | 该次会话的判定 | — |

表 2 用的是会话聚合值（`summary_sessions.txt` 里各 metric 的 `baseline_mean`、`optimized_mean`、`session_mean_speedup`），不是单次 `kem_ab` 的均值；两者在小数点后第三位就有差别，论文用的是前者。

## 复现

在对应平台上执行：

```bash
cd packages/sntrup653-crt-cortex-a53
CPU_PIN=<核号> bash ./run_polymul.sh      # 表 1
CPU_PIN=<核号> bash ./run_sessions.sh     # 表 2
```

结果写回包内 `results/`。轮数、预热次数、配对顺序与随机种子见 `packages/<平台>/protocol.conf`。

时间数字与机器状态相关，换一块板子不会得到逐位相同的结果；可复现的是协议、代码和数量级，以及同一块板卡上优化版与基线的比值。
