# 核心代码分块索引

源文件 `packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c`（1433 行）按算法层次切分为 19 块，`src/` 下每块一个文件。切分只加注释头、不改代码，`tools/join.py` 去掉注释头即可拼回并比对 SHA-256；引用用整文件链接，行号不会漂移。三个平台包中的源文件逐字节相同。

## 推荐阅读顺序

从 B19 的对外接口开始读，生产路径只有一条：

B19 入口函数 → B06 表初始化 → B07 输入分块 → B12 77 点 NEON 变换（正向） → B14 十六维局部乘法 → B12 逆变换 → B16 低位 73 项分支 → B17 目标环折回 → B19 编码输出

B05、B09 是主路径反复调用的模 q 运算与 NEON 约减辅助函数，B15 提供系数索引与四通道点积。其余几块不在主路径上：

- B08 是标量 Good–Thomas 参考实现，只在非 NEON 构建和内部自检中调用。
- B10 是 7 点/11 点和差配对变换的基础函数，由 B12 调用。
- B11 是公开补零模式下的编译期特化，宏展开进 B12。
- B13 是十六维局部乘法的标量参考实现与 NEON 辅助函数，辅助函数由 B14 调用。
- B18 只在内部测试宏打开时使用，不属于生产路径。

## 三条路径

| 路径 | 涉及块 | 说明 |
|---|---|---|
| 生产 | B19 → B06/B07 → B12 → B14 → B16 → B17 | 交付包实际编译、计时的代码 |
| 参考 | B08、B13 的标量版本 | 与 NEON 版本对照，内部自检使用 |
| 测试 | B18、B07 的 `compare_blocks_detail` | 定义 `SNTRUP653_INTERNAL_TEST_HOOKS` 时才编译 |

## 分块表

| 块 | 论文小节 | 内容 | 输入 → 输出 | 路径 | 主要函数 / 宏 | 原文件行号 | 分块文件 |
|---|---|---|---|---|---|---|---|
| B01 | §3.1 §3.2 | 文件头、环参数与静态断言 | — | 基础 | `MAIN_N、LOW_N、BLOCKS、BS、_Static_assert` | L1–L67 | [`01_params.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/01_params.c) |
| B02 | §3.1 §3.3 | Good–Thomas 索引与公开补零模式 | (r, s) → 块下标 | 基础 | `SNTRUP_GT_IDX、SNTRUP_GT_ACT、SNTRUP_GT_ACT_COUNT` | L68–L104 | [`02_gt_index.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/02_gt_index.c) |
| B03 | §4.3 | 编译期开关（消融实验） | — | 基础 | `SNTRUP653_PUBLIC_ZS_MODE、SNTRUP653_PUBLIC_ZS_RUNTIME_MASK、SNTRUP653_LOW73_NEON、SNTRUP653_FINAL_FOLD_NEON` | L105–L148 | [`03_switches.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/03_switches.c) |
| B04 | §3.3 | 变换常量与配对 DFT 表 | — | 基础 | `LAMBDA77、POW7_*、POW11_*、PAIR7_*、PAIR11_*` | L149–L175 | [`04_tables.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/04_tables.c) |
| B05 | §2.2 §3.3 | Barrett 约减与模 q 运算 | 整数 → [0, q) | 基础 | `BARRETT_MU_Q64/Q32、mod_q_u64_barrett、mod_q_u32_barrett、add_q_raw、sub_q_raw、mul_q_raw、center_q_raw` | L176–L273 | [`05_barrett.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/05_barrett.c) |
| B06 | §3.3 | 表初始化（77 次单位根、7/11 点 DFT 常数） | void → 单位根与 DFT 表 | 生产 | `Fq_freeze、pow_q、inv_q、find_root77、init_pair_tables7/11、init_tables` | L274–L420 | [`06_init_tables.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/06_init_tables.c) |
| B07 | §3.2 §4.4 | 分块表示与内部测试接口 | f[653], g[653] → Blocks77 | 生产 | `Blocks77、MatrixGT、blockify_A、blockify_B、zero_blocks、compare_blocks_detail` | L421–L471 | [`07_blockify.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/07_blockify.c) |
| B08 | §3.3 | Good–Thomas 77 点变换（标量参考实现） | Blocks77 → Blocks77 | 参考 | `good_thomas_77_ntt_zs` | L472–L526 | [`08_gt77_ref.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/08_gt77_ref.c) |
| B09 | §2.5 §3.3 | NEON 约减与存储辅助函数 | int32x4 → 约减后存储 | 基础 | `vmod_q_u32_barrett、store_mod_q_s32x4、store_mod_q_inv77_s32x4、add_q_if_negative_s32x4` | L527–L587 | [`09_neon_modq.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/09_neon_modq.c) |
| B10 | §3.3 | 11 点/7 点配对 DFT（四通道） | int32x4[11] 或 [7] → 同形 | 基础 | `dft11_pairwise_4lane、dft11_pairwise_pruned_4lane、dft7_pairwise_4lane` | L588–L700 | [`10_dft_pair.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/10_dft_pair.c) |
| B11 | §3.3 §4.3 | 第一层编译期特化（零项消除） | — | 生产 | `SNTRUP_L1_LOAD、SNTRUP_L1_DC、SNTRUP_L1_TERM、SNTRUP_L1_K、SNTRUP_L1_ROW` | L701–L816 | [`11_layer1_spec.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/11_layer1_spec.c) |
| B12 | §3.3 | 77 点变换的 NEON 主循环 | Blocks77 → Blocks77 | 生产 | `good_thomas_77_ntt_zs_neon` | L817–L898 | [`12_gt77_neon.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/12_gt77_neon.c) |
| B13 | §3.4 | 十六维加权卷积：标量参考实现与 NEON 辅助函数 | int[16] × int[16] → int[16] | 参考 | `weighted_mul16_scalar、w16_reduce_s32_pair、w16_mul_const、w16_add_mod、w16_sub_mod、w16_tmvp8_mla、w16_weighted_tmvp8` | L899–L983 | [`13_w16_helpers.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/13_w16_helpers.c) |
| B14 | §3.4 | 十六维局部乘法：两个 8 维加权 TMVP 分支与重构 | int[16] × int[16] → int[16] | 生产 | `weighted_mul16_2ntt_tmvp8_neon、weighted_mul16` | L984–L1025 | [`14_w16_kernel.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/14_w16_kernel.c) |
| B15 | §3.5 | 系数索引与四通道点积 | (Blocks77, idx) → 系数 | 生产 | `main_coeff、D_idx、dot_i16_neon_4lane` | L1026–L1059 | [`15_dot.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/15_dot.c) |
| B16 | §3.5 | 低位分支（73 项截断卷积） | f[653], g[653] → clow[73] | 生产 | `low_part73_scalar、low_part73_neon` | L1060–L1086 | [`16_low73.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/16_low73.c) |
| B17 | §3.5 | 最终折回与中心化 | Blocks77, clow[73] → out[653] | 生产 | `final_folding_scalar、final_folding_neon` | L1087–L1158 | [`17_fold.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/17_fold.c) |
| B18 | §4.4 | 内部自检 | (tests, seed) → PASS/FAIL | 测试 | `sntrup653_internal_selftest` | L1159–L1386 | [`18_selftest.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/18_selftest.c) |
| B19 | §3 §4.1 | 对外接口函数 | inbytes, kbytes → outbytes | 生产 | `PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653` | L1387–L1433 | [`19_entry.c`](https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/19_entry.c) |

## 纯文本链接（可直接粘进邮件或投稿系统）

```text
https://github.com/karry190805/sntrup653-crt-neon
B01  01_params.c                      https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/01_params.c
B02  02_gt_index.c                    https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/02_gt_index.c
B03  03_switches.c                    https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/03_switches.c
B04  04_tables.c                      https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/04_tables.c
B05  05_barrett.c                     https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/05_barrett.c
B06  06_init_tables.c                 https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/06_init_tables.c
B07  07_blockify.c                    https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/07_blockify.c
B08  08_gt77_ref.c                    https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/08_gt77_ref.c
B09  09_neon_modq.c                   https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/09_neon_modq.c
B10  10_dft_pair.c                    https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/10_dft_pair.c
B11  11_layer1_spec.c                 https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/11_layer1_spec.c
B12  12_gt77_neon.c                   https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/12_gt77_neon.c
B13  13_w16_helpers.c                 https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/13_w16_helpers.c
B14  14_w16_kernel.c                  https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/14_w16_kernel.c
B15  15_dot.c                         https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/15_dot.c
B16  16_low73.c                       https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/16_low73.c
B17  17_fold.c                        https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/17_fold.c
B18  18_selftest.c                    https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/18_selftest.c
B19  19_entry.c                       https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/src/19_entry.c
https://github.com/karry190805/sntrup653-crt-neon/blob/v1.0.0/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L1-L1433   （整份源文件）
```

## 复现入口

三个平台包各自自包含。根目录 `README.md` 的“三种跑法”给出本地自检、板卡正确性与性能复现三条命令。
