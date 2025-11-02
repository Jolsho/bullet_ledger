#pragma once

#include "blst.h"
#include "pnt_sclr.h"

size_t const BIT_COUNT = 256;

// --------------- SRS ---------------
class SRS {
public:
    std::vector<blst_p1> g1_powers_jacob;
    std::vector<blst_p1_affine> g1_powers_aff;

    std::vector<blst_p2> g2_powers_jacob;
    std::vector<blst_p2_affine> g2_powers_aff;
    blst_p2 h;                      // generator in G2 (h == g2_powers[0])
    blst_scalar s;
    
    SRS(size_t degree, const blst_scalar &s);
    size_t max_degree();
};

blst_scalar eval_poly(
    const scalar_vec& coeffs,
    blst_scalar& z
);

blst_p1 commit(
    const scalar_vec& coeffs, 
    const SRS& srs);

scalar_vec derive_q(
    const scalar_vec& coeffs, 
    const blst_scalar& z);

bool verify_proof(
    const blst_p1& C,
    const blst_scalar& Y,
    const blst_scalar& Z,
    const blst_p1& Pi,
    const SRS& S);
