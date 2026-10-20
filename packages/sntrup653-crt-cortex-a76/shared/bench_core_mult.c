#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "params.h"
#include "crypto_core_multsntrup653.h"
#include "crypto_encode_653xint16.h"

#define BENCH_STRINGIFY_INNER(x) #x
#define BENCH_STRINGIFY(x) BENCH_STRINGIFY_INNER(x)
#ifndef BENCH_IMPL_TAG
#define BENCH_IMPL_TAG unspecified
#endif

#ifndef BENCH_BACKEND_TAG
#define BENCH_BACKEND_TAG unspecified
#endif

static const char *bench_impl_name(void) {
    return BENCH_STRINGIFY(BENCH_IMPL_TAG);
}

static const char *bench_backend(void) {
    return BENCH_STRINGIFY(BENCH_BACKEND_TAG);
}

static double now_ms(void) {
    struct timespec ts;
#ifdef CLOCK_MONOTONIC_RAW
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void compiler_barrier(const void *ptr) {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" : : "r"(ptr) : "memory");
#else
    (void)ptr;
#endif
}

static int cmpd(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return (x > y) - (x < y);
}

static void report(const char* name, double* arr, int n) {
    double sum = 0.0, minv = arr[0], maxv = arr[0];
    int i;
    for (i = 0; i < n; i++) {
        sum += arr[i];
        if (arr[i] < minv) minv = arr[i];
        if (arr[i] > maxv) maxv = arr[i];
    }
    qsort(arr, (size_t)n, sizeof(double), cmpd);
    int p95idx = (int)(((long long)n * 95 + 99) / 100) - 1;
    if (p95idx < 0) p95idx = 0;
    if (p95idx >= n) p95idx = n - 1;
    printf("%s { mean_ms: %.6f, median_ms: %.6f, p95_ms: %.6f, min_ms: %.6f, max_ms: %.6f }\n",
           name, sum / n, arr[n / 2], arr[p95idx], minv, maxv);
}

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

static int parse_int_arg(const char* s, int fallback) {
    char* end = NULL;
    long v;
    if (!s || !*s) return fallback;
    v = strtol(s, &end, 10);
    if (*end != '\0') return fallback;
    if (v < -2147483647L || v > 2147483647L) return fallback;
    return (int)v;
}

static uint32_t parse_u32_arg(const char* s, uint32_t fallback) {
    char* end = NULL;
    unsigned long v;
    if (!s || !*s) return fallback;
    v = strtoul(s, &end, 10);
    if (*end != '\0') return fallback;
    return (uint32_t)v;
}

static int16_t center_mod_q(int32_t x) {
    int32_t r = x % q;
    if (r < 0) r += q;
    if (r > q / 2) r -= q;
    return (int16_t)r;
}

static void gen_input(unsigned char inbytes[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_INPUTBYTES],
                      unsigned char kbytes[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES]) {
    int16_t f[p];
    int pos[p];
    int i;
    for (i = 0; i < p; i++) {
        int32_t r = (int32_t)(xorshift32() % (uint32_t)q);
        f[i] = center_mod_q(r);
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
        kbytes[pos[i]] = (xorshift32() & 1u) ? 1u : 255u; /* 255 decodes to -1 in PQClean small representation. */
    }
    PQCLEAN_SNTRUP653_CLEAN_crypto_encode_653xint16(inbytes, f);
}

static uint64_t fnv1a64_update(uint64_t h, const unsigned char* data, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= (uint64_t)data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

int main(int argc, char** argv) {
    int rounds = 50;
    int loops = 1000;
    int correctness_tests = 200;
    int warmup_loops = 100;
    uint32_t seed = 1u;
    int i, r;

    if (argc >= 2) rounds = parse_int_arg(argv[1], rounds);
    if (argc >= 3) loops = parse_int_arg(argv[2], loops);
    if (argc >= 4) correctness_tests = parse_int_arg(argv[3], correctness_tests);
    if (argc >= 5) warmup_loops = parse_int_arg(argv[4], warmup_loops);
    if (argc >= 6) seed = parse_u32_arg(argv[5], seed);

    if (rounds <= 0 || loops <= 0 || correctness_tests < 0 || warmup_loops < 0) {
        fprintf(stderr, "usage: %s [rounds>0] [loops>0] [correctness_tests>=0] [warmup_loops>=0] [seed]\n", argv[0]);
        fprintf(stderr, "invalid benchmark parameters: rounds=%d loops=%d correctness_tests=%d warmup_loops=%d seed=%u\n",
                rounds, loops, correctness_tests, warmup_loops, seed);
        return 2;
    }

    unsigned char* invec = (unsigned char*)malloc((size_t)rounds * PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_INPUTBYTES);
    unsigned char* kvec = (unsigned char*)malloc((size_t)rounds * PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES);
    double* times = (double*)malloc((size_t)rounds * sizeof(double));
    if (!invec || !kvec || !times) {
        fprintf(stderr, "allocation failed\n");
        free(invec); free(kvec); free(times);
        return 3;
    }

    unsigned char out[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_OUTPUTBYTES];
    unsigned char inbytes[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_INPUTBYTES];
    unsigned char kbytes[PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES];
    uint64_t correctness_hash = 14695981039346656037ULL;
    volatile uint64_t bench_sink = 0;

    rng_seed(seed);
    for (i = 0; i < correctness_tests; i++) {
        gen_input(inbytes, kbytes);
        PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653(out, inbytes, kbytes);
        correctness_hash = fnv1a64_update(correctness_hash, out, sizeof(out));
    }
    printf("Core correctness smoke: produced %d outputs; compare core_correctness_hash across variants.\n", correctness_tests);
    printf("core_correctness_hash=0x%016llx\n", (unsigned long long)correctness_hash);

    rng_seed(seed + 0x9e3779b9u);
    for (r = 0; r < rounds; r++) {
        gen_input(invec + (size_t)r * PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_INPUTBYTES,
                  kvec + (size_t)r * PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES);
    }

    for (r = 0; r < rounds; r++) {
        for (i = 0; i < warmup_loops; i++) {
            int idx = (r + i) % rounds;
            const unsigned char* in = invec + (size_t)idx * PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_INPUTBYTES;
            const unsigned char* key = kvec + (size_t)idx * PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES;
            PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653(out, in, key);
            compiler_barrier(out);
            bench_sink ^= (uint64_t)out[(unsigned)(i + r) % PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_OUTPUTBYTES];
        }
    }

    for (r = 0; r < rounds; r++) {
        double t0 = now_ms();
        for (i = 0; i < loops; i++) {
            int idx = (r + i) % rounds;
            const unsigned char* in = invec + (size_t)idx * PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_INPUTBYTES;
            const unsigned char* key = kvec + (size_t)idx * PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES;
            PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653(out, in, key);
            compiler_barrier(out);
            /* Consume one byte from every call. This prevents LTO/dead-store
               elimination from turning repeated calls into a single call. */
            bench_sink += (uint64_t)out[(unsigned)(i + r) % PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_OUTPUTBYTES];
        }
        double t1 = now_ms();
        times[r] = (t1 - t0) / loops;
    }

    {
        const char *csv_path = getenv("SNTRUP_BENCH_CSV");
        if (csv_path && *csv_path) {
            FILE *csv = fopen(csv_path, "w");
            if (!csv) {
                perror("fopen SNTRUP_BENCH_CSV");
                free(invec); free(kvec); free(times);
                return 4;
            }
            fprintf(csv, "implementation,round,ms_per_call\n");
            for (r = 0; r < rounds; r++) {
                fprintf(csv, "%s,%d,%.9f\n", bench_impl_name(), r, times[r]);
            }
            if (fclose(csv) != 0) {
                perror("fclose SNTRUP_BENCH_CSV");
                free(invec); free(kvec); free(times);
                return 4;
            }
            printf("raw_csv=%s\n", csv_path);
        }
    }

    printf("implementation=%s\n", bench_impl_name());
    printf("backend=%s\n", bench_backend());
    printf("Parameters: p=%d, q=%d, w=%d\n", p, q, w);
    printf("Benchmark: %d rounds, %d loops per round, warmup_loops=%d, correctness_tests=%d, seed=%u; reported ms is per call\n",
           rounds, loops, warmup_loops, correctness_tests, seed);
    report("crypto_core_multsntrup653", times, rounds);
    printf("bench_sink=%llu\n", (unsigned long long)bench_sink);

    free(invec); free(kvec); free(times);
    return 0;
}
