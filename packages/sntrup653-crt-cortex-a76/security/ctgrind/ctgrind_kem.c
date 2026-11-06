/* ctgrind-style harness: mark the secret key as undefined and run decapsulation
 * under Valgrind Memcheck.  Secret-dependent branches or addresses are reported
 * by Valgrind.  This is a taint check, not a formal constant-time proof.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "api.h"
#include "randombytes.h"
#include <valgrind/memcheck.h>

#define SKB PQCLEAN_SNTRUP653_CLEAN_CRYPTO_SECRETKEYBYTES
#define CTB PQCLEAN_SNTRUP653_CLEAN_CRYPTO_CIPHERTEXTBYTES
#define PKB PQCLEAN_SNTRUP653_CLEAN_CRYPTO_PUBLICKEYBYTES
#define SSB PQCLEAN_SNTRUP653_CLEAN_CRYPTO_BYTES

int main(void) {
    uint8_t pk[PKB], sk[SKB], ct[CTB], ss[SSB];
    uint8_t ss0[SSB];
    randombytes_seed(1);
    if (PQCLEAN_SNTRUP653_CLEAN_crypto_kem_keypair(pk, sk) != 0 ||
        PQCLEAN_SNTRUP653_CLEAN_crypto_kem_enc(ct, ss0, pk) != 0) return 2;
    /* The ciphertext and public key are public; only sk is secret. */
    VALGRIND_MAKE_MEM_UNDEFINED(sk, SKB);
    if (PQCLEAN_SNTRUP653_CLEAN_crypto_kem_dec(ss, ct, sk) != 0) return 3;
    VALGRIND_MAKE_MEM_DEFINED(ss, SSB);
    return (ss[0] == 0xff) ? 1 : 0;
}
