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

#include "helpers.h"
#include "polynomial.h"
#include "kzg.h"

// ================== COMMIT POLYNOMIAL ==================
// commits to f(x) via evaluating f(r)
void commit_g1(
    blst_p1* C,
    const Polynomial& coeffs, 
    const SRS& srs
) {
    // TODO -- more effecient way to do this?
    blst_p1 tmp;
    for (size_t i = 0; i < coeffs.size(); i++) {
        blst_p1_mult(&tmp, &srs.g1_powers_jacob[i], coeffs[i].b, 256);
        blst_p1_add_or_double(C, C, &tmp);
    }
}


Polynomial multiply_binomial(const Polynomial &P, const blst_scalar &w) {
    size_t d = P.size();
    Polynomial Q(d + 1, num_scalar(0));

    // Q[i+1] = P[i] (shift)
    // Q[i]   = P[i] * w (added to existing term)
    for (size_t i = 0; i < d; i++) {
        blst_scalar tmp;
        // P[i] * w
        blst_sk_mul_n_check(&tmp, &P[i], &w);  

        // add to Q[i] (coefficient of x^i)
        blst_sk_add_n_check(&Q[i], &Q[i], &tmp);   

        // add to Q[i+1] (coefficient of x^(i+1))
        blst_sk_add_n_check(&Q[i+1], &Q[i+1], &P[i]); 
    }

    return Q;
}


Polynomial differentiate_polynomial(const Polynomial &f) {
    if (f.size() == 0) return {};
    Polynomial df(f.size() - 1);

    for (auto i = 0; i < df.size(); i++) {
        blst_scalar pow = num_scalar(i + 1);
        blst_sk_mul_n_check(&df[i], &f[i + 1], &pow);
    }

    return df;
}

bool batch_inv(Scalar_vec &out, const Scalar_vec &in) {
    int i;
    blst_scalar accumulator = ONE_SK;
    for (i = 0; i < out.size(); i++) {
        out[i] = accumulator;
        blst_sk_mul_n_check(&accumulator, &accumulator, &in[i]);
    }

    if (scalar_is_zero(accumulator)) return false;

    blst_sk_inverse(&accumulator, &accumulator);

    for (i = out.size() - 1; i >= 0; i--) {
        blst_sk_mul_n_check(&out[i], &out[i], &accumulator);
        blst_sk_mul_n_check(&accumulator, &accumulator, &in[i]);
    }

    return true;
}

std::optional<Polynomial> derive_quotient(
    const Scalar_vec &poly_eval,
    const blst_scalar &z,
    const blst_scalar &y,
    const NTTRoots &roots
) {
    int i;
    uint64_t m = 0;
    size_t len = poly_eval.size();

    Scalar_vec inverses(len, ZERO_SK);
    Scalar_vec inverses_in(len);
    Scalar_vec q_poly(len);

    for (i = 0; i < len; i++) {
        if (equal_scalars(z, roots.roots[i])) {
            m = i + 1;
            inverses_in[i] = ONE_SK;
            continue;
        }

        // (p_i - y) / (w_i - z)
        blst_sk_sub_n_check(&q_poly[i], &poly_eval[i], &y);
        blst_sk_sub_n_check(&inverses_in[i], &roots.roots[i], &z);
    }

    if (!batch_inv(inverses, inverses_in)) return std::nullopt;

    for (i = 0; i < len; i++) {
        blst_sk_mul_n_check(&q_poly[i], &q_poly[i], &inverses[i]);
    }

    blst_scalar tmp;
    if (m != 0) {
        q_poly[--m] = ZERO_SK;
        for (i = 0; i < len; i++) {
            if (i == m) continue;

            // Build denominator: z * (z - w_i) 
            blst_sk_sub_n_check(&tmp, &z, &roots.roots[i]);
            blst_sk_mul_n_check(&inverses_in[i], &tmp, &z);
        }

        if (!batch_inv(inverses, inverses_in)) return std::nullopt;

        for (i = 0; i < len; i++) {
            if (i == m) continue;

            // Build Numerator: w_i * (p_i - y)
            blst_sk_sub_n_check(&tmp, &poly_eval[i], &y);
            blst_sk_mul_n_check(&tmp, &tmp, &roots.roots[i]);

            // Do the division: (p_i - y) * w_i / (z * (z - w_i))
            blst_sk_mul_n_check(&tmp, &tmp, &inverses[i]);
            blst_sk_add_n_check(&q_poly[m], &q_poly[m], &tmp);
        }
    }

    return q_poly;
}
