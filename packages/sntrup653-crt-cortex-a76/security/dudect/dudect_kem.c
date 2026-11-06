/* dudect harness for sntrup653 decapsulation.
 * Two classes are two independent valid (sk, ct) pairs.
 * This is a statistical timing test, not a formal constant-time proof.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "api.h"
#include "randombytes.h"

#define DUDECT_IMPLEMENTATION
#include "dudect.h"

#define SKB PQCLEAN_SNTRUP653_CLEAN_CRYPTO_SECRETKEYBYTES
#define CTB PQCLEAN_SNTRUP653_CLEAN_CRYPTO_CIPHERTEXTBYTES
#define SSB PQCLEAN_SNTRUP653_CLEAN_CRYPTO_BYTES
#define CHUNK (SKB + CTB)

static uint8_t sk0[SKB], ct0[CTB], sk1[SKB], ct1[CTB];
static uint8_t ss[SSB];

uint8_t do_one_computation(uint8_t *data) {
    const uint8_t *sk = data;
    const uint8_t *ct = data + SKB;
    PQCLEAN_SNTRUP653_CLEAN_crypto_kem_dec(ss, ct, sk);
    return ss[0] ^ ss[SSB - 1];
}

void prepare_inputs(dudect_config_t *c, uint8_t *input_data, uint8_t *classes) {
    for (size_t i = 0; i < c->number_measurements; i++) {
        classes[i] = randombit();
        uint8_t *dst = input_data + i * c->chunk_size;
        if (classes[i] == 0) {
            memcpy(dst, sk0, SKB);
            memcpy(dst + SKB, ct0, CTB);
        } else {
            memcpy(dst, sk1, SKB);
            memcpy(dst + SKB, ct1, CTB);
        }
    }
}

int main(int argc, char **argv) {
    size_t n = 100000;
    if (argc > 1) n = (size_t)strtoull(argv[1], NULL, 10);
    uint8_t pk[PQCLEAN_SNTRUP653_CLEAN_CRYPTO_PUBLICKEYBYTES];
    uint8_t ss0[SSB];
    randombytes_seed(1);
    if (PQCLEAN_SNTRUP653_CLEAN_crypto_kem_keypair(pk, sk0) != 0 ||
        PQCLEAN_SNTRUP653_CLEAN_crypto_kem_enc(ct0, ss0, pk) != 0) return 2;
    if (PQCLEAN_SNTRUP653_CLEAN_crypto_kem_keypair(pk, sk1) != 0 ||
        PQCLEAN_SNTRUP653_CLEAN_crypto_kem_enc(ct1, ss0, pk) != 0) return 3;

    dudect_config_t config = { .chunk_size = CHUNK, .number_measurements = n };
    dudect_ctx_t ctx;
    dudect_init(&ctx, &config);
    dudect_state_t state = DUDECT_NO_LEAKAGE_EVIDENCE_YET;
    int chunks = (argc > 2) ? atoi(argv[2]) : 5;
    for (int i = 0; i < chunks && state == DUDECT_NO_LEAKAGE_EVIDENCE_YET; i++)
        state = dudect_main(&ctx);
    dudect_free(&ctx);
    printf("dudect_state=%d\n", (int)state);
    return (int)state;
}
