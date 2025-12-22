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

#pragma once
#include "blst.h"
#include "hashing.h"
#include "settings.h"
#include <optional>
#include <vector>

using Scalar_vec = std::vector<blst_scalar>;

std::optional<std::tuple<blst_p1, blst_p1>> prove_kzg(
    const Scalar_vec &evals,
    const size_t eval_idx,
    const KZGSettings &s
);


bool verify_kzg(
    const blst_p1 C, 
    const blst_scalar z, 
    const blst_scalar y, 
    const blst_p1 Pi, 
    const SRS &S
);

bool batch_verify(
    std::vector<blst_p1> &Pis,
    std::vector<blst_p1> &Cs,
    std::vector<size_t> &Z_idxs,
    Scalar_vec &Ys,
    Hash base_r,
    KZGSettings &kzg
);
