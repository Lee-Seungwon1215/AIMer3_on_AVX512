// SPDX-License-Identifier: MIT

/* Cortex-M55 verifier schedule.  It preserves the reference transcript and
 * proof equations while evaluating independent MPC Frobenius powers and
 * challenge products in four-party batches. */

#include "api.h"
#include "aim3.h"
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

#define M55_PROFILE_BEGIN() m55_measure_start()
#define M55_PROFILE_ADD(counter) ((counter) += m55_measure_end())
#else
#define M55_PROFILE_BEGIN() ((void)0)
#define M55_PROFILE_ADD(counter) ((void)0)
#endif

#define AIMER_WRAP_INNER(name) __wrap_##name
#define AIMER_WRAP(name) AIMER_WRAP_INNER(name)

static void m55_verify_commit_and_expand_tape(tape_t *tape, uint8_t *commit,
                                               const uint8_t *salt, size_t rep,
                                               size_t party,
                                               const uint8_t *seed)
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

static void m55_verify_recompute_last_share(const proof_t *proof, tape_t *tape)
{
  gf temporary = {0,};

  gf_from_bytes(temporary, proof->delta_pt_bytes);
  gf_add(tape->pt_share, tape->pt_share, temporary);
  for (size_t ell = 0; ell < AIMER_L; ++ell)
  {
    gf_from_bytes(temporary, proof->delta_ys_bytes[ell]);
    gf_add(tape->y_shares[ell], tape->y_shares[ell], temporary);
  }
  gf_from_bytes(temporary, proof->delta_c_bytes);
  gf_add(tape->c_share, tape->c_share, temporary);
}

static void m55_verify_recompute_missing_alpha(const proof_t *proof,
                                                gf *alpha_shares, gf *alpha)
{
  for (size_t ell = 0; ell < AIMER_L + 1u; ++ell)
  {
    gf_from_bytes(alpha_shares[ell],
                  proof->missing_alpha_share_bytes[ell]);
    gf_add(alpha[ell], alpha[ell], alpha_shares[ell]);
  }
}

static void m55_verify_recompute_v_shares(
    gf *v_shares, gf (*b_shares)[AIMER_L + 1u], const gf *alpha,
    size_t missing_party)
{
  gf_set0(v_shares[missing_party]);
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

    for (size_t ell = 0; ell < AIMER_L + 1u; ++ell)
    {
      for (size_t lane = 0; lane < active_lanes; ++lane)
      {
        const size_t party = party_base + lane;
        if (party == missing_party)
        {
          gf_set0(inputs[lane]);
        }
        else
        {
          gf_copy(inputs[lane], b_shares[party][ell]);
        }
      }
      m55_gf_mul_const_batch4(products, inputs, alpha[ell], active_lanes);
      for (size_t lane = 0; lane < active_lanes; ++lane)
      {
        const size_t party = party_base + lane;
        if (party != missing_party)
        {
          gf_add(v_shares[party], v_shares[party], products[lane]);
        }
      }
    }
  }

  for (size_t party = 0; party < AIMER_N; ++party)
  {
    if (party != missing_party)
    {
      gf_add(v_shares[missing_party], v_shares[missing_party],
             v_shares[party]);
    }
  }
}

static int m55_crypto_sign_verify_internal(
    const uint8_t *sig, size_t siglen, const uint8_t *message,
    size_t message_length, const uint8_t *prefix, size_t prefix_length,
    const uint8_t *public_key)
{
  if (siglen != CRYPTO_BYTES)
  {
    return -1;
  }

  const signature_t *const signature = (const signature_t *)sig;
#if defined(AIMER_PHASE_PROFILE)
  uint64_t linear_cycles = 0u;
  uint64_t setup_cycles = 0u;
  uint64_t tree_cycles = 0u;
  uint64_t tape_cycles = 0u;
  uint64_t mpc_cycles = 0u;
  uint64_t mpc_icache_refills = 0u;
  uint64_t mpc_dcache_refills = 0u;
  uint64_t xz_product_cycles = 0u;
  uint64_t b_product_cycles = 0u;
  uint64_t transcript_cycles = 0u;
  uint64_t finish_cycles = 0u;
  const uint64_t total_start = m55_cycle_count64();
  m55_mpc_profile_reset();
#endif
  gf ciphertext = {0,};
  gf_from_bytes(ciphertext, public_key + AIM3_IV_SIZE);

  aim_lin_t *const linear = malloc(sizeof(*linear));
  if (linear == NULL)
  {
    return -1;
  }
  M55_PROFILE_BEGIN();
  aim3_generate_linear(linear, public_key);
  M55_PROFILE_ADD(linear_cycles);

  hash_instance ctx_e;
  hash_instance ctx_h1;
  hash_instance ctx_h2;
  uint8_t indices[AIMER_T];
  uint8_t mu[AIMER_COMMIT_SIZE];

  M55_PROFILE_BEGIN();
  hash_init(&ctx_e);
  hash_update(&ctx_e, signature->h_2, AIMER_COMMIT_SIZE);
  hash_final(&ctx_e);
  hash_squeeze(&ctx_e, indices, sizeof(indices));
  hash_ctx_release(&ctx_e);
  for (size_t rep = 0; rep < AIMER_T; ++rep)
  {
    indices[rep] &= (uint8_t)((1u << AIMER_LOGN) - 1u);
  }

  hash_init(&ctx_e);
  hash_update(&ctx_e, signature->h_1, AIMER_COMMIT_SIZE);
  hash_final(&ctx_e);

  hash_init_prefix(&ctx_h1, HASH_PREFIX_0);
  hash_update(&ctx_h1, public_key, AIM3_IV_SIZE + AIM3_NUM_BYTES_FIELD);
  hash_update(&ctx_h1, prefix, prefix_length);
  hash_update(&ctx_h1, message, message_length);
  hash_final(&ctx_h1);
  hash_squeeze(&ctx_h1, mu, sizeof(mu));
  hash_ctx_release(&ctx_h1);

  hash_init_prefix(&ctx_h1, HASH_PREFIX_1);
  hash_update(&ctx_h1, mu, sizeof(mu));
  hash_update(&ctx_h1, signature->salt, AIMER_SALT_SIZE);
  hash_init_prefix(&ctx_h2, HASH_PREFIX_2);
  hash_update(&ctx_h2, signature->h_1, AIMER_COMMIT_SIZE);
  hash_update(&ctx_h2, signature->salt, AIMER_SALT_SIZE);

  uint8_t (*const nodes)[AIMER_SEED_SIZE] =
      malloc((2u * AIMER_N - 2u) * sizeof(*nodes));
  gf (*const b_shares)[AIMER_L + 1u] =
      malloc(AIMER_N * sizeof(*b_shares));
  gf (*const alpha_shares)[AIMER_L + 1u] =
      malloc(AIMER_N * sizeof(*alpha_shares));
  gf *const v_shares = malloc(AIMER_N * sizeof(*v_shares));
  M55_PROFILE_ADD(setup_cycles);

  if ((nodes == NULL) || (b_shares == NULL) || (alpha_shares == NULL) ||
      (v_shares == NULL))
  {
    free(linear);
    free(nodes);
    free(b_shares);
    free(alpha_shares);
    free(v_shares);
    hash_ctx_release(&ctx_e);
    hash_ctx_release(&ctx_h1);
    hash_ctx_release(&ctx_h2);
    return -1;
  }

  for (size_t rep = 0; rep < AIMER_T; ++rep)
  {
    const size_t missing_party = indices[rep];
    gf epsilons[AIMER_L + 1u];
    gf alpha[AIMER_L + 1u];
    M55_PROFILE_BEGIN();
    reconstruct_tree(nodes, signature->salt,
                     signature->proofs[rep].reveal_path, rep,
                     missing_party);
    hash_squeeze(&ctx_e, (uint8_t *)epsilons, sizeof(epsilons));
    memset(alpha, 0, sizeof(alpha));
    M55_PROFILE_ADD(tree_cycles);

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

      M55_PROFILE_BEGIN();
      for (size_t lane = 0; lane < active_lanes; ++lane)
      {
        const size_t party = party_base + lane;
        if (party == missing_party)
        {
          memset(&tapes[lane], 0, sizeof(tapes[lane]));
          hash_update(&ctx_h1,
                      signature->proofs[rep].missing_commitment,
                      AIMER_COMMIT_SIZE);
          m55_verify_recompute_missing_alpha(
              &signature->proofs[rep], alpha_shares[party], alpha);
          continue;
        }

        uint8_t commitment[AIMER_COMMIT_SIZE];
        m55_verify_commit_and_expand_tape(
            &tapes[lane], commitment, signature->salt, rep, party,
            nodes[AIMER_N + party - 2u]);
        hash_update(&ctx_h1, commitment, sizeof(commitment));
        if (party == AIMER_N - 1u)
        {
          m55_verify_recompute_last_share(&signature->proofs[rep],
                                          &tapes[lane]);
        }
      }
      M55_PROFILE_ADD(tape_cycles);

#if defined(AIMER_PHASE_PROFILE)
      const uint32_t mpc_icache_before = m55_icache_refill_count();
      const uint32_t mpc_dcache_before = m55_dcache_refill_count();
#endif
      M55_PROFILE_BEGIN();
      m55_aim3_mpc_batch4(checks, linear, tapes, ciphertext, party_base,
                          active_lanes);
      M55_PROFILE_ADD(mpc_cycles);
#if defined(AIMER_PHASE_PROFILE)
      const uint32_t mpc_icache_after = m55_icache_refill_count();
      const uint32_t mpc_dcache_after = m55_dcache_refill_count();
      mpc_icache_refills +=
          (mpc_icache_after - mpc_icache_before) & UINT32_C(0xffff);
      mpc_dcache_refills +=
          (mpc_dcache_after - mpc_dcache_before) & UINT32_C(0xffff);
#endif
      for (size_t lane = 0; lane < active_lanes; ++lane)
      {
        const size_t party = party_base + lane;
        if (party == missing_party)
        {
          continue;
        }
        gf_copy(v_shares[party], checks[lane].c_share);
        for (size_t ell = 0; ell < AIMER_L + 1u; ++ell)
        {
          gf_copy(b_shares[party][ell], checks[lane].b_shares[ell]);
        }
      }

      M55_PROFILE_BEGIN();
      for (size_t ell = 0; ell < AIMER_L + 1u; ++ell)
      {
        for (size_t lane = 0; lane < active_lanes; ++lane)
        {
          const size_t party = party_base + lane;
          if (party == missing_party)
          {
            gf_set0(x_inputs[lane]);
            gf_set0(z_inputs[lane]);
          }
          else
          {
            gf_copy(x_inputs[lane], checks[lane].x_shares[ell]);
            gf_copy(z_inputs[lane], checks[lane].z_shares[ell]);
          }
        }
        m55_gf_mul_const_batch4(x_products, x_inputs, epsilons[ell],
                                active_lanes);
        m55_gf_mul_const_batch4(z_products, z_inputs, epsilons[ell],
                                active_lanes);
        for (size_t lane = 0; lane < active_lanes; ++lane)
        {
          const size_t party = party_base + lane;
          if (party == missing_party)
          {
            continue;
          }
          gf_add(alpha_shares[party][ell], checks[lane].a_shares[ell],
                 x_products[lane]);
          gf_add(alpha[ell], alpha[ell], alpha_shares[party][ell]);
          gf_add(v_shares[party], v_shares[party], z_products[lane]);
        }
      }
      M55_PROFILE_ADD(xz_product_cycles);
    }

    M55_PROFILE_BEGIN();
    m55_verify_recompute_v_shares(v_shares, b_shares, alpha,
                                   missing_party);
    M55_PROFILE_ADD(b_product_cycles);
    M55_PROFILE_BEGIN();
    hash_update(&ctx_h2, (const uint8_t *)alpha_shares,
                AIM3_NUM_BYTES_FIELD * AIMER_N * (AIMER_L + 1u));
    hash_update(&ctx_h2, (const uint8_t *)v_shares,
                AIM3_NUM_BYTES_FIELD * AIMER_N);
    hash_update(&ctx_h1, signature->proofs[rep].delta_pt_bytes,
                AIM3_NUM_BYTES_FIELD * (AIMER_L + 2u));
    M55_PROFILE_ADD(transcript_cycles);
  }
  hash_ctx_release(&ctx_e);

  M55_PROFILE_BEGIN();
  uint8_t h1_prime[AIMER_COMMIT_SIZE];
  uint8_t h2_prime[AIMER_COMMIT_SIZE];
  hash_final(&ctx_h1);
  hash_squeeze(&ctx_h1, h1_prime, sizeof(h1_prime));
  hash_ctx_release(&ctx_h1);
  hash_final(&ctx_h2);
  hash_squeeze(&ctx_h2, h2_prime, sizeof(h2_prime));
  hash_ctx_release(&ctx_h2);

  free(linear);
  free(nodes);
  free(b_shares);
  free(alpha_shares);
  free(v_shares);

  M55_PROFILE_ADD(finish_cycles);

  const int result =
      (memcmp(h1_prime, signature->h_1, AIMER_COMMIT_SIZE) == 0 &&
       memcmp(h2_prime, signature->h_2, AIMER_COMMIT_SIZE) == 0)
          ? 0
          : -1;

#if defined(AIMER_PHASE_PROFILE)
  const uint64_t total_cycles = m55_cycle_count64() - total_start;
  printf("PHASE_PROFILE_VERIFY,param=%s,backend=%s,linear=%llu,setup=%llu,"
         "tree=%llu,tape=%llu,mpc=%llu,mpc_affine=%llu,mpc_frobenius=%llu,"
         "mpc_icache_refills=%llu,"
         "mpc_dcache_refills=%llu,"
         "xz_products=%llu,b_products=%llu,"
         "transcript=%llu,finish=%llu,total=%llu,result=%d\n",
         xstr(PARAMS), xstr(AIMER_BACKEND),
         (unsigned long long)linear_cycles,
         (unsigned long long)setup_cycles,
         (unsigned long long)tree_cycles,
         (unsigned long long)tape_cycles,
         (unsigned long long)mpc_cycles,
         (unsigned long long)m55_mpc_profile_affine_cycles(),
         (unsigned long long)m55_mpc_profile_frobenius_cycles(),
         (unsigned long long)mpc_icache_refills,
         (unsigned long long)mpc_dcache_refills,
         (unsigned long long)xz_product_cycles,
         (unsigned long long)b_product_cycles,
         (unsigned long long)transcript_cycles,
         (unsigned long long)finish_cycles,
         (unsigned long long)total_cycles, result);
#endif

  return result;
}

int AIMER_WRAP(crypto_sign_verify)(const uint8_t *sig, size_t siglen,
                                   const uint8_t *message,
                                   size_t message_length,
                                   const uint8_t *ctx, size_t ctxlen,
                                   const uint8_t *public_key)
{
  uint8_t prefix[256];
  if (ctxlen > 255u)
  {
    return -1;
  }
  prefix[0] = (uint8_t)ctxlen;
  for (size_t i = 0; i < ctxlen; ++i)
  {
    prefix[i + 1u] = ctx[i];
  }
  return m55_crypto_sign_verify_internal(sig, siglen, message, message_length,
                                         prefix, ctxlen + 1u, public_key);
}

int AIMER_WRAP(crypto_sign_open)(uint8_t *message, size_t *message_length,
                                 const uint8_t *signed_message,
                                 size_t signed_message_length,
                                 const uint8_t *ctx, size_t ctxlen,
                                 const uint8_t *public_key)
{
  if (signed_message_length < CRYPTO_BYTES)
  {
    return -1;
  }
  const size_t plain_length = signed_message_length - CRYPTO_BYTES;
  const uint8_t *const signature = signed_message + plain_length;
  if (AIMER_WRAP(crypto_sign_verify)(signature, CRYPTO_BYTES, signed_message,
                                     plain_length, ctx, ctxlen,
                                     public_key) != 0)
  {
    return -1;
  }
  memmove(message, signed_message, plain_length);
  *message_length = plain_length;
  return 0;
}
