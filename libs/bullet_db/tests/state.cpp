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
#include "helpers.h"
#include "kzg.h"
#include "ledger.h"
#include <filesystem>

void main_state_trie() {
    namespace fs = std::filesystem;
    const char* path = "./fake_db";
    if (fs::exists(path)) fs::remove_all(path);
    fs::create_directory(path);

    size_t CACHE_SIZE{128};
    size_t MAP_SIZE{10ULL * 1024 * 1024 * 1024};
    std::string DST{"bullet"};
    blst_scalar SECRET{num_scalar(13)};

    Ledger l(path, CACHE_SIZE, MAP_SIZE, DST, SECRET);

    std::vector<Hash> raw_hashes(512);
    for (int i = 0; i < raw_hashes.capacity(); i++) {
        seeded_hash(&raw_hashes[i], i);
    }


    // --- Insert phase ---
    uint16_t block_id = 1;
    uint8_t idx = 3;

    int i = 0;
    for (Hash h: raw_hashes) {

        ByteSlice key(h.h, 32);
        ByteSlice value(h.h, 32);

        int res = l.put(key, value, idx, block_id);
        printf("INSERT %d, %d\n\n", i, res);
        assert(res == OK);
        i++;
    }

    Hash h;
    int res = l.finalize_block(block_id, &h);
    printf("FINALIZED %d\n", res);
    assert(res == OK);


    // --- Proving phase ---
    std::vector<Commitment> Cs;
    std::vector<Proof> Pis;
    std::vector<size_t> Zs;
    std::vector<blst_scalar> Ys;

    Hash base;
    seeded_hash(&base, 2);

    const KZGSettings* settings = l.get_settings();

    i = 0;
    for (Hash h: raw_hashes) {

        ByteSlice key(h.h, sizeof(h.h));
        ByteSlice value(h.h, 32);

        int res = l.generate_proof(Cs, Pis, key, block_id, idx);
        printf("GENERATE %d\n", res);
        assert(res == OK);

        Hash key_hash;
        derive_hash(key_hash.h, key);
        key_hash.h[31] = idx;
        l.derive_Zs_n_Ys(key_hash, value, &Cs, &Pis, &Zs, &Ys, settings);

        printf("PROVING %d == ", i);

        for (int k{}; k < Pis.size(); k++) {
            assert(verify_kzg(
                Cs[k], 
                settings->roots.roots[Zs[k]], 
                Ys[k], 
                Pis[k], 
                settings->setup
            ));
            printf("%d/%zu, ", k+1, Pis.size());
        }

        assert(batch_verify(Pis, Cs, Zs, Ys, base, *settings));

        Cs.clear();
        Pis.clear();
        Zs.clear();
        Ys.clear();

        printf("\n");
        i++;
    }

    fs::remove_all("./fake_db");

    printf("VERKLE STATE SUCCESSFUL \n");
}
