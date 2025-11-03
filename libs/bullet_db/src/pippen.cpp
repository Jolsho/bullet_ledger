#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <blst.h>
#include "pippen.h"
#include "path.h"

// =====================================================
// ============= CONSTRUCT / DESTRUCT ==================
// =====================================================

const size_t AVG_EXPECTED_CAP = 1234; // TODO -- actually calculate that

PippMan::PippMan(SRS& srs) {
    size_t n = srs.g1_powers_aff.size();
    scalar_ptrs.reserve(AVG_EXPECTED_CAP);
    point_ptrs.reserve(AVG_EXPECTED_CAP);
    g1_powers_aff_ptrs.reserve(n);

    for(size_t i = 0; i < n; i++) {
        g1_powers_aff_ptrs[i] = &srs.g1_powers_aff[i];
    }
}

PippMan::~PippMan() {
    free_scratch_space();
}

// ==============================================
// ============= SCRATCH_SPACE ==================
// ==============================================

void PippMan::new_scratch_space(size_t n) {
    if (scratch_size != n) {
        free_scratch_space();
        scratch_size = blst_p1s_mult_pippenger_scratch_sizeof(n);
        scratch_space = (limb_t*)malloc(scratch_size);
    }
}

void PippMan::free_scratch_space() { 
    scratch_size = 0;
    free(scratch_space);
}


// =================================================
// ============= POINTS N SCALARS ==================
// =================================================

// TODO -- not sure about this shrinking logic
void PippMan::fill_points(const std::vector<blst_p1_affine>& points) {
    if (point_ptrs.capacity() < points.size()) {
        point_ptrs.resize(points.size());
    } else if (point_ptrs.capacity() > AVG_EXPECTED_CAP && points.size() < AVG_EXPECTED_CAP) {
        point_ptrs.resize(AVG_EXPECTED_CAP);
    }
    for (size_t i = 0; i < points.size(); i++) { point_ptrs[i] = &points[i]; }
}

void PippMan::clear_points() {
    for (size_t i = 0; i < point_ptrs.size(); i++) { point_ptrs[i] = NULL; }
}

void PippMan::fill_scalars(const scalar_vec& scalars) {
    if (scalar_ptrs.capacity() < scalars.size()) {
        scalar_ptrs.resize(scalars.size());
    } else if (scalar_ptrs.capacity() > AVG_EXPECTED_CAP && scalars.size() < AVG_EXPECTED_CAP) {
        scalar_ptrs.resize(AVG_EXPECTED_CAP);
    }
    for (size_t i = 0; i < scalars.size(); i++) { scalar_ptrs[i] = scalars[i].b; }
}
void PippMan::clear_scalars() {
    for (size_t i = 0; i < scalar_ptrs.size(); i++) { scalar_ptrs[i] = NULL; }
}


// ==============================================
// ============= FUNCTIONALITY ==================
// ==============================================

blst_p1_affine PippMan::commit(const scalar_vec& coeffs) {
    fill_scalars(coeffs);
    new_scratch_space(coeffs.size());
    blst_p1 C;
    blst_p1s_mult_pippenger(&C, 
        g1_powers_aff_ptrs.data(), 
        coeffs.size(), 
        scalar_ptrs.data(), BIT_COUNT, 
        scratch_space);

    blst_p1_affine C_aff;
    blst_p1_to_affine(&C_aff, &C);

    return C_aff;
}

void PippMan::mult_p1s(
    blst_p1 &agg,
    const scalar_vec& scalars,
    const std::vector<blst_p1_affine>& points
) {
    new_scratch_space(points.size());
    fill_scalars(scalars);
    fill_points(points);
    blst_p1s_mult_pippenger(
        &agg, 
        point_ptrs.data(), 
        points.size(), 
        scalar_ptrs.data(), 
        BIT_COUNT, 
        scratch_space);
}

void PippMan::mult_fixed_base(
    blst_p1 &agg,
    const scalar_vec& scalars,
    const blst_p1* base
) {
    blst_p1_affine base_aff;
    blst_p1_to_affine(&base_aff, base);
    const blst_p1_affine* point_arr[] = { &base_aff };

    new_scratch_space(1);
    fill_scalars(scalars);
    blst_p1s_mult_pippenger(&agg, 
        point_arr, 1, 
        scalar_ptrs.data(), 
        BIT_COUNT, 
        scratch_space);
}

std::tuple<blst_p1_affine, blst_p1_affine> PippMan::path_commit_n_proof(
    const blst_scalar seed,                 // initial challenge
    const vector<blst_p1_affine> proofs,    // Pi_i
    vector<blst_p1_affine> commits,         // C_i
    const vector<blst_scalar> zs,           // evaluation points
    vector<blst_scalar> values              // v_i
) {
    assert(proofs.size() == commits.size());
    assert(proofs.size() == zs.size());
    assert(proofs.size() == values.size());

    vector<blst_scalar> alphas(proofs.size());
    blst_scalar prev = seed;
    for (size_t i = 0; i < proofs.size(); ++i) {

        compute_challenge_inplace(prev, values[i], zs[i]);
        alphas[i] = prev;

        // v_i *= alpha
        scalar_mul_inplace(values[i], alphas[i]);
    }

    blst_p1 C_agg = new_p1();
    blst_p1 V_agg = new_p1();

    // C_part =  sum(α_i * C_i)
    mult_p1s(C_agg, alphas, commits);

    // V_part = sum(α_i * v_i) * g1
    // sincle values is now [(α_i * v_i) ... ]
    mult_fixed_base(V_agg, values, blst_p1_generator());

    // C_part += -(V_part)
    blst_p1_cneg(&V_agg, true);
    blst_p1_add_or_double(&C_agg, &C_agg, &V_agg);

    // aggregate( alpha * Pi_i )
    blst_p1 P_agg = new_p1();
    mult_p1s(P_agg, alphas, proofs);

    // convert aggregate projectives to affine
    blst_p1_affine Pi_aff, C_aff;
    blst_p1_to_affine(&Pi_aff, &P_agg);
    blst_p1_to_affine(&C_aff, &C_agg);

    return std::make_tuple(C_aff, Pi_aff);
}
