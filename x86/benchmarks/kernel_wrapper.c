// SPDX-License-Identifier: MIT
// Compiled once per parameter set/backend to expose type-safe kernel wrappers.

#include "aim3.h"
#include "field.h"
#include "params.h"
#include "sign.h"

#include <stddef.h>
#include <stdint.h>

#define BENCH_NAME_(name) AIMER_NAMESPACE(bench_##name)
#define BENCH_NAME(name) BENCH_NAME_(name)

#if defined(BENCH_BATCH)
#define gf_sqr_N AIMER_NAMESPACE(gf_sqr_N)
#define gf_mul_add_N AIMER_NAMESPACE(gf_mul_add_N)
#define gf_mat_vec_mul_N AIMER_NAMESPACE(gf_mat_vec_mul_N)
#define gf_mat_vec_mul_add_N AIMER_NAMESPACE(gf_mat_vec_mul_add_N)
#define aim3_mpc_N AIMER_NAMESPACE(aim3_mpc_N)

extern void gf_sqr_N(gf out[AIMER_N], const gf in[AIMER_N]);
extern void gf_mul_add_N(gf out[AIMER_N], const gf in[AIMER_N],
                         const gf multiplier);
extern void gf_mat_vec_mul_N(gf out[AIMER_N], const gf in[AIMER_N],
                             const gf matrix[AIM3_NUM_BITS_FIELD]);
extern void gf_mat_vec_mul_add_N(gf out[AIMER_N], const gf in[AIMER_N],
                                 const gf matrix[AIM3_NUM_BITS_FIELD]);
extern void aim3_mpc_N(mult_chk_t mult_checks[AIMER_N], const aim_lin_t *lin,
                       const tape_t tapes[AIMER_N], const gf ciphertext);
#endif

void BENCH_NAME(field_mul)(void *out, const void *left, const void *right) {
  gf_mul((uint64_t *)out, (const uint64_t *)left, (const uint64_t *)right);
}

void BENCH_NAME(field_sqr)(void *out, const void *in) {
  gf_sqr((uint64_t *)out, (const uint64_t *)in);
}

void BENCH_NAME(field_inv)(void *out, const void *in) {
  gf_inv((uint64_t *)out, (const uint64_t *)in);
}

void BENCH_NAME(gf_matrix)(void *out, const void *in, const void *matrix) {
  gf_mat_vec_mul((uint64_t *)out, (const uint64_t *)in,
                 (const gf *)matrix);
}

void BENCH_NAME(gf_sqr_batch)(void *out_raw, const void *in_raw) {
  gf *out = out_raw;
  const gf *in = in_raw;
#if defined(BENCH_BATCH)
  gf_sqr_N(out, in);
#else
  for (size_t party = 0; party < AIMER_N; party++) {
    gf_sqr(out[party], in[party]);
  }
#endif
}

void BENCH_NAME(gf_mul_add_batch)(void *out_raw, const void *in_raw,
                                  const void *multiplier_raw) {
  gf *out = out_raw;
  const gf *in = in_raw;
  const gf *multiplier = multiplier_raw;
#if defined(BENCH_BATCH)
  gf_mul_add_N(out, in, *multiplier);
#else
  for (size_t party = 0; party < AIMER_N; party++) {
    gf_mul_add(out[party], in[party], *multiplier);
  }
#endif
}

void BENCH_NAME(gf_matrix_batch)(void *out_raw, const void *in_raw,
                                 const void *matrix_raw) {
  gf *out = out_raw;
  const gf *in = in_raw;
  const gf *matrix = matrix_raw;
#if defined(BENCH_BATCH)
  gf_mat_vec_mul_N(out, in, matrix);
#else
  for (size_t party = 0; party < AIMER_N; party++) {
    gf_mat_vec_mul(out[party], in[party], matrix);
  }
#endif
}

void BENCH_NAME(gf_matrix_add_batch)(void *out_raw, const void *in_raw,
                                     const void *matrix_raw) {
  gf *out = out_raw;
  const gf *in = in_raw;
  const gf *matrix = matrix_raw;
#if defined(BENCH_BATCH)
  gf_mat_vec_mul_add_N(out, in, matrix);
#else
  for (size_t party = 0; party < AIMER_N; party++) {
    gf_mat_vec_mul_add(out[party], in[party], matrix);
  }
#endif
}

void BENCH_NAME(mpc_batch)(void *checks_raw, const void *linear_raw,
                           const void *tapes_raw, const void *ciphertext_raw) {
  mult_chk_t *checks = checks_raw;
  const aim_lin_t *linear = linear_raw;
  const tape_t *tapes = tapes_raw;
  const gf *ciphertext = ciphertext_raw;
#if defined(BENCH_BATCH)
  aim3_mpc_N(checks, linear, tapes, *ciphertext);
#else
  for (size_t party = 0; party < AIMER_N; party++) {
    aim3_mpc(&checks[party], linear, &tapes[party], *ciphertext, party);
  }
#endif
}

size_t BENCH_NAME(words)(void) { return AIM3_NUM_WORDS_FIELD; }
size_t BENCH_NAME(bits)(void) { return AIM3_NUM_BITS_FIELD; }
size_t BENCH_NAME(parties)(void) { return AIMER_N; }
size_t BENCH_NAME(sboxes)(void) { return AIMER_L; }
size_t BENCH_NAME(tape_bytes)(void) { return sizeof(tape_t); }
size_t BENCH_NAME(linear_bytes)(void) { return sizeof(aim_lin_t); }
size_t BENCH_NAME(check_bytes)(void) { return sizeof(mult_chk_t); }
