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

#include "blake3.h"
#include "verkle.h"
#include <iomanip>
#include <iostream>

bool iszero(const ByteSlice &slice) {
    byte zero_byte{0};
    for (auto &b: slice) if (b != zero_byte) return false;
    return true;
}

uint64_array u64_to_array(uint64_t num) { return std::bit_cast<std::array<byte, 8>>(num); }
uint64_t u64_from_array(uint64_array a) { return std::bit_cast<uint64_t>(a); }

void commit_from_bytes(const byte* src, Commitment* dst) {
    blst_p1_affine aff;
    blst_p1_uncompress(&aff, src);
    blst_p1_from_affine(dst, &aff);
}

Hash derive_kv_hash(const Hash &key_hash, const Hash &val_hash) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, 
                         key_hash.data(), 
                         key_hash.size());
    blake3_hasher_update(&hasher, 
                         val_hash.data(), 
                         val_hash.size());

    Hash hash;
    blake3_hasher_finalize(
        &hasher, 
        reinterpret_cast<uint8_t*>(hash.data()), 
        hash.size());

    return hash;
}


Hash derive_hash(const ByteSlice &value) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, value.data(), value.size());
    Hash hash;
    blake3_hasher_finalize(
        &hasher, 
        reinterpret_cast<uint8_t*>(hash.data()), 
        hash.size()
    );
    return hash;
}

Commitment derive_init_commit(
    byte nib, 
    const Commitment &c, 
    Ledger &ledger
) {
    scalar_vec Fx(ORDER, new_scalar());
    Fx[nib] = p1_to_scalar(&c);
    return commit_g1_projective(Fx, *ledger.get_srs());
}

void print_hash(const Hash &hash)
{
    for (byte b : hash) {
        std::cout << std::hex
                  << std::setw(2)
                  << std::setfill('0')
                  << static_cast<unsigned>(b);
    }
    std::cout << std::dec << std::endl; // restore formatting
}
