// SPDX-License-Identifier: MIT
//
// AIMer optimized (AVX2 / AVX-512) hash layer for the OQS packaging: routes both
// the single (x1) and 4-way (x4) SHAKE through the shared liboqs OQS_SHA3 /
// OQS_SHA3_x4 API (which dispatches to the Intel AVX512VL Keccak on capable CPUs;
// see common/sha3/). 128-bit security uses SHAKE128, 192/256-bit use SHAKE256.
//
// The OQS incremental contexts are heap-backed opaque handles, so the AVX2/AVX512
// signing/tree code that used to clone a precomputed context with `memcpy` must
// instead use hash_clone_x4 / hash_ctx_clone_x4 (OQS ctx_clone) and release every
// context. Wrappers below provide those.

#include "hash.h"
#include <stddef.h>
#include <stdint.h>

#if SECURITY_BITS == 128
#define X_inc_init        OQS_SHA3_shake128_inc_init
#define X_inc_absorb      OQS_SHA3_shake128_inc_absorb
#define X_inc_finalize    OQS_SHA3_shake128_inc_finalize
#define X_inc_squeeze     OQS_SHA3_shake128_inc_squeeze
#define X_inc_release     OQS_SHA3_shake128_inc_ctx_release
#define X_inc_clone       OQS_SHA3_shake128_inc_ctx_clone
#define X4_inc_init       OQS_SHA3_shake128_x4_inc_init
#define X4_inc_absorb     OQS_SHA3_shake128_x4_inc_absorb
#define X4_inc_finalize   OQS_SHA3_shake128_x4_inc_finalize
#define X4_inc_squeeze    OQS_SHA3_shake128_x4_inc_squeeze
#define X4_inc_release    OQS_SHA3_shake128_x4_inc_ctx_release
#define X4_inc_clone      OQS_SHA3_shake128_x4_inc_ctx_clone
#else
#define X_inc_init        OQS_SHA3_shake256_inc_init
#define X_inc_absorb      OQS_SHA3_shake256_inc_absorb
#define X_inc_finalize    OQS_SHA3_shake256_inc_finalize
#define X_inc_squeeze     OQS_SHA3_shake256_inc_squeeze
#define X_inc_release     OQS_SHA3_shake256_inc_ctx_release
#define X_inc_clone       OQS_SHA3_shake256_inc_ctx_clone
#define X4_inc_init       OQS_SHA3_shake256_x4_inc_init
#define X4_inc_absorb     OQS_SHA3_shake256_x4_inc_absorb
#define X4_inc_finalize   OQS_SHA3_shake256_x4_inc_finalize
#define X4_inc_squeeze    OQS_SHA3_shake256_x4_inc_squeeze
#define X4_inc_release    OQS_SHA3_shake256_x4_inc_ctx_release
#define X4_inc_clone      OQS_SHA3_shake256_x4_inc_ctx_clone
#endif

// ---- x1 ----
void hash_init(hash_instance *ctx) { X_inc_init(ctx); }
void hash_init_prefix(hash_instance *ctx, uint8_t prefix) { X_inc_init(ctx); X_inc_absorb(ctx, &prefix, 1); }
void hash_update(hash_instance *ctx, const uint8_t *data, size_t len) { X_inc_absorb(ctx, data, len); }
void hash_final(hash_instance *ctx) { X_inc_finalize(ctx); }
void hash_squeeze(hash_instance *ctx, uint8_t *buf, size_t len) { X_inc_squeeze(buf, len, ctx); }
void hash_ctx_release(hash_instance *ctx) { X_inc_release(ctx); }
// copy src's state into dst; dst MUST already be initialized (hash_init).
// Replacement for the old `memcpy(&dst, &src, sizeof(hash_instance))`.
void hash_ctx_clone(hash_instance *dst, const hash_instance *src) { X_inc_clone(dst, src); }

// ---- x4 ----
void hash_init_x4(hash_instance_x4 *ctx) { X4_inc_init(ctx); }
void hash_init_prefix_x4(hash_instance_x4 *ctx, uint8_t prefix)
{
  X4_inc_init(ctx);
  X4_inc_absorb(ctx, &prefix, &prefix, &prefix, &prefix, 1);
}
void hash_update_x4(hash_instance_x4 *ctx, const uint8_t **data, size_t len)
{
  X4_inc_absorb(ctx, data[0], data[1], data[2], data[3], len);
}
void hash_update_x4_1(hash_instance_x4 *ctx, const uint8_t *data, size_t len)
{
  X4_inc_absorb(ctx, data, data, data, data, len);
}
void hash_final_x4(hash_instance_x4 *ctx) { X4_inc_finalize(ctx); }
void hash_squeeze_x4(hash_instance_x4 *ctx, uint8_t **buf, size_t len)
{
  X4_inc_squeeze(buf[0], buf[1], buf[2], buf[3], len, ctx);
}
void hash_ctx_release_x4(hash_instance_x4 *ctx) { X4_inc_release(ctx); }
// copy src's state into dst; dst MUST already be initialized (hash_init_x4).
// Replacement for the old `memcpy(&dst, &src, sizeof(hash_instance_x4))`.
void hash_clone_x4(hash_instance_x4 *dst, const hash_instance_x4 *src) { X4_inc_clone(dst, src); }
