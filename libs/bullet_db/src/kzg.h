#pragma once

#include "blst.h"
#include "pnt_sclr.h"

size_t const BIT_COUNT = 256;

// =======================================
// ============= SRS =====================
// =======================================

class SRS {
public:
    std::vector<blst_p1> g1_powers_jacob;
    std::vector<blst_p1_affine> g1_powers_aff;

    std::vector<blst_p2> g2_powers_jacob;
    std::vector<blst_p2_affine> g2_powers_aff;

    blst_p2 h;  // generator in G2 (h == g2_powers[0])
    blst_p2 g;  // generator in G1 (g == g1_powers[0])
    
    SRS(size_t degree, const blst_scalar &s);
    size_t max_degree();
};


// =======================================
// ============= BASIC KZG ===============
// =======================================

// Y = f(z)
blst_scalar eval_poly(
    const scalar_vec& coeffs,
    const blst_scalar& z);

// C_f(r)
blst_p1_affine commit(
    const scalar_vec& coeffs, 
    const SRS& srs);

// Q(r) = (f(r) - f(z)) / (r - z)
scalar_vec derive_q(
    const scalar_vec& coeffs, 
    const blst_scalar& z);

// e( C-gY, g2 ) == e( Pi, g2(r-z) )
bool verify_proof(
    const blst_p1_affine& C,
    const blst_scalar& Y,
    const blst_scalar& Z,
    const blst_p1_affine& Pi,
    const SRS& S);
