# opt22 选型说明 — Cortex-X1C

## 结论：保留当前设备版（2× + l2u2 + no-lrs），不采用新的 pointwise 分批

在 X1C 本机用同一份设备版源码生成 1/2/3/4/6/8/10/13/19 次展开候选，ABBA
配对 5 轮（10 个比值），全部先通过 `core_correctness_hash` 校验：

| 候选 | B/A | 候选 | B/A |
|---|---:|---|---:|
| pw1 | 0.924900 | pw6 | 0.995189 |
| pw2 | 0.999976 | pw8 | 0.991636 |
| pw3 | 0.992201 | pw10 | 0.986189 |
| pw4 | 0.993606 | pw13 | 0.958008 |
| | | pw19 | 0.940972 |

最好的 `pw2` 也只有 0.999976，不能证明有收益，因此保留现有设备版。

## 本目录内容

- `optimized/`：当前 X1C 设备版核心（含 stage 诊断打点，生产构建为 no-op）。
- `baseline/`：PQClean clean 参考实现。
- `results/`：运行 `run_opt22_campaign.sh` 后生成的输出目录；
  本包不预置任何测量结果或构建产物。

## 数学不变量

`p=653, q=4621, w=288`、77×16、Good–Thomas 7/11、TMVP、根/表/置换/权重、
canonical 输出均未改动；`math_fingerprint=0xa866aa344f934dca`。
