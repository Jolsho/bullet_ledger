/*
 * Bullet Ledger
 * Copyright (C) 2025 Joshua Olson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "kzg.h"

// ======================================================
// ========== SINGLE POINT SINGLE FUNCTION ==============
// ======================================================



// ================== EVAL POLYNOMIAL ====================

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

// ================== COMMIT POLYNOMIAL ==================

// commits to f(x) via evaluating f(r)
blst_p1 commit_g1_projective(
    const scalar_vec& coeffs, 
    const SRS& srs
) {
    blst_p1 C = new_p1();
    blst_p1 tmp;
    for (size_t i = 0; i < coeffs.size(); i++) {
        p1_mult(tmp, srs.g1_powers_jacob[i], coeffs[i]);
        blst_p1_add_or_double(&C, &C, &tmp);
    }
    return C;
}

blst_p1_affine commit_g1(
    const scalar_vec& coeffs, 
    const SRS& srs
) {
    blst_p1 C = commit_g1_projective(coeffs, srs);
    return p1_to_affine(C);
}

blst_p2_affine commit_g2(
    const scalar_vec& coeffs, 
    const SRS& srs
) {
    blst_p2 C = new_p2();
    blst_p2 tmp;
    for (size_t i = 0; i < coeffs.size(); i++) {
        p2_mult(tmp, srs.g2_powers_jacob[i], coeffs[i]);
        blst_p2_add_or_double(&C, &C, &tmp);
    }
    return p2_to_affine(C);
}


// ============= OPEN SINGLE (synthetic) =================

// Q(x) = (f(x) - f(z)) / (x - z)
scalar_vec derive_q(
    const scalar_vec& coeffs, 
    const blst_scalar& z
) {
    size_t n = coeffs.size();
    if (n < 2) return scalar_vec{};

    scalar_vec q(n - 1);
    blst_scalar curr = coeffs[n - 1];
    q[n - 2] = curr;

    blst_scalar next;
    for (int i = (int)n - 2; i >= 1; --i) {

        // next = (curr * z) + coeffs[i]
        blst_sk_mul_n_check(&next, &curr, &z);
        scalar_add_inplace(next, coeffs[i]);

        // push value into next slot
        q[i - 1] = next;

        // set curr as next
        curr = next;
    }
    return q;
}


// ============= VERIFY SINGLE POINT ====================

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
    p1_mult(C_Y, S.g, Y);
    blst_p1_cneg(&C_Y, true);
    // (- gY) + C == C - gY
    blst_p1_add_or_double_affine(&C_Y, &C_Y, &C);

    // to affine
    blst_p1_affine C_Y_aff = p1_to_affine(C_Y);


    // Compute g2(r - z)
    blst_p2 R_Z;
    // z -> g2(z) -> g2(-z)
    p2_mult(R_Z, S.h, Z);
    blst_p2_cneg(&R_Z, true);
    // g2(-z) + g2(r) == g2(r - z)
    blst_p2_add_or_double(&R_Z, &R_Z, &S.g2_powers_jacob[1]);

    // to affine
    blst_p2_affine R_Z_aff = p2_to_affine(R_Z);


    // e(C - gY, g2) == e(pi, g2(r - z))
    blst_fp12 lhs, rhs;
    blst_miller_loop(&lhs, &S.g2_powers_aff[0], &C_Y_aff);
    blst_miller_loop(&rhs, &R_Z_aff, &Pi);
    blst_final_exp(&lhs, &lhs);
    blst_final_exp(&rhs, &rhs);

    return blst_fp12_is_equal(&lhs, &rhs);
}

