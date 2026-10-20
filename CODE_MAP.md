# 核心代码分块索引

论文第 3 节的核心多项式乘法实现在同一个源文件里。三个平台交付包中的该文件逐字节相同（SHA-256 见各包的 `SOURCE_MANIFEST.sha256`），因此全部分块都指向同一份代码：

```text
packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c   （共 1434 行）
```

链接格式为 GitHub 行号永久链接 `.../blob/<commit>/<file>#L<起>-L<止>`，当前固定到提交 `e4efbe017adb195e56e5fdd6235e93812683ea97`；代码若再变动，重新运行 `python tools/make_links.py --ref <新提交>` 即可。

| 块 | 论文小节 | 内容 | 主要函数 / 宏 | 行号 | 链接 |
|---|---|---|---|---|---|
| B01 | §3.1 §3.2 | 文件头、环参数与静态断言 | `MAIN_N、LOW_N、BLOCKS、BS、_Static_assert` | L1–L67 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L1-L67) |
| B02 | §3.1 §3.3 | Good–Thomas 索引与公开零位模式 | `SNTRUP_GT_IDX、SNTRUP_GT_ACT、SNTRUP_GT_ACT_COUNT` | L68–L104 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L68-L104) |
| B03 | §4.3 | 编译期开关（消融入口） | `SNTRUP653_PUBLIC_ZS_MODE、SNTRUP653_PUBLIC_ZS_RUNTIME_MASK、SNTRUP653_LOW73_NEON、SNTRUP653_FINAL_FOLD_NEON` | L105–L148 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L105-L148) |
| B04 | §3.3 | 变换常量与成对 DFT 表 | `LAMBDA77、POW7_*、POW11_*、PAIR7_*、PAIR11_*` | L149–L175 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L149-L175) |
| B05 | §2.2 §3.3 | Barrett 约减与模 q 运算 | `BARRETT_MU_Q64/Q32、mod_q_u64_barrett、mod_q_u32_barrett、add_q_raw、sub_q_raw、mul_q_raw、center_q_raw` | L176–L273 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L176-L273) |
| B06 | §3.3 | 表初始化（77 次单位根、7/11 点 DFT 常数） | `Fq_freeze、pow_q、inv_q、find_root77、init_pair_tables7/11、init_tables` | L274–L420 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L274-L420) |
| B07 | §3.2 §4.4 | 分块表示与内部测试钩子 | `Blocks77、MatrixGT、blockify_A、blockify_B、zero_blocks、compare_blocks_detail` | L421–L471 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L421-L471) |
| B08 | §3.3 | Good–Thomas 77 点变换（标量参考） | `good_thomas_77_ntt_zs` | L472–L526 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L472-L526) |
| B09 | §2.5 §3.3 | NEON 约减与存储助手 | `vmod_q_u32_barrett、store_mod_q_s32x4、store_mod_q_inv77_s32x4、add_q_if_negative_s32x4` | L527–L587 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L527-L587) |
| B10 | §3.3 | 11 点/7 点成对 DFT（四通道） | `dft11_pairwise_4lane、dft11_pairwise_pruned_4lane、dft7_pairwise_4lane` | L588–L700 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L588-L700) |
| B11 | §3.3 §4.3 | 第一层编译期特化（公开零裁剪） | `SNTRUP_L1_LOAD、SNTRUP_L1_DC、SNTRUP_L1_TERM、SNTRUP_L1_K、SNTRUP_L1_ROW` | L701–L816 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L701-L816) |
| B12 | §3.3 | NEON 版 77 点变换主循环 | `good_thomas_77_ntt_zs_neon` | L817–L898 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L817-L898) |
| B13 | §3.4 | 十六维加权卷积：标量参考与 NEON 助手 | `weighted_mul16_scalar、w16_reduce_s32_pair、w16_mul_const、w16_add_mod、w16_sub_mod、w16_tmvp8_mla、w16_weighted_tmvp8` | L899–L983 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L899-L983) |
| B14 | §3.4 | 双 8 维 NTT + TMVP8 加权卷积主核 | `weighted_mul16_2ntt_tmvp8_neon、weighted_mul16` | L984–L1025 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L984-L1025) |
| B15 | §3.5 | 系数索引与四通道点积 | `main_coeff、D_idx、dot_i16_neon_4lane` | L1026–L1059 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L1026-L1059) |
| B16 | §3.5 | 低位分支（73 项截断卷积） | `low_part73_scalar、low_part73_neon` | L1060–L1086 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L1060-L1086) |
| B17 | §3.5 | 最终折回与中心化 | `final_folding_scalar、final_folding_neon` | L1087–L1158 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L1087-L1158) |
| B18 | §4.4 | 自检与内部测试钩子 | `sntrup653_internal_selftest` | L1159–L1386 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L1159-L1386) |
| B19 | §3 §4.1 | 对外入口 | `PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653` | L1387–L1434 | [打开](https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L1387-L1434) |

## 纯文本链接（可直接粘进邮件或投稿系统）

```text
https://github.com/OWNER/sntrup653-crt-neon
B01  文件头、环参数与静态断言  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L1-L67  L1-L67
B02  Good–Thomas 索引与公开零位模式  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L68-L104  L68-L104
B03  编译期开关（消融入口）  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L105-L148  L105-L148
B04  变换常量与成对 DFT 表  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L149-L175  L149-L175
B05  Barrett 约减与模 q 运算  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L176-L273  L176-L273
B06  表初始化（77 次单位根、7/11 点 DFT 常数）  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L274-L420  L274-L420
B07  分块表示与内部测试钩子  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L421-L471  L421-L471
B08  Good–Thomas 77 点变换（标量参考）  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L472-L526  L472-L526
B09  NEON 约减与存储助手  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L527-L587  L527-L587
B10  11 点/7 点成对 DFT（四通道）  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L588-L700  L588-L700
B11  第一层编译期特化（公开零裁剪）  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L701-L816  L701-L816
B12  NEON 版 77 点变换主循环  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L817-L898  L817-L898
B13  十六维加权卷积：标量参考与 NEON 助手  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L899-L983  L899-L983
B14  双 8 维 NTT + TMVP8 加权卷积主核  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L984-L1025  L984-L1025
B15  系数索引与四通道点积  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L1026-L1059  L1026-L1059
B16  低位分支（73 项截断卷积）  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L1060-L1086  L1060-L1086
B17  最终折回与中心化  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L1087-L1158  L1087-L1158
B18  自检与内部测试钩子  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L1159-L1386  L1159-L1386
B19  对外入口  https://github.com/OWNER/sntrup653-crt-neon/blob/e4efbe017adb195e56e5fdd6235e93812683ea97/packages/sntrup653-crt-cortex-a53/optimized/crypto_core_multsntrup653.c#L1387-L1434  L1387-L1434
```

## 复现入口

每个平台包都是自包含的，按 `README.md` 的“快速开始”一节解压后执行 `sudo bash ./run_on_target.sh` 即可重跑编译、正确性与计时。
