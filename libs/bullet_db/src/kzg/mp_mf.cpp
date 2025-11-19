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
#include "blake3.h"
#include "points.h"
#include <array>


scalar_vec fiat_shamir_mm(
    const std::vector<blst_p1> &Cs,
    const std::vector<scalar_vec> &Ys_mat,
    const scalar_vec &Zs,
    const size_t m
) {
    blake3_hasher h;
    blake3_hasher_init(&h);

    // Domain separation
    blake3_hasher_update(&h, "KZG-multi-open", 15);

    // Hash commitments
    std::array<uint8_t,48> buf;
    for (auto &C : Cs) {
        blst_p1_compress(buf.data(), &C);
        blake3_hasher_update(&h, buf.data(), buf.size());
    }

    // Hash evaluation scalars
    for (auto &y_r : Ys_mat) {
        for (auto &y : y_r) {
            blake3_hasher_update(&h, y.b, 32);
        }
    }

    // Hash zs
    for (auto &z: Zs) 
        blake3_hasher_update(&h, z.b, 32);

    // Produce alpha
    std::array<uint8_t,32> digest;
    blake3_hasher_finalize(&h, digest.data(), digest.size());

    blst_scalar alpha;
    blst_scalar_from_le_bytes(&alpha, digest.data(), 32);

    // derive powers of alpha
    scalar_vec powers(m);
    powers[0] = new_scalar(1);
    for (size_t i = 1; i < m; ++i) 
        // field multiply
        blst_sk_mul_n_check(&powers[i], &powers[i-1], &alpha);
    
    return powers;
}

scalar_vec derive_aggregate_polynomial_mm(
    const std::vector<scalar_vec> &funcs,    // m polynomials
    const scalar_vec &alphas            // length m
) {
    size_t m = funcs.size();
    size_t deg = funcs[0].size();

    // Compute Fx = sum( alphas_i * funcs_i ) 
    scalar_vec Fx(deg, new_scalar());
    for (size_t i = 0; i < m; i++)
        Fx = poly_add(Fx, poly_scale(funcs[i], alphas[i]));

    return Fx;
}

scalar_vec derive_aggregate_evaluation(
    const std::vector<scalar_vec> &Ys_mat,   // m x k matrix
    const scalar_vec &alphas,           // length m
    const size_t m
) {
    size_t k = Ys_mat[0].size();
    blst_scalar tmp = new_scalar();

    // Y = sum( alphas_j * y_{j, i} )
    scalar_vec Y_agg(k, new_scalar(0));
    for (size_t i = 0; i < k; ++i) {
        for (size_t j = 0; j < m; ++j) {
            blst_sk_mul_n_check(&tmp, &alphas[j], &Ys_mat[j][i]);
            scalar_add_inplace(Y_agg[i], tmp);
        }
    }
    return Y_agg;
}


// return commit(funcs_aggregate) & Pi
std::tuple<blst_p1, blst_p1_affine> multi_func_multi_point_prover(
    const std::vector<scalar_vec> &funcs,
    const std::vector<blst_p1> &Cs,
    const scalar_vec &Zs,
    const std::vector<scalar_vec> &Ys_mat,
    const SRS &S
) {
    size_t m = funcs.size();
    size_t k = Zs.size();

    // alpha
    scalar_vec alphas = fiat_shamir_mm(Cs, Ys_mat, Zs, m);

    // compute aggregated polynomial Fx and aggregated Ys
    scalar_vec Fx = derive_aggregate_polynomial_mm(funcs, alphas);
    scalar_vec Y = derive_aggregate_evaluation(Ys_mat, alphas, funcs.size());

    // compute C_F efficiently from commitments Cs:
    blst_p1 C_F = new_p1();
    blst_p1 tmpP = new_p1();
    for (size_t j = 0; j < m; ++j) {
        p1_mult(tmpP, Cs[j], alphas[j]);
        blst_p1_add_or_double(&C_F, &C_F, &tmpP);
    }

    // compute quotient q(x) = (F(x) - I(x)) / D(x)
    scalar_vec Qx = derive_q_multi(Fx, Zs, Y, S);

    // Pi = commit_g1(Q, S)  (i.e., g(q(s)))
    blst_p1 Pi = commit_g1_projective(Qx, S);

    return {C_F, p1_to_affine(Pi)};
}

// e(C_agg - g(I(s)), g2) == e( Pi, g2(Z(s)))
bool multi_func_multi_point_verify(
    const std::vector<blst_p1> &Cs,
    const scalar_vec &Zs,
    const std::vector<scalar_vec> &Ys_mat,
    const blst_p1_affine &Pi,
    const SRS &S
) {
    // fiat shamir challenges
    auto alphas = fiat_shamir_mm(Cs, Ys_mat, Zs, Cs.size());

    // C_agg = sum( a *  C_i)
    blst_p1 C_agg = new_p1();
    for (size_t i = 0; i < Cs.size(); i++) {
        blst_p1 scaled;
        p1_mult(scaled, Cs[i], alphas[i]);
        blst_p1_add_or_double(&C_agg, &C_agg, &scaled);
    }

    // evaluation aggregate to derive I(x)
    scalar_vec Y_agg = derive_aggregate_evaluation(Ys_mat, alphas, Cs.size());

    // C_agg - C_I == C_aff
    scalar_vec I = derive_I(Zs, Y_agg);
    blst_p1 C_I = commit_g1_projective(I, S);
    blst_p1_cneg(&C_I, true);
    blst_p1_add_or_double(&C_agg, &C_agg, &C_I);
    blst_p1_affine C = p1_to_affine(C_agg);


    // g2(Z(x))
    scalar_vec Z = derive_Z(Zs);
    blst_p2_affine C_Z = commit_g2(Z, S);


    // e(C, g2) == e(Pi, C_Z)
    blst_fp12 lhs, rhs;
    blst_miller_loop(&lhs,&S.g2_powers_aff[0], &C);
    blst_miller_loop(&rhs, &C_Z, &Pi);
    // Final exponentiation
    blst_final_exp(&lhs, &lhs);
    blst_final_exp(&rhs, &rhs);
    return blst_fp12_is_equal(&lhs, &rhs);
}
