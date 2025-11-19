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
#include <ostream>
#include <vector>

using scalar_vec = std::vector<blst_scalar>;

// =======================================
// =============== SCALARS ===============
// =======================================

blst_scalar new_scalar(const uint64_t v = 0);
blst_scalar rand_scalar();
bool scalar_is_zero(const blst_scalar &s);
std::ostream& operator<<(std::ostream& os, const blst_scalar &x);

blst_scalar scalar_mul(const blst_scalar &a, const blst_scalar &b);
blst_scalar scalar_add(const blst_scalar &a, const blst_scalar &b);
blst_scalar scalar_sub(const blst_scalar &a, const blst_scalar &b);
void scalar_add_inplace(blst_scalar &dst, const blst_scalar &src);
void scalar_sub_inplace(blst_scalar &dst, const blst_scalar &src);
void scalar_mul_inplace(blst_scalar &dst, const blst_scalar &mult);
void scalar_pow(blst_scalar &out, const blst_scalar &base, uint64_t exp);

blst_scalar neg_scalar(const blst_scalar &sk);
blst_scalar inv_scalar(const blst_scalar &a);
void inv_scalar_inplace(blst_scalar &a);
