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
#include <array>
#include <vector>
#include <cstring>
#include <ostream>

using bytes32 = std::array<byte, 32>;
using scalar_vec = std::vector<blst_scalar>;

struct key_pair {
    blst_p1 pk;
    blst_scalar sk;
};

std::tuple<const byte*, size_t> str_to_bytes(const char* str);

bytes32 gen_rand_32();

key_pair gen_key_pair(
    const char* tag, 
    bytes32 &seed
);

bool verify_sig(
    const blst_p1 &PK,
    blst_p2 &signature, 
    const byte* msg,
    size_t msg_len,
    const byte* tag,
    size_t tag_len
);

bool verify_aggregate_signature(
    std::vector<blst_p1>& pks,
    const blst_p2& agg_sig,
    const byte* msg,
    size_t msg_len,
    const uint8_t* tag,
    size_t tag_len
);



// =======================================
// =============== POINTS ================
// =======================================

blst_p1 new_p1();
blst_p2 new_p2();
blst_p1_affine p1_to_affine(const blst_p1 &p1);
blst_p1 p1_from_affine(const blst_p1_affine &aff);
blst_p2 p2_from_affine(const blst_p2_affine &aff);
blst_p2_affine p2_to_affine(const blst_p2 &p2);
void p1_mult(blst_p1& dst, const blst_p1 &a, const blst_scalar &b);

std::array<uint8_t, 48> compress_p1(const blst_p1 *pk);
void print_p1(const blst_p1* pk);
void print_p1_affine(const blst_p1_affine* pk);
blst_scalar p1_to_scalar(const blst_p1 *p1);

std::array<uint8_t, 96> compress_p2(const blst_p2& pk);
void print_p2(const blst_p2& pk);
void print_p2_affine(const blst_p1_affine& pk);
void p2_mult(blst_p2& dst, const blst_p2 &a, const blst_scalar &b);


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
