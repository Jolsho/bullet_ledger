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
#include "blake3.h"
#include "types.h"

using Hash = std::array<byte, 32>;
class BlakeHasher {
private: blake3_hasher h_;
public:
    BlakeHasher() { blake3_hasher_init(&h_); }
    ~BlakeHasher() = default;

    void update(const byte* data, const size_t size) {
        blake3_hasher_update(&h_, data, size);
    }
    Hash finalize() {
        Hash hash;
        blake3_hasher_finalize(&h_, 
            reinterpret_cast<uint8_t*>(hash.data()), 
            hash.size());
        return hash;
    }
};

Hash derive_kv_hash(const Hash &key_hash, const Hash &val_hash);

Hash derive_hash(const ByteSlice &value);

void print_hash(const Hash &hash);
Hash seeded_hash(int i);
