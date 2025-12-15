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
#include "settings.h"
#include <optional>

void commit_g1(blst_p1* C, const Polynomial& coeffs, const SRS& srs);

Polynomial multiply_binomial(
    const Polynomial &P,
    const blst_scalar &w
);

Polynomial differentiate_polynomial(const Polynomial &f);

std::optional<Polynomial> derive_quotient(
    const Scalar_vec &poly_eval,
    const blst_scalar &z,
    const blst_scalar &y,
    const NTTRoots &roots
);
