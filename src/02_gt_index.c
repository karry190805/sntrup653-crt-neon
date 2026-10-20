/* ---------------------------------------------------------------------------
 * B02  Good–Thomas 索引与公开零位模式  —— 论文 §3.1 §3.3
 * 源文件第 68--104 行（共 37 行）
 *
 * 本文件是 crypto_core_multsntrup653.c 的分块视图，由 tools/split.py 原样切分
 * 生成，不是独立编译单元。按 B01…B19 顺序去掉本注释头后拼接即得原文件，
 * 可用 tools/join.py 校验 SHA-256。
 * ------------------------------------------------------------------------- */
/* ==== SPLIT:HEADER END ==== */
/* ---------------------------------------------------------------------------
 * Public padding pattern as integer constant expressions.
 *
 * The first layer of the 11-point-first Good--Thomas order fixes r and runs a
 * DFT over s, reading block
 *
 *     SNTRUP_GT_IDX(r,s) = (22*r + 56*s) mod 77 .
 *
 * A column is public padding exactly when that index is >= ACTIVE_BLOCKS.
 * Both the index and the predicate are integer constant expressions as soon as
 * r and s are literals, which is what makes the specialisation below possible.
 * The pattern depends only on the public parameter p = 653: no key material,
 * no ciphertext and no run-time value takes part in it.
 * ------------------------------------------------------------------------- */
#define SNTRUP_GT_IDX(RR, SS) (((22 * (RR)) + (56 * (SS))) % 77)
#define SNTRUP_GT_ACT(RR, SS) (SNTRUP_GT_IDX(RR, SS) < ACTIVE_BLOCKS)
#define SNTRUP_GT_ACT_COUNT(RR)                                     \
    (SNTRUP_GT_ACT(RR, 0) + SNTRUP_GT_ACT(RR, 1) +                  \
     SNTRUP_GT_ACT(RR, 2) + SNTRUP_GT_ACT(RR, 3) +                  \
     SNTRUP_GT_ACT(RR, 4) + SNTRUP_GT_ACT(RR, 5) +                  \
     SNTRUP_GT_ACT(RR, 6) + SNTRUP_GT_ACT(RR, 7) +                  \
     SNTRUP_GT_ACT(RR, 8) + SNTRUP_GT_ACT(RR, 9) +                  \
     SNTRUP_GT_ACT(RR, 10))

/* 6+6+6+6+6+6+5 = 41 = ACTIVE_BLOCKS.  If this ever fails, the specialised
 * schedule and the ACTIVE_COL_LIST table no longer describe the same columns. */
_Static_assert(SNTRUP_GT_ACT_COUNT(0) + SNTRUP_GT_ACT_COUNT(1) +
               SNTRUP_GT_ACT_COUNT(2) + SNTRUP_GT_ACT_COUNT(3) +
               SNTRUP_GT_ACT_COUNT(4) + SNTRUP_GT_ACT_COUNT(5) +
               SNTRUP_GT_ACT_COUNT(6) == ACTIVE_BLOCKS,
               "specialised first layer must cover every active block");
_Static_assert(SNTRUP_GT_ACT_COUNT(0) == 6 && SNTRUP_GT_ACT_COUNT(1) == 6 &&
               SNTRUP_GT_ACT_COUNT(2) == 6 && SNTRUP_GT_ACT_COUNT(3) == 6 &&
               SNTRUP_GT_ACT_COUNT(4) == 6 && SNTRUP_GT_ACT_COUNT(5) == 6 &&
               SNTRUP_GT_ACT_COUNT(6) == 5,
               "unexpected public padding pattern for p = 653");
