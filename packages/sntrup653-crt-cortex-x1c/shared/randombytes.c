#include "randombytes.h"
#include <stdint.h>
#include <stddef.h>

/* Deterministic benchmark-only randombytes.
 * This is for repeatable timing/correctness tests; not for production use. */
static uint64_t rb_state = 1;
static uint64_t rb_calls = 0;
static uint64_t rb_bytes = 0;

void randombytes_seed(uint64_t seed) {
    rb_state = seed ? seed : UINT64_C(1);
    rb_calls = 0;
    rb_bytes = 0;
}

uint64_t randombytes_calls(void) { return rb_calls; }
uint64_t randombytes_bytes(void) { return rb_bytes; }

static uint64_t splitmix64_next(void) {
    uint64_t z = (rb_state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

void randombytes(uint8_t *out, size_t outlen) {
    size_t i = 0;
    rb_calls++;
    rb_bytes += (uint64_t)outlen;
    while (i < outlen) {
        uint64_t x = splitmix64_next();
        size_t j;
        for (j = 0; j < 8 && i < outlen; j++, i++) {
            out[i] = (uint8_t)(x & 0xff);
            x >>= 8;
        }
    }
}
