/*
 * Bullet Ledger
 * Copyright (C) 2025 Joshua Olson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "kzg.h"

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
    scratch_space = nullptr;
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
    blst_p1 C = new_p1();

    blst_p1s_mult_pippenger(&C, 
        g1_powers_aff_ptrs.data(), 
        coeffs.size(), 
        scalar_ptrs.data(), 256, 
        scratch_space);
    return p1_to_affine(C);
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
        256, 
        scratch_space);
}

void PippMan::mult_fixed_base(
    blst_p1 &agg,
    const scalar_vec& scalars,
    const blst_p1 &base
) {
    blst_p1_affine base_aff = p1_to_affine(base);
    const blst_p1_affine* point_arr[] = { &base_aff };

    new_scratch_space(1);
    fill_scalars(scalars);
    blst_p1s_mult_pippenger(&agg, 
        point_arr, 1, 
        scalar_ptrs.data(), 
        256, 
        scratch_space);
}
