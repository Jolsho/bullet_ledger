#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "blst.h"
#include "../src/kzg.h"
#include "../src/pippen.h"
#include "../src/path.h"

using std::vector;

// Computes the quotient polynomial q(x) = (f(x) - y_alpha) / Z(x)
// coeffs: coefficients of f(x) (lowest degree first)
// Zs: evaluation points z_1..z_t
// y_alpha: combined evaluation using alpha linear combination
scalar_vec derive_q_multi(
    const scalar_vec& coeffs, 
    const scalar_vec& Z_poly, 
    const blst_scalar& y_alpha
) {
    size_t n = coeffs.size();
    if (n == 0 || Z_poly.empty()) return scalar_vec{};

    // Step 2: Compute f_adj(x) = f(x) - y_alpha
    scalar_vec f_adj = coeffs;
    scalar_sub_inplace(f_adj[0], y_alpha);

    // Step 3: Polynomial long division q(x) = f_adj(x) / Z(x)
    // We'll implement simple long division (highest-degree first)
    size_t deg_f = f_adj.size() - 1;
    size_t deg_Z = Z_poly.size() - 1;
    if (deg_f < deg_Z) return scalar_vec{0}; // degree too small

    scalar_vec q(deg_f - deg_Z + 1, new_scalar());
    scalar_vec remainder = f_adj;

    for (int i = deg_f; i >= (int)deg_Z; i--) {
        blst_scalar coeff_qi;
        blst_scalar_div(&coeff_qi, &remainder[i], &Z_poly[deg_Z]); // quotient term
        q[i - deg_Z] = coeff_qi;

        // Subtract coeff_qi * Z(x) * x^{i-deg_Z} from remainder
        for (size_t j = 0; j <= deg_Z; j++) {
            blst_scalar term;
            blst_sk_mul_n_check(&term, &coeff_qi, &Z_poly[j]);
            scalar_sub_inplace(remainder[j + i - deg_Z], term);
        }
    }

    return q; // coefficients of quotient polynomial
}

// Correct KZG multipoint batch proof using a single α
void prove_multi_point(
    blst_p1* Pi_agg,                  // output: aggregate proof
    const blst_scalar &seed,          // Fiat-Shamir seed
    const vector<blst_scalar> &Ys,    // evaluation results
    const vector<blst_scalar> &Zs,    // evaluation points
    const vector<blst_scalar> &coeffs,// polynomial coefficients a_0..a_d
    const SRS &S                       // SRS: g^{τ^i} elements
) {
    size_t t = Ys.size();
    assert(Zs.size() == t);

    // Step 1: compute Fiat-Shamir challenge
    // TODO -- this is not right need a better hash function
    blst_scalar alpha = seed;
    for (size_t i = 0; i < t; i++) {
        compute_challenge_inplace(alpha, Ys[i], Zs[i]); // hash seed, y_i, z_i
    }

    // Step 2: compute Z(x) = Π (x - z_i)
    scalar_vec Z_poly(1, new_scalar(1)); // start with Z(x) = 1
    for (size_t i = 0; i < t; i++) {

        // multiply Z_poly by (x - z_i)
        vector<blst_scalar> new_poly(Z_poly.size() + 1);

        for (size_t k = 0; k < Z_poly.size(); k++) {

            // -z_i
            blst_scalar zp_k_neg;
            blst_sk_sub_n_check(&zp_k_neg, &zp_k_neg, &Z_poly[k]);

            blst_sk_mul_n_check(&new_poly[k], &zp_k_neg, &Zs[i]);
            new_poly[k+1] = Z_poly[k];
        }
        Z_poly = new_poly;
    }

    // Step 3: compute α-linear combination numerator: Σ α^i * f(z_i) - y_alpha
    blst_scalar y_alpha = new_scalar(0);
    blst_scalar tmp;
    for (size_t i = 0; i < t; i++) {
        scalar_pow(tmp, alpha, i);        // α^i
        scalar_mul_inplace(tmp, Ys[i]);     // α^i * y_i
        scalar_add_inplace(y_alpha, tmp);
    }

    // Step 4: compute quotient polynomial q_alpha(x) = (f(x) - Σ α^i y_i) / Z(x)
    vector<blst_scalar> q_poly = derive_q_multi(coeffs, Z_poly, y_alpha); 

    // Step 5: compute proof: Pi_agg = g1^{q_alpha(τ)}
    blst_p1 Pi_term;
    for (size_t i = 0; i < q_poly.size(); i++) {
        blst_p1_mult(&Pi_term, &S.g1_powers_jacob[i], q_poly[i].b, BIT_COUNT);
        blst_p1_add_or_double(Pi_agg, Pi_agg, &Pi_term);
    }
}

// Verifier for KZG multipoint batch opening
bool verify_multi_point(
    const blst_p1 &Pi_agg,             // proof from prover
    const blst_p1 &C,                   // commitment to polynomial
    const vector<blst_scalar> &Ys,      // claimed evaluation values
    const vector<blst_scalar> &Zs,      // evaluation points
    const blst_scalar &seed,            // Fiat-Shamir seed
    const blst_p2 &h2                   // generator in G2 for pairing
) {
    size_t t = Ys.size();
    assert(Zs.size() == t);

    // Step 1: compute the Fiat-Shamir challenge α
    blst_scalar alpha = seed;
    for (size_t i = 0; i < t; i++) {
        compute_challenge_inplace(alpha, Ys[i], Zs[i]);
    }

    // Step 2: compute y_alpha = Σ α^i * y_i
    blst_scalar y_alpha = 0;
    for (size_t i = 0; i < t; i++) {
        blst_scalar tmp;
        blst_scalar_exp(&tmp, &alpha, i);   // α^i
        blst_scalar_mul(&tmp, &tmp, &Ys[i]);
        blst_scalar_add(&y_alpha, &y_alpha, &tmp);
    }

    // Step 3: compute Z(x) = Π (x - z_i) evaluated at τ using SRS
    // For simplicity, assume we have h2^{τ^i} elements for polynomial evaluation in G2
    blst_p2 h2_Z;
    blst_p2_set_infinity(&h2_Z);
    vector<blst_scalar> Z_poly = {1}; // start with Z(x) = 1
    for (size_t i = 0; i < t; i++) {
        vector<blst_scalar> new_poly(Z_poly.size() + 1);
        for (size_t j = 0; j < Z_poly.size(); j++) {
            new_poly[j] = -Z_poly[j] * Zs[i];   // x^0 term
            new_poly[j+1] = Z_poly[j];          // x^1 term
        }
        Z_poly = new_poly;
    }
    // evaluate Z(τ) in G2: h2_Z = Σ Z_poly[i] * h2^{τ^i}
    for (size_t i = 0; i < Z_poly.size(); i++) {
        blst_p2 tmp;
        blst_p2_mult(&tmp, &h2, Z_poly[i].b, BIT_COUNT);
        blst_p2_add_or_double(&h2_Z, &h2_Z, &tmp);
    }

    // Step 4: compute C * g1^{-y_alpha}
    blst_p1 neg_y;
    blst_p1_mult(&neg_y, &BLST_G1_GENERATOR, y_alpha.b, BIT_COUNT);
    blst_p1_sub(&neg_y, &C, &neg_y);   // C - g1^{y_alpha}

    // Step 5: pairing check
    // e(Pi_agg, h2^{Z(τ)}) == e(C * g1^{-y_alpha}, h2)
    return blst_pairing(&Pi_agg, &h2_Z, &neg_y, &h2);
}

