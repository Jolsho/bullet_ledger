#include "kzg.h"

// =======================================
// ============= SRS =====================
// =======================================

size_t SRS::max_degree() { return g1_powers_jacob.size() - 1; }

SRS::SRS(size_t degree, const blst_scalar &s) {
    g1_powers_jacob.resize(degree + 1);
    g1_powers_aff.resize(degree + 1);

    g2_powers_jacob.resize(degree + 1);
    g2_powers_aff.resize(degree + 1);

    blst_p1 g1 = *blst_p1_generator();
    blst_p2 g2 = *blst_p2_generator();
    h = g2;
    g = g1;

    // s(0) = 1
    blst_scalar pow_s = new_scalar(1);

    // Compute Jacobian powers
    for (size_t i = 0; i <= degree; i++) {
        p1_mult(g1_powers_jacob[i], g1, pow_s);
        p2_mult(g2_powers_jacob[i], g2, pow_s);
        scalar_mul_inplace(pow_s, s);
    }

    // Convert all to affine in a separate loop
    for (size_t i = 0; i <= degree; i++) {
        blst_p1_to_affine(&g1_powers_aff[i], &g1_powers_jacob[i]);
        blst_p2_to_affine(&g2_powers_aff[i], &g2_powers_jacob[i]);
    }
}
