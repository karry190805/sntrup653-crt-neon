/* ---------------------------------------------------------------------------
 * B04  变换常量与成对 DFT 表  —— 论文 §3.3
 * 源文件第 149--175 行（共 27 行）
 *
 * 本文件是 crypto_core_multsntrup653.c 的分块视图，由 tools/split.py 原样切分
 * 生成，不是独立编译单元。按 B01…B19 顺序去掉本注释头后拼接即得原文件，
 * 可用 tools/join.py 校验 SHA-256。
 * ------------------------------------------------------------------------- */
/* ==== SPLIT:HEADER END ==== */
static atomic_int tables_state = ATOMIC_VAR_INIT(0);
static int INV2 = 0;
static int INV77 = 0;
static int ROOT77 = 0;
static int LAMBDA77[BLOCKS];
static int SQRT_LAMBDA77[BLOCKS];
static int INV2_SQRT_LAMBDA77[BLOCKS];
static int POW7_F[7][7];
static int POW7_I[7][7];
static int POW11_F[11][11];
static int POW11_I[11][11];
/* Pairwise direct DFT constants: P=(w^{kj}+w^{-kj})/2, M=(w^{kj}-w^{-kj})/2.
 * Indices use k=1..(n-1)/2 and j=1..(n-1)/2. */
static int PAIR7_P_F[4][4];
static int PAIR7_M_F[4][4];
static int PAIR7_P_I[4][4];
static int PAIR7_M_I[4][4];
static int PAIR11_P_F[6][6];
static int PAIR11_M_F[6][6];
static int PAIR11_P_I[6][6];
static int PAIR11_M_I[6][6];
static int GT_IDX_RS[11][7];
static int GT_K_UV[7][11];
static int ACTIVE_COLS_PER_R[7];
static int ACTIVE_COL_LIST[7][11];
static int ACTIVE_PAIR_MASK11[7][6];
