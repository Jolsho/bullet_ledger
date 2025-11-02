#include <cstdlib>
#include <cstring>
#include <blst.h>
#include "kzg.h"

// -------------------- eval polynomial --------------------
blst_scalar eval_poly(
    const scalar_vec& coeffs,
    blst_scalar& z
) {
    blst_scalar v_k = new_scalar();
    blst_scalar tmp;
    for (size_t i = 0; i < coeffs.size(); i++) {
        scalar_pow(tmp, z, i); // z^i
        scalar_mul_inplace(tmp, coeffs[i]);
        scalar_add_inplace(v_k, tmp);
    }
    return v_k;
}

// -------------------- commit polynomial --------------------
blst_p1 commit(
    const scalar_vec& coeffs, 
    const SRS& srs
) {
    blst_p1 C = new_p1();
    blst_p1 tmp;
    for (size_t i = 0; i < coeffs.size(); i++) {
        blst_p1_mult(&tmp, &srs.g1_powers_jacob[i], coeffs[i].b, BIT_COUNT);
        blst_p1_add_or_double(&C, &C, &tmp);
    }
    return C;
}


// -------------------- open at z (synthetic division) --------------------
scalar_vec derive_q(
    const scalar_vec& coeffs, 
    const blst_scalar& z
) {
    size_t n = coeffs.size();
    if (n < 2) return scalar_vec{};

    scalar_vec q(n - 1);
    blst_scalar curr = coeffs[n - 1];
    q[n - 2] = curr;

    for (int i = (int)n - 2; i >= 1; --i) {
        blst_scalar next;
        blst_sk_mul_n_check(&next, &curr, &z);
        scalar_add_inplace(next, coeffs[i]);
        q[i - 1] = next;
        curr = next;
    }
    return q;
}


bool verify_proof(
    const blst_p1& C,
    const blst_scalar& Y,
    const blst_scalar& Z,
    const blst_p1& Pi,
    const SRS& S 
) {
    // Compute C - g^{Y}
    blst_p1 neg_temp;
    blst_p1 C_minus_Y;

    // g^{Y} using generator g1_powers_jacob[0] (assumed)
    blst_p1_mult(&neg_temp, &S.g1_powers_jacob[0], Y.b, BIT_COUNT);
    blst_p1_cneg(&neg_temp, true);

    // C - g^{Y}
    blst_p1_add_or_double(&C_minus_Y, &C, &neg_temp);

    // Compute g2^{s - z} = g2^{s} * g2^{-z} 
    blst_p2 g2_neg_z; 
    blst_p2 g2_s_minus_z;

    // g2^{z}, multiply generator srs.h by z
    blst_p2_mult(&g2_neg_z, &S.h, Z.b, BIT_COUNT);
    blst_p2_cneg(&g2_neg_z, true);  // inverse

    // g2^{s - z} = g2^{s} * g2^{-z}
    blst_p2_add_or_double(&g2_s_minus_z, &S.g2_powers_jacob[1], &g2_neg_z);

    // Pairing check
    blst_fp12 lhs, rhs;

    // e(C - g^{Y}, g2)
    blst_p1_affine aff_C;
    blst_p1_to_affine(&aff_C, &C_minus_Y);
    blst_p2_affine aff_g2;
    blst_p2_to_affine(&aff_g2, &S.h);
    blst_miller_loop(&lhs, &aff_g2, &aff_C);
    blst_final_exp(&lhs, &lhs);

    // e(pi, g2^{s - z})
    blst_p1_affine aff_pi;
    blst_p1_to_affine(&aff_pi, &Pi);

    blst_p2_affine aff_g2_s_minus_z;
    blst_p2_to_affine(&aff_g2_s_minus_z, &g2_s_minus_z);

    blst_miller_loop(&rhs, &aff_g2_s_minus_z, &aff_pi);
    blst_final_exp(&rhs, &rhs);

    // Compare pairings
    return blst_fp12_is_equal(&lhs, &rhs);
}

// ------------- SRS ---------------------
size_t SRS::max_degree() {
        return g1_powers_jacob.size() - 1; 
}

SRS::SRS(size_t degree, const blst_scalar &s) {
    g1_powers_jacob.resize(degree + 1);
    g1_powers_aff.resize(degree + 1);

    g2_powers_jacob.resize(degree + 1);
    g2_powers_aff.resize(degree + 1);

    blst_p1 g1 = *blst_p1_generator();
    blst_p2 g2 = *blst_p2_generator();
    h = g2;

    // s^0 = 1
    blst_scalar pow_s = new_scalar(1);

    // tmp variables
    blst_p1 tmp1;
    blst_p1_affine tmp1_5;
    blst_p2 tmp2;
    blst_p2_affine tmp2_5;

    //blst_p1s_to_affine(blst_p1_affine *dst, const blst_p1 *const *points, size_t npoints)
    for (size_t i = 0; i <= degree; i++) {
        // G1: g1^(s^i)
        blst_p1_mult(&tmp1, &g1, pow_s.b, BIT_COUNT);
        g1_powers_jacob[i] = tmp1;

        blst_p1_to_affine(&tmp1_5, &tmp1);
        g1_powers_aff[i] = tmp1_5;

        // G2: g2^(s^i)
        blst_p2_mult(&tmp2, &g2, pow_s.b, BIT_COUNT);
        g2_powers_jacob[i] = tmp2;

        blst_p2_to_affine(&tmp2_5, &tmp2);
        g2_powers_aff[i] = tmp2_5;

        // pow_s *= s  (next power)
        scalar_mul_inplace(pow_s, s);
    }
}
