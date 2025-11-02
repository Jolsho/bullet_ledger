#include <cstdlib>
#include <cstring>
#include <vector>
#include <blst.h>
#include "pippen.h"


PippMan::PippMan(SRS& srs) {
    size_t n = srs.g1_powers_aff.size();
    scalar_ptrs.reserve(n);
    g1_powers_aff_ptrs.reserve(n);

    for(size_t i = 0; i < n; i++) {
        g1_powers_aff_ptrs[i] = &srs.g1_powers_aff[i];
    }

    size_t scratch_size = blst_p1s_mult_pippenger_scratch_sizeof(n);
    scratch_space = (limb_t*)malloc(scratch_size);
}

PippMan::~PippMan() {
    free(scratch_space);
}

void PippMan::fill_scalars(const scalar_vec& scalars) {
    for (size_t i = 0; i < scalars.size(); i++) {
        scalar_ptrs[i] = scalars[i].b;
    }
}

void PippMan::clear_scalars() {
    for (size_t i = 0; i < scalar_ptrs.size(); i++) {
        scalar_ptrs[i] = NULL;
    }
}

blst_p1 PippMan::commit(const scalar_vec& coeffs) {
    fill_scalars(coeffs);
    blst_p1 C;
    blst_p1s_mult_pippenger(&C, 
        g1_powers_aff_ptrs.data(), coeffs.size(), 
        scalar_ptrs.data(), BIT_COUNT, 
        scratch_space);

    return C;
}

blst_scalar PippMan::evaluate_polynomial(
    const scalar_vec& coeffs,
    const blst_scalar& z
) {
    blst_scalar result = new_scalar(0);
    for (int i = coeffs.size() - 1; i >= 0; i--) {
        blst_sk_mul_n_check(&result, &result, &z);  // result *= z
        blst_sk_add_n_check(&result, &result, &coeffs[i]); // result += coeffs[i]
    }
    return result;
}

