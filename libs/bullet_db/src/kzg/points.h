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
#include <string>

using Commitment = blst_p1;
using Proof = blst_p1_affine;


// =======================================
// =============== POINTS ================
// =======================================

blst_p1 new_p1();
blst_p2 new_p2();
void p1_from_bytes(const byte* src, blst_p1* dst);
blst_p1_affine p1_to_affine(const blst_p1 &p1);
blst_p1 p1_from_affine(const blst_p1_affine &aff);
blst_p2 p2_from_affine(const blst_p2_affine &aff);
blst_p2_affine p2_to_affine(const blst_p2 &p2);
void p1_mult(blst_p1& dst, const blst_p1 &a, const blst_scalar &b);

std::array<byte, 48> compress_p1(const blst_p1 *pk);
void print_p1(const blst_p1* pk);
void print_p1_affine(const blst_p1_affine* pk);

std::array<uint8_t, 96> compress_p2(const blst_p2& pk);
void print_p2(const blst_p2& pk);
void print_p2_affine(const blst_p1_affine& pk);
void p2_mult(blst_p2& dst, const blst_p2 &a, const blst_scalar &b);

void hash_p1_to_scalar(const blst_p1* p1, blst_scalar* s, const std::string* tag);


