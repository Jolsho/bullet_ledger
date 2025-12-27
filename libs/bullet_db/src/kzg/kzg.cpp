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

#include "fft.h"
#include "kzg.h"
#include "helpers.h"
#include "polynomial.h"
#include <cassert>

// Returns C, Pi
std::optional<blst_p1> prove_kzg(
    const Scalar_vec &evals,
    const size_t eval_idx,
    const KZGSettings &s
) {

    blst_scalar z = s.roots.roots[eval_idx];
    blst_scalar y = evals[eval_idx];

    auto q_opt = derive_quotient(evals, z, y, s.roots);
    if (!q_opt.has_value()) return std::nullopt;

    // Q -> coeff form
    Scalar_vec Q = q_opt.value();
    inverse_fft_in_place(Q, s.roots.inv_roots);

    // COMMIT TO Q
    blst_p1 P; 
    commit_g1(&P, Q, s.setup);


    // eval -> coeff form
    Scalar_vec fx(evals);
    inverse_fft_in_place(fx, s.roots.inv_roots);


    return {P};
}

bool verify_kzg(
    const blst_p1 C, 
    const blst_scalar z, 
    const blst_scalar y, 
    const blst_p1 Pi, 
    const SRS &S
) {
    assert(S.g1_powers_aff.size() >= 2);
    assert(S.g2_powers_aff.size() >= 2);

    // tmp = - [y]_1
    blst_p1 tmp;
    blst_p1_mult(&tmp, blst_p1_generator(), y.b, 256);
    blst_p1_cneg(&tmp, true);

    // C_Y_PI_Z = C + tmp
    blst_p1 C_Y_PI_Z;
    blst_p1_add_or_double(&C_Y_PI_Z, &C, &tmp);

    // tmp = z * Pi
    blst_p1_mult(&tmp, &Pi, z.b, 256);

    // C_Y_PI_Z + tmp
    blst_p1_add_or_double(&C_Y_PI_Z, &C_Y_PI_Z, &tmp);

    blst_p1_affine C_aff, Pi_aff;
    blst_p1_to_affine(&C_aff, &C_Y_PI_Z);
    blst_p1_to_affine(&Pi_aff, &Pi);

    
    // e(C - [y]_1 + (z * Pi), g2) = e(Pi, [s]_2)
    blst_fp12 lhs, rhs;
    blst_miller_loop(&lhs, &S.g2_powers_aff[0], &C_aff);
    blst_miller_loop(&rhs, &S.g2_powers_aff[1], &Pi_aff);

    blst_final_exp(&lhs, &lhs);
    blst_final_exp(&rhs, &rhs);

    return blst_fp12_finalverify(&lhs, &rhs);
}

void fiat_shamir(
    Hash* out,
    const blst_p1 &C,
    const blst_p1 &Pi,
    const blst_scalar &Z,
    const blst_scalar &Y,
    const Hash &base_r,
    byte buff[48]
) {
    BlakeHasher hasher;
    hasher.update(base_r.h, 32);
    hasher.update(Z.b, 32);
    hasher.update(Y.b, 32);

    blst_p1_compress(buff, &C);
    hasher.update(buff, 48);

    blst_p1_compress(buff, &Pi);
    hasher.update(buff, 48);

    hasher.finalize(out->h);
}

bool batch_verify(
    std::vector<blst_p1> &Pis,
    std::vector<blst_p1> &Cs,
    std::vector<size_t> &Z_idxs,
    Scalar_vec &Ys,
    Hash base_r,
    const KZGSettings &kzg
) {
    assert(Pis.size() == Cs.size());
    assert(Ys.size() == Z_idxs.size());
    assert(Pis.size() == Ys.size());

    blst_p1 agg_left = new_inf_p1();
    blst_p1 agg_right = new_inf_p1();
    blst_p1 tmp;
    blst_scalar Z, r;
    byte buff[48];
    Hash hash = new_hash();

    for(int i{}; i < Pis.size(); i++) {

        Z = kzg.roots.roots[Z_idxs[i]];

        // derive random scalar r via fiat-shamir
        fiat_shamir(&hash, Cs[i], Pis[i], Z, Ys[i], base_r, buff);
        hash_to_sk(&r, hash.h);
        if (scalar_is_zero(r)) return false;

        // tmp = - g1(Y)
        blst_p1_mult(&tmp, blst_p1_generator(), Ys[i].b, 256);
        blst_p1_cneg(&tmp, true);

        // tmp = C - g1(Y)
        blst_p1_add_or_double(&tmp, &tmp, &Cs[i]);

        // tmp = r * (C - g1(Y))
        blst_p1_mult(&tmp, &tmp, r.b, 256);

        // aggregate (Pi * r) to left hand side
        blst_p1_add_or_double(&agg_right, &agg_right, &tmp);

        // tmp = (Pi * r)
        blst_p1_mult(&tmp, &Pis[i], r.b, 256);

        blst_p1_add_or_double(&agg_left, &agg_left, &tmp);

        // tmp = (Pi * r * z)
        blst_p1_mult(&tmp, &tmp, Z.b, 256);
        
        // aggregate (C - g1(y) + (z * r * Pi)) to right hand side
        blst_p1_add_or_double(&agg_right, &agg_right, &tmp);
    }

    blst_p1_affine l_aff, r_aff;
    blst_p1_to_affine(&r_aff, &agg_right);
    blst_p1_to_affine(&l_aff, &agg_left);

    
    //  e(SUM(Pi_i * r_i), g2(s)) == 
    //  e(SUM(r_i * (C_i - g1(y_i)) + (z_i * r_i * Pi_i )) , g2)
    blst_fp12 lhs, rhs;
    blst_miller_loop(&lhs, &kzg.setup.g2_powers_aff[1], &l_aff);
    blst_miller_loop(&rhs, &kzg.setup.g2_powers_aff[0], &r_aff);

    blst_final_exp(&lhs, &lhs);
    blst_final_exp(&rhs, &rhs);


    return blst_fp12_finalverify(&lhs, &rhs);
}
