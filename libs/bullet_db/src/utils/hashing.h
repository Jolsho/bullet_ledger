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
#include "blst.h"
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>

struct Hash {
    byte h[32];
};

using ByteSlice = std::span<byte>;

class BlakeHasher {
private: blake3_hasher h_;
public:
    BlakeHasher() { blake3_hasher_init(&h_); }
    ~BlakeHasher() = default;

    void update(const byte* data, const size_t size) {
        blake3_hasher_update(&h_, data, size);
    }
    void finalize(byte* out) {
        blake3_hasher_finalize(&h_, static_cast<uint8_t*>(out), 32);
    }
};

void derive_kv_hash(Hash out, const Hash &key_hash, const Hash &val_hash);

void derive_hash(byte* out, const ByteSlice &value);

Hash new_hash(const byte* h = nullptr);
void print_hash(const Hash &hash);
void seeded_hash(Hash* out, int i);
void hash_p1_to_scalar(const blst_p1* p1, blst_scalar* s, const std::string* tag);
