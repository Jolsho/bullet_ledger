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
#include "points.h"
#include <iomanip>
#include <iostream>

Hash derive_kv_hash(const Hash &key_hash, const Hash &val_hash) {
    BlakeHasher hasher;
    hasher.update(key_hash.data(), key_hash.size());
    hasher.update(val_hash.data(), val_hash.size());
    return hasher.finalize();
}

Hash derive_hash(const ByteSlice &value) {
    BlakeHasher hasher;
    hasher.update(value.data(), value.size());
    return hasher.finalize();
}

void hash_p1_to_scalar(const blst_p1* p1, blst_scalar* s, const std::string* tag) {
    BlakeHasher hasher;
    hasher.update(reinterpret_cast<const byte*>(tag->data()), tag->size());

    auto c_bytes = compress_p1(p1);
    hasher.update(
        c_bytes.data(), 
        c_bytes.size());

    Hash h = hasher.finalize();

    blst_scalar_from_be_bytes(
        s, reinterpret_cast<const byte*>(h.data()), h.size());
}

void print_hash(const Hash &hash) {
    for (byte b : hash) {
        std::cout << std::hex
                  << std::setw(2)
                  << std::setfill('0')
                  << static_cast<unsigned>(b);
    }
    std::cout << std::dec << std::endl; // restore formatting
}
