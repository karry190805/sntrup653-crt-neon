#define SNTRUP653_INTERNAL_TEST_HOOKS 1
#include "../optimized/crypto_core_multsntrup653.c"
#include <pthread.h>
#include <stdlib.h>

#define PROBE __attribute__((noinline, used))
PROBE int audit_reduce_i64(int64_t x) { return mod_q_i64_raw(x); }
PROBE int audit_reduce_u32(uint32_t x) { return mod_q_u32_barrett(x); }
PROBE int audit_add(int a, int b) { return add_q_raw(a, b); }
PROBE int audit_sub(int a, int b) { return sub_q_raw(a, b); }
PROBE int audit_center(int64_t x) { return center_q_raw(x); }
PROBE int audit_normalize_short(int8_t x) { return mod_q_i64_raw(x); }
PROBE int audit_normalize_fq(int16_t x) { return mod_q_i64_raw(x); }

#ifndef PROBE_ONLY
int baseline_crypto_core_multsntrup653(unsigned char *, const unsigned char *, const unsigned char *);
static pthread_barrier_t first_call_barrier;
static int thread_failed[8];

static void *first_call(void *arg) {
    int id = *(int *)arg;
    Fq f[p];
    unsigned char in[2 * p], key[p], got[2 * p], ref[2 * p];
    for (int i = 0; i < p; i++) {
        f[i] = (Fq)((i * 73 + id * 19) % q - q / 2);
        key[i] = (i % 3 == 0) ? 255 : (unsigned char)(i % 3 == 1);
    }
    crypto_encode_pxint16(in, f);
    baseline_crypto_core_multsntrup653(ref, in, key);
    pthread_barrier_wait(&first_call_barrier);
    PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653(got, in, key);
    thread_failed[id] = memcmp(ref, got, sizeof(got)) != 0;
    return NULL;
}

static int ref_mod(int64_t x) {
    int r = (int)(x % q);
    return r < 0 ? r + q : r;
}

static int test_reducers(void) {
    const int64_t extremes[] = {INT64_MIN, INT64_MIN + 1, INT64_MAX,
                               -(int64_t)q, 0, q, (int64_t)UINT32_MAX};
    for (unsigned i = 0; i < sizeof(extremes) / sizeof(extremes[0]); i++)
        if (audit_reduce_i64(extremes[i]) != ref_mod(extremes[i])) return 1;
    for (int x = -200000; x <= 200000; x++) {
        int r = ref_mod(x), centered = r > q / 2 ? r - q : r;
        if (audit_reduce_i64(x) != r || audit_center(x) != centered) return 2;
    }
    for (int x = INT16_MIN; x <= INT16_MAX; x++)
        if (Fq_freeze(x) != (Fq)(ref_mod(x) > q / 2 ? ref_mod(x) - q : ref_mod(x))) return 3;
    for (unsigned i = 0; i < 100000; i++) {
        uint64_t u = ((uint64_t)selftest_rand32() << 32) | selftest_rand32();
        int64_t x;
        memcpy(&x, &u, sizeof(x));
        if (mod_q_u64_barrett(u) != (int)(u % q) ||
            audit_reduce_u32((uint32_t)u) != (int)((uint32_t)u % q) ||
            audit_reduce_i64(x) != ref_mod(x)) return 4;
        int a = (int)(u % q), b = (int)((u >> 32) % q);
        if (audit_add(a, b) != ref_mod(a + b) || audit_sub(a, b) != ref_mod(a - b)) return 5;
    }
    return 0;
}

static int test_interfaces(void) {
    for (int trial = 0; trial < 1000; trial++) {
        unsigned char in[2 * p], key[p], ref[2 * p], got[2 * p + 32], alias[2 * p];
        Fq f[p];
        small g[p];
        int low_ref[LOW_N], low_opt[LOW_N];
        for (int i = 0; i < 2 * p; i++) in[i] = (unsigned char)selftest_rand32();
        for (int i = 0; i < p; i++) key[i] = (unsigned char)selftest_rand32();
        baseline_crypto_core_multsntrup653(ref, in, key);
        memset(got, 0xa5, sizeof(got));
        PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653(got + 16, in, key);
        if (memcmp(ref, got + 16, sizeof(ref))) return 1;
        for (int i = 0; i < 16; i++)
            if (got[i] != 0xa5 || got[16 + 2 * p + i] != 0xa5) return 2;
        memcpy(alias, in, sizeof(alias));
        PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653(alias, alias, key);
        if (memcmp(ref, alias, sizeof(ref))) return 3;
        crypto_decode_pxint16(f, in);
        for (int i = 0; i < p; i++) {
            f[i] = Fq_freeze(f[i]);
            int gi = (int8_t)key[i], bit = gi & 1;
            g[i] = (small)(bit - (gi & (bit << 1)));
        }
        low_part73_scalar(low_ref, f, g);
        low_part73_neon(low_opt, f, g);
        if (memcmp(low_ref, low_opt, sizeof(low_ref))) return 4;
    }
    return 0;
}

int main(void) {
    pthread_t threads[8];
    int ids[8];
    if (pthread_barrier_init(&first_call_barrier, NULL, 8)) return 1;
    for (int i = 0; i < 8; i++) {
        ids[i] = i;
        if (pthread_create(&threads[i], NULL, first_call, &ids[i])) exit(1);
    }
    for (int i = 0; i < 8; i++) {
        if (pthread_join(threads[i], NULL) || thread_failed[i]) return 2;
    }
    pthread_barrier_destroy(&first_call_barrier);
    puts("concurrent_first_call=PASS threads=8 (stress check, not a race proof)");
    int rc = test_reducers();
    if (rc) { fprintf(stderr, "reducers=FAIL code=%d\n", rc); return 3; }
    puts("reducers=PASS exhaustive_bounded_and_random_full_width");
    rc = sntrup653_internal_selftest(1000, 20260920);
    if (rc) { fprintf(stderr, "internal=FAIL code=%d\n", rc); return 4; }
    puts("internal=PASS");
    ACTIVE_PAIR_MASK11[0][1] ^= 1;
    rc = sntrup653_internal_selftest(0, 1);
    ACTIVE_PAIR_MASK11[0][1] ^= 1;
    if (rc != 11) { fprintf(stderr, "mask_negative_test=FAIL code=%d\n", rc); return 5; }
    puts("mask_negative_test=PASS");
    rc = test_interfaces();
    if (rc) { fprintf(stderr, "interfaces=FAIL code=%d\n", rc); return 6; }
    puts("interfaces=PASS raw_bytes_alias_canaries_low73 cases=1000");
    puts("security_functional_checks=PASS");
    return 0;
}
#endif
