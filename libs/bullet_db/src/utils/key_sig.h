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
#include <vector>
#include <array>

using bytes32 = std::array<byte, 32>;

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



