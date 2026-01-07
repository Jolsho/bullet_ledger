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
#include "bigint.h"
#include "blst.h"

blst_scalar num_scalar(const uint64_t v);

const blst_scalar ZERO_SK = num_scalar(0);
const blst_scalar ONE_SK = num_scalar(1);

bool scalar_is_zero(const blst_scalar &s);
bool equal_scalars(const blst_scalar &a, const blst_scalar &b);
void print_scalar(blst_scalar* s);
void print_p1(blst_p1* p);
blst_p1 p1_from_bytes(const byte* buff);
blst_p2 p2_from_bytes(const byte* buff);
blst_scalar modular_pow(const blst_scalar &base, const BigInt &exp);
void hash_to_sk(blst_scalar* dst, const byte* hash);
blst_p1 new_p1();
blst_p1 new_inf_p1();
blst_p2 new_p2();
blst_p2 new_inf_p2();
