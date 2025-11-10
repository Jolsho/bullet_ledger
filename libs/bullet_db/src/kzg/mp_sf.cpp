#include <cstdio>
#include <stdexcept>
#include <cassert>
#include "blst.h"
#include "kzg.h"

// ======================================================
// ========== MULTI_POINT SINGLE FUNCTION ===============
// ======================================================



// ============= DERIVE VANISHING POLYNOMIAL =============

// derive_Z: given evaluation points z[0..n-1], return coefficients of Z(X) = prod_j (X - z_j)
scalar_vec derive_Z(const scalar_vec &zs) {
    scalar_vec Z = { new_scalar(1) }; // start with 1
    for (const blst_scalar &z : zs) {

        // multiply Z by (X - z) -> new coeffs
        scalar_vec term = { neg_scalar(z), new_scalar(1) }; // -z + 1*X
        Z = poly_mul(Z, term);
    }
    poly_normalize(Z);
    return Z;
}


// ======== LAGRANGE INTERPOLATION POLYNOMIAL ============

// derive_I: Lagrange interpolation that returns polynomial I(X) of degree < n
// zs: x points, ys: corresponding y values (same length)
// I(X) =  SUM( y_i * L_i(X) ) where i == 0..len(zs) - 1
scalar_vec derive_I(const scalar_vec &zs, const scalar_vec &ys) {
    size_t n = zs.size();
    if (n != ys.size()) throw std::runtime_error("derive_I: point/value length mismatch");
    if (n == 0) return {};

    // naive O(n^2) Lagrange basis sum
    scalar_vec I = { new_scalar() };

    for (size_t i = 0; i < n; i++) {
        // compute L_i(X) = PRODUCT_[i!=k] (X - z_k) / (z_i - z_k)
        
        scalar_vec lagrange = { new_scalar(1) }; 

        blst_scalar denominator = new_scalar(1);

        for (size_t k = 0; k < n; k++) {
            if (k == i) continue;

            // multiply numer by (X - z_k)
            scalar_vec term = { neg_scalar(zs[k]), new_scalar(1) };
            lagrange = poly_mul(lagrange, term);

            // denom *= (z_i - z_k)
            scalar_mul_inplace(denominator, scalar_sub(zs[i], zs[k]));
        }

        if (scalar_is_zero(denominator)) 
            throw std::runtime_error("derive_I: duplicate interpolation abscissae (denominator zero)");

        // accumulate (scale lagrange by yi * inv_denom)
        I = poly_add(I, poly_scale(lagrange, scalar_mul(ys[i], inv_scalar(denominator))));
    }
    poly_normalize(I);
    return I;
}


// =============== QUOTIENT POLYNOMIAL ==================

// Returns coefficients of Q(X)
scalar_vec derive_q_multi(
    const scalar_vec &Fx, 
    const scalar_vec &zs, 
    const scalar_vec &ys, 
    const SRS &S
) {
    if (zs.size() != ys.size()) 
        throw std::runtime_error("derive_q_multi: points and values mismatch");

    // derive Z(X)
    scalar_vec Z = derive_Z(zs);

    // derive I(X) (interpolant)
    scalar_vec I = derive_I(zs, ys);

    // compute numerator N(X) = f(X) - I(X)
    scalar_vec N = poly_sub(Fx, I);

    // divide N by Z -> Q (quotient) and remainder R
    scalar_vec Q, R;
    poly_divmod(N, Z, Q, R);

    // Optional: check remainder is zero (if interpolation exactly matches f on those points)
    poly_normalize(R);
    if (!R.empty()) {
        // In theory if I interpolates f at zs, then f - I vanishes on zs and Z divides N.
        // If not zero, something is wrong (floating / field error, or degrees)
        throw std::runtime_error("derive_q_multi: non-zero remainder after dividing by Z (f - I not divisible by Z)");
    }

    // Q is the quotient polynomial sought
    poly_normalize(Q);
    return Q;

}


// ========== VERIFY MULTI_POINT_PROOF ==================

// e( C - g1(I), g2 )  ==  e( Pi, g2(Z) )
bool verify_multi_proof(
    blst_p1_affine &C_aff,
    scalar_vec &ys,
    scalar_vec &zs,
    blst_p1_affine &Pi,
    SRS &S
) {
    // basic checks
    // no points -> undefined for multi-open
    if (zs.size() != ys.size()) return false;
    if (zs.empty()) return false; 

    // interpolant I(X) for (zs, ys)
    scalar_vec Ix = derive_I(zs, ys);
    poly_normalize(Ix);

    // Z(X) = ∏(X - z_j)
    scalar_vec Zx = derive_Z(zs);
    poly_normalize(Zx);

    // commitments
    blst_p1_affine I_g1 = commit_g1(Ix, S);
    blst_p2_affine Z_g2 = commit_g2(Zx, S);

    //  C - g1(I);
    blst_p1 C = p1_from_affine(C_aff);
    blst_p1 I = p1_from_affine(I_g1);
    blst_p1_cneg(&I, true);
    blst_p1_add_or_double(&C, &C, &I);
    blst_p1_to_affine(&C_aff, &C);

    // e( C - g1(I), g2 )  ==  e( Pi, g2(Z) )
    blst_fp12 lhs, rhs;
    blst_miller_loop(&lhs, &S.g2_powers_aff[0], &C_aff);  
    blst_miller_loop(&rhs, &Z_g2, &Pi); 

    // Final exponentiation
    blst_final_exp(&lhs, &lhs);
    blst_final_exp(&rhs, &rhs);
    return blst_fp12_is_equal(&lhs, &rhs);
}



