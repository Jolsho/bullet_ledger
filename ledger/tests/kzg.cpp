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

#include <cassert>
#include "blst.h"
#include "fft.h"
#include "hashing.h"
#include "helpers.h"
#include "polynomial.h"
#include "settings.h"
#include "kzg.h"

void test_fft() {
    printf("TESTING f -> FFT -> IFFT == f \n");
    const size_t DEGREE = 256;

    // root * inv_root == 1
    NTTRoots roots = build_roots(DEGREE);
    for (size_t i = 0; i < DEGREE; i++) {
        blst_scalar tmp;
        blst_sk_mul_n_check(&tmp, &roots.roots[i], &roots.inv_roots[i]);
        assert(equal_scalars(tmp, num_scalar(1)));  
    }

    Scalar_vec evals(DEGREE, blst_scalar());

    Hash hash = new_hash();
    for (auto i = 0; i < DEGREE; i++) {
        seeded_hash(&hash, i);
        hash_to_sk(&evals[i], hash.h);
    }

    // evals from coeff to eval form
    Scalar_vec fx = evals;
    inverse_fft_in_place(fx, roots.inv_roots);

    Scalar_vec coeffs = fx;

    fft_in_place(fx, roots.roots);
    for (auto i = 0; i < DEGREE; i++) {
        assert(equal_scalars(evals[i], fx[i]));
    }

    // evals from eval to coeff form
    inverse_fft_in_place(evals, roots.inv_roots);
    for (auto i = 0; i < DEGREE; i++) {
        assert(equal_scalars(evals[i], coeffs[i]));
    }
    printf("FFT / IFFT SUCCESS\n\n");
}

void test_polynomial() {
    // f(x) = 2 + 3x + x^2
    Polynomial f = {
        num_scalar(2), 
        num_scalar(3), 
        num_scalar(1)
    };
    // ff(x) = (x + 1)(x + 2)
    Polynomial ff = {num_scalar(1)};
    ff = multiply_binomial(ff, num_scalar(1));
    ff = multiply_binomial(ff, num_scalar(2));
    for (auto i = 0; i < f.size(); i++)
        assert(equal_scalars(f[i],ff[i]));

    // f'(x) = 3 + 2x
    Polynomial df = {
        num_scalar(3), 
        num_scalar(2)
    };
    Polynomial dff = differentiate_polynomial(f);
    for (auto i = 0; i < df.size(); i++) 
        assert(equal_scalars(df[i],dff[i]));
}


void main_kzg() {
    test_fft();
    test_polynomial();

    printf("TESTING KZG SINGLE & BATCH \n");

    const size_t DEGREE = 256;
    KZGSettings settings = init_settings(DEGREE, num_scalar(69), "TAG");

    int count = 10;
    std::vector<blst_p1> Pis; Pis.reserve(count);
    std::vector<blst_p1> Cs; Cs.reserve(count);
    Scalar_vec Ys; Ys.reserve(count);
    std::vector<size_t> Z_idxs; Z_idxs.reserve(count);

    Scalar_vec evals(DEGREE, blst_scalar());
    Hash hash = new_hash();
    for (int k{}; k < count; k++) {
        int n = k * DEGREE;
        for (int i{n}; i < n + DEGREE; i++) {
            seeded_hash(&hash, i);
            hash_to_sk(&evals[i - n], hash.h);
        }

        // evals from eval to coeff form in fx
        Scalar_vec fx(evals);
        inverse_fft_in_place(fx, settings.roots.inv_roots);

        size_t idx = k;

        // PROVE AND VERIFY f(3)
        auto Pi = prove_kzg(evals, idx, settings).value();

        blst_scalar z = settings.roots.roots[idx];
        blst_scalar y = evals[idx];

        blst_p1 C;
        commit_g1(&C, fx, settings.setup);

        Cs.push_back(C);
        Pis.push_back(Pi);
        Ys.push_back(y);
        Z_idxs.push_back(idx);

        assert(verify_kzg(C, z, y, Pi, settings.setup));
        assert(!verify_kzg(C, settings.roots.roots[idx+1], y, Pi, settings.setup));
        assert(!verify_kzg(C, z, evals[idx+1], Pi, settings.setup));
    }

    seeded_hash(&hash, 2);
    assert(batch_verify(Pis, Cs, Z_idxs, Ys, hash, settings));
    
    Z_idxs[0]++;
    assert(!batch_verify(Pis, Cs, Z_idxs, Ys, hash, settings));
    Z_idxs[0]--;

    blst_scalar tmp = Ys[0];
    Ys[0] = num_scalar(2);
    assert(!batch_verify(Pis, Cs, Z_idxs, Ys, hash, settings));
    Ys[0] = tmp;

    printf("SUCCESSFUL KZG \n\n");

}
