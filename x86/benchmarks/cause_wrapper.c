// SPDX-License-Identifier: MIT
// Compiled per parameter/backend only for bench_avx512_causes.

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
#define gf_mat_vec_mul_N AIMER_NAMESPACE(gf_mat_vec_mul_N)
#define gf_mat_vec_mul_add_N AIMER_NAMESPACE(gf_mat_vec_mul_add_N)
#define aim3_mpc_N AIMER_NAMESPACE(aim3_mpc_N)

extern void gf_sqr_N(gf out[AIMER_N], const gf in[AIMER_N]);
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

void BENCH_NAME(field_mul_add)(void *out, const void *left,
                               const void *right) {
  gf_mul_add((uint64_t *)out, (const uint64_t *)left,
             (const uint64_t *)right);
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

void BENCH_NAME(generate_linear)(void *linear_raw, const void *iv_raw) {
  aim3_generate_linear((aim_lin_t *)linear_raw, (const uint8_t *)iv_raw);
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

void BENCH_NAME(mpc_affine_setup)(void *checks_raw, const void *linear_raw,
                                  const void *tapes_raw,
                                  const void *ciphertext_raw) {
  mult_chk_t *checks = checks_raw;
  const aim_lin_t *linear = linear_raw;
  const tape_t *tapes = tapes_raw;
  const gf *ciphertext = ciphertext_raw;
  gf inputs[AIMER_N];
  gf outputs[AIMER_N];

  for (size_t party = 0; party < AIMER_N; party++) {
    for (size_t ell = 0; ell < AIMER_L + 1; ell++) {
      gf_copy(checks[party].a_shares[ell], tapes[party].a_shares[ell]);
    }
    for (size_t ell = 0; ell < AIMER_L; ell++) {
      gf_copy(checks[party].b_shares[ell], tapes[party].y_shares[ell]);
    }
    gf_copy(checks[party].b_shares[AIMER_L], tapes[party].pt_share);
    gf_copy(checks[party].c_share, tapes[party].c_share);
    gf_copy(inputs[party], tapes[party].pt_share);
  }
  gf_add(checks[AIMER_N - 1].b_shares[AIMER_L],
         checks[AIMER_N - 1].b_shares[AIMER_L], *ciphertext);

  for (size_t ell = 0; ell < AIMER_L; ell++) {
#if defined(BENCH_BATCH)
    gf_mat_vec_mul_N(outputs, (const gf *)inputs, linear->mat_A[ell]);
#else
    for (size_t party = 0; party < AIMER_N; party++) {
      gf_mat_vec_mul(outputs[party], inputs[party], linear->mat_A[ell]);
    }
#endif
    for (size_t party = 0; party < AIMER_N; party++) {
      gf_copy(checks[party].x_shares[ell], outputs[party]);
    }
    gf_add(checks[AIMER_N - 1].x_shares[ell],
           checks[AIMER_N - 1].x_shares[ell], linear->vec_b[ell]);
  }

  for (size_t party = 0; party < AIMER_N; party++) {
    gf_set0(outputs[party]);
  }
  for (size_t ell = 0; ell < AIMER_L; ell++) {
    for (size_t party = 0; party < AIMER_N; party++) {
      gf_copy(inputs[party], tapes[party].y_shares[ell]);
    }
#if defined(BENCH_BATCH)
    gf_mat_vec_mul_add_N(outputs, (const gf *)inputs,
                         linear->mat_A[ell + AIMER_L]);
#else
    for (size_t party = 0; party < AIMER_N; party++) {
      gf_mat_vec_mul_add(outputs[party], inputs[party],
                         linear->mat_A[ell + AIMER_L]);
    }
#endif
  }
  for (size_t party = 0; party < AIMER_N; party++) {
    gf_copy(checks[party].x_shares[AIMER_L], outputs[party]);
  }
  gf_add(checks[AIMER_N - 1].x_shares[AIMER_L],
         checks[AIMER_N - 1].x_shares[AIMER_L], linear->vec_b[AIMER_L]);
}

void BENCH_NAME(mpc_frobenius)(void *checks_raw, const void *linear_raw,
                               const void *tapes_raw,
                               const void *ciphertext_raw) {
  mult_chk_t *checks = checks_raw;
#if defined(BENCH_BATCH)
  gf inputs[AIMER_N];
  gf outputs[AIMER_N];
#endif
  (void)linear_raw;
  (void)tapes_raw;
  (void)ciphertext_raw;

  for (size_t ell = 0; ell < AIMER_L + 1; ell++) {
#if defined(BENCH_BATCH)
    for (size_t party = 0; party < AIMER_N; party++) {
      gf_copy(inputs[party], checks[party].x_shares[ell]);
    }
    gf_sqr_N(outputs, (const gf *)inputs);
    for (size_t exponent = 1; exponent < aim3_exponents[ell]; exponent++) {
      gf_sqr_N(outputs, (const gf *)outputs);
    }
    for (size_t party = 0; party < AIMER_N; party++) {
      gf_copy(checks[party].z_shares[ell], outputs[party]);
    }
#else
    for (size_t party = 0; party < AIMER_N; party++) {
      gf_sqr(checks[party].z_shares[ell], checks[party].x_shares[ell]);
      for (size_t exponent = 1; exponent < aim3_exponents[ell]; exponent++) {
        gf_sqr(checks[party].z_shares[ell],
               checks[party].z_shares[ell]);
      }
    }
#endif
    checks[AIMER_N - 1].z_shares[ell][0] ^= 1;
  }
}

size_t BENCH_NAME(words)(void) { return AIM3_NUM_WORDS_FIELD; }
size_t BENCH_NAME(bits)(void) { return AIM3_NUM_BITS_FIELD; }
size_t BENCH_NAME(parties)(void) { return AIMER_N; }
size_t BENCH_NAME(sboxes)(void) { return AIMER_L; }
size_t BENCH_NAME(repetitions)(void) { return AIMER_T; }
size_t BENCH_NAME(exponent_sum)(void) {
  size_t sum = 0;
  for (size_t ell = 0; ell < AIMER_L + 1; ell++) {
    sum += aim3_exponents[ell];
  }
  return sum;
}
size_t BENCH_NAME(tape_bytes)(void) { return sizeof(tape_t); }
size_t BENCH_NAME(linear_bytes)(void) { return sizeof(aim_lin_t); }
size_t BENCH_NAME(check_bytes)(void) { return sizeof(mult_chk_t); }
