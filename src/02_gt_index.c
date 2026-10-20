/* B02 Good–Thomas 索引与公开零位模式
 *
 * crypto_core_multsntrup653.c 第 68–104 行，共 37 行。
 * 分块视图，不单独编译；去掉本注释后按 B01–B19 顺序拼接即为原文件，
 * tools/join.py 可校验 SHA-256。
 */
/* 以上为视图注释，以下为原文件内容 */
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
