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
#include <cstddef>
#include <cstdio>
#include <cstring>
#include "helpers.h"


void fft_in_place( 
    std::vector<blst_scalar> &a, 
    const std::vector<blst_scalar> &roots 
) { 
    size_t n = a.size(); 

    // Bit-reversal permutation
    size_t j{}; 
    for (size_t i{1}; i < n; i++) { 
        size_t bit = n >> 1; 
        for (; j & bit; bit >>= 1) 
            j ^= bit; 
        j ^= bit; 
        if (i < j) 
            std::swap(a[i], a[j]); 
    } 

    // Cooley–Tukey butterflies
    for (size_t len{2}; len <= n; len <<= 1) { 
        size_t half = len >> 1; 
        size_t step = n / len; 

        for (size_t i{}; i < n; i += len) { 
            size_t root_index{}; 

            for (size_t k{}; k < half; k++) { 
                blst_scalar t = a[i + k + half]; 
                blst_sk_mul_n_check(&t, &t, &roots[root_index]);

                blst_scalar u = a[i + k]; 
                blst_sk_add_n_check(&a[i + k], &u, &t);           // a[i+k] = u + t
                blst_sk_sub_n_check(&a[i + k + half], &u, &t);    // a[i+k+half] = u - t

                root_index += step; 
            } 
        } 
    } 
}


void inverse_fft_in_place(
    std::vector<blst_scalar> &a, 
    const std::vector<blst_scalar> &inv_roots
) {
    fft_in_place(a, inv_roots);

    blst_scalar inv_n = num_scalar(a.size());
    blst_sk_inverse(&inv_n, &inv_n);
    for (auto &x : a)
        blst_sk_mul_n_check(&x, &x, &inv_n);
}

