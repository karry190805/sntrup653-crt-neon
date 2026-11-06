# opt22 选型说明 — Cortex-A76

## 结论：不改变 opt21-r2 源码

opt22 在 A76 上只测试了 pointwise 循环分批与 `gt_layer2_*` unroll 的组合，
全部使用修正后的“主循环 + 显式尾块”生成器，每个候选都先通过
`core_correctness_hash=0xfe69baced6245e47` 才进入计时。

| 候选 | 配对加速比 B/A | 胜场 |
|---|---:|---:|
| pw1 + l2u2 | 0.948078 | 0/10 |
| pw2 | 0.999128 | 0/10 |
| pw2 + l2u2 | 0.999933 | 4/10 |
| pw3 | 0.981080 | 0/10 |
| pw3 + l2u2 | 0.986268 | 0/10 |
| pw4 + l2u2 | 1.001287 | 10/10 |
| pw6 + l2u2 | 0.997208 | 0/10 |
| pw8 + l2u2 | 0.982400 | 0/10 |

唯一超过 1 的 `pw4 + l2u2` 只有 +0.13%，低于 R6 规定的 0.3% 门槛，因此
按规则保留 opt21-r2，不为“看起来有变化”而引入回归。

## 本目录内容

- `optimized/`：A76 的 opt21-r2 核心（含只在 `SNTRUP653_STAGE_PROFILE=1`
  时生效的诊断打点；生产构建展开为 `(void)0`）。
- `baseline/`：PQClean clean 参考实现。
- `results/`：运行 `run_opt22_campaign.sh` 后生成的输出目录；
  本包不预置任何测量结果或构建产物。

## 数学不变量

`p=653, q=4621, w=288`、77×16 block、Good–Thomas 7/11、TMVP、根/表/置换/
权重、canonical 输出均未改动；`math_fingerprint=0xa866aa344f934dca`。
