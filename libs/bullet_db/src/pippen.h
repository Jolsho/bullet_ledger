#pragma once

#include "blst.h"
#include "kzg.h"

// --------------- PIP SHIT ---------------
class PippMan {
public:
    PippMan(SRS& srs);
    ~PippMan();
    blst_p1 commit(const scalar_vec& coeffs);
    blst_scalar evaluate_polynomial(
        const scalar_vec& coeffs,
        const blst_scalar& z
    );

private:
    std::vector<blst_p1_affine*> g1_powers_aff_ptrs;
    std::vector<const byte*> scalar_ptrs;
    limb_t *scratch_space;
    void fill_scalars(const scalar_vec& scalars);
    void clear_scalars();
};


