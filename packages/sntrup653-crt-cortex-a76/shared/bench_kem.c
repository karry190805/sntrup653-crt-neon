#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "api.h"
#include "randombytes.h"

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

static int parse_nonnegative(const char *s, int fallback) {
    char *end = NULL;
    long v;
    if (!s || !*s) return fallback;
    v = strtol(s, &end, 10);
    if (*end != '\0' || v < 0 || v > 2147483647L) return fallback;
    return (int)v;
}

static int cmpd(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return (x > y) - (x < y);
}

static void report(const char* name, double* arr, int n) {
    double sum = 0, minv = arr[0], maxv = arr[0];
    for (int i = 0; i < n; i++) {
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

int main(int argc, char** argv) {
    int rounds = 1000;
    int warmup = 100;
    uint64_t seed = 1;
    if (argc >= 2) rounds = parse_nonnegative(argv[1], rounds);
    if (argc >= 3) warmup = parse_nonnegative(argv[2], warmup);
    if (argc >= 4) {
        char *end = NULL;
        unsigned long long value = strtoull(argv[3], &end, 10);
        if (!argv[3][0] || *end != '\0') return 2;
        seed = (uint64_t)value;
    }
    if (rounds <= 0) {
        fprintf(stderr, "usage: %s [rounds>0] [warmup>=0] [seed]\n", argv[0]);
        return 2;
    }

    uint8_t pk[PQCLEAN_SNTRUP653_CLEAN_CRYPTO_PUBLICKEYBYTES];
    uint8_t sk[PQCLEAN_SNTRUP653_CLEAN_CRYPTO_SECRETKEYBYTES];
    uint8_t ct[PQCLEAN_SNTRUP653_CLEAN_CRYPTO_CIPHERTEXTBYTES];
    uint8_t ss1[PQCLEAN_SNTRUP653_CLEAN_CRYPTO_BYTES];
    uint8_t ss2[PQCLEAN_SNTRUP653_CLEAN_CRYPTO_BYTES];

    double* tk = (double*)malloc((size_t)rounds * sizeof(double));
    double* te = (double*)malloc((size_t)rounds * sizeof(double));
    double* td = (double*)malloc((size_t)rounds * sizeof(double));
    double* te2e = (double*)malloc((size_t)rounds * sizeof(double));
    volatile uint64_t sink = 0;
    if (!tk || !te || !td || !te2e) {
        fprintf(stderr, "allocation failed\n");
        free(tk); free(te); free(td); free(te2e);
        return 3;
    }

    randombytes_seed(seed);

    for (int i = 0; i < warmup; i++) {
        PQCLEAN_SNTRUP653_CLEAN_crypto_kem_keypair(pk, sk);
        PQCLEAN_SNTRUP653_CLEAN_crypto_kem_enc(ct, ss1, pk);
        PQCLEAN_SNTRUP653_CLEAN_crypto_kem_dec(ss2, ct, sk);
        compiler_barrier(ss2);
        sink += ss2[0];
    }

    for (int i = 0; i < rounds; i++) {
        double t0, t1, t0e, t1e;

        /* End-to-end: one clock interval around the whole keygen->encaps->decaps
         * sequence, in addition to the per-operation intervals below. */
        t0e = now_ms();

        t0 = now_ms();
        PQCLEAN_SNTRUP653_CLEAN_crypto_kem_keypair(pk, sk);
        compiler_barrier(pk);
        t1 = now_ms();
        tk[i] = t1 - t0;

        t0 = now_ms();
        PQCLEAN_SNTRUP653_CLEAN_crypto_kem_enc(ct, ss1, pk);
        compiler_barrier(ct);
        t1 = now_ms();
        te[i] = t1 - t0;

        t0 = now_ms();
        PQCLEAN_SNTRUP653_CLEAN_crypto_kem_dec(ss2, ct, sk);
        compiler_barrier(ss2);
        t1 = now_ms();
        td[i] = t1 - t0;

        t1e = now_ms();
        te2e[i] = t1e - t0e;

        if (memcmp(ss1, ss2, PQCLEAN_SNTRUP653_CLEAN_CRYPTO_BYTES) != 0) {
            fprintf(stderr, "KEM mismatch at round %d\n", i);
            return 2;
        }
        sink += ss1[0] + ss2[PQCLEAN_SNTRUP653_CLEAN_CRYPTO_BYTES - 1];
    }

    {
        const char *csv_path = getenv("SNTRUP_BENCH_CSV");
        if (csv_path && *csv_path) {
            FILE *csv = fopen(csv_path, "w");
            if (!csv) {
                perror("fopen SNTRUP_BENCH_CSV");
                free(tk); free(te); free(td); free(te2e);
                return 4;
            }
            fprintf(csv, "implementation,round,keypair_ms,encaps_ms,decaps_ms,total_ms,e2e_ms\n");
            for (int i = 0; i < rounds; i++) {
                fprintf(csv, "%s,%d,%.9f,%.9f,%.9f,%.9f,%.9f\n",
                        bench_impl_name(), i, tk[i], te[i], td[i], tk[i] + te[i] + td[i], te2e[i]);
            }
            if (fclose(csv) != 0) {
                perror("fclose SNTRUP_BENCH_CSV");
                free(tk); free(te); free(td); free(te2e);
                return 4;
            }
            printf("raw_csv=%s\n", csv_path);
        }
    }

    printf("implementation=%s\n", bench_impl_name());
    printf("backend=%s\n", bench_backend());
    printf("KEM correctness: PASS on %d measured rounds after %d warmup rounds\n", rounds, warmup);
    printf("rounds=%d warmup=%d seed=%llu randombytes_calls=%llu randombytes_bytes=%llu\n",
           rounds, warmup, (unsigned long long)seed,
           (unsigned long long)randombytes_calls(),
           (unsigned long long)randombytes_bytes());
    report("keypair", tk, rounds);
    report("encaps", te, rounds);
    report("decaps", td, rounds);
    report("kem_total_e2e", te2e, rounds);
    printf("bench_sink=%llu\n", (unsigned long long)sink);

    free(tk); free(te); free(td); free(te2e);
    return 0;
}
