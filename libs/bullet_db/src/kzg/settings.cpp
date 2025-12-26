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

#include "bigint.h"
#include "settings.h"
#include "helpers.h"

NTTRoots build_roots(size_t n) {
    
    std::string p("0x73eda753299d7d483339d80809a1d80553bda402fffe5bfeffffffff00000001");
    BigInt m = BigInt::from_hex(p.data());
    m.sub_u64(1);
    m.div_u64(n);

    // g = 5
    blst_scalar g = num_scalar(5);

    // w = g^m
    blst_scalar w = modular_pow(g, m);
    
    std::vector<blst_scalar> roots(n);
    std::vector<blst_scalar> inv_roots(n);

    roots[0] = num_scalar(1);
    inv_roots[0] = num_scalar(1);


    for (size_t i{1}; i < n; i++) {
        blst_sk_mul_n_check(&roots[i], &roots[i - 1], &w);
        blst_sk_inverse(&inv_roots[i], &roots[i]);
    }

    // SANITY CHECKS
    BigInt nn(n);
    
    // w^n == 1
    assert(equal_scalars(modular_pow(w, nn), num_scalar(1)));  

    // w^(n/2) != 1
    nn.div_u64(2);
    assert(!equal_scalars(modular_pow(w, nn), num_scalar(1)));  
    
    return {roots, inv_roots};
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

    g = *blst_p1_generator();
    h = *blst_p2_generator();

    // s(0) = 1
    blst_scalar pow_s = num_scalar(1);

    // Compute Jacobian powers
     
    for (size_t i{}; i <= degree; i++) {

        blst_p1_mult(&g1_powers_jacob[i], &g, pow_s.b, 256);
        blst_p2_mult(&g2_powers_jacob[i], &h, pow_s.b, 256);

        blst_sk_mul_n_check(&pow_s, &pow_s, &s);
    }

    // Convert all to affine in a separate loop
    for (size_t i{}; i <= degree; i++) {
        blst_p1_to_affine(&g1_powers_aff[i], &g1_powers_jacob[i]);
        blst_p2_to_affine(&g2_powers_aff[i], &g2_powers_jacob[i]);
    }
}

KZGSettings init_settings(size_t degree, const blst_scalar &s, std::string tag) {
    NTTRoots roots = build_roots(degree);
    SRS setup(degree, s);
    return { roots, setup , tag};
}
