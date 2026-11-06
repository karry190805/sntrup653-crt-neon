/*
 * Contract and range validation for the opt22 sntrup653 core.
 *
 * The core is Rq-by-small, not dense-by-dense.  The second argument is a
 * byte-encoded ternary polynomial; the 3*v step in the KEM is applied by
 * crypto_core_scale3 to the first argument.  This harness therefore tests:
 *   - general first arguments (centered, mod-q, edge, and multiples of 3)
 *   - the ternary second argument and arbitrary byte patterns
 *   - the separate scale3 and mult3 paths used by the KEM
 *
 * It also computes the largest low-73 triangular convolution value and checks
 * that the production int32 accumulator plus its bias stays below INT32_MAX.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define p 653
#define q 4621
#define LOW_N 73
#define INBYTES 1306
#define KEYBYTES 653
#define OUTBYTES 1306
#define MULT3_OUT 653
#define SCALE_OUT 1306

int baseline_mult(uint8_t *out, const uint8_t *in, const uint8_t *k);
int optimized_mult(uint8_t *out, const uint8_t *in, const uint8_t *k);
int baseline_mult3(uint8_t *out, const uint8_t *in, const uint8_t *k);
int optimized_mult3(uint8_t *out, const uint8_t *in, const uint8_t *k);
int baseline_scale3(uint8_t *out, const uint8_t *in);
int optimized_scale3(uint8_t *out, const uint8_t *in);

void PQCLEAN_SNTRUP653_CLEAN_crypto_encode_653xint16(uint8_t *out, const int16_t *f);
void PQCLEAN_SNTRUP653_CLEAN_crypto_decode_653xint16(int16_t *f, const uint8_t *in);

static uint32_t rng_state = 20261103u;

static uint32_t xs(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static int16_t center_mod_q(int64_t x) {
    int64_t r = x % q;
    if (r < 0) r += q;
    if (r > (q - 1) / 2) r -= q;
    return (int16_t)r;
}

static void encode_f(uint8_t out[INBYTES], const int16_t f[p]) {
    PQCLEAN_SNTRUP653_CLEAN_crypto_encode_653xint16(out, f);
}

static void decode_f(int16_t f[p], const uint8_t in[INBYTES]) {
    PQCLEAN_SNTRUP653_CLEAN_crypto_decode_653xint16(f, in);
}

enum f_type { F_CENTERED, F_MODQ, F_EDGE, F_CENTER_EDGE, F_THREE_V };
enum g_type { G_TERNARY, G_BYTE_PATTERN, G_RANDOM_BYTES };

static const char *f_name(enum f_type t) {
    switch (t) {
    case F_CENTERED: return "random_centered";
    case F_MODQ: return "random_modq";
    case F_EDGE: return "edge_values";
    case F_CENTER_EDGE: return "centered_edge_values";
    case F_THREE_V: return "three_v_-3_0_3";
    }
    return "?";
}

static const char *g_name(enum g_type t) {
    switch (t) {
    case G_TERNARY: return "ternary_-1_0_1";
    case G_BYTE_PATTERN: return "byte_pattern_0_1_255_3_253";
    case G_RANDOM_BYTES: return "random_bytes";
    }
    return "?";
}

static void gen_f(enum f_type t, int16_t f[p]) {
    static const int edge[] = {
        0, 1, -1, 3, -3, 2310, -2310, q - 1, -(q - 2), (q - 1) / 2, -((q - 1) / 2)
    };
    int i;
    for (i = 0; i < p; i++) {
        switch (t) {
        case F_CENTERED:
            f[i] = (int16_t)((int)(xs() % q) - (q / 2));
            break;
        case F_MODQ:
            f[i] = (int16_t)(xs() % q);
            break;
        case F_EDGE:
            f[i] = (int16_t)edge[i % (int)(sizeof(edge) / sizeof(edge[0]))];
            break;
        case F_CENTER_EDGE: {
            static const int cedge[] = { 0, 1, -1, 3, -3, 2310, -2310 };
            f[i] = (int16_t)cedge[i % (int)(sizeof(cedge) / sizeof(cedge[0]))];
            break;
        }
        case F_THREE_V: {
            static const int v3[] = { -3, 0, 3 };
            f[i] = (int16_t)v3[xs() % 3u];
            break;
        }
        }
    }
}

static uint8_t ternary_byte(int v) {
    return (uint8_t)(v < 0 ? 255 : v);
}

static void gen_g(enum g_type t, uint8_t k[KEYBYTES]) {
    static const uint8_t pat[] = { 0, 1, 255, 3, 253, 2, 254, 127, 128 };
    int i;
    for (i = 0; i < KEYBYTES; i++) {
        switch (t) {
        case G_TERNARY:
            k[i] = ternary_byte((int)(xs() % 3u) - 1);
            break;
        case G_BYTE_PATTERN:
            k[i] = pat[i % (int)(sizeof(pat) / sizeof(pat[0]))];
            break;
        case G_RANDOM_BYTES:
            k[i] = (uint8_t)xs();
            break;
        }
    }
}

static int8_t decode_small_byte(uint8_t x) {
    int8_t gi = (int8_t)x;
    int8_t gi0 = (int8_t)(gi & 1);
    return (int8_t)(gi0 - (gi & (gi0 << 1)));
}

static int64_t low73_max_abs(const int16_t f[p], const uint8_t k[KEYBYTES]) {
    int8_t g[p];
    int64_t best = 0;
    int kk, i;
    for (i = 0; i < p; i++) g[i] = decode_small_byte(k[i]);
    for (kk = 0; kk < LOW_N; kk++) {
        int64_t acc = 0;
        for (i = 0; i <= kk; i++) {
            int64_t fc = f[i] % q;
            if (fc < 0) fc += q;
            acc += fc * (int64_t)g[kk - i];
        }
        if (acc < 0) acc = -acc;
        if (acc > best) best = acc;
    }
    return best;
}

static int run_mult(enum f_type ft, enum g_type gt, int cases,
                    int64_t *max_low73, int *bad) {
    int16_t f[p];
    uint8_t k[KEYBYTES];
    uint8_t in[INBYTES];
    uint8_t ob[OUTBYTES], oo[OUTBYTES];
    int i, fail = 0;
    int64_t local_max = 0;
    for (i = 0; i < cases; i++) {
        gen_f(ft, f);
        gen_g(gt, k);
        encode_f(in, f);
        memset(ob, 0xa5, sizeof(ob));
        memset(oo, 0x5a, sizeof(oo));
        if (baseline_mult(ob, in, k) != 0 || optimized_mult(oo, in, k) != 0) {
            fprintf(stderr, "mult return-code failure f=%s g=%s case=%d\n", f_name(ft), g_name(gt), i);
            *bad += 1;
            return 1;
        }
        if (memcmp(ob, oo, sizeof(ob)) != 0) {
            fprintf(stderr, "mult mismatch f=%s g=%s case=%d\n", f_name(ft), g_name(gt), i);
            fail++;
            break;
        }
        {
            int64_t v = low73_max_abs(f, k);
            if (v > local_max) local_max = v;
        }
    }
    if (local_max > *max_low73) *max_low73 = local_max;
    *bad += fail;
    printf("mult f=%s g=%s cases=%d mismatches=%d low73_max_abs=%lld int32_safe=%d\n",
           f_name(ft), g_name(gt), cases, fail, (long long)local_max,
           (local_max + (int64_t)LOW_N * q <= INT32_MAX) ? 1 : 0);
    return fail != 0;
}

static int run_scale3(int cases, int *bad) {
    static const enum f_type types[] = { F_CENTERED, F_CENTER_EDGE, F_THREE_V };
    int16_t f[p], expected[p];
    uint8_t in[INBYTES], ob[SCALE_OUT], oo[SCALE_OUT], exp[SCALE_OUT];
    int t, i, fail = 0;
    for (t = 0; t < (int)(sizeof(types) / sizeof(types[0])); t++) {
        for (i = 0; i < cases; i++) {
            int j;
            gen_f(types[t], f);
            encode_f(in, f);
            if (baseline_scale3(ob, in) != 0 || optimized_scale3(oo, in) != 0) {
                fprintf(stderr, "scale3 return-code failure type=%s case=%d\n", f_name(types[t]), i);
                *bad += 1;
                return 1;
            }
            if (memcmp(ob, oo, sizeof(ob)) != 0) {
                fprintf(stderr, "scale3 mismatch type=%s case=%d\n", f_name(types[t]), i);
                fail++;
                break;
            }
            for (j = 0; j < p; j++) expected[j] = center_mod_q((int64_t)3 * f[j]);
            encode_f(exp, expected);
            if (memcmp(ob, exp, sizeof(exp)) != 0) {
                fprintf(stderr, "scale3 expected-value mismatch type=%s case=%d\n", f_name(types[t]), i);
                fail++;
                break;
            }
        }
    }
    *bad += fail;
    printf("scale3 cases_per_type=%d types=3 mismatches=%d expected_3f_mod_q=%s\n",
           cases, fail, fail ? "FAIL" : "PASS");
    return fail != 0;
}

static int run_mult3(int cases, int *bad) {
    uint8_t in[KEYBYTES], k[KEYBYTES], ob[MULT3_OUT], oo[MULT3_OUT];
    int i, fail = 0;
    for (i = 0; i < cases; i++) {
        gen_g(G_TERNARY, in);
        gen_g(G_TERNARY, k);
        if (baseline_mult3(ob, in, k) != 0 || optimized_mult3(oo, in, k) != 0) {
            fprintf(stderr, "mult3 return-code failure case=%d\n", i);
            *bad += 1;
            return 1;
        }
        if (memcmp(ob, oo, sizeof(ob)) != 0) {
            fprintf(stderr, "mult3 mismatch case=%d\n", i);
            fail++;
            break;
        }
    }
    *bad += fail;
    printf("mult3 cases=%d mismatches=%d\n", cases, fail);
    return fail != 0;
}

int main(int argc, char **argv) {
    int cases = 100000;
    int bad = 0;
    int64_t max_low73 = 0;
    int i;
    if (argc >= 2) {
        char *end = NULL;
        long v = strtol(argv[1], &end, 10);
        if (!argv[1][0] || *end != '\0' || v <= 0) return 2;
        cases = (int)v;
    }
    if (argc >= 3) rng_state = (uint32_t)strtoul(argv[2], NULL, 10);

    /* Four first-argument classes, each with the production ternary key. */
    for (i = 0; i < 4; i++) {
        if (run_mult((enum f_type)i, G_TERNARY, cases, &max_low73, &bad)) break;
    }
    /* Arbitrary key bytes and a byte-pattern decoder sweep. */
    if (!bad) run_mult(F_CENTERED, G_BYTE_PATTERN, cases, &max_low73, &bad);
    if (!bad) run_mult(F_CENTERED, G_RANDOM_BYTES, cases, &max_low73, &bad);
    /* KEM 3*v path and the R3 product path. */
    if (!bad) run_scale3(cases, &bad);
    if (!bad) run_mult3(cases, &bad);

    printf("contract_summary cases_per_class=%d mismatches=%d low73_max_abs=%lld low73_bias=%d low73_max_plus_bias=%lld int32_safe=%d\n",
           cases, bad, (long long)max_low73, LOW_N * q,
           (long long)(max_low73 + (int64_t)LOW_N * q),
           (max_low73 + (int64_t)LOW_N * q <= INT32_MAX) ? 1 : 0);
    if (bad) {
        fprintf(stderr, "contract_validation=FAIL\n");
        return 1;
    }
    printf("contract_validation=PASS\n");
    return 0;
}
