#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "params.h"
#include "crypto_core_multsntrup653.h"
#include "crypto_encode_653xint16.h"

enum { BATCH = 1000 };
static unsigned char inputs[BATCH][2 * p], keys[BATCH][p];
static unsigned char labels[BATCH];
static double elapsed[BATCH];
static uint32_t rng = 20260920;
static uint32_t next32(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static double now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts)) { perror("clock_gettime"); exit(2); }
    return (double)ts.tv_sec * 1e9 + ts.tv_nsec;
}
struct moments { size_t n; double mean, m2; };
static void observe(struct moments *m, double x) {
    double d = x - m->mean;
    m->n++;
    m->mean += d / (double)m->n;
    m->m2 += d * (x - m->mean);
}
static double welch(const struct moments *a, const struct moments *b) {
    double den = a->m2 / ((double)(a->n - 1) * a->n) +
                 b->m2 / ((double)(b->n - 1) * b->n);
    if (den == 0) return a->mean == b->mean ? 0 : INFINITY;
    return (a->mean - b->mean) / sqrt(den);
}
static void prepare(int mode) {
    int16_t f[p];
    for (int i = 0; i < BATCH; i++) labels[i] = (unsigned char)(i & 1);
    for (int i = BATCH - 1; i > 0; i--) {
        int j = (int)(next32() % (unsigned)(i + 1));
        unsigned char v = labels[i]; labels[i] = labels[j]; labels[j] = v;
    }
    for (int i = 0; i < BATCH; i++) {
        int pos[p];
        for (int j = 0; j < p; j++) {
            f[j] = (int16_t)((mode || labels[i]) ? (int)(next32() % q) - q / 2
                                                : ((j * 73) % q) - q / 2);
            keys[i][j] = 0;
            pos[j] = j;
        }
        for (int j = 0; j < w; j++) {
            if (labels[i]) {
                int k = j + (int)(next32() % (unsigned)(p - j));
                int tmp = pos[j]; pos[j] = pos[k]; pos[k] = tmp;
            }
            keys[i][pos[j]] = labels[i] ? ((next32() & 1) ? 1 : 255)
                                        : ((j & 1) ? 1 : 255);
        }
        crypto_encode_pxint16(inputs[i], f);
    }
}
int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: timing_leakage SAMPLES MODE(0|1) RAW.csv\n");
        return 2;
    }
    char *end;
    long samples = strtol(argv[1], &end, 10);
    if (*end || samples < 10000 || samples > 10000000 || samples % BATCH) return 2;
    long mode = strtol(argv[2], &end, 10);
    if (*end || (mode != 0 && mode != 1)) return 2;
    FILE *raw = fopen(argv[3], "wx");
    if (!raw) { perror("timing raw"); return 2; }
    fprintf(raw, "sample,class,ns\n");
    unsigned char out[2 * p];
    struct moments groups[2] = {{0}};
    double max_t = 0;
    prepare((int)mode);
    for (int i = 0; i < BATCH; i++)
        PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653(out, inputs[i], keys[i]);
    for (long done = 0; done < samples; done += BATCH) {
        prepare((int)mode);
        for (int i = 0; i < BATCH; i++) {
            __asm__ __volatile__("" ::: "memory");
            double start = now_ns();
            PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653(out, inputs[i], keys[i]);
            __asm__ __volatile__("" : : "r"(out) : "memory");
            elapsed[i] = now_ns() - start;
        }
        for (int i = 0; i < BATCH; i++) {
            observe(&groups[labels[i]], elapsed[i]);
            fprintf(raw, "%ld,%u,%.0f\n", done + i, labels[i], elapsed[i]);
        }
        if ((done + BATCH) % 10000 == 0) {
            double t = welch(&groups[0], &groups[1]);
            if (!isfinite(t)) { fclose(raw); return 3; }
            if (fabs(t) > max_t) max_t = fabs(t);
            printf("samples=%ld t=%.6f max_abs_t=%.6f\n", done + BATCH, t, max_t);
        }
    }
    if (fclose(raw)) return 2;
    printf("mode=%ld class0_n=%zu class1_n=%zu threshold=5 max_abs_t=%.6f\n",
           mode, groups[0].n, groups[1].n, max_t);
    puts(max_t > 5 ? "timing_screen=ALERT" : "timing_screen=NO_SIGNAL");
    puts("method=raw_first_order_Welch; not dudect; not a constant-time proof");
    return max_t > 5 ? 1 : 0;
}
