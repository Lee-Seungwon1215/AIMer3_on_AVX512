// SPDX-License-Identifier: MIT

/*
 * Memory-bounded signer for the Cortex-M55 port.
 *
 * The upstream reference signer keeps every repetition's seed tree,
 * commitment, multiplication check, alpha share, and v share alive at once.
 * That exceeds the STM32N657 SRAM for the "s" parameter sets.  This file
 * wraps only the two signing entry points and recomputes deterministic party
 * data between phases.  The transcript order and serialized signature stay
 * identical to the reference implementation.
 */

#include "api.h"
#include "aim3.h"
#include "common/crypto_declassify.h"
#include "common/rng.h"
#include "field.h"
#include "hash.h"
#include "m55_field_batch.h"
#include "m55_mpc_batch.h"
#include "params.h"
#include "sign.h"
#include "tree.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(AIMER_PHASE_PROFILE)
#include "m55_platform.h"
#include <stdio.h>
#endif

#define AIMER_WRAP_INNER(name) __wrap_##name
#define AIMER_WRAP(name) AIMER_WRAP_INNER(name)

static void m55_commit_and_expand_tape(tape_t *tape, uint8_t *commit,
                                       const uint8_t *salt, size_t rep,
                                       size_t party, const uint8_t *seed)
{
  hash_instance ctx;
  const uint8_t rep_byte = (uint8_t)rep;
  const uint8_t party_byte = (uint8_t)party;

  hash_init_prefix(&ctx, HASH_PREFIX_5);
  hash_update(&ctx, salt, AIMER_SALT_SIZE);
  hash_update(&ctx, &rep_byte, sizeof(rep_byte));
  hash_update(&ctx, &party_byte, sizeof(party_byte));
  hash_update(&ctx, seed, AIMER_SEED_SIZE);
  hash_final(&ctx);
  hash_squeeze(&ctx, commit, AIMER_COMMIT_SIZE);
  hash_squeeze(&ctx, (uint8_t *)tape, sizeof(*tape));
  hash_ctx_release(&ctx);
}

static void m55_adjust_last_share(proof_t *proof, tape_t *delta, tape_t *tape,
                                  const gf pt_gf,
                                  const gf *sbox_outputs)
{
  gf_add(delta->pt_share, delta->pt_share, pt_gf);
  gf_to_bytes(proof->delta_pt_bytes, delta->pt_share);
  gf_add(tape->pt_share, delta->pt_share, tape->pt_share);

  for (size_t ell = 0; ell < AIMER_L; ++ell)
  {
    gf_add(delta->y_shares[ell], delta->y_shares[ell], sbox_outputs[ell]);
    gf_to_bytes(proof->delta_ys_bytes[ell], delta->y_shares[ell]);
    gf_add(tape->y_shares[ell], delta->y_shares[ell], tape->y_shares[ell]);
  }

  for (size_t ell = 0; ell < AIMER_L + 1; ++ell)
  {
    gf_mul_add(delta->c_share, delta->a_shares[ell], sbox_outputs[ell]);
  }
  gf_to_bytes(proof->delta_c_bytes, delta->c_share);
  gf_add(tape->c_share, delta->c_share, tape->c_share);
}

static void m55_recompute_last_share(const proof_t *proof, tape_t *tape)
{
  gf temp = {0,};

  gf_from_bytes(temp, proof->delta_pt_bytes);
  gf_add(tape->pt_share, tape->pt_share, temp);
  for (size_t ell = 0; ell < AIMER_L; ++ell)
  {
    gf_from_bytes(temp, proof->delta_ys_bytes[ell]);
    gf_add(tape->y_shares[ell], tape->y_shares[ell], temp);
  }
  gf_from_bytes(temp, proof->delta_c_bytes);
  gf_add(tape->c_share, tape->c_share, temp);
}

static int m55_phase_1(
    signature_t *sign, uint8_t root_seeds[AIMER_T][AIMER_SEED_SIZE],
    uint8_t (*nodes)[AIMER_SEED_SIZE],
    uint8_t (*commits)[AIMER_COMMIT_SIZE], const aim_lin_t *lin,
    const gf pt_gf, const gf ct_gf, const gf *sbox_outputs,
    const uint8_t *sk, const uint8_t *rnd, const uint8_t *m, size_t mlen,
    const uint8_t *pre, size_t prelen)
{
  hash_instance ctx;
  uint8_t mu[AIMER_COMMIT_SIZE];

  hash_init_prefix(&ctx, HASH_PREFIX_0);
  hash_update(&ctx, sk + AIM3_NUM_BYTES_FIELD,
              AIM3_IV_SIZE + AIM3_NUM_BYTES_FIELD);
  hash_update(&ctx, pre, prelen);
  hash_update(&ctx, m, mlen);
  hash_final(&ctx);
  hash_squeeze(&ctx, mu, sizeof(mu));
  hash_ctx_release(&ctx);

  hash_init_prefix(&ctx, HASH_PREFIX_3);
  hash_update(&ctx, sk, AIM3_NUM_BYTES_FIELD);
  hash_update(&ctx, mu, sizeof(mu));
  hash_update(&ctx, rnd, SECURITY_BYTES);
  hash_final(&ctx);
  hash_squeeze(&ctx, sign->salt, AIMER_SALT_SIZE);
  hash_squeeze(&ctx, root_seeds[0],
               AIMER_T * AIMER_SEED_SIZE);
  hash_ctx_release(&ctx);

  hash_init_prefix(&ctx, HASH_PREFIX_1);
  hash_update(&ctx, mu, sizeof(mu));
  hash_update(&ctx, sign->salt, AIMER_SALT_SIZE);

  for (size_t rep = 0; rep < AIMER_T; ++rep)
  {
    tape_t delta;
    memset(&delta, 0, sizeof(delta));
    expand_tree(nodes, sign->salt, rep, root_seeds[rep]);

    for (size_t party_base = 0; party_base < AIMER_N;
         party_base += M55_PARTY_BATCH_LANES)
    {
      tape_t tapes[M55_PARTY_BATCH_LANES];
      mult_chk_t checks[M55_PARTY_BATCH_LANES];
      size_t active_lanes = AIMER_N - party_base;
      if (active_lanes > M55_PARTY_BATCH_LANES)
      {
        active_lanes = M55_PARTY_BATCH_LANES;
      }

      for (size_t lane = 0; lane < active_lanes; ++lane)
      {
        const size_t party = party_base + lane;
        m55_commit_and_expand_tape(&tapes[lane], commits[party], sign->salt,
                                   rep, party,
                                   nodes[party + AIMER_N - 1u]);

        gf_add(delta.pt_share, delta.pt_share, tapes[lane].pt_share);
        for (size_t ell = 0; ell < AIMER_L; ++ell)
        {
          gf_add(delta.y_shares[ell], delta.y_shares[ell],
                 tapes[lane].y_shares[ell]);
        }
        for (size_t ell = 0; ell < AIMER_L + 1u; ++ell)
        {
          gf_add(delta.a_shares[ell], delta.a_shares[ell],
                 tapes[lane].a_shares[ell]);
        }
        gf_add(delta.c_share, delta.c_share, tapes[lane].c_share);

        if (party == AIMER_N - 1u)
        {
          m55_adjust_last_share(&sign->proofs[rep], &delta, &tapes[lane],
                                pt_gf, sbox_outputs);
        }
      }

      m55_aim3_mpc_batch4(checks, lin, tapes, ct_gf, party_base,
                          active_lanes);
    }

    hash_update(&ctx, (const uint8_t *)commits,
                AIMER_N * AIMER_COMMIT_SIZE);
    hash_update(&ctx, sign->proofs[rep].delta_pt_bytes,
                AIM3_NUM_BYTES_FIELD * (AIMER_L + 2));
  }

  hash_final(&ctx);
  hash_squeeze(&ctx, sign->h_1, AIMER_COMMIT_SIZE);
  hash_ctx_release(&ctx);
  return 0;
}

static void m55_phase_2_and_3(
    signature_t *sign,
    const uint8_t root_seeds[AIMER_T][AIMER_SEED_SIZE],
    uint8_t (*nodes)[AIMER_SEED_SIZE], gf (*alpha_shares)[AIMER_L + 1],
    gf (*b_shares)[AIMER_L + 1], gf *v_shares, const aim_lin_t *lin,
    const gf ct_gf)
{
  hash_instance ctx_e;
  hash_instance ctx_h2;

  hash_init(&ctx_e);
  hash_update(&ctx_e, sign->h_1, AIMER_COMMIT_SIZE);
  hash_final(&ctx_e);

  hash_init_prefix(&ctx_h2, HASH_PREFIX_2);
  hash_update(&ctx_h2, sign->h_1, AIMER_COMMIT_SIZE);
  hash_update(&ctx_h2, sign->salt, AIMER_SALT_SIZE);

  for (size_t rep = 0; rep < AIMER_T; ++rep)
  {
    gf epsilons[AIMER_L + 1];
    gf alpha[AIMER_L + 1];
    uint8_t unused_commit[AIMER_COMMIT_SIZE];

    hash_squeeze(&ctx_e, (uint8_t *)epsilons, sizeof(epsilons));
    crypto_declassify(epsilons, sizeof(epsilons));
    memset(alpha, 0, sizeof(alpha));
    expand_tree(nodes, sign->salt, rep, root_seeds[rep]);

    for (size_t party_base = 0; party_base < AIMER_N;
         party_base += M55_PARTY_BATCH_LANES)
    {
      tape_t tapes[M55_PARTY_BATCH_LANES];
      mult_chk_t checks[M55_PARTY_BATCH_LANES];
      gf x_inputs[M55_PARTY_BATCH_LANES];
      gf z_inputs[M55_PARTY_BATCH_LANES];
      gf x_products[M55_PARTY_BATCH_LANES];
      gf z_products[M55_PARTY_BATCH_LANES];
      size_t active_lanes = AIMER_N - party_base;
      if (active_lanes > M55_PARTY_BATCH_LANES)
      {
        active_lanes = M55_PARTY_BATCH_LANES;
      }

      for (size_t lane = 0; lane < active_lanes; ++lane)
      {
        const size_t party = party_base + lane;
        m55_commit_and_expand_tape(&tapes[lane], unused_commit, sign->salt, rep,
                                   party, nodes[party + AIMER_N - 1]);
        if (party == AIMER_N - 1)
        {
          m55_recompute_last_share(&sign->proofs[rep], &tapes[lane]);
        }
      }
      m55_aim3_mpc_batch4(checks, lin, tapes, ct_gf, party_base,
                          active_lanes);
      for (size_t lane = 0; lane < active_lanes; ++lane)
      {
        const size_t party = party_base + lane;
        gf_copy(v_shares[party], checks[lane].c_share);
        for (size_t ell = 0; ell < AIMER_L + 1; ++ell)
        {
          gf_copy(b_shares[party][ell], checks[lane].b_shares[ell]);
        }
      }

      for (size_t ell = 0; ell < AIMER_L + 1; ++ell)
      {
        for (size_t lane = 0; lane < active_lanes; ++lane)
        {
          gf_copy(x_inputs[lane], checks[lane].x_shares[ell]);
          gf_copy(z_inputs[lane], checks[lane].z_shares[ell]);
        }
        m55_gf_mul_const_batch4(x_products, x_inputs, epsilons[ell],
                                active_lanes);
        m55_gf_mul_const_batch4(z_products, z_inputs, epsilons[ell],
                                active_lanes);
        for (size_t lane = 0; lane < active_lanes; ++lane)
        {
          const size_t party = party_base + lane;
          gf_add(alpha_shares[party][ell], checks[lane].a_shares[ell],
                 x_products[lane]);
          gf_add(alpha[ell], alpha[ell], alpha_shares[party][ell]);
          gf_add(v_shares[party], v_shares[party], z_products[lane]);
        }
      }
    }

    crypto_declassify(alpha, sizeof(alpha));
    for (size_t party_base = 0; party_base < AIMER_N;
         party_base += M55_PARTY_BATCH_LANES)
    {
      gf inputs[M55_PARTY_BATCH_LANES];
      gf products[M55_PARTY_BATCH_LANES];
      size_t active_lanes = AIMER_N - party_base;
      if (active_lanes > M55_PARTY_BATCH_LANES)
      {
        active_lanes = M55_PARTY_BATCH_LANES;
      }
      for (size_t ell = 0; ell < AIMER_L + 1; ++ell)
      {
        for (size_t lane = 0; lane < active_lanes; ++lane)
        {
          gf_copy(inputs[lane], b_shares[party_base + lane][ell]);
        }
        m55_gf_mul_const_batch4(products, inputs, alpha[ell], active_lanes);
        for (size_t lane = 0; lane < active_lanes; ++lane)
        {
          const size_t party = party_base + lane;
          gf_add(v_shares[party], v_shares[party], products[lane]);
        }
      }
    }

    hash_update(&ctx_h2, (const uint8_t *)alpha_shares,
                AIM3_NUM_BYTES_FIELD * AIMER_N * (AIMER_L + 1));
    hash_update(&ctx_h2, (const uint8_t *)v_shares,
                AIM3_NUM_BYTES_FIELD * AIMER_N);
  }

  hash_final(&ctx_h2);
  hash_squeeze(&ctx_h2, sign->h_2, AIMER_COMMIT_SIZE);
  hash_ctx_release(&ctx_h2);
  hash_ctx_release(&ctx_e);
}

static void m55_phase_5(
    signature_t *sign,
    const uint8_t root_seeds[AIMER_T][AIMER_SEED_SIZE],
    uint8_t (*nodes)[AIMER_SEED_SIZE], const aim_lin_t *lin,
    const gf ct_gf)
{
  uint8_t indices[AIMER_T];
  hash_instance ctx_e;

  hash_init(&ctx_e);
  hash_update(&ctx_e, sign->h_2, AIMER_COMMIT_SIZE);
  hash_final(&ctx_e);
  hash_squeeze(&ctx_e, indices, sizeof(indices));
  hash_ctx_release(&ctx_e);
  for (size_t rep = 0; rep < AIMER_T; ++rep)
  {
    indices[rep] &= (1U << AIMER_LOGN) - 1U;
  }
  crypto_declassify(indices, sizeof(indices));

  hash_init(&ctx_e);
  hash_update(&ctx_e, sign->h_1, AIMER_COMMIT_SIZE);
  hash_final(&ctx_e);

  for (size_t rep = 0; rep < AIMER_T; ++rep)
  {
    const size_t i_bar = indices[rep];
    gf epsilons[AIMER_L + 1];
    tape_t tape;
    mult_chk_t mult_chk;

    hash_squeeze(&ctx_e, (uint8_t *)epsilons, sizeof(epsilons));
    expand_tree(nodes, sign->salt, rep, root_seeds[rep]);
    reveal_all_but(sign->proofs[rep].reveal_path,
                   (const uint8_t (*)[AIMER_SEED_SIZE])nodes, i_bar);

    m55_commit_and_expand_tape(
        &tape, sign->proofs[rep].missing_commitment, sign->salt, rep, i_bar,
        nodes[i_bar + AIMER_N - 1]);
    if (i_bar == AIMER_N - 1)
    {
      m55_recompute_last_share(&sign->proofs[rep], &tape);
    }

    memset(&mult_chk, 0, sizeof(mult_chk));
    aim3_mpc(&mult_chk, lin, &tape, ct_gf, i_bar);
    for (size_t ell = 0; ell < AIMER_L + 1; ++ell)
    {
      gf alpha_share;
      gf_copy(alpha_share, mult_chk.a_shares[ell]);
      gf_mul_add(alpha_share, mult_chk.x_shares[ell], epsilons[ell]);
      gf_to_bytes(sign->proofs[rep].missing_alpha_share_bytes[ell],
                  alpha_share);
    }
  }
  hash_ctx_release(&ctx_e);
}

static int m55_sign_signature_internal(uint8_t *sig, size_t *siglen,
                                       const uint8_t *m, size_t mlen,
                                       const uint8_t *pre, size_t prelen,
                                       const uint8_t *rnd,
                                       const uint8_t *sk)
{
  signature_t *const sign = (signature_t *)sig;
  uint8_t root_seeds[AIMER_T][AIMER_SEED_SIZE];
  gf pt_gf = {0,};
  gf ct_gf = {0,};
#if defined(AIMER_PHASE_PROFILE)
  uint64_t linear_cycles;
  uint64_t sbox_cycles;
  uint64_t phase_1_cycles;
  uint64_t phase_2_3_cycles;
  uint64_t phase_5_cycles;
  uint64_t phase_start;
#endif

  aim_lin_t *const lin = malloc(sizeof(*lin));
  uint8_t (*const nodes)[AIMER_SEED_SIZE] =
      malloc((2 * AIMER_N - 1) * sizeof(*nodes));
  uint8_t (*const commits)[AIMER_COMMIT_SIZE] =
      malloc(AIMER_N * sizeof(*commits));
  gf (*const alpha_shares)[AIMER_L + 1] =
      malloc(AIMER_N * sizeof(*alpha_shares));
  gf (*const b_shares)[AIMER_L + 1] =
      malloc(AIMER_N * sizeof(*b_shares));
  gf *const v_shares = malloc(AIMER_N * sizeof(*v_shares));

  if ((lin == NULL) || (nodes == NULL) || (commits == NULL) ||
      (alpha_shares == NULL) || (b_shares == NULL) || (v_shares == NULL))
  {
    free(lin);
    free(nodes);
    free(commits);
    free(alpha_shares);
    free(b_shares);
    free(v_shares);
    return -1;
  }

  gf_from_bytes(pt_gf, sk);
  gf_from_bytes(ct_gf, sk + AIM3_NUM_BYTES_FIELD + AIM3_IV_SIZE);
#if defined(AIMER_PHASE_PROFILE)
  phase_start = m55_cycle_count64();
#endif
  aim3_generate_linear(lin, sk + AIM3_NUM_BYTES_FIELD);
#if defined(AIMER_PHASE_PROFILE)
  linear_cycles = m55_cycle_count64() - phase_start;
#endif

  gf sbox_outputs[AIMER_L + 1];
#if defined(AIMER_PHASE_PROFILE)
  phase_start = m55_cycle_count64();
#endif
  aim3_sbox_outputs(sbox_outputs, lin, pt_gf, ct_gf);
#if defined(AIMER_PHASE_PROFILE)
  sbox_cycles = m55_cycle_count64() - phase_start;
#endif
  crypto_declassify(ct_gf, sizeof(ct_gf));

#if defined(AIMER_PHASE_PROFILE)
  phase_start = m55_cycle_count64();
#endif
  (void)m55_phase_1(sign, root_seeds, nodes, commits, lin, pt_gf, ct_gf,
                    sbox_outputs, sk, rnd, m, mlen, pre, prelen);
#if defined(AIMER_PHASE_PROFILE)
  phase_1_cycles = m55_cycle_count64() - phase_start;
  phase_start = m55_cycle_count64();
#endif
  m55_phase_2_and_3(sign, root_seeds, nodes, alpha_shares, b_shares,
                    v_shares, lin, ct_gf);
#if defined(AIMER_PHASE_PROFILE)
  phase_2_3_cycles = m55_cycle_count64() - phase_start;
  phase_start = m55_cycle_count64();
#endif
  m55_phase_5(sign, root_seeds, nodes, lin, ct_gf);
#if defined(AIMER_PHASE_PROFILE)
  phase_5_cycles = m55_cycle_count64() - phase_start;
#endif
  *siglen = CRYPTO_BYTES;

#if defined(AIMER_PHASE_PROFILE)
  printf("PHASE_PROFILE_SIGN,param=%s,backend=%s,linear=%llu,sbox=%llu,"
         "phase1=%llu,phase23=%llu,phase5=%llu,total_phases=%llu\n",
         xstr(PARAMS), xstr(AIMER_BACKEND),
         (unsigned long long)linear_cycles,
         (unsigned long long)sbox_cycles,
         (unsigned long long)phase_1_cycles,
         (unsigned long long)phase_2_3_cycles,
         (unsigned long long)phase_5_cycles,
         (unsigned long long)(linear_cycles + sbox_cycles + phase_1_cycles +
                              phase_2_3_cycles + phase_5_cycles));
#endif

  free(lin);
  free(nodes);
  free(commits);
  free(alpha_shares);
  free(b_shares);
  free(v_shares);
  return 0;
}

int AIMER_WRAP(crypto_sign_signature)(uint8_t *sig, size_t *siglen,
                                      const uint8_t *m, size_t mlen,
                                      const uint8_t *ctx, size_t ctxlen,
                                      const uint8_t *sk)
{
  uint8_t prefix[256];
  uint8_t randomness[SECURITY_BYTES];

  if (ctxlen > 255)
  {
    return -1;
  }
  prefix[0] = (uint8_t)ctxlen;
  for (size_t i = 0; i < ctxlen; ++i)
  {
    prefix[i + 1] = ctx[i];
  }

#ifdef RANDOMIZED_SIGNING
  randombytes(randomness, sizeof(randomness));
#else
  memset(randomness, 0, sizeof(randomness));
#endif

  return m55_sign_signature_internal(sig, siglen, m, mlen, prefix,
                                     ctxlen + 1, randomness, sk);
}

int AIMER_WRAP(crypto_sign)(uint8_t *sm, size_t *smlen, const uint8_t *m,
                            size_t mlen, const uint8_t *ctx, size_t ctxlen,
                            const uint8_t *sk)
{
  const int result = AIMER_WRAP(crypto_sign_signature)(
      sm + mlen, smlen, m, mlen, ctx, ctxlen, sk);
  if (result != 0)
  {
    return result;
  }
  memcpy(sm, m, mlen);
  *smlen += mlen;
  return 0;
}
