/*
 * test_concurrent_firstcall.c — race check on the first call.
 *
 * The production core lazily initialises its transform tables on the first
 * entry (init_tables, guarded by an atomic state word).  Two properties matter:
 *
 *   1. every thread that loses the initialisation race must observe a fully
 *      initialised table set  (release/acquire on tables_state);
 *   2. the results of all concurrent first calls must equal the results of the
 *      same calls made later, serially.
 *
 * Property 2 is checked here directly.  Property 1 is checked deterministically
 * when the binary is built with -fsanitize=thread, which is how the package
 * invokes it (run_correctness.sh builds both a plain and a TSan binary).
 *
 * usage: test_concurrent_firstcall [threads] [seed]
 *   threads defaults to 32, seed to 1.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "params.h"
#include "crypto_core_multsntrup653.h"

#ifndef CORE_UNDER_TEST
#define CORE_UNDER_TEST PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653
#endif

int CORE_UNDER_TEST(unsigned char *outbytes, const unsigned char *inbytes, const unsigned char *kbytes);

#define INBYTES  PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_INPUTBYTES
#define KEYBYTES PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES
#define OUTBYTES PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_OUTPUTBYTES

#define MAX_THREADS 256

static pthread_barrier_t start_barrier;

typedef struct {
    const unsigned char *in;
    const unsigned char *key;
    unsigned char *out;
    int rc;
} job_t;

static void *worker(void *arg) {
    job_t *j = (job_t *)arg;
    /* Everyone enters the core at the same instant, so the lazy table
     * initialisation is exercised as a genuine race. */
    pthread_barrier_wait(&start_barrier);
    j->rc = CORE_UNDER_TEST(j->out, j->in, j->key);
    return NULL;
}

/* Same generator family as the rest of the package. */
static uint32_t rng_state = 1u;

static void rng_seed(uint32_t s) { rng_state = s ? s : 1u; }

static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static void gen_case(unsigned char in[INBYTES], unsigned char key[KEYBYTES]) {
    int16_t f[p];
    int pos[p];
    int i;
    for (i = 0; i < p; i++) {
        f[i] = (int16_t)((int32_t)(xorshift32() % (uint32_t)q) - q / 2);
        key[i] = 0;
        pos[i] = i;
    }
    for (i = 0; i < w; i++) {
        int j = i + (int)(xorshift32() % (uint32_t)(p - i));
        int t = pos[i];
        pos[i] = pos[j];
        pos[j] = t;
    }
    for (i = 0; i < w; i++) {
        key[pos[i]] = (unsigned char)((xorshift32() & 1u) ? 1 : 255);
    }
    for (i = 0; i < p; i++) {
        uint16_t u = (uint16_t)f[i];
        in[2 * i] = (unsigned char)(u & 0xffu);
        in[2 * i + 1] = (unsigned char)(u >> 8);
    }
}

static int parse_int_arg(const char *s, int fallback) {
    char *end = NULL;
    long v;
    if (!s || !*s) return fallback;
    v = strtol(s, &end, 10);
    if (*end != '\0') return fallback;
    if (v < 1 || v > MAX_THREADS) return fallback;
    return (int)v;
}

int main(int argc, char **argv) {
    int threads = (argc >= 2) ? parse_int_arg(argv[1], 32) : 32;
    uint32_t seed = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 10) : 1u;
    static unsigned char in[MAX_THREADS][INBYTES];
    static unsigned char key[MAX_THREADS][KEYBYTES];
    static unsigned char out_parallel[MAX_THREADS][OUTBYTES];
    unsigned char out_serial[OUTBYTES];
    job_t jobs[MAX_THREADS];
    pthread_t tids[MAX_THREADS];
    int i;

    if (threads < 1 || threads > MAX_THREADS) {
        fprintf(stderr, "usage: %s [threads 1..%d] [seed]\n", argv[0], MAX_THREADS);
        return 2;
    }

    rng_seed(seed);
    for (i = 0; i < threads; i++) {
        gen_case(in[i], key[i]);
        memset(out_parallel[i], 0xa5, OUTBYTES);
    }

    if (pthread_barrier_init(&start_barrier, NULL, (unsigned)threads) != 0) {
        fprintf(stderr, "pthread_barrier_init failed\n");
        return 3;
    }
    for (i = 0; i < threads; i++) {
        jobs[i].in = in[i];
        jobs[i].key = key[i];
        jobs[i].out = out_parallel[i];
        jobs[i].rc = -1;
        if (pthread_create(&tids[i], NULL, worker, &jobs[i]) != 0) {
            fprintf(stderr, "pthread_create failed for thread %d\n", i);
            return 3;
        }
    }
    for (i = 0; i < threads; i++) {
        pthread_join(tids[i], NULL);
    }
    pthread_barrier_destroy(&start_barrier);

    for (i = 0; i < threads; i++) {
        if (jobs[i].rc != 0) {
            fprintf(stderr, "FAIL thread %d: core returned %d\n", i, jobs[i].rc);
            return 1;
        }
    }

    /* Serial replay of exactly the same inputs. */
    for (i = 0; i < threads; i++) {
        memset(out_serial, 0x5a, OUTBYTES);
        if (CORE_UNDER_TEST(out_serial, in[i], key[i]) != 0) {
            fprintf(stderr, "FAIL serial replay %d: core returned nonzero\n", i);
            return 1;
        }
        if (memcmp(out_serial, out_parallel[i], OUTBYTES) != 0) {
            size_t k;
            for (k = 0; k < OUTBYTES; k++) {
                if (out_serial[k] != out_parallel[i][k]) break;
            }
            fprintf(stderr,
                    "FAIL concurrent first-call differs from serial replay: "
                    "thread=%d byte=%zu parallel=%u serial=%u\n",
                    i, k, (unsigned)out_parallel[i][k], (unsigned)out_serial[k]);
            return 1;
        }
    }

    printf("concurrent first-call: PASS threads=%d seed=%u cases=%d\n",
           threads, seed, threads);
    return 0;
}
