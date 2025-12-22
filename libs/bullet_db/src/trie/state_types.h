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
#include "hashing.h"
#include "blst.h"
#include <cstdint>

using Commitment = blst_p1;
using Proof = blst_p1;

constexpr uint64_t BRANCH_ORDER = 256;
constexpr uint64_t LEAF_ORDER   = 128;

constexpr byte BRANCH = static_cast<byte>(69);
constexpr byte LEAF   = static_cast<byte>(71);
constexpr byte EDGE   = static_cast<byte>(73);

extern const Hash ZERO_HASH;
inline bool hash_is_zero(const Hash& h) {
    return std::memcmp(h.h, ZERO_HASH.h, 32) == 0;
}

enum LedgerCodes {
    OK = 0,
    EXISTS = 0,

    NOT_EXIST = 1,
    NOT_IN_SHARD = 2,
    ROOT_ERR = 3,
    DB_ERR = 4,
    LOAD_NODE_ERR = 5,
    KZG_PROOF_ERR = 6,
    DELETED = 7,
    ALREADY_DELETED = 8,
    DELETE_VALUE_ERR= 9,
    REPLACE_VALUE_ERR= 10 
};
