// SPDX-License-Identifier: MIT

#ifndef HASH_H
#define HASH_H

#include "params.h"
#include <oqs/sha3.h>
#include <oqs/sha3x4.h>
#include <stddef.h>
#include <stdint.h>

// AIMER_HASH_PREFIX_0 (per-parameter-set domain separator) is supplied via -D
// in the Makefile (0x00,0x10,0x20,0x30,0x40,0x50 for 128f..256s).
static const uint8_t HASH_PREFIX_0 = (uint8_t)AIMER_HASH_PREFIX_0;
static const uint8_t HASH_PREFIX_1 = 1;
static const uint8_t HASH_PREFIX_2 = 2;
static const uint8_t HASH_PREFIX_3 = 3;
static const uint8_t HASH_PREFIX_4 = 4;
static const uint8_t HASH_PREFIX_5 = 5;

// Hash contexts are the shared OQS_SHA3 opaque handles (dispatch to Intel
// AVX512VL Keccak when available). x1 + 4-way (x4).
#if SECURITY_BITS == 128
typedef OQS_SHA3_shake128_inc_ctx hash_instance;
typedef OQS_SHA3_shake128_x4_inc_ctx hash_instance_x4;
#else
typedef OQS_SHA3_shake256_inc_ctx hash_instance;
typedef OQS_SHA3_shake256_x4_inc_ctx hash_instance_x4;
#endif

#define hash_init AIMER_NAMESPACE(hash_init)
void hash_init(hash_instance *ctx);
#define hash_init_prefix AIMER_NAMESPACE(hash_init_prefix)
void hash_init_prefix(hash_instance *ctx, uint8_t prefix);
#define hash_update AIMER_NAMESPACE(hash_update)
void hash_update(hash_instance *ctx, const uint8_t *data, size_t data_len);
#define hash_final AIMER_NAMESPACE(hash_final)
void hash_final(hash_instance *ctx);
#define hash_squeeze AIMER_NAMESPACE(hash_squeeze)
void hash_squeeze(hash_instance *ctx, uint8_t *buffer, size_t buffer_len);
#define hash_ctx_release AIMER_NAMESPACE(hash_ctx_release)
void hash_ctx_release(hash_instance *ctx);
#define hash_ctx_clone AIMER_NAMESPACE(hash_ctx_clone)
void hash_ctx_clone(hash_instance *dst, const hash_instance *src);

#define hash_init_x4 AIMER_NAMESPACE(hash_init_x4)
void hash_init_x4(hash_instance_x4 *ctx);
#define hash_init_prefix_x4 AIMER_NAMESPACE(hash_init_prefix_x4)
void hash_init_prefix_x4(hash_instance_x4 *ctx, uint8_t prefix);
#define hash_update_x4 AIMER_NAMESPACE(hash_update_x4)
void hash_update_x4(hash_instance_x4 *ctx, const uint8_t **data, size_t data_len);
#define hash_update_x4_1 AIMER_NAMESPACE(hash_update_x4_1)
void hash_update_x4_1(hash_instance_x4 *ctx, const uint8_t *data, size_t data_len);
#define hash_final_x4 AIMER_NAMESPACE(hash_final_x4)
void hash_final_x4(hash_instance_x4 *ctx);
#define hash_squeeze_x4 AIMER_NAMESPACE(hash_squeeze_x4)
void hash_squeeze_x4(hash_instance_x4 *ctx, uint8_t **buffer, size_t buffer_len);
#define hash_ctx_release_x4 AIMER_NAMESPACE(hash_ctx_release_x4)
void hash_ctx_release_x4(hash_instance_x4 *ctx);
#define hash_clone_x4 AIMER_NAMESPACE(hash_clone_x4)
void hash_clone_x4(hash_instance_x4 *dst, const hash_instance_x4 *src);

#endif // HASH_H
