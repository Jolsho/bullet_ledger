#pragma once

#include "blst.h"
#include "kzg.h"
#include <cstddef>
#include <vector>

using std::vector;

// =======================================
// ============== PIP SHIT ===============
// =======================================

class PippMan {
public:
    PippMan(SRS& srs);
    ~PippMan();

    blst_p1_affine commit(const scalar_vec& coeffs);

    void mult_p1s(
        blst_p1 &agg,
        const scalar_vec& scalars,
        const vector<blst_p1_affine>& points);

    void mult_fixed_base(
        blst_p1 &agg,
        const scalar_vec& scalars,
        const blst_p1* base);

    std::tuple<blst_p1_affine, blst_p1_affine> path_commit_n_proof(
        const blst_scalar seed,                 // initial challenge
        const vector<blst_p1_affine> proofs,    // Pi_i
        vector<blst_p1_affine> commits,         // C_i
        const vector<blst_scalar> zs,           // evaluation points
        vector<blst_scalar> values              /* v_i */);

private:
    limb_t *scratch_space;
    vector<blst_p1_affine*> g1_powers_aff_ptrs;
    vector<const byte*> scalar_ptrs;
    vector<const blst_p1_affine*> point_ptrs;
    size_t scratch_size;

    void fill_scalars(const scalar_vec& scalars);
    void fill_points(const vector<blst_p1_affine>& points);
    void clear_scalars();
    void clear_points();
    void new_scratch_space(size_t n);
    size_t current_scratch_size();
    void free_scratch_space();
};


