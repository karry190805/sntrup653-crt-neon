/*
 * test_endtoend_oracle.c — first-principles oracle for the whole core.
 *
 * Everything else in this package compares the delivered core against the
 * untrusted-but-unchanged PQClean reference.  That chain has one structural
 * weakness: if the reference itself were wrong, or if a shared helper were
 * wrong, both sides would agree on the wrong answer.  This test removes that
 * dependency.  It computes
 *
 *     c = A * B  mod (q, x^653 - x - 1)          A = f, B = g
 *
 * directly from the mathematical definition:
 *
 *     1. ordinary integer convolution  d[k] = sum_{i+j=k} f[i] * g[j]
 *        (exact in int64; |f| <= 2310, |g| <= 1, so |d| <= 653*2310 < 2^21)
 *     2. fold with x^653 = x + 1, i.e. for i = 653 + k the monomial x^i
 *        contributes x^(k+1) + x^k, giving
 *            c[r] = d[r] + [r >= 1] d[652 + r] + [r <= 651] d[653 + r]     (r<653)
 *     3. centre into (-q/2, q/2]
 *
 * The encoding and decoding of the 16-bit little-endian coefficient vectors is
 * done here in three lines each; no file from baseline/ or optimized/ other
 * than the public prototype is used.  The byte layout is pinned by
 * params.h-free constants derived from p = 653 and q = 4621, and asserted
 * against params.h at compile time.
 *
 * Case sequence is fixed and shared with oracle_reference.py, which recomputes
 * the same 1971-case aggregate independently in Python.  If the two agree and
 * the core matches the oracle on every case, the delivered core realises the
 * documented ring product, and it does so without the reference implementation
 * being trusted.
 *
 * usage: test_endtoend_oracle [mode] [count] [seed]
 *   mode = fixed   fixed boundary/bilinear-basis cases only
 *   mode = axes    every f-axis and g-axis monomial               (1959 cases)
 *   mode = full    exhaustive 653 x 653 monomial pairs         (426409 cases)
 *   mode = random  count random sparse-short-vector cases
 *   mode = all     fixed + axes + count random                      [default]
 *   mode = every   fixed + axes + full + count random
 *
 *   count defaults to 20000, seed to 1.
 *
 * The first 1971 cases (12 fixed + 1959 axes) are always executed first and
 * their aggregate FNV-1a-64 hash is printed as
 *     cases_hash=<16 hex digits>
 * check_oracle_hash.py compares that value with expected_oracle_hashes.txt,
 * which was produced by the independent Python model.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "params.h"
#include "crypto_core_multsntrup653.h"

/* The two candidate implementations.  build.sh renames the entry symbol of
 * each object file, exactly as it does for test_core_equiv.c. */
#ifndef BASELINE_CORE
#define BASELINE_CORE baseline_crypto_core_multsntrup653
#endif
#ifndef OPTIMIZED_CORE
#define OPTIMIZED_CORE optimized_crypto_core_multsntrup653
#endif

int BASELINE_CORE(unsigned char *outbytes, const unsigned char *inbytes, const unsigned char *kbytes);
int OPTIMIZED_CORE(unsigned char *outbytes, const unsigned char *inbytes, const unsigned char *kbytes);

#define INBYTES  PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_INPUTBYTES
#define KEYBYTES PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES
#define OUTBYTES PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_OUTPUTBYTES

/* Cross-check the dimensions this file hardcodes against the public headers. */
typedef char assert_p_is_653[(p == 653) ? 1 : -1];
typedef char assert_q_is_4621[(q == 4621) ? 1 : -1];
typedef char assert_inbytes[(INBYTES == 2 * p) ? 1 : -1];
typedef char assert_outbytes[(OUTBYTES == 2 * p) ? 1 : -1];
typedef char assert_keybytes[(KEYBYTES == p) ? 1 : -1];

#define FIXED_CASES 12
#define AXIS_CASES  (3 * p)          /* p f-axes + p g(+)-axes + p g(-)-axes */
#define HASH_CASES  (FIXED_CASES + AXIS_CASES)

/* ------------------------------------------------------------------ encoding
 * PQClean packs the coefficient vector as 653 little-endian uint16 values and
 * the short vector as 653 bytes whose bit 0 selects the sign.  Both are
 * re-implemented here so that no shared helper participates in the oracle. */
static void put_le16(unsigned char *s, int v) {
    uint16_t u = (uint16_t)v;
    s[0] = (unsigned char)(u & 0xffu);
    s[1] = (unsigned char)((u >> 8) & 0xffu);
}

static int get_le16(const unsigned char *s) {
    return (int)(int16_t)((uint16_t)s[0] | ((uint16_t)s[1] << 8));
}

static void encode_f(unsigned char inbytes[INBYTES], const int f[p]) {
    int i;
    for (i = 0; i < p; i++) put_le16(inbytes + 2 * i, f[i]);
}

/* core reads  gi0 = kbytes[i] & 1;  g = gi0 - (gi & (gi0 << 1))  */
static void encode_g(unsigned char kbytes[KEYBYTES], const int g[p]) {
    int i;
    for (i = 0; i < p; i++) kbytes[i] = (unsigned char)(g[i] > 0 ? 1 : (g[i] < 0 ? 255 : 0));
}

/* ------------------------------------------------------------------- oracle */
static void oracle_product(int c[p], const int f[p], const int g[p]) {
    static int64_t d[2 * p - 1];
    int i, j, r;

    memset(d, 0, sizeof(d));
    for (i = 0; i < p; i++) {
        int64_t fi = f[i];
        if (fi == 0) continue;
        for (j = 0; j < p; j++) {
            if (g[j]) d[i + j] += fi * (int64_t)g[j];
        }
    }
    for (r = 0; r < p; r++) {
        int64_t acc = d[r];
        if (r >= 1) acc += d[p - 1 + r];          /* 652 + r */
        if (r <= p - 2) acc += d[p + r];          /* 653 + r */
        acc %= q;
        if (acc < 0) acc += q;
        if (acc > q / 2) acc -= q;
        c[r] = (int)acc;
    }
}

/* ---------------------------------------------------------------------- rng */
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

/* ------------------------------------------------------------------- cases */
/* 12 fixed cases.  Kept tiny and explicit so that oracle_reference.py can
 * reproduce the list verbatim. */
static void make_fixed_case(int id, int f[p], int g[p]) {
    int i;
    memset(f, 0, sizeof(int) * (size_t)p);
    memset(g, 0, sizeof(int) * (size_t)p);
    switch (id) {
    case 0:                                       /* f = 0, g = 0 */
        break;
    case 1:                                       /* f = 0, g = +1 on 0..287 */
        for (i = 0; i < w; i++) g[i] = 1;
        break;
    case 2:                                       /* f = 1, g = 0 */
        for (i = 0; i < p; i++) f[i] = 1;
        break;
    case 3:                                       /* f = 1, g = +1 on 0..287 */
        for (i = 0; i < p; i++) f[i] = 1;
        for (i = 0; i < w; i++) g[i] = 1;
        break;
    case 4:                                       /* extreme positive */
        for (i = 0; i < p; i++) f[i] = q / 2;
        for (i = 0; i < w; i++) g[i] = 1;
        break;
    case 5:                                       /* extreme negative */
        for (i = 0; i < p; i++) f[i] = -(q / 2);
        for (i = 0; i < w; i++) g[i] = -1;
        break;
    case 6:                                       /* D = 1     (r = 0 path) */
        f[0] = 1;
        g[0] = 1;
        break;
    case 7:                                       /* D = x^1304 (r = 652 path) */
        f[p - 1] = 1;
        g[p - 1] = 1;
        break;
    case 8:                                       /* D = x^1232 (r = 579 path) */
        f[p - 1] = 1;
        g[580] = 1;
        break;
    case 9:                                       /* D = x^1231 (main boundary) */
        f[p - 1] = 1;
        g[579] = 1;
        break;
    case 10:                                      /* alternating signs, dense */
        for (i = 0; i < p; i++) f[i] = (i & 1) ? -(q / 2) : (q / 2);
        for (i = 0; i < w; i++) g[i] = 1;
        break;
    default:                                      /* D = -1 */
        f[0] = 1;
        g[0] = -1;
        break;
    }
}

/* Axes: f = e_i with g = e_0; f = e_0 with g = +-e_i.  3p cases. */
static void make_axis_case(int id, int f[p], int g[p]) {
    int i = id % p;
    int which = id / p;
    memset(f, 0, sizeof(int) * (size_t)p);
    memset(g, 0, sizeof(int) * (size_t)p);
    if (which == 0) {
        f[i] = 1;
        g[0] = 1;
    } else {
        f[0] = 1;
        g[i] = (which == 1) ? 1 : -1;
    }
}

/* Random cases: dense f in [-q/2, q/2], g with exactly w entries of +-1. */
static void make_random_case(int f[p], int g[p]) {
    int i;
    static int pos[p];
    for (i = 0; i < p; i++) {
        f[i] = (int)(xorshift32() % (uint32_t)q) - q / 2;
        g[i] = 0;
        pos[i] = i;
    }
    for (i = 0; i < w; i++) {
        int j = i + (int)(xorshift32() % (uint32_t)(p - i));
        int t = pos[i];
        pos[i] = pos[j];
        pos[j] = t;
    }
    for (i = 0; i < w; i++) g[pos[i]] = (xorshift32() & 1u) ? 1 : -1;
}

/* ------------------------------------------------------------ enumeration */
/* Modes for the exhaustive monomial sweep: f = e_i, g = e_j. */
static void make_monomial_case(int i, int j, int f[p], int g[p]) {
    memset(f, 0, sizeof(int) * (size_t)p);
    memset(g, 0, sizeof(int) * (size_t)p);
    f[i] = 1;
    g[j] = 1;
}

/* ------------------------------------------------------------------- hash */
#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME  1099511628211ULL

static uint64_t fnv_update(uint64_t h, const unsigned char *data, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= (uint64_t)data[i];
        h *= FNV_PRIME;
    }
    return h;
}

/* ------------------------------------------------------------------- main */
static int failures = 0;
static long long checked = 0;
static uint64_t hash_opt = FNV_OFFSET;
static uint64_t hash_base = FNV_OFFSET;
static uint64_t hash_oracle = FNV_OFFSET;

static void report_mismatch(const char *impl, const char *kind, int a, int b, int r,
                            int got, int want) {
    if (failures < 20) {
        fprintf(stderr,
                "FAIL %s %s=(%d,%d) coeff=%d got=%d want=%d\n",
                impl, kind, a, b, r, got, want);
    }
}

static int run_one(const char *kind, int a, int b, const int f[p], const int g[p],
                   int check_baseline) {
    unsigned char inbytes[INBYTES];
    unsigned char kbytes[KEYBYTES];
    unsigned char out[OUTBYTES];
    int expect[p];
    int i;
    int bad = 0;

    encode_f(inbytes, f);
    encode_g(kbytes, g);
    oracle_product(expect, f, g);

    memset(out, 0xa5, sizeof(out));
    if (OPTIMIZED_CORE(out, inbytes, kbytes) != 0) {
        fprintf(stderr, "FAIL %s: optimized core returned nonzero\n", kind);
        failures++;
        return 1;
    }
    for (i = 0; i < p; i++) {
        int got = get_le16(out + 2 * i);
        if (got != expect[i]) {
            report_mismatch("optimized", kind, a, b, i, got, expect[i]);
            bad = 1;
        }
    }
    if (bad) failures++;

    hash_opt = fnv_update(hash_opt, out, sizeof(out));
    {
        /* Feed the oracle's own bytes so the reference hash is comparable. */
        unsigned char ob[OUTBYTES];
        for (i = 0; i < p; i++) put_le16(ob + 2 * i, expect[i]);
        hash_oracle = fnv_update(hash_oracle, ob, sizeof(ob));
    }

    if (check_baseline) {
        memset(out, 0x5a, sizeof(out));
        if (BASELINE_CORE(out, inbytes, kbytes) != 0) {
            fprintf(stderr, "FAIL %s: baseline core returned nonzero\n", kind);
            failures++;
            return 1;
        }
        for (i = 0; i < p; i++) {
            int got = get_le16(out + 2 * i);
            if (got != expect[i]) {
                report_mismatch("baseline", kind, a, b, i, got, expect[i]);
                bad = 1;
            }
        }
        if (bad) failures++;
        hash_base = fnv_update(hash_base, out, sizeof(out));
    }

    checked++;
    return bad;
}

static int parse_int_arg(const char *s, int fallback) {
    char *end = NULL;
    long v;
    if (!s || !*s) return fallback;
    v = strtol(s, &end, 10);
    if (*end != '\0') return fallback;
    if (v < 0 || v > 2000000000L) return fallback;
    return (int)v;
}

int main(int argc, char **argv) {
    const char *mode = (argc >= 2) ? argv[1] : "all";
    int count = (argc >= 3) ? parse_int_arg(argv[2], 20000) : 20000;
    uint32_t seed = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 10) : 1u;
    int f[p];
    int g[p];
    int i, j, id;
    int want_fixed, want_axes, want_full, want_random;
    int hash_barrier;

    if (count < 0) count = 0;
    if (strcmp(mode, "fixed") == 0) {
        want_fixed = 1; want_axes = 0; want_full = 0; want_random = 0;
    } else if (strcmp(mode, "axes") == 0) {
        want_fixed = 0; want_axes = 1; want_full = 0; want_random = 0;
    } else if (strcmp(mode, "full") == 0) {
        want_fixed = 0; want_axes = 0; want_full = 1; want_random = 0;
    } else if (strcmp(mode, "random") == 0) {
        want_fixed = 0; want_axes = 0; want_full = 0; want_random = 1;
    } else if (strcmp(mode, "every") == 0) {
        want_fixed = 1; want_axes = 1; want_full = 1; want_random = 1;
    } else if (strcmp(mode, "all") == 0) {
        want_fixed = 1; want_axes = 1; want_full = 0; want_random = 1;
    } else {
        fprintf(stderr, "usage: %s [fixed|axes|full|random|all|every] [count] [seed]\n", argv[0]);
        return 2;
    }

    rng_seed(seed);

    /* ---- part 1: the shared 1971-case hash stream ---------------------- */
    if (want_fixed) {
        for (id = 0; id < FIXED_CASES; id++) {
            make_fixed_case(id, f, g);
            run_one("fixed", id, 0, f, g, 1);
        }
    }
    if (want_axes) {
        for (i = 0; i < AXIS_CASES; i++) {
            make_axis_case(i, f, g);
            run_one("axis", i, 0, f, g, 1);
        }
    }
    hash_barrier = (want_fixed && want_axes) ? 1 : 0;
    if (hash_barrier) {
        printf("cases_hash=%016llx\n", (unsigned long long)hash_opt);
        if (hash_opt != hash_oracle) {
            fprintf(stderr, "FAIL: core hash 0x%016llx differs from oracle hash 0x%016llx\n",
                    (unsigned long long)hash_opt, (unsigned long long)hash_oracle);
            failures++;
        }
        if (hash_opt != hash_base) {
            fprintf(stderr, "FAIL: baseline hash 0x%016llx differs from optimized hash 0x%016llx\n",
                    (unsigned long long)hash_base, (unsigned long long)hash_opt);
            failures++;
        }
    }

    /* ---- part 2: exhaustive monomial sweep ----------------------------- */
    if (want_full) {
        long long pairs = 0;
        for (i = 0; i < p; i++) {
            for (j = 0; j < p; j++) {
                make_monomial_case(i, j, f, g);
                /* The reference is exercised once per i on the first row only;
                 * the equality with the oracle is what matters. */
                run_one("monomial", i, j, f, g, (j == 0));
                pairs++;
            }
        }
        printf("monomial_pairs_checked=%lld\n", pairs);
    }

    /* ---- part 3: random cases ------------------------------------------ */
    if (want_random) {
        for (i = 0; i < count; i++) {
            make_random_case(f, g);
            run_one("random", i, 0, f, g, (i % 16 == 0));
        }
    }

    if (failures) {
        fprintf(stderr, "endtoend oracle: FAIL failures=%d cases=%lld\n", failures, checked);
        return 1;
    }
    printf("mode=%s seed=%u random_cases=%d\n", mode, seed, want_random ? count : 0);
    printf("endtoend oracle: PASS cases=%lld core_vs_oracle=identical\n", checked);
    printf("all_cases_hash=%016llx\n", (unsigned long long)hash_opt);
    /* Only the 1971-case prefix is compared across implementations; the
     * baseline is sampled on the exhaustive and random sweeps, so its counter
     * is reported for information and is not required to track the core. */
    (void)hash_base;
    return 0;
}
