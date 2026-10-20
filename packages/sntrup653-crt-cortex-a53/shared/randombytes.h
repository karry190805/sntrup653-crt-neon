#ifndef RANDOMBYTES_H
#define RANDOMBYTES_H
#include <stddef.h>
#include <stdint.h>
void randombytes(uint8_t *out, size_t outlen);
void randombytes_seed(uint64_t seed);
uint64_t randombytes_calls(void);
uint64_t randombytes_bytes(void);
#endif
