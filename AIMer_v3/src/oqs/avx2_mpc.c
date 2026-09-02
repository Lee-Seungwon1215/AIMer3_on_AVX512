// SPDX-License-Identifier: MIT
// AIM3-specific MPC batching. This preserves each party's scalar equations and
// changes only their evaluation schedule.

#include "avx2_mpc.h"
#include "avx2_field128_batch.h"

#include <stddef.h>

void aim3_mpc_N(mult_chk_t mult_checks[AIMER_N], const aim_lin_t *lin,
                const tape_t tapes[AIMER_N], const gf ciphertext) {
	gf inputs[AIMER_N];
	gf outputs[AIMER_N];

	for (size_t party = 0; party < AIMER_N; party++) {
		for (size_t ell = 0; ell < AIMER_L + 1; ell++) {
			gf_copy(mult_checks[party].a_shares[ell],
			        tapes[party].a_shares[ell]);
		}
		for (size_t ell = 0; ell < AIMER_L; ell++) {
			gf_copy(mult_checks[party].b_shares[ell],
			        tapes[party].y_shares[ell]);
		}
		gf_copy(mult_checks[party].b_shares[AIMER_L],
		        tapes[party].pt_share);
		gf_copy(mult_checks[party].c_share, tapes[party].c_share);
		gf_copy(inputs[party], tapes[party].pt_share);
	}
	gf_add(mult_checks[AIMER_N - 1].b_shares[AIMER_L],
	       mult_checks[AIMER_N - 1].b_shares[AIMER_L], ciphertext);

	/* First AIM3 input affine layers share the same party inputs. */
	for (size_t ell = 0; ell < AIMER_L; ell++) {
		gf_mat_vec_mul_N(outputs, (const gf *)inputs, lin->mat_A[ell]);
		for (size_t party = 0; party < AIMER_N; party++) {
			gf_copy(mult_checks[party].x_shares[ell], outputs[party]);
		}
		gf_add(mult_checks[AIMER_N - 1].x_shares[ell],
		       mult_checks[AIMER_N - 1].x_shares[ell], lin->vec_b[ell]);
	}

	/* Final S-box input is the XOR of both output affine-layer shares. */
	for (size_t party = 0; party < AIMER_N; party++) {
		gf_set0(outputs[party]);
	}
	for (size_t ell = 0; ell < AIMER_L; ell++) {
		for (size_t party = 0; party < AIMER_N; party++) {
			gf_copy(inputs[party], tapes[party].y_shares[ell]);
		}
		gf_mat_vec_mul_add_N(outputs, (const gf *)inputs,
		                     lin->mat_A[ell + AIMER_L]);
	}
	for (size_t party = 0; party < AIMER_N; party++) {
		gf_copy(mult_checks[party].x_shares[AIMER_L], outputs[party]);
	}
	gf_add(mult_checks[AIMER_N - 1].x_shares[AIMER_L],
	       mult_checks[AIMER_N - 1].x_shares[AIMER_L],
	       lin->vec_b[AIMER_L]);

	/* Frobenius powers are independent across all parties. */
	for (size_t ell = 0; ell < AIMER_L + 1; ell++) {
		for (size_t party = 0; party < AIMER_N; party++) {
			gf_copy(inputs[party], mult_checks[party].x_shares[ell]);
		}
		gf_sqr_N(outputs, (const gf *)inputs);
		for (size_t exponent = 1; exponent < aim3_exponents[ell]; exponent++) {
			gf_sqr_N(outputs, (const gf *)outputs);
		}
		for (size_t party = 0; party < AIMER_N; party++) {
			gf_copy(mult_checks[party].z_shares[ell], outputs[party]);
		}
		mult_checks[AIMER_N - 1].z_shares[ell][0] ^= 1;
	}
}
