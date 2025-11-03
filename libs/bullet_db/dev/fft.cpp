#include <cstring>
#include <blst.h>
#include "../src/pnt_sclr.h"

// Negate scalar
inline blst_scalar scalar_neg(const blst_scalar& a) {
    blst_scalar neg = new_scalar();
    blst_sk_sub_n_check(&neg, &neg, &a);   // neg = 0 - a
    return neg;
}

// BULK POLY EVAL
// Naive DFT over scalars
scalar_vec dft(
    const scalar_vec& coeffs, 
    const scalar_vec& roots
) {
    size_t N = roots.size();
    scalar_vec eval(N, new_scalar());

    for (size_t i = 0; i < N; ++i) {
        blst_scalar acc = new_scalar(); 
        blst_scalar_from_uint64(&acc, 0);  // acc = 0
        blst_scalar x_pow = new_scalar(1);  // x^0 = 1

        for (size_t j = 0; j < coeffs.size(); ++j) {
            blst_scalar tmp;
            blst_sk_mul_n_check(&tmp, &coeffs[j], &x_pow);
            scalar_add_inplace(acc, tmp);
            scalar_mul_inplace(x_pow, roots[i]);
        }
        eval[i] = acc;
    }
    return eval;
}

// Naive inverse DFT
scalar_vec idft(
    const scalar_vec& evals, 
    const scalar_vec& roots, 
    const blst_scalar& invN
) {
    size_t N = roots.size();
    scalar_vec coeffs(N, new_scalar());

    for (size_t i = 0; i < N; ++i) {
        blst_scalar acc = new_scalar();
        blst_scalar_from_uint64(&acc, 0);

        for (size_t j = 0; j < N; ++j) {
            blst_scalar w_pow;
            size_t idx = (j * i) % N;
            blst_sk_mul_n_check(&w_pow, &evals[j], &roots[idx]);
            blst_sk_add_n_check(&acc, &acc, &w_pow);
        }
        blst_sk_mul_n_check(&coeffs[i], &acc, &invN);
    }
    return coeffs;
}

// Polynomial multiplication (naive)
scalar_vec poly_mul(
    const scalar_vec& A, 
    const scalar_vec& B
) {
    scalar_vec R(A.size() + B.size() - 1, new_scalar());
    blst_scalar tmp;
    for (size_t i = 0; i < A.size(); ++i) {
        for (size_t j = 0; j < B.size(); ++j) {
            blst_sk_mul_n_check(&tmp, &A[i], &B[j]);
            scalar_add_inplace(R[i + j], tmp);
        }
    }
    return R;
}

// Compute powers of a primitive root (roots of unity)
scalar_vec get_roots_of_unity(
    size_t N, 
    const blst_scalar& root
) {
    scalar_vec roots(N, new_scalar());
    roots[0] = new_scalar(1);
    for (size_t i = 1; i < N; ++i) {
        blst_sk_mul_n_check(&roots[i], &roots[i - 1], &root);
    }
    return roots;
}

// Compute Q(x) = P(x) / Z(x) using FFT
scalar_vec derive_q_fft(
    const scalar_vec& coeffs, 
    const scalar_vec& zs, 
    const blst_scalar& primitive_root
) {
    size_t n = coeffs.size();
    if (n < 2) return scalar_vec{};

    // 1. Build vanishing polynomial Z(x) = prod(x - z_i)
    scalar_vec Z = {new_scalar(1)}; // start with 1
    for (auto z : zs) {
        scalar_vec term = {scalar_neg(z), new_scalar(1)}; // (x - z)
        Z = poly_mul(Z, term);
    }

    // 2. Next power-of-2 for FFT
    size_t N = 1;
    while (N < n + Z.size() - 1) N <<= 1;

    // 3. Compute roots of unity
    blst_scalar root_N = primitive_root; // primitive N-th root of unity in the field
    scalar_vec roots = get_roots_of_unity(N, root_N);

    // 4. Zero-pad P and Z
    scalar_vec P_pad = coeffs; P_pad.resize(N, new_scalar());
    scalar_vec Z_pad = Z; Z_pad.resize(N, new_scalar());

    // 5. FFT evaluation
    scalar_vec P_eval = dft(P_pad, roots);
    scalar_vec Z_eval = dft(Z_pad, roots);

    // 6. Pointwise division
    scalar_vec Q_eval(N, new_scalar());
    blst_scalar inv;
    for (size_t i = 0; i < N; ++i) {
        blst_sk_inverse(&inv, &Z_eval[i]);       // 1 / Z_eval[i]
        blst_sk_mul_n_check(&Q_eval[i], &P_eval[i], &inv);
    }

    // 7. Inverse FFT
    blst_scalar sc_N = new_scalar(N);
    blst_scalar invN; blst_sk_inverse(&invN, &sc_N);
    scalar_vec Q_coeffs = idft(Q_eval, roots, invN);

    return Q_coeffs;
}

