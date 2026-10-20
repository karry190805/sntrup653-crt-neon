#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "randombytes.h"

static int parse_positive(const char *s, int fallback) {
    char *end = NULL;
    long v;
    if (!s || !*s) return fallback;
    v = strtol(s, &end, 10);
    if (*end != '\0' || v <= 0 || v > 2147483647L) return fallback;
    return (int)v;
}

static int write_exact(FILE *out, const void *buf, size_t len) {
    return fwrite(buf, 1, len, out) == len ? 0 : -1;
}

int main(int argc, char **argv) {
    const char *path;
    int cases = 100;
    uint64_t seed = 1;
    FILE *out;
    uint8_t pk[PQCLEAN_SNTRUP653_CLEAN_CRYPTO_PUBLICKEYBYTES];
    uint8_t sk[PQCLEAN_SNTRUP653_CLEAN_CRYPTO_SECRETKEYBYTES];
    uint8_t ct[PQCLEAN_SNTRUP653_CLEAN_CRYPTO_CIPHERTEXTBYTES];
    uint8_t ss_enc[PQCLEAN_SNTRUP653_CLEAN_CRYPTO_BYTES];
    uint8_t ss_dec[PQCLEAN_SNTRUP653_CLEAN_CRYPTO_BYTES];
    int i;

    if (argc < 2) {
        fprintf(stderr, "usage: %s OUTPUT.bin [cases>0] [seed]\n", argv[0]);
        return 2;
    }
    path = argv[1];
    if (argc >= 3) cases = parse_positive(argv[2], cases);
    if (argc >= 4) {
        char *end = NULL;
        unsigned long long value = strtoull(argv[3], &end, 10);
        if (!argv[3][0] || *end != '\0') return 2;
        seed = (uint64_t)value;
    }
    randombytes_seed(seed);

    out = fopen(path, "wb");
    if (!out) {
        perror("fopen transcript");
        return 3;
    }

    for (i = 0; i < cases; i++) {
        if (PQCLEAN_SNTRUP653_CLEAN_crypto_kem_keypair(pk, sk) != 0 ||
            PQCLEAN_SNTRUP653_CLEAN_crypto_kem_enc(ct, ss_enc, pk) != 0 ||
            PQCLEAN_SNTRUP653_CLEAN_crypto_kem_dec(ss_dec, ct, sk) != 0) {
            fprintf(stderr, "KEM call failed at case %d\n", i);
            fclose(out);
            return 4;
        }
        if (memcmp(ss_enc, ss_dec, sizeof(ss_enc)) != 0) {
            fprintf(stderr, "KEM shared-secret mismatch at case %d\n", i);
            fclose(out);
            return 5;
        }
        if (write_exact(out, pk, sizeof(pk)) || write_exact(out, sk, sizeof(sk)) ||
            write_exact(out, ct, sizeof(ct)) || write_exact(out, ss_enc, sizeof(ss_enc))) {
            fprintf(stderr, "transcript write failed at case %d\n", i);
            fclose(out);
            return 6;
        }
        /* Exercise implicit rejection without changing the timed benchmark. */
        ct[(unsigned)i % sizeof(ct)] ^= (uint8_t)(1u << ((unsigned)i % 8));
        if (PQCLEAN_SNTRUP653_CLEAN_crypto_kem_dec(ss_dec, ct, sk) != 0 ||
            memcmp(ss_enc, ss_dec, sizeof(ss_enc)) == 0 ||
            write_exact(out, ss_dec, sizeof(ss_dec))) {
            fprintf(stderr, "modified-ciphertext test failed at case %d\n", i);
            fclose(out);
            return 8;
        }
    }

    if (fclose(out) != 0) {
        perror("fclose transcript");
        return 7;
    }
    printf("KEM transcript generated: cases=%d seed=%llu randombytes_calls=%llu randombytes_bytes=%llu file=%s\n",
           cases, (unsigned long long)seed,
           (unsigned long long)randombytes_calls(),
           (unsigned long long)randombytes_bytes(), path);
    return 0;
}
