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

#include "verkle.h"
#include <cassert>
#include <filesystem>
#include <random>

Hash pseudo_random_hash(int i) {
    std::mt19937_64 gen(i);            // 64-bit PRNG
    std::uniform_int_distribution<uint64_t> dist;

    Hash hash;

    for (int i = 0; i < 4; ++i) {        // 4 * 8 bytes = 32 bytes
        uint64_t num = dist(gen);
        for (int j = 0; j < 8; ++j) {
            hash[i*8 + j] = static_cast<byte>((num >> (8 * j)) & 0xFF);
        }
    }
    return hash;
}

void main_state_trie() {
    namespace fs = std::filesystem;
    const char* path = "./fake_db";
    if (fs::exists(path)) fs::remove_all(path);
    fs::create_directory(path);

    Ledger l(path, 6, 
        10 * 1024 * 1024, 
        "bullet", 
        new_scalar(13)
    );

    std::vector<Hash> raw_hashes;
    raw_hashes.reserve(25);
    for (int i = 0; i < raw_hashes.capacity(); i++) {
        Hash rnd = pseudo_random_hash(i);
        raw_hashes.push_back(rnd);
    }


    // --- Insert phase ---
    int i = 0;
    for (Hash h: raw_hashes) {
        ByteSlice key(h.data(), h.size());
        ByteSlice value(h.data(), h.size());

        auto virt_root = l.virtual_put(key, value, 1).value();

        auto root = l.put(key, value, 1).value();

        auto virt = compress_p1(&virt_root);
        assert(std::equal(virt.begin(), virt.end(), compress_p1(&root).begin()));

        ByteSlice got = l.get_value(key, 1).value();
        assert(std::equal(got.begin(), got.end(), value.begin()));
        printf("INSERT %d\n", i);
        i++;
    }

    // --- Proving phase ---
    i = 0;
    for (Hash h: raw_hashes) {
        ByteSlice key(h.data(), h.size());
        print_hash(h);
        auto [C, Pi, Ws, Ys, Zs] = l.get_existence_proof(key, 1).value();
        assert(multi_func_multi_point_verify(Ws, Zs, Ys, Pi, *l.get_srs()));

        if (*Zs[0].b != 0) *Zs[0].b = 0;
        else *Zs[0].b = 1;
        assert(!multi_func_multi_point_verify(Ws, Zs, Ys, Pi, *l.get_srs()));
        printf("PROVED %d\n", i);
        i++;
    }

    // --- Remove phase ---
    i = 0;
    for (Hash h: raw_hashes) {
        std::span<byte> key(h.data(), h.size());
        l.remove(key, 1);
        auto got = l.get_value(key, 1);
        assert(got.has_value() == false);
        printf("DELETED %d\n", i);
        i++;
    }
    

    fs::remove_all("./fake_db");

    printf("VERKLE STATE SUCCESSFUL \n");
}
