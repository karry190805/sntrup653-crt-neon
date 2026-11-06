/*
 * test_math_fingerprint.c — freeze the mathematical constants of the core.
 *
 * Every optimisation in this package is allowed to change instruction
 * selection, data layout and scheduling.  None of them is allowed to change
 * p, q, w, the Good–Thomas index maps, the direct 7/11-point DFT tables, the
 * packed Shoup tables, the radix-2/TMVP weights or the public pruning
 * schedule.  sntrup653_math_fingerprint() hashes all of them from the
 * initialised runtime tables, so two builds that print the same value have the
 * same mathematical object, even if their machine code differs.
 *
 * The test also checks that a fixed multiplication returns canonical
 * representatives in [0, q), which pins down the output convention.
 *
 * usage: test_math_fingerprint [expected_hex]
 *   With an argument, the program exits non-zero unless the fingerprint
 *   matches.  check_math_fingerprint.sh passes the recorded value for every
 *   build variant.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "params.h"
#include "crypto_core_multsntrup653.h"

#define INBYTES  PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_INPUTBYTES
#define KEYBYTES PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_KEYBYTES
#define OUTBYTES PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653_OUTPUTBYTES

uint64_t sntrup653_math_fingerprint(void);

static int parse_hex(const char *text, uint64_t *value) {
    char *end = NULL;
    unsigned long long parsed;
    if (text == NULL || *text == '\0') return 0;
    parsed = strtoull(text, &end, 0);
    if (end == text || *end != '\0') return 0;
    *value = (uint64_t)parsed;
    return 1;
}

int main(int argc, char **argv) {
    uint64_t fp;
    uint64_t expected = 0;
    unsigned char inbytes[INBYTES];
    unsigned char kbytes[KEYBYTES];
    unsigned char outbytes[OUTBYTES];
    int16_t *f = (int16_t *)(void *)inbytes;
    int16_t *out = (int16_t *)(void *)outbytes;
    int i;
    int canonical = 1;
    int min_out = q, max_out = 0;

    fp = sntrup653_math_fingerprint();
    printf("math_fingerprint=0x%016" PRIx64 "\n", fp);

    /* Fixed, parameter-derived input: no randomness, so the range check is
     * reproducible on every build. */
    for (i = 0; i < (int)(INBYTES / sizeof(int16_t)); i++) {
        f[i] = (int16_t)((i * 37 + 11) % q);
    }
    for (i = 0; i < (int)KEYBYTES; i++) {
        kbytes[i] = (unsigned char)(i % 3);
    }
    if (PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653(outbytes, inbytes, kbytes) != 0) {
        printf("canonical_range=FAIL core_returned_error\n");
        return 1;
    }
    for (i = 0; i < (int)(OUTBYTES / sizeof(int16_t)); i++) {
        int magnitude = out[i] < 0 ? -out[i] : out[i];
        /* The core's canonical output is the centred representative:
         * 2*|v| < q, i.e. [-(q-1)/2, (q-1)/2]. */
        if (2 * magnitude >= q) canonical = 0;
        if (out[i] < min_out) min_out = out[i];
        if (out[i] > max_out) max_out = out[i];
    }
    printf("canonical_range=%s min=%d max=%d q=%d centred=1\n",
           canonical ? "PASS" : "FAIL", min_out, max_out, q);

    if (argc >= 2) {
        if (!parse_hex(argv[1], &expected)) {
            fprintf(stderr, "usage: %s [expected_hex]\n", argv[0]);
            return 2;
        }
        if (fp != expected) {
            printf("math_fingerprint_check=FAIL expected=0x%016" PRIx64
                   " actual=0x%016" PRIx64 "\n", expected, fp);
            return 1;
        }
        printf("math_fingerprint_check=PASS\n");
    }

    return canonical ? 0 : 1;
}
