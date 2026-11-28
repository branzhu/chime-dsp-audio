#pragma once
#include <stddef.h>
#include <stdint.h>
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 0x02000009
int psa_crypto_init(void);
int psa_hash_compute(unsigned alg,const uint8_t *input,size_t length,uint8_t *out,size_t capacity,size_t *out_length);
