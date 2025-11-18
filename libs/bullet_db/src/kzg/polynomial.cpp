// SPDX-License-Identifier: GPL-2.0-only

#include "kzg.h"

// ---------------------------
// Polynomial helpers
// ---------------------------
// Polynomials represented as scalar_vec coeffs, low-to-high: a0 + a1*X + a2*X^2 + ...

void poly_normalize(scalar_vec &p) {
    while (!p.empty() && scalar_is_zero(p.back())) p.pop_back();
}

// Polynomial addition: c = a + b
scalar_vec poly_add(const scalar_vec &a, const scalar_vec &b) {
    size_t n = std::max(a.size(), b.size());
    scalar_vec c(n, new_scalar());
    for (size_t i = 0; i < n; i++) {
        blst_scalar ai = (i < a.size()) ? a[i] : new_scalar();
        blst_scalar bi = (i < b.size()) ? b[i] : new_scalar();
        blst_sk_add_n_check(&c[i], &ai, &bi);
    }
    poly_normalize(c);
    return c;
}

// Polynomial subtraction: c = a - b
scalar_vec poly_sub(const scalar_vec &a, const scalar_vec &b) {
    size_t n = std::max(a.size(), b.size());
    scalar_vec c(n, new_scalar());
    for (size_t i = 0; i < n; i++) {
        blst_scalar ai = (i < a.size()) ? a[i] : new_scalar();
        blst_scalar bi = (i < b.size()) ? b[i] : new_scalar();
        blst_sk_sub_n_check(&c[i], &ai, &bi);
    }
    poly_normalize(c);
    return c;
}

// Scalar multiplication of polynomial: c = a * scalar
scalar_vec poly_scale(const scalar_vec &a, const blst_scalar &s) {
    scalar_vec c(a.size());
    for (size_t i = 0; i < a.size(); i++) 
        blst_sk_mul_n_check(&c[i], &a[i], &s);
    poly_normalize(c);
    return c;
}

// Polynomial multiplication (naive O(n^2))
scalar_vec poly_mul(const scalar_vec &a, const scalar_vec &b) {
    if (a.empty() || b.empty()) return {};
    scalar_vec c(a.size() + b.size() - 1, new_scalar());

    // c[i+j] += a[i] * b[j]
    for (size_t i = 0; i < a.size(); i++) {
        for (size_t j = 0; j < b.size(); j++) {
            scalar_add_inplace(c[i + j], scalar_mul(a[i], b[j]));
        }
    }
    poly_normalize(c);
    return c;
}

// Polynomial long division: divide A by B -> quotient Q and remainder R such that A = B*Q + R
// Assumes B != 0 and deg(B) >= 0
void poly_divmod(const scalar_vec &A, const scalar_vec &B, scalar_vec &Q, scalar_vec &R) {
    if (B.empty()) throw std::runtime_error("poly_divmod: divide by zero polynomial");

    R = A;
    poly_normalize(R);
    scalar_vec divisor = B;
    poly_normalize(divisor);

    if (R.size() < divisor.size()) {
        Q.clear();
        poly_normalize(R);
        return;
    }

    // Initialize Q
    size_t n = R.size() - divisor.size() + 1;
    Q.assign(n, new_scalar());

    // Get inverse off last term in divisor
    blst_scalar inv_leading_divisor = inv_scalar(divisor.back());

    for (size_t k = 0; k < n; k++) {
        // process from high degree down
        size_t i = n - 1 - k; 

        // Q[i] = R[R_n - 1 + i] * inv_divisor_lead
        blst_sk_mul_n_check(&Q[i], &R[divisor.size() - 1 + i], &inv_leading_divisor);

        // R -= (coeff * x^i) * b
        for (size_t j = 0; j < divisor.size(); j++) 
            scalar_sub_inplace(R[i + j], scalar_mul(Q[i], divisor[j]));
    }

    // normalize remainder
    poly_normalize(Q);
    poly_normalize(R);
}
