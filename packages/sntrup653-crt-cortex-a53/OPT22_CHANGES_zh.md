# opt22 选型说明 — Cortex-A53

## 结论：采用 pointwise 2 次展开 + 显式尾块，并给 `gt_layer2_*` 加 unroll 2

实机 ABBA 配对（strict same-flags，3 平台同一份源码）结果：

| 候选 | 配对加速比 B/A | 胜场 | CI95 |
|---|---:|---:|---|
| pw1 + l2u2 | 1.066926 | 6/6 | [1.0559, 1.0780] |
| pw2 | 1.064157 | 6/6 | [1.0495, 1.0789] |
| **pw2 + l2u2** | **1.072917** | **6/6** | **[1.0587, 1.0872]** |
| pw3 + l2u2 | 1.043364 | 6/6 | [1.0250, 1.0618] |

所有候选的 `core_correctness_hash` 都是 `0xfe69baced6245e47`。

## 源码改动（仅两处，数学不变）

1. pointwise 循环从 4 次一组改为 2 次一组，余下 1 个 block 由生成器显式
   展开（不是“只特判最后一块”的旧写法）：

   ```c
   /* opt22-pointwise-batch */
   for (m = 0; m + 1 < BLOCKS; m += 2) {
       weighted_mul16_scaled16(A16[m + 0], ..., m + 0, 1);
       weighted_mul16_scaled16(A16[m + 1], ..., m + 1, 1);
   }
   weighted_mul16_scaled16(A16[m + 0], ..., m + 0, 1);   /* m == 76 */
   ```

   生成器会重新解析生成的 C 文本，证明 0..76 每个 block 恰好访问一次。
2. `gt_layer2_fwd16` / `gt_layer2_inv16` 的 `cbase` 循环各加一行
   `#pragma GCC unroll 2`。

## 本目录内容

- `optimized/`：opt22 核心。
- `baseline/`：PQClean clean 参考实现。
- `results/`：运行 `run_opt22_campaign.sh` 后生成的输出目录；
  本包不预置任何测量结果或构建产物。

## 数学不变量

`p=653, q=4621, w=288`、77×16、Good–Thomas 7/11、TMVP、根/表/置换/权重、
canonical 输出均未改动；`math_fingerprint=0xa866aa344f934dca`。
