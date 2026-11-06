/*
 * Exhaustive single-bit flip consistency test for the KEM ciphertext.
 *
 * For each generated ciphertext and each of its 897*8 bits, flip exactly one
 * bit, run decapsulation, and record the return code, whether the recovered
 * shared secret equals the original one (implicit accept), and the 32-byte
 * output.  The baseline and optimized binaries are seeded identically, so
 * their transcript files can be compared byte for byte.
 *
 * This is an error-path consistency test, not a security proof: a flipped
 * ciphertext may still be accepted with tiny probability.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "randombytes.h"

#define CTBYTES PQCLEAN_SNTRUP653_CLEAN_CRYPTO_CIPHERTEXTBYTES
#define SSBYTES PQCLEAN_SNTRUP653_CLEAN_CRYPTO_BYTES
#define PKBYTES PQCLEAN_SNTRUP653_CLEAN_CRYPTO_PUBLICKEYBYTES
#define SKBYTES PQCLEAN_SNTRUP653_CLEAN_CRYPTO_SECRETKEYBYTES

static int write_exact(FILE *f, const void *p, size_t n) {
    return fwrite(p, 1, n, f) == n ? 0 : -1;
}

int main(int argc, char **argv) {
    const char *path;
    int batches = 2;
    uint64_t seed = 1;
    FILE *out;
    uint8_t pk[PKBYTES], sk[SKBYTES], ct[CTBYTES], ct2[CTBYTES];
    uint8_t ss0[SSBYTES], ss[SSBYTES];
    uint32_t magic = 0x53424631u; /* "SBF1" */
    uint32_t bits = CTBYTES * 8u;
    uint32_t b;
    unsigned long long total = 0, accepted = 0, nonzero_rc = 0;
    int i;

    if (argc < 2) {
        fprintf(stderr, "usage: %s OUT.bin [batches>0] [seed]\n", argv[0]);
        return 2;
    }
    path = argv[1];
    if (argc >= 3) {
        char *end = NULL;
        long v = strtol(argv[2], &end, 10);
        if (!argv[2][0] || *end != '\0' || v <= 0) return 2;
        batches = (int)v;
    }
    if (argc >= 4) {
        char *end = NULL;
        unsigned long long v = strtoull(argv[3], &end, 10);
        if (!argv[3][0] || *end != '\0') return 2;
        seed = v;
    }

    out = fopen(path, "wb");
    if (!out) { perror("fopen"); return 3; }
    if (write_exact(out, &magic, 4) || write_exact(out, &bits, 4) ||
        write_exact(out, &batches, 4)) return 4;
    randombytes_seed(seed);

    for (b = 0; b < (uint32_t)batches; b++) {
        int rc0;
        if (PQCLEAN_SNTRUP653_CLEAN_crypto_kem_keypair(pk, sk) != 0 ||
            PQCLEAN_SNTRUP653_CLEAN_crypto_kem_enc(ct, ss0, pk) != 0) {
            fprintf(stderr, "KEM setup failed at batch %u\n", b);
            fclose(out); return 5;
        }
        rc0 = PQCLEAN_SNTRUP653_CLEAN_crypto_kem_dec(ss, ct, sk);
        if (rc0 != 0 || memcmp(ss, ss0, SSBYTES) != 0) {
            fprintf(stderr, "valid ciphertext rejected at batch %u\n", b);
            fclose(out); return 6;
        }
        if (write_exact(out, ct, CTBYTES) || write_exact(out, ss0, SSBYTES) ||
            write_exact(out, &rc0, sizeof(rc0))) return 7;

        for (i = 0; i < (int)bits; i++) {
            int rc;
            int acc;
            memcpy(ct2, ct, CTBYTES);
            ct2[i >> 3] ^= (uint8_t)(1u << (i & 7));
            rc = PQCLEAN_SNTRUP653_CLEAN_crypto_kem_dec(ss, ct2, sk);
            acc = (memcmp(ss, ss0, SSBYTES) == 0);
            if (write_exact(out, &i, sizeof(i)) ||
                write_exact(out, &rc, sizeof(rc)) ||
                write_exact(out, &acc, sizeof(acc)) ||
                write_exact(out, ss, SSBYTES)) return 8;
            total++;
            accepted += (unsigned)acc;
            nonzero_rc += (rc != 0);
        }
    }
    if (fclose(out) != 0) { perror("fclose"); return 9; }
    printf("single_bit_flip batches=%d bits_per_ct=%u total=%llu accepted=%llu rejected=%llu nonzero_rc=%llu file=%s\n",
           batches, bits, total, accepted, total - accepted, nonzero_rc, path);
    return 0;
}
