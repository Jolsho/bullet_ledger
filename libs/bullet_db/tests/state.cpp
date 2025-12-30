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
#include "ledger.h"
#include "processing.h"
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

    std::vector<Hash> raw_hashes(32);
    for (int i = 0; i < raw_hashes.capacity(); i++) {
        seeded_hash(&raw_hashes[i], i);
    }



    ///////////////////////////
    // --- Insert phase --- //
    /////////////////////////
    uint16_t block_id = 1;
    uint8_t idx = 3;

    int i = 0;
    for (Hash h: raw_hashes) {

        ByteSlice key(h.h, 32);
        ByteSlice value(h.h, 32);

        int res = l.create_account(key, block_id);
        assert(res == OK);

        res = l.put(key, value, idx, block_id);
        assert(res == OK);
        printf("INSERT %d, %d\n", i, res);
        i++;
    }


    /////////////////////////////
    // --- FINALIZE phase --- //
    ///////////////////////////
    Hash h;
    printf("FINALIZING......\n");
    int res = finalize_block(l, block_id, &h);
    printf("DONE FINALIZING\n");
    assert(res == OK);





    ////////////////////////////
    // --- Proving phase --- //
    //////////////////////////
    std::vector<Commitment> Cs;
    std::vector<Proof> Pis;

    Hash base;
    seeded_hash(&base, 2);

    const Gadgets_ptr gadgets = l.get_gadgets();

    
    for (i = 0; i < 4; i++) {

        ByteSlice rh{raw_hashes[i].h, sizeof(raw_hashes[i].h)};

        Hash val_hash;
        derive_hash(val_hash.h, rh);

        Hash key_hash = val_hash;
        key_hash.h[31] = idx;

        int res = generate_proof(l, Cs, Pis, key_hash, block_id);
        printf("GENERATED %d\n", res);
        assert(res == OK);

        // PROVING
        assert(valid_proof(l, &Cs, &Pis, key_hash, val_hash, idx));
        printf("PROVED %d\n", res);

        Cs.clear();
        Pis.clear();

        printf("\n");
        i++;
    }

    ByteSlice rh{raw_hashes[0].h, sizeof(raw_hashes[0].h)};
    Hash val_hash;
    derive_hash(val_hash.h, rh);

    Hash key_hash = val_hash;

    key_hash.h[31] = 32;
    res = generate_proof(l, Cs, Pis, key_hash, 0);
    key_hash.h[31] = idx;

    assert(res != OK);
    assert(res == NOT_EXIST);




    ////////////////////////////
    // --- JUSTIFY phase --- //
    //////////////////////////
    res = justify_block(l, block_id);
    assert(res == OK);

    res = generate_proof(l, Cs, Pis, key_hash, 0);
    assert(res == OK);
    assert(valid_proof(l, &Cs, &Pis, key_hash, val_hash, idx));
    printf("SUCCESSFUL JUSTIFICATION \n");



    //////////////////////////
    // --- PRUNE phase --- //
    ////////////////////////
    block_id = 3;
    idx = 4;

    i = 0;
    for (Hash h: raw_hashes) {

        ByteSlice key(h.h, 32);
        ByteSlice val_hash(h.h, 32);

        int res = l.put(key, val_hash, idx, block_id);
        printf("INSERT %d, %d\n", i, res);
        assert(res == OK);
        i++;
    }

    res = prune_block(l, block_id);
    assert(res == OK);


    printf("SUCCESSFUL PRUNING \n");



    fs::remove_all("./fake_db");

    printf("VERKLE STATE SUCCESSFUL \n");
}
