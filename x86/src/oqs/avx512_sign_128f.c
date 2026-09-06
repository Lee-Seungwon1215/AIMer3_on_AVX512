// SPDX-License-Identifier: MIT

#include "api.h"
#include "aim3.h"
#include "oqs/avx512_mpc.h"
#include "field.h"
#include "hash.h"
#include "params.h"
#include "sign.h"
#include "tree.h"
#include "common/crypto_declassify.h"
#include "common/rng.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void commit_and_expand_tape(tape_t *tape, uint8_t *commit,
                                   const uint8_t *salt,
                                   size_t rep, size_t party,
                                   const uint8_t *seed)
{
  hash_instance ctx;

  uint8_t rep_byte = (uint8_t)rep;
  uint8_t party_byte = (uint8_t)party;

  hash_init_prefix(&ctx, HASH_PREFIX_5);
  hash_update(&ctx, salt, AIMER_SALT_SIZE);
  hash_update(&ctx, &rep_byte, sizeof(rep_byte));
  hash_update(&ctx, &party_byte, sizeof(party_byte));
  hash_update(&ctx, seed, AIMER_SEED_SIZE);
  hash_final(&ctx);

  hash_squeeze(&ctx, commit, AIMER_COMMIT_SIZE);
  hash_squeeze(&ctx, (uint8_t *)tape, sizeof(tape_t));
  hash_ctx_release(&ctx);
}

static inline void adjust_last_share(
  proof_t *proof, tape_t *delta, tape_t *tape,
  const gf pt_gf, const gf *sbox_outputs)
{  
  gf_add(delta->pt_share, delta->pt_share, pt_gf);
  gf_to_bytes(proof->delta_pt_bytes, delta->pt_share);
  gf_add(tape->pt_share, delta->pt_share, tape->pt_share);

  for (size_t i = 0; i < AIMER_L; i++)
  {
    gf_add(delta->y_shares[i], delta->y_shares[i], sbox_outputs[i]);
    gf_to_bytes(proof->delta_ys_bytes[i], delta->y_shares[i]);
    gf_add(tape->y_shares[i], delta->y_shares[i], tape->y_shares[i]);
  }

  // c = sum a[i] * y[i] where y = sbox outs
  for (size_t i = 0; i < AIMER_L + 1; i++)
  {
    gf_mul_add(delta->c_share, delta->a_shares[i], sbox_outputs[i]);
  }
  gf_to_bytes(proof->delta_c_bytes, delta->c_share);
  gf_add(tape->c_share, delta->c_share, tape->c_share);
}

// committing to the seeds and the execution views of the parties
// Returns 0 on success or -1 on allocation failure.
static int run_phase_1(signature_t *sign,
                       uint8_t commits[AIMER_T][AIMER_N][AIMER_COMMIT_SIZE],
                       uint8_t nodes[AIMER_T][2 * AIMER_N - 1][AIMER_SEED_SIZE],
                       mult_chk_t mult_chk[AIMER_T][AIMER_N],
                       const uint8_t *sk, const uint8_t *rnd,
                       const uint8_t *m, size_t mlen,
                       const uint8_t *pre, size_t prelen)
{
  gf pt_gf = {0,}, ct_gf = {0,};
  gf_from_bytes(pt_gf, sk);
  gf_from_bytes(ct_gf, sk + AIM3_NUM_BYTES_FIELD + AIM3_IV_SIZE);

  // message pre-hashing
  hash_instance ctx;
  hash_init_prefix(&ctx, HASH_PREFIX_0);
  hash_update(&ctx, sk + AIM3_NUM_BYTES_FIELD,
              AIM3_IV_SIZE + AIM3_NUM_BYTES_FIELD);
  hash_update(&ctx, pre, prelen);
  hash_update(&ctx, m, mlen);
  hash_final(&ctx);

  uint8_t mu[AIMER_COMMIT_SIZE];
  hash_squeeze(&ctx, mu, AIMER_COMMIT_SIZE);
  hash_ctx_release(&ctx);

  // derive the binary matrix and the vector from the initial vector
  aim_lin_t *lin = malloc(sizeof(aim_lin_t));
  if (lin == NULL)
  {
    return -1;
  }
  aim3_generate_linear(lin, sk + AIM3_NUM_BYTES_FIELD);

  // compute sboxes' outputs
  gf sbox_outputs[AIMER_L + 1];
  aim3_sbox_outputs(sbox_outputs, lin, pt_gf, ct_gf);

  // generate salt
  hash_init_prefix(&ctx, HASH_PREFIX_3);
  hash_update(&ctx, sk, AIM3_NUM_BYTES_FIELD);
  hash_update(&ctx, mu, AIMER_COMMIT_SIZE);
  hash_update(&ctx, rnd, SECURITY_BYTES);
  hash_final(&ctx);
  hash_squeeze(&ctx, sign->salt, AIMER_SALT_SIZE);

  // generate root seeds
  uint8_t root_seeds[AIMER_T][AIMER_SEED_SIZE];
  hash_squeeze(&ctx, root_seeds[0], sizeof(root_seeds));
  hash_ctx_release(&ctx);

  // hash_instance for h_1
  hash_init_prefix(&ctx, HASH_PREFIX_1);
  hash_update(&ctx, mu, AIMER_COMMIT_SIZE);
  hash_update(&ctx, sign->salt, AIMER_SALT_SIZE);

  // ct_gf is part of the public key, so it is public throughout the MPC
  crypto_declassify(ct_gf, sizeof(ct_gf));

  for (size_t rep = 0; rep < AIMER_T; rep++)
  {
    // compute parties' seeds using binary tree
    expand_tree(nodes[rep], sign->salt, rep, root_seeds[rep]);

    // initialize adjustment values
    tape_t delta, tapes[AIMER_N];
    memset(&delta, 0, sizeof(tape_t));

    for (size_t party = 0; party < AIMER_N; party++)
    {
      // generate execution views and commitments
      commit_and_expand_tape(&tapes[party], commits[rep][party], sign->salt, rep, party,
                             nodes[rep][party + AIMER_N - 1]);

      // compute offsets
      gf_add(delta.pt_share, delta.pt_share, tapes[party].pt_share);
      for (size_t i = 0; i < AIMER_L; i++)
      {
        gf_add(delta.y_shares[i], delta.y_shares[i], tapes[party].y_shares[i]);
      }
      for (size_t i = 0; i < AIMER_L + 1; i++)
      {
        gf_add(delta.a_shares[i], delta.a_shares[i], tapes[party].a_shares[i]);
      }
      gf_add(delta.c_share, delta.c_share, tapes[party].c_share);

      // adjust the last share and prepare the proof and h_1
      if (party == AIMER_N - 1)
      {
        adjust_last_share(&sign->proofs[rep], &delta, &tapes[party], pt_gf,
                          sbox_outputs);
      }

    }
    aim3_mpc_N(mult_chk[rep], lin, tapes, ct_gf);
    hash_update(&ctx, (const uint8_t*)commits[rep], AIMER_COMMIT_SIZE * AIMER_N);

    // NOTE: depend on the order of values in proof_t
    hash_update(&ctx, sign->proofs[rep].delta_pt_bytes,
                AIM3_NUM_BYTES_FIELD * (AIMER_L + 2));
  }

  // commit to salt, (all commitments of parties' seeds,
  // delta_pt, delta_t, delta_c) for all repetitions
  hash_final(&ctx);
  hash_squeeze(&ctx, sign->h_1, AIMER_COMMIT_SIZE);
  hash_ctx_release(&ctx);

  free(lin);
  return 0;
}

static void run_phase_2_and_3(signature_t *sign,
                              gf alpha_shares[AIMER_T][AIMER_N][AIMER_L + 1],
                              gf v_share[AIMER_T][AIMER_N],
                              const mult_chk_t mult_chk[AIMER_T][AIMER_N])
{
  gf epsilons[AIMER_L + 1];
  gf alpha[AIMER_L + 1];

  hash_instance ctx_e;
  hash_init(&ctx_e);
  hash_update(&ctx_e, sign->h_1, AIMER_COMMIT_SIZE);
  hash_final(&ctx_e);

  hash_instance ctx;
  hash_init_prefix(&ctx, HASH_PREFIX_2);
  hash_update(&ctx, sign->h_1, AIMER_COMMIT_SIZE);
  hash_update(&ctx, sign->salt, AIMER_SALT_SIZE);

  for (size_t rep = 0; rep < AIMER_T; rep++)
  {
    hash_squeeze(&ctx_e, (uint8_t *)epsilons, sizeof(epsilons));

    crypto_declassify(epsilons, sizeof(epsilons));

    for (size_t ell = 0; ell < AIMER_L + 1; ell++)
    {
      gf_set0(alpha[ell]);
    }

    for (size_t party = 0; party < AIMER_N; party++)
    {
      // alpha_share[ell] = a_share[ell] + x_share[ell] * eps[ell]
      // v_share = c_share - sum alpha[ell] * b_share[ell] + sum z_share[ell] * eps[ell]
      gf_copy(v_share[rep][party], mult_chk[rep][party].c_share);
      for (size_t ell = 0; ell < AIMER_L + 1; ell++)
      {
        gf_copy(alpha_shares[rep][party][ell],
                mult_chk[rep][party].a_shares[ell]);
        gf_mul_add(alpha_shares[rep][party][ell],
                   mult_chk[rep][party].x_shares[ell], epsilons[ell]);

        gf_add(alpha[ell], alpha[ell], alpha_shares[rep][party][ell]);

        gf_mul_add(v_share[rep][party],
                   mult_chk[rep][party].z_shares[ell], epsilons[ell]);
      }
    }

    // alpha is opened, so we can finish calculating v_share
    crypto_declassify(alpha, sizeof(alpha));
    for (size_t party = 0; party < AIMER_N; party++)
    {
      for (size_t ell = 0; ell < AIMER_L + 1; ell++)
      {
        gf_mul_add(v_share[rep][party],
                   mult_chk[rep][party].b_shares[ell], alpha[ell]);
      }
    }
    hash_update(&ctx, (const uint8_t *)alpha_shares[rep],
                AIM3_NUM_BYTES_FIELD * AIMER_N * (AIMER_L + 1));
    hash_update(&ctx, (const uint8_t *)v_share[rep],
                AIM3_NUM_BYTES_FIELD * AIMER_N);
  }
  hash_final(&ctx);
  hash_squeeze(&ctx, sign->h_2, AIMER_COMMIT_SIZE);
  hash_ctx_release(&ctx);
  hash_ctx_release(&ctx_e);
}

////////////////////////////////////////////////////////////////////////////////
// Returns 0 on success, -1 when a zero S-box input requires a retry with fresh
// randomness, and a value < -1 for a fatal error such as an allocation failure.
static int crypto_sign_keypair_internal(uint8_t *ct,
                                        const uint8_t *pt, const uint8_t *iv)
{
  return aim3_nozeroinpsb(ct, pt, iv);
}

int crypto_sign_keypair(uint8_t *pk, uint8_t *sk)
{
  if (!pk || !sk)
  {
    return -1;
  }

  uint8_t pt[AIM3_NUM_BYTES_FIELD];
  uint8_t iv[AIM3_IV_SIZE];
  uint8_t ct[AIM3_NUM_BYTES_FIELD];

  while (true)
  {
    randombytes(pt, AIM3_NUM_BYTES_FIELD);
    randombytes(iv, AIM3_IV_SIZE);
    int ret = crypto_sign_keypair_internal(ct, pt, iv);
    if (ret == 0)
    {
      break;
    }
    if (ret != -1)
    {
      // allocation failure: do not retry
      return -1;
    }
  }

  memcpy(pk, iv, AIM3_IV_SIZE);
  memcpy(pk + AIM3_IV_SIZE, ct, AIM3_NUM_BYTES_FIELD);
  memcpy(sk, pt, AIM3_NUM_BYTES_FIELD);
  memcpy(sk + AIM3_NUM_BYTES_FIELD, pk, AIM3_IV_SIZE + AIM3_NUM_BYTES_FIELD);

  return 0;
}

static int crypto_sign_signature_internal(uint8_t *sig, size_t *siglen,
                                          const uint8_t *m, size_t mlen,
                                          const uint8_t *pre, size_t prelen,
                                          const uint8_t *rnd, const uint8_t *sk)
{
  signature_t *sign = (signature_t *)sig;

  //////////////////////////////////////////////////////////////////////////
  // Phase 1: Committing to the seeds and the execution views of parties. //
  //////////////////////////////////////////////////////////////////////////

  // nodes for seed trees
  uint8_t (*nodes)[2 * AIMER_N - 1][AIMER_SEED_SIZE] =
    malloc(sizeof(*nodes) * AIMER_T);

  // commitments for seeds
  uint8_t (*commits)[AIMER_N][AIMER_COMMIT_SIZE] =
    malloc(sizeof(*commits) * AIMER_T);

  // multiplication check inputs
  mult_chk_t (*mult_chk)[AIMER_N] = malloc(sizeof(*mult_chk) * AIMER_T);

  // multiplication check outputs
  gf (*alpha_shares)[AIMER_N][AIMER_L + 1] =
    malloc(sizeof(*alpha_shares) * AIMER_T);
  gf (*v_share)[AIMER_N] = malloc(sizeof(*v_share) * AIMER_T);

  if (!nodes || !commits || !mult_chk || !alpha_shares || !v_share)
  {
    free(nodes);
    free(commits);
    free(mult_chk);
    free(alpha_shares);
    free(v_share);
    return -1;
  }

  memset(mult_chk, 0, AIMER_T * sizeof(*mult_chk));
  memset(alpha_shares, 0, AIMER_T * sizeof(*alpha_shares));
  memset(v_share, 0, AIMER_T * sizeof(*v_share));

  // commitments for phase 1
  if (run_phase_1(sign, commits, nodes, mult_chk, sk, rnd, m, mlen, pre,
                  prelen) != 0)
  {
    free(nodes);
    free(commits);
    free(mult_chk);
    free(alpha_shares);
    free(v_share);
    return -1;
  }

  /////////////////////////////////////////////////////////////////
  // Phase 2, 3: Challenging and committing to the simulation of //
  //             the multiplication checking protocol.           //
  /////////////////////////////////////////////////////////////////

  // compute the commitment of phase 3
  run_phase_2_and_3(sign, alpha_shares, v_share,
                    (const mult_chk_t (*)[AIMER_N])mult_chk);

  //////////////////////////////////////////////////////
  // Phase 4: Challenging views of the MPC protocols. //
  //////////////////////////////////////////////////////

  hash_instance ctx;
  hash_init(&ctx);
  hash_update(&ctx, sign->h_2, AIMER_COMMIT_SIZE);
  hash_final(&ctx);

  uint8_t indices[AIMER_T]; // AIMER_N <= 256
  hash_squeeze(&ctx, indices, AIMER_T);
  hash_ctx_release(&ctx);
  for (size_t rep = 0; rep < AIMER_T; rep++)
  {
    indices[rep] &= (1 << AIMER_LOGN) - 1;
  }

  //////////////////////////////////////////////////////
  // Phase 5: Opening the views of the MPC protocols. //
  //////////////////////////////////////////////////////

  crypto_declassify(indices, sizeof(indices));
  for (size_t rep = 0; rep < AIMER_T; rep++)
  {
    size_t i_bar = indices[rep];
    reveal_all_but(sign->proofs[rep].reveal_path,
                   (const uint8_t (*)[AIMER_SEED_SIZE])nodes[rep], i_bar);
    memcpy(sign->proofs[rep].missing_commitment, commits[rep][i_bar],
           AIMER_COMMIT_SIZE);
    for (size_t ell = 0; ell < AIMER_L + 1; ell++)
    {
      gf_to_bytes(sign->proofs[rep].missing_alpha_share_bytes[ell],
                  alpha_shares[rep][i_bar][ell]);
    }
  }
  *siglen = CRYPTO_BYTES;

  free(nodes);
  free(commits);
  free(mult_chk);
  free(alpha_shares);
  free(v_share);
  return 0;
}

int crypto_sign_signature(uint8_t *sig, size_t *siglen,
                          const uint8_t *m, size_t mlen,
                          const uint8_t *ctx, size_t ctxlen, const uint8_t *sk)
{
  if (ctxlen > 255)
  {
    return -1;
  }

  uint8_t prefix[256];
  uint8_t randomness[SECURITY_BYTES];

  prefix[0] = ctxlen;
  for (size_t i = 0; i < ctxlen; i++)
  {
    prefix[1 + i] = ctx[i];
  }

#ifdef RANDOMIZED_SIGNING
  randombytes(randomness, SECURITY_BYTES);
#else
  for (size_t i = 0; i < SECURITY_BYTES; i++)
  {
    randomness[i] = 0;
  }
#endif

  if (crypto_sign_signature_internal(sig, siglen, m, mlen, prefix, 1 + ctxlen,
                                     randomness, sk) != 0)
  {
    return -1;
  }
  return 0;
}

int crypto_sign(uint8_t *sm, size_t *smlen, const uint8_t *m, size_t mlen,
                const uint8_t *ctx, size_t ctxlen, const uint8_t *sk)
{
  int ret = crypto_sign_signature(sm + mlen, smlen, m, mlen, ctx, ctxlen, sk);
  if (ret != 0)
  {
    return ret;
  }

  memcpy(sm, m, mlen);
  *smlen += mlen;

  return 0;
}

static inline void recompute_last_share(const proof_t *proof, tape_t *tape)
{
  gf temp = {0,};

  gf_from_bytes(temp, proof->delta_pt_bytes);
  gf_add(tape->pt_share, tape->pt_share, temp);

  for (size_t ell = 0; ell < AIMER_L; ell++)
  {
    gf_from_bytes(temp, proof->delta_ys_bytes[ell]);
    gf_add(tape->y_shares[ell], tape->y_shares[ell], temp);
  }

  gf_from_bytes(temp, proof->delta_c_bytes);
  gf_add(tape->c_share, tape->c_share, temp);
}

static inline void recompute_missing_alpha(const proof_t *proof,
                                           gf *alpha_shares, gf *alpha)
{
  for (size_t ell = 0; ell < AIMER_L + 1; ell++)
  {
    gf_from_bytes(alpha_shares[ell],
                  proof->missing_alpha_share_bytes[ell]);
    gf_add(alpha[ell], alpha[ell], alpha_shares[ell]);
  }
}

static inline void recompute_v_shares(gf *v_shares,
                                      gf (*b_shares)[AIMER_L + 1],
                                      gf *alpha, size_t i_bar)
{
  gf_set0(v_shares[i_bar]);
  for (size_t party = 0; party < AIMER_N; party++)
  {
    if (party == i_bar)
    {
      continue;
    }
    for (size_t ell = 0; ell < AIMER_L + 1; ell++)
    {
      gf_mul_add(v_shares[party], b_shares[party][ell], alpha[ell]);
    }
    gf_add(v_shares[i_bar], v_shares[i_bar], v_shares[party]);
  }
}

static int crypto_sign_verify_internal(const uint8_t *sig, size_t siglen,
                                       const uint8_t *m, size_t mlen,
                                       const uint8_t *pre, size_t prelen,
                                       const uint8_t *pk)
{
  if (siglen != CRYPTO_BYTES)
  {
    return -1;
  }

  const signature_t *sign = (const signature_t *)sig;

  gf ct_gf = {0,};
  gf_from_bytes(ct_gf, pk + AIM3_IV_SIZE);

  // derive the binary matrix and the vector from the initial vector
  aim_lin_t *lin = malloc(sizeof(aim_lin_t));
  if (lin == NULL)
  {
    return -1;
  }
  aim3_generate_linear(lin, pk);

  hash_instance ctx_e, ctx_h1, ctx_h2;

  // indices = Expand(h_2)
  hash_init(&ctx_e);
  hash_update(&ctx_e, sign->h_2, AIMER_COMMIT_SIZE);
  hash_final(&ctx_e);

  uint8_t indices[AIMER_T]; // AIMER_N <= 256
  hash_squeeze(&ctx_e, indices, AIMER_T);
  hash_ctx_release(&ctx_e);
  for (size_t rep = 0; rep < AIMER_T; rep++)
  {
    indices[rep] &= (1 << AIMER_LOGN) - 1;
  }

  // epsilons = Expand(h_1)
  hash_init(&ctx_e);
  hash_update(&ctx_e, sign->h_1, AIMER_COMMIT_SIZE);
  hash_final(&ctx_e);

  // message pre-hashing
  uint8_t mu[AIMER_COMMIT_SIZE];
  hash_init_prefix(&ctx_h1, HASH_PREFIX_0);
  hash_update(&ctx_h1, pk, AIM3_IV_SIZE + AIM3_NUM_BYTES_FIELD);
  hash_update(&ctx_h1, pre, prelen);
  hash_update(&ctx_h1, m, mlen);
  hash_final(&ctx_h1);
  hash_squeeze(&ctx_h1, mu, AIMER_COMMIT_SIZE);
  hash_ctx_release(&ctx_h1);

  // ready for computing h_1' and h_2'
  hash_init_prefix(&ctx_h1, HASH_PREFIX_1);
  hash_update(&ctx_h1, mu, AIMER_COMMIT_SIZE);
  hash_update(&ctx_h1, sign->salt, AIMER_SALT_SIZE);

  hash_init_prefix(&ctx_h2, HASH_PREFIX_2);
  hash_update(&ctx_h2, sign->h_1, AIMER_COMMIT_SIZE);
  hash_update(&ctx_h2, sign->salt, AIMER_SALT_SIZE);

  uint8_t (*nodes)[AIMER_SEED_SIZE] =
    malloc(sizeof(*nodes) * (2 * AIMER_N - 2));

  gf (*b_shares)[AIMER_L + 1] = malloc(AIMER_N * sizeof(*b_shares));
  gf (*alpha_shares)[AIMER_L + 1] = malloc(AIMER_N * sizeof(*alpha_shares));
  gf *v_shares = malloc(AIMER_N * sizeof(gf));

  if (!nodes || !b_shares || !alpha_shares || !v_shares)
  {
    free(lin);
    free(nodes);
    free(b_shares);
    free(alpha_shares);
    free(v_shares);
    return -1;
  }

  for (size_t rep = 0; rep < AIMER_T; rep++)
  {
    size_t i_bar = indices[rep];
    reconstruct_tree(nodes, sign->salt, sign->proofs[rep].reveal_path,
                     rep, i_bar);

    gf epsilons[AIMER_L + 1];
    hash_squeeze(&ctx_e, (uint8_t *)epsilons, sizeof(epsilons));

    gf alpha[AIMER_L + 1];
    memset(alpha, 0, sizeof(alpha));

    tape_t tapes[AIMER_N];
    mult_chk_t mult_checks[AIMER_N];
    memset(tapes, 0, sizeof(tapes));
    memset(mult_checks, 0, sizeof(mult_checks));

    for (size_t party = 0; party < AIMER_N; party++)
    {
      if (party == i_bar)
      {
        hash_update(&ctx_h1, sign->proofs[rep].missing_commitment,
                    AIMER_COMMIT_SIZE);
        recompute_missing_alpha(&sign->proofs[rep], alpha_shares[i_bar], alpha);
        continue;
      }

      uint8_t commit[AIMER_COMMIT_SIZE];
      commit_and_expand_tape(&tapes[party], commit, sign->salt, rep, party,
                             nodes[AIMER_N + party - 2]);
      hash_update(&ctx_h1, commit, AIMER_COMMIT_SIZE);

      if (party == AIMER_N - 1)
      {
        recompute_last_share(&sign->proofs[rep], &tapes[party]);
      }
    }

    aim3_mpc_N(mult_checks, lin, tapes, ct_gf);

    for (size_t party = 0; party < AIMER_N; party++)
    {
      if (party == i_bar)
      {
        continue;
      }

      const mult_chk_t *mult_chk = &mult_checks[party];
      gf_copy(v_shares[party], mult_chk->c_share);
      for (size_t ell = 0; ell < AIMER_L + 1; ell++)
      {
        gf_copy(alpha_shares[party][ell], mult_chk->a_shares[ell]);
        gf_mul_add(alpha_shares[party][ell], mult_chk->x_shares[ell],
                   epsilons[ell]);
        gf_add(alpha[ell], alpha[ell], alpha_shares[party][ell]);

        gf_copy(b_shares[party][ell], mult_chk->b_shares[ell]);
        gf_mul_add(v_shares[party], mult_chk->z_shares[ell], epsilons[ell]);
      }
    }

    // alpha is opened, so we can finish calculating v_share
    recompute_v_shares(v_shares, b_shares, alpha, i_bar);

    // v is opened
    hash_update(&ctx_h2, (const uint8_t *)alpha_shares,
                AIM3_NUM_BYTES_FIELD * AIMER_N * (AIMER_L + 1));
    hash_update(&ctx_h2, (const uint8_t *)v_shares,
                AIM3_NUM_BYTES_FIELD * AIMER_N);
    // NOTE: depend on the order of values in proof_t
    hash_update(&ctx_h1, sign->proofs[rep].delta_pt_bytes,
                AIM3_NUM_BYTES_FIELD * (AIMER_L + 2));
  }
  hash_ctx_release(&ctx_e);

  uint8_t h_1_prime[AIMER_COMMIT_SIZE];
  hash_final(&ctx_h1);
  hash_squeeze(&ctx_h1, h_1_prime, AIMER_COMMIT_SIZE);
  hash_ctx_release(&ctx_h1);

  uint8_t h_2_prime[AIMER_COMMIT_SIZE];
  hash_final(&ctx_h2);
  hash_squeeze(&ctx_h2, h_2_prime, AIMER_COMMIT_SIZE);
  hash_ctx_release(&ctx_h2);

  free(lin);
  free(nodes);
  free(b_shares);
  free(alpha_shares);
  free(v_shares);

  if (memcmp(h_1_prime, sign->h_1, AIMER_COMMIT_SIZE) != 0 ||
      memcmp(h_2_prime, sign->h_2, AIMER_COMMIT_SIZE) != 0)
  {
    return -1;
  }

  return 0;
}

int crypto_sign_verify(const uint8_t *sig, size_t siglen,
                       const uint8_t *m, size_t mlen,
                       const uint8_t *ctx, size_t ctxlen, const uint8_t *pk)
{
  if (ctxlen > 255)
  {
    return -1;
  }

  uint8_t prefix[256];

  prefix[0] = ctxlen;
  for (size_t i = 0; i < ctxlen; i++)
  {
    prefix[1 + i] = ctx[i];
  }

  return crypto_sign_verify_internal(sig, siglen, m, mlen, prefix, 1 + ctxlen,
                                     pk);
}

int crypto_sign_open(uint8_t *m, size_t *mlen, const uint8_t *sm, size_t smlen,
                     const uint8_t *ctx, size_t ctxlen, const uint8_t *pk)
{
  if (smlen < CRYPTO_BYTES)
  {
    return -1;
  }

  const size_t message_len = smlen - CRYPTO_BYTES;
  const uint8_t *message = sm;
  const uint8_t *signature = sm + message_len;

  if (crypto_sign_verify(signature, CRYPTO_BYTES, message, message_len,
                         ctx, ctxlen, pk))
  {
    return -1;
  }

  memmove(m, message, message_len);
  *mlen = message_len;

  return 0;
}
