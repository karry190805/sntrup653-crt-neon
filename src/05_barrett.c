/* B05 Barrett 约减与模 q 运算
 *
 * crypto_core_multsntrup653.c 第 176–273 行，共 98 行。
 * 分块视图，不单独编译；去掉本注释后按 B01–B19 顺序拼接即为原文件，
 * tools/join.py 可校验 SHA-256。
 */
/* 以上为视图注释，以下为原文件内容 */
#ifndef USE_BARRETT_REDUCE
#define USE_BARRETT_REDUCE 1
#endif

#if USE_BARRETT_REDUCE
/*
 * Fixed-modulus Barrett reduction for q = 4621.
 * USE_BARRETT_REDUCE defaults to 1 in this package.
 * Set -DUSE_BARRETT_REDUCE=0 to fall back to the conservative % q path
 * for validation.
 * Scalar correction and sign masks use explicit AArch64 instructions.
 * This fixes secret-dependent branches, not all possible side channels.
 */
static const uint64_t BARRETT_MU_Q64 = 3991937691778738ULL; /* floor(2^64 / 4621) */
static const uint32_t BARRETT_MU_Q32 = 929445U;             /* floor(2^32 / 4621) */

static inline uint64_t ct_sub_q_u64(uint64_t x) {
    uint64_t r;
    __asm__ ("subs %0, %1, %2\n\tcsel %0, %0, %1, hs"
             : "=&r"(r) : "r"(x), "r"((uint64_t)q) : "cc");
    return r;
}

static inline uint64_t ct_sign_mask_i64(int64_t x) {
    uint64_t mask;
    __asm__ ("asr %0, %1, #63" : "=r"(mask) : "r"(x));
    return mask;
}

static inline int mod_q_u64_barrett(uint64_t x) {
    uint64_t t = (uint64_t)(((unsigned __int128)x * BARRETT_MU_Q64) >> 64);
    uint64_t r = x - t * (uint64_t)q;
    r = ct_sub_q_u64(r);
    r = ct_sub_q_u64(r);
    return (int)r;
}

static inline int mod_q_u32_barrett(uint32_t x) {
    uint32_t t = (uint32_t)(((uint64_t)x * BARRETT_MU_Q32) >> 32);
    uint32_t r = x - t * (uint32_t)q;
    r = (uint32_t)ct_sub_q_u64(r);
    r = (uint32_t)ct_sub_q_u64(r);
    return (int)r;
}
#endif

static inline int mod_q_i64_raw(int64_t x) {
#if USE_BARRETT_REDUCE
    uint64_t mask = ct_sign_mask_i64(x);
    uint64_t ax = ((uint64_t)x ^ mask) - mask;
    uint64_t r = (uint64_t)mod_q_u64_barrett(ax);
    uint64_t neg_r = ct_sub_q_u64((uint64_t)q - r);
    return (int)((r & ~mask) | (neg_r & mask));
#else
    int64_t r = x % q;
    if (r < 0) r += q;
    return (int)r;
#endif
}

static inline int add_q_raw(int a, int b) {
#if USE_BARRETT_REDUCE
    return (int)ct_sub_q_u64((uint64_t)(a + b));
#else
    int r = a + b;
    if (r >= q) r -= q;
    return r;
#endif
}

static inline int sub_q_raw(int a, int b) {
#if USE_BARRETT_REDUCE
    return (int)ct_sub_q_u64((uint64_t)(a + q - b));
#else
    int r = a - b;
    if (r < 0) r += q;
    return r;
#endif
}

static inline int mul_q_raw(int a, int b) {
    return mod_q_i64_raw((int64_t)a * b);
}

static inline Fq center_q_raw(int64_t x) {
    int r = mod_q_i64_raw(x);
#if USE_BARRETT_REDUCE
    int centered;
    __asm__ ("sub %w0, %w1, %w2\n\tcmp %w1, %w3\n\tcsel %w0, %w0, %w1, hi"
             : "=&r"(centered) : "r"(r), "r"(q), "r"(q / 2) : "cc");
    return (Fq)centered;
#else
    if (r > q / 2) r -= q;
    return (Fq)r;
#endif
}

/* Match the centered input normalization used by the PQClean clean core. */