#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <vector>

#include "blst.h"
#include "blst_aux.h"
#include "pippen.h"
#include "path.h"

using std::vector;

//================================================
//============= UPDATE COMMITS ===================
//================================================

void update_parent(
    blst_p1_affine &C_parent,
    blst_p1 &delta
) {
    // convert parent to projective
    blst_p1 C_parent_pro;
    blst_p1_from_affine(&C_parent_pro, &C_parent);

    // add delta to parent
    blst_p1_add_or_double(&C_parent_pro, &C_parent_pro, &delta);

    // convert back to affine
    blst_p1_to_affine(&C_parent, &C_parent_pro);
}

// (v_new - v_old) * g1 == delta_p1
// C_parent += delta_p1
// return delta_p1
blst_p1 update_leaf_parent(
    blst_p1_affine &C_parent, 
    const blst_scalar &v_old,
    const blst_scalar &v_new
) {
    // delta = v_new - v_old
    blst_scalar delta = v_new;
    blst_sk_sub_n_check(&delta, &v_new, &v_old);

    // delta * G1
    blst_p1 delta_commit;
    blst_p1_mult(&delta_commit, blst_p1_generator(), delta.b, BIT_COUNT);

    update_parent(C_parent, delta_commit);

    // convert back to affine
    return delta_commit;
}

// use delta_p1 from update_leaf_parent() as delta_in
// C_parent is updated in place, and delta_in is scaled by child_alpha
void update_internal_parent(
    blst_p1_affine &C_parent,
    blst_p1 &delta_in,
    blst_scalar child_alpha
) {
    // scale delta
    blst_p1_mult(&delta_in, &delta_in, child_alpha.b, BIT_COUNT);
    update_parent(C_parent,  delta_in);
}



//================================================
//============= FIAT-SHAMIR CALCULATIONS =========
//================================================

// Compute challenge α = H(prev || value || z)
void compute_challenge_inplace(
    blst_scalar& prev,          // previous challenge
    const blst_scalar& value,   // leaf or node value at this level
    const blst_scalar& z        // evaluation point
) {
    
    // 32 bytes each for prev, value, z
    byte* buf = (byte*)malloc(96); 
    memcpy(buf, prev.b, 32);
    buf += 32;
    memcpy(buf, value.b, 32);
    buf += 32;
    memcpy(buf, z.b, 32);
    buf -= 64;

    uint8_t hash[32];
    blst_sha256(hash, buf, 96);
    free(buf);

    blst_scalar_from_lendian(&prev, hash);
}



//================================================
//============= DERIVE_N_VERIFY KZGs =============
//================================================

std::tuple<blst_p1_affine, blst_p1_affine> verkle_kzg( 
    const blst_scalar seed,             // initial challenge
    const vector<blst_p1> proofs,       // Pi_i
    const vector<blst_p1> commits,      // C_i 
    const vector<blst_scalar> zs,       // evaluation points
    const vector<blst_scalar> values    // v_i (leaf or child commitments)
) {
    assert(proofs.size() == commits.size());
    assert(proofs.size() == zs.size());
    assert(proofs.size() == values.size());

    blst_p1 pi_agg = proofs[0];
    blst_p1 c_agg = commits[0];

    blst_scalar alpha = seed;
    blst_p1 tmp;
    // start from i = 1
    for (size_t i = 1; i < proofs.size(); ++i) {

        // Fiat–Shamir
        compute_challenge_inplace(alpha, values[i], zs[i]);
        
        // pi_agg += alpha * Pi_j
        blst_p1_mult(&tmp, &proofs[i], alpha.b, BIT_COUNT);
        blst_p1_add_or_double(&pi_agg, &pi_agg, &tmp);

        // C_i
        blst_p1 child_minus_val = commits[i];

        // commit to v_i -> v_i_g
        blst_p1 v_i_g;
        blst_sk_to_pk_in_g1(&v_i_g, &values[i]);
        // negate sign
        blst_p1_cneg(&v_i_g, true);

        // C_i - v_i_g
        blst_p1_add_or_double(&child_minus_val, &child_minus_val, &v_i_g);

        // C_i - v_i_g * alpha
        blst_p1_mult(&tmp, &child_minus_val, alpha.b, BIT_COUNT);

        // c_agg += C_i - v_i_g * alpha
        blst_p1_add_or_double(&c_agg, &c_agg, &tmp);
    }

    // normalization
    blst_p1_affine pi_agg_aff, c_agg_aff;
    blst_p1_to_affine(&pi_agg_aff, &pi_agg);
    blst_p1_to_affine(&c_agg_aff, &c_agg);

    return std::make_tuple(c_agg_aff, pi_agg_aff);
}


bool verify_verkle_kzg(
    const blst_scalar &seed,            // Fiat-Shamir seed
    const blst_p1_affine &C_agg,        // agg commitment
    const blst_p1_affine &pi_agg,       // aggregated proof
    const vector<blst_scalar> &values,  // leaf values
    const vector<blst_scalar> &zs,      // leaf indices
    const SRS &S                        // SRS
) {
    if (values.size() != zs.size() || values.empty()) return false;

    blst_scalar alpha = seed;
    std::vector<blst_scalar> alphas(values.size());

    // z_sum = sum(a_i * z_i)
    // alpha_sum = sum(a_i)
    blst_scalar z_sum = new_scalar();
    blst_scalar alpha_sum = new_scalar();
    blst_scalar tmp_sk;
    for (size_t i = 0; i < values.size(); ++i) {

        // calculate a_i
        compute_challenge_inplace(alpha, values[i], zs[i]);
        alphas[i] = alpha;

        // sum(a_i * z_i)
        blst_sk_mul_n_check(&tmp_sk, &alphas[i], &zs[i]);
        scalar_add_inplace(z_sum, tmp_sk);

        // sum(a_i)
        scalar_add_inplace(alpha_sum, alphas[i]);
    }

    // Z_agg = g2(z_sum) - g2(r)(alpha_sum)
    blst_p2 Z_agg = *blst_p2_generator();

    // (Z_agg(as g2) *= z_sum) == g2(z_sum)
    blst_p2_mult(&Z_agg, &Z_agg, z_sum.b, BIT_COUNT);

    // -g2(r)(alpha_sum)
    blst_p2 tmp;
    blst_p2_mult(&tmp, &S.g2_powers_jacob[1], alpha_sum.b, BIT_COUNT);
    blst_p2_cneg(&tmp, true);

    // Z_agg(as g2(z_sum)) += -g2(r)(alpha_sum) == Z_agg
    blst_p2_add_or_double(&Z_agg, &Z_agg, &tmp);

    // Z_agg -> Z_agg_affine
    blst_p2_affine Z_aff;
    blst_p2_to_affine(&Z_aff, &Z_agg);


    // check pairing
    blst_pairing* ctx = (blst_pairing*)malloc(blst_pairing_sizeof());
    blst_pairing_init(ctx, false, nullptr, 0);

    // e(C_agg, Z_agg) == e(Pi_agg, g2)
    blst_pairing_aggregate_pk_in_g1(ctx, &C_agg, &Z_aff, nullptr, 0);
    blst_pairing_aggregate_pk_in_g1(ctx, &pi_agg, &S.g2_powers_aff[0], nullptr, 0);

    blst_pairing_commit(ctx);
    bool res = blst_pairing_finalverify(ctx);
    free(ctx);

    return res;
}
