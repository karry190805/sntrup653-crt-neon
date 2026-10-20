#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "params.h"
#include "crypto_core_multsntrup653.h"
#include "crypto_encode_653xint16.h"

#ifndef BASELINE_CRYPTO_CORE_MULT
#define BASELINE_CRYPTO_CORE_MULT baseline_crypto_core_multsntrup653
#endif

#ifndef OPTIMIZED_CRYPTO_CORE_MULT
#define OPTIMIZED_CRYPTO_CORE_MULT optimized_crypto_core_multsntrup653
#endif

int BASELINE_CRYPTO_CORE_MULT(unsigned char *outbytes, const unsigned char *inbytes, const unsigned char *kbytes);
int OPTIMIZED_CRYPTO_CORE_MULT(unsigned char *outbytes, const unsigned char *inbytes, const unsigned char *kbytes);

static uint32_t rng_state = 1u;

static void rng_seed(uint32_t seed) {
    rng_state = seed ? seed : 1u;
}

static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static int parse_int_arg(const char *s, int fallback) {
    char *end = NULL;
    long v;
    if (!s || !*s) return fallback;
    v = strtol(s, &end, 10);
    if (*end != '\0') return fallback;
    if (v < 0 || v > 2147483647L) return fallback;
    return (int)v;
}

static int16_t center_mod_q(int32_t x) {
    int32_t r = x % q;
    if (r < 0) r += q;
    if (r > q / 2) r -= q;
    return (int16_t)r;
}

static void encode_f(unsigned char inbytes[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_INPUTBYTES],
                     const int16_t f[p]) {
    PQCLEAN_SNTRUP653_CLEAN_crypto_encode_653xint16(inbytes, f);
}

static void clear_case(int16_t f[p], unsigned char kbytes[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES]) {
    memset(f, 0, (size_t)p * sizeof(f[0]));
    memset(kbytes, 0, PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES);
}

static void set_short(unsigned char kbytes[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES],
                      int pos,
                      int sign) {
    kbytes[pos] = (sign < 0) ? 255u : 1u;
}

static void fill_random_f(int16_t f[p]) {
    int i;
    for (i = 0; i < p; i++) {
        f[i] = center_mod_q((int32_t)(xorshift32() % (uint32_t)q));
    }
}

static void fill_random_short_w(unsigned char kbytes[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES]) {
    int pos[p];
    int i;
    memset(kbytes, 0, PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES);
    for (i = 0; i < p; i++) pos[i] = i;
    for (i = 0; i < w; i++) {
        int j = i + (int)(xorshift32() % (uint32_t)(p - i));
        int tmp = pos[i];
        pos[i] = pos[j];
        pos[j] = tmp;
    }
    for (i = 0; i < w; i++) {
        set_short(kbytes, pos[i], (xorshift32() & 1u) ? 1 : -1);
    }
}

static int run_case(const char *name,
                    const int16_t f[p],
                    const unsigned char kbytes[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES]) {
    unsigned char inbytes[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_INPUTBYTES];
    unsigned char out_baseline[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_OUTPUTBYTES];
    unsigned char out_optimized[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_OUTPUTBYTES];
    size_t i;
    int rc_baseline;
    int rc_optimized;

    encode_f(inbytes, f);
    memset(out_baseline, 0xa5, sizeof(out_baseline));
    memset(out_optimized, 0x5a, sizeof(out_optimized));

    rc_baseline = BASELINE_CRYPTO_CORE_MULT(out_baseline, inbytes, kbytes);
    rc_optimized = OPTIMIZED_CRYPTO_CORE_MULT(out_optimized, inbytes, kbytes);
    if (rc_baseline != 0 || rc_optimized != 0) {
        fprintf(stderr, "FAIL %s: rc_baseline=%d rc_optimized=%d\n", name, rc_baseline, rc_optimized);
        return 1;
    }
    for (i = 0; i < sizeof(out_baseline); i++) {
        if (out_baseline[i] != out_optimized[i]) {
            fprintf(stderr, "FAIL %s: first byte mismatch at %zu baseline=%u optimized=%u\n",
                    name, i, (unsigned)out_baseline[i], (unsigned)out_optimized[i]);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    int random_tests = 10000;
    uint32_t seed = 1u;
    int failures = 0;
    int i;
    int16_t f[p];
    unsigned char kbytes[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES];

    if (argc >= 2) random_tests = parse_int_arg(argv[1], random_tests);
    if (argc >= 3) seed = (uint32_t)strtoul(argv[2], NULL, 10);
    if (random_tests < 0) random_tests = 0;
    rng_seed(seed);

    clear_case(f, kbytes);
    failures += run_case("all_zero", f, kbytes);

    clear_case(f, kbytes);
    f[0] = 1;
    set_short(kbytes, 0, 1);
    failures += run_case("unit_vector", f, kbytes);

    clear_case(f, kbytes);
    for (i = 0; i < p; i++) f[i] = (int16_t)(q / 2);
    fill_random_short_w(kbytes);
    failures += run_case("max_center_value", f, kbytes);

    clear_case(f, kbytes);
    for (i = 0; i < p; i++) {
        f[i] = (int16_t)((i & 1) ? -(q / 2) : (q / 2));
        set_short(kbytes, i, (i & 1) ? -1 : 1);
    }
    failures += run_case("alternating_signs", f, kbytes);

    clear_case(f, kbytes);
    fill_random_f(f);
    for (i = 0; i < w; i++) set_short(kbytes, i, (i & 1) ? -1 : 1);
    failures += run_case("short_support_begin", f, kbytes);

    clear_case(f, kbytes);
    fill_random_f(f);
    for (i = 0; i < w; i++) set_short(kbytes, p - w + i, (i & 1) ? -1 : 1);
    failures += run_case("short_support_end", f, kbytes);

    clear_case(f, kbytes);
    fill_random_f(f);
    fill_random_short_w(kbytes);
    failures += run_case("random_short_w288", f, kbytes);

    for (i = 0; i < random_tests; i++) {
        char name[64];
        fill_random_f(f);
        fill_random_short_w(kbytes);
        snprintf(name, sizeof(name), "random_%d", i);
        failures += run_case(name, f, kbytes);
        if (failures) break;
    }

    if (failures) {
        fprintf(stderr, "core equivalence: FAIL\n");
        return 1;
    }
    printf("core equivalence: PASS (%d random tests plus fixed cases, seed=%u)\n",
           random_tests, seed);
    return 0;
}
