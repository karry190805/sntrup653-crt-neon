/*
 * Diagnostic per-stage profiler for the sntrup653 CRT core.
 *
 * The core is compiled with -DSNTRUP653_STAGE_PROFILE=1, which expands each
 * SNTRUP_STAGE_MARK() into "append one id and one cntvct_el0 timestamp".  The
 * driver resets the buffer before every core call, validates the id sequence,
 * and turns the timestamps into per-stage durations.  That keeps the marks to
 * two stores, no call and no memory clobber, so the core does not spill NEON
 * registers around them.
 *
 * IMPORTANT: the marks still cost a counter read plus two stores each, so the
 * rows below are diagnostic only.  They are useful for relative attribution and
 * must not be summed and presented as the production cycle count.
 *
 * usage: bench_stage_profile [rounds] [loops] [seed] [csv_out]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "params.h"
#include "crypto_core_multsntrup653.h"
#include "crypto_encode_653xint16.h"
#include "stage_profile_ids.h"

#define STAGE_MAX_CALL_MARKS 32
#define STAGE_SLOTS (STAGE_MAX_CALL_MARKS + 1)

unsigned long long sntrup653_stage_ts[SNTRUP653_STAGE_MAX];
unsigned char sntrup653_stage_ids[SNTRUP653_STAGE_MAX];
int sntrup653_stage_idx;

static const char *const stage_names[SNTRUP_STAGE_COUNT] = {
    "blockify_A", "blockify_B",
    "fwd_dft11", "fwd_dft7", "pointwise",
    "inv_dft11", "inv_dft7", "low73", "final_fold", "encode", "end",
};

/* Expected id sequence inside one core call (validated every call).  The
 * forward transform runs once per operand, so its two marks appear twice. */
static const unsigned char stage_seq[] = {
    SNTRUP_STAGE_blockify_A, SNTRUP_STAGE_blockify_B,
    SNTRUP_STAGE_fwd_dft11, SNTRUP_STAGE_fwd_dft7,
    SNTRUP_STAGE_fwd_dft11, SNTRUP_STAGE_fwd_dft7,
    SNTRUP_STAGE_pointwise,
    SNTRUP_STAGE_inv_dft11, SNTRUP_STAGE_inv_dft7,
    SNTRUP_STAGE_low73, SNTRUP_STAGE_final_fold,
    SNTRUP_STAGE_encode, SNTRUP_STAGE_end,
};
#define STAGE_MARKS ((int)(sizeof(stage_seq) / sizeof(stage_seq[0])))
#define STAGE_SLOTS_NEEDED (STAGE_MARKS + 1)

static unsigned long long timer_hz(void) {
    unsigned long long f;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(f));
    return f ? f : 1ULL;
}

static inline unsigned long long timer_now(void) {
    unsigned long long t;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(t));
    return t;
}

static void stage_begin(void) {
    sntrup653_stage_idx = 0;
    sntrup653_stage_ts[sntrup653_stage_idx] = timer_now();
    sntrup653_stage_ids[sntrup653_stage_idx] = 0xFF;
    sntrup653_stage_idx++;
}

static void dump_sequence(void) {
    int i;
    fprintf(stderr, "observed:");
    for (i = 0; i < sntrup653_stage_idx; i++) {
        unsigned id = sntrup653_stage_ids[i];
        fprintf(stderr, " %s", id < SNTRUP_STAGE_COUNT ? stage_names[id] : "BEGIN");
    }
    fprintf(stderr, "\n");
}

static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static int16_t center_mod_q(int32_t r) {
    r %= q;
    if (r < 0) {
        r += q;
    }
    if (r > q / 2) {
        r -= q;
    }
    return (int16_t)r;
}

static void gen_input(unsigned char inbytes[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_INPUTBYTES],
                      unsigned char kbytes[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES]) {
    int16_t f[p];
    int pos[p];
    int i;
    for (i = 0; i < p; i++) {
        f[i] = center_mod_q((int32_t)(xorshift32() % (uint32_t)q));
        kbytes[i] = 0;
        pos[i] = i;
    }
    for (i = 0; i < w; i++) {
        int j = i + (int)(xorshift32() % (uint32_t)(p - i));
        int tmp = pos[i];
        pos[i] = pos[j];
        pos[j] = tmp;
    }
    for (i = 0; i < w; i++) {
        kbytes[pos[i]] = (xorshift32() & 1u) ? 1u : 255u;
    }
    PQCLEAN_SNTRUP653_CLEAN_crypto_encode_653xint16(inbytes, f);
}

static int parse_int(const char *s, int fallback) {
    char *end = NULL;
    long v;
    if (!s || !*s) {
        return fallback;
    }
    v = strtol(s, &end, 10);
    if (*end != '\0' || v < 1 || v > 100000000L) {
        return fallback;
    }
    return (int)v;
}

int main(int argc, char **argv) {
    int rounds = argc > 1 ? parse_int(argv[1], 40) : 40;
    int loops = argc > 2 ? parse_int(argv[2], 2000) : 2000;
    uint32_t seed = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 10) : 20260901u;
    const char *csv_path = argc > 4 ? argv[4] : NULL;
    unsigned char inbytes[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_INPUTBYTES];
    unsigned char kbytes[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES];
    unsigned char out[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_OUTPUTBYTES];
    volatile uint64_t sink = 0;
    long long calls;
    int r, i, s;
    unsigned long long hz = timer_hz();
    unsigned long long marked_ticks = 0;
    double stage_ticks[SNTRUP_STAGE_COUNT];
    FILE *csv = NULL;

    rng_state = seed ? seed : 1u;
    memset(stage_ticks, 0, sizeof(stage_ticks));

    /* Warm-up pays the one-time table init and settles the frequency.  The
     * marks fire here too, so reset the timestamp buffer for every call. */
    for (r = 0; r < 500; r++) {
        gen_input(inbytes, kbytes);
        stage_begin();
        PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653(out, inbytes, kbytes);
        sink ^= out[r % sizeof(out)];
    }

    for (r = 0; r < rounds; r++) {
        gen_input(inbytes, kbytes);
        for (i = 0; i < loops; i++) {
            stage_begin();
            PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653(out, inbytes, kbytes);
            if (sntrup653_stage_idx != STAGE_SLOTS_NEEDED) {
                fprintf(stderr, "stage marker drift: got %d slots, expected %d\n",
                        sntrup653_stage_idx, STAGE_SLOTS_NEEDED);
                dump_sequence();
                return 4;
            }
            for (s = 0; s < STAGE_MARKS; s++) {
                unsigned id = sntrup653_stage_ids[s + 1];
                unsigned long long d;
                if (id != stage_seq[s]) {
                    fprintf(stderr, "stage id drift at mark %d: got %s expected %s\n",
                            s, id < SNTRUP_STAGE_COUNT ? stage_names[id] : "BEGIN",
                            stage_names[stage_seq[s]]);
                    dump_sequence();
                    return 5;
                }
                d = sntrup653_stage_ts[s + 1] - sntrup653_stage_ts[s];
                marked_ticks += d;
                stage_ticks[id] += (double)d;
            }
            sink ^= out[(unsigned)(i + r) % sizeof(out)];
        }
    }

    calls = (long long)rounds * (long long)loops;

    if (csv_path && *csv_path) {
        csv = fopen(csv_path, "w");
        if (!csv) {
            perror("fopen csv");
            return 3;
        }
    }

    printf("# Diagnostic stage profile (compiled with -DSNTRUP653_STAGE_PROFILE=1)\n");
    printf("# clock=cntvct_el0 cntfrq_el0=%llu  rounds=%d loops=%d seed=%u calls=%lld\n",
           hz, rounds, loops, seed, calls);
    printf("# Marks add an inline counter read + two stores each: rows are diagnostic, do NOT sum them as the production total.\n");
    printf("stage,ns_per_call,pct_of_marked\n");
    if (csv) {
        fprintf(csv, "stage,ns_per_call,pct_of_marked\n");
    }
    for (s = 0; s < SNTRUP_STAGE_COUNT; s++) {
        double ns = stage_ticks[s] * 1e9 / (double)hz / (double)calls;
        double pct = marked_ticks > 0 ? 100.0 * stage_ticks[s] / (double)marked_ticks : 0.0;
        printf("%s,%.3f,%.2f\n", stage_names[s], ns, pct);
        if (csv) {
            fprintf(csv, "%s,%.3f,%.2f\n", stage_names[s], ns, pct);
        }
    }
    printf("sum_marked,%.3f,100.00\n", (double)marked_ticks * 1e9 / (double)hz / (double)calls);
    if (csv) {
        fprintf(csv, "sum_marked,%.3f,100.00\n", (double)marked_ticks * 1e9 / (double)hz / (double)calls);
        fclose(csv);
    }
    printf("sink=%llu\n", (unsigned long long)sink);
    return 0;
}
