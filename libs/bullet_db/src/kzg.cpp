#include <cstdlib>
#include <cstring>
#include <blst.h>
#include "kzg.h"

// -------------------- eval polynomial --------------------
blst_scalar eval_poly(
    const scalar_vec& Fx,
    const blst_scalar& z
) {
    // f(z) = y
    blst_scalar Y = new_scalar();
    for (int i = Fx.size() - 1; i >= 0; i--) {
        scalar_mul_inplace(Y, z);
        scalar_add_inplace(Y, Fx[i]);
    }
    return Y;
}

// -------------------- commit polynomial --------------------
blst_p1_affine commit(
    const scalar_vec& coeffs, 
    const SRS& srs
) {
    blst_p1 C = new_p1();
    blst_p1 tmp;
    for (size_t i = 0; i < coeffs.size(); i++) {
        blst_p1_mult(&tmp, &srs.g1_powers_jacob[i], coeffs[i].b, BIT_COUNT);
        blst_p1_add_or_double(&C, &C, &tmp);
    }
    blst_p1_affine C_aff;
    blst_p1_to_affine(&C_aff, &C);
    return C_aff;
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
    const blst_p1_affine& C,
    const blst_scalar& Y,
    const blst_scalar& Z,
    const blst_p1_affine& Pi,
    const SRS& S 
) {
    // Compute C - gY
    blst_p1 C_Y;
    // 0 -> gY -> -gY
    blst_p1_mult(&C_Y, &S.g1_powers_jacob[0], Y.b, BIT_COUNT);
    blst_p1_cneg(&C_Y, true);
    // (- gY) + C == C - gY
    blst_p1_add_or_double_affine(&C_Y, &C_Y, &C);

    // to affine
    blst_p1_affine C_Y_aff;
    blst_p1_to_affine(&C_Y_aff, &C_Y);


    // Compute g2(r - z)
    blst_p2 R_Z;
    // z -> g2(z) -> g2(-z)
    blst_p2_mult(&R_Z, &S.h, Z.b, BIT_COUNT);
    blst_p2_cneg(&R_Z, true);
    // g2(-z) + g2(r) == g2(r - z)
    blst_p2_add_or_double(&R_Z, &R_Z, &S.g2_powers_jacob[1]);

    // to affine
    blst_p2_affine R_Z_aff;
    blst_p2_to_affine(&R_Z_aff, &R_Z);


    // e(C - gY, g2) == e(pi, g2(r - z))
    blst_fp12 lhs, rhs;
    blst_miller_loop(&lhs, &S.g2_powers_aff[0], &C_Y_aff);
    blst_miller_loop(&rhs, &R_Z_aff, &Pi);
    blst_final_exp(&lhs, &lhs);
    blst_final_exp(&rhs, &rhs);

    return blst_fp12_is_equal(&lhs, &rhs);
}



// =======================================
// ============= SRS =====================
// =======================================
size_t SRS::max_degree() { return g1_powers_jacob.size() - 1; }

SRS::SRS(size_t degree, const blst_scalar &s) {
    g1_powers_jacob.resize(degree + 1);
    g1_powers_aff.resize(degree + 1);

    g2_powers_jacob.resize(degree + 1);
    g2_powers_aff.resize(degree + 1);

    blst_p1 g1 = *blst_p1_generator();
    blst_p2 g2 = *blst_p2_generator();
    h = g2;

    // s(0) = 1
    blst_scalar pow_s = new_scalar(1);

    // Compute Jacobian powers
    for (size_t i = 0; i <= degree; i++) {
        blst_p1_mult(&g1_powers_jacob[i], &g1, pow_s.b, BIT_COUNT);
        blst_p2_mult(&g2_powers_jacob[i], &g2, pow_s.b, BIT_COUNT);
        scalar_mul_inplace(pow_s, s);
    }

    // Convert all to affine in a separate loop
    for (size_t i = 0; i <= degree; i++) {
        blst_p1_to_affine(&g1_powers_aff[i], &g1_powers_jacob[i]);
        blst_p2_to_affine(&g2_powers_aff[i], &g2_powers_jacob[i]);
    }
}
