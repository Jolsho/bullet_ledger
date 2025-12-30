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

#include "hashing.h"
#include <iomanip>
#include <iostream>
#include <random>

void derive_kv_hash(Hash out, const Hash &key_hash, const Hash &val_hash) {
    BlakeHasher hasher;
    hasher.update(key_hash.h, 32);
    hasher.update(val_hash.h, 32);
    hasher.finalize(out.h);
}

void derive_hash(byte* out, const ByteSlice &value) {
    BlakeHasher hasher;
    hasher.update(value.data(), value.size());
    hasher.finalize(out);
}

void hash_p1_to_scalar(const blst_p1* p1, blst_scalar* s, const std::string* tag) {
    BlakeHasher hasher;
    hasher.update(reinterpret_cast<const byte*>(tag->data()), tag->size());

    byte c_bytes[48];
    blst_p1_compress(c_bytes, p1);

    hasher.update(c_bytes, 48);

    Hash h = new_hash();
    hasher.finalize(h.h);
    blst_scalar_from_le_bytes(s, h.h, 32);
}

Hash new_hash(const byte* h) { 
    Hash hash; 
    if (h != nullptr) {
        std::memcpy(hash.h, h, 32);
    } else {
        std::memset(hash.h, 0, 32);
    }
    return hash;
}

void print_hash(const Hash &hash) {
    for (const byte* i{hash.h}; i < hash.h + 32; i++) {
        std::cout << std::hex
                  << std::setw(2)
                  << std::setfill('0')
                  << static_cast<unsigned>(*i);
    }
    std::cout << std::dec << std::endl; // restore formatting
}

void seeded_hash(Hash* out, int i) {
    std::mt19937_64 gen(i);            // 64-bit PRNG
    std::uniform_int_distribution<uint64_t> dist;

    for (i = 0; i < 4; ++i) {        // 4 * 8 bytes = 32 bytes
        uint64_t num = dist(gen);
        for (int j{} ; j < 8; ++j) {
            out->h[i*8 + j] = static_cast<byte>((num >> (8 * j)) & 0xFF);
        }
    }
}

