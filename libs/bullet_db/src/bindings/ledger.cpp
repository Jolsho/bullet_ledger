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

#include <sys/random.h>
#include "helpers.h"
#include "processing.h"

extern "C" {
    extern const int SECRET_SIZE = 32;
}

void* ledger_open(
    const char* path, 
    size_t cache_size,
    size_t map_size,
    const char* tag,
    unsigned char* secret,
    size_t secret_size
) {
    blst_scalar s;
    if (secret) {

        Hash secret_hash;
        derive_hash(secret_hash.h, {secret, secret_size});
        hash_to_sk(&s, secret_hash.h);
        std::memset(secret, 0, secret_size);
        std::memset(secret_hash.h, 0, sizeof(secret_hash.h));
    } else {
        byte random[32];
        assert(getrandom(random, 32, 0) == 32);
        hash_to_sk(&s, random);
        std::memset(random, 0, 32);
    }
    return new Ledger(path, cache_size, map_size, tag, s);
}

int ledger_get_SRS(
    void *ledger, 
    void** out,
    size_t* out_size
) {
    auto l = reinterpret_cast<Ledger*>(ledger);

    size_t EXPECTED_SIZE = (
        BRANCH_ORDER * blst_p1_sizeof() + 
        BRANCH_ORDER * blst_p2_sizeof()
    );

    std::vector<blst_p1> g1s;
    std::vector<blst_p2> g2s;

    SRS* setup = &l->get_gadgets()->settings.setup;

    *out = malloc(EXPECTED_SIZE);

    byte* cursor = reinterpret_cast<byte*>(*out);

    for (auto &g1: setup->g1_powers_jacob) {
        blst_p1_compress(cursor, &g1);
        cursor += blst_p1_sizeof();
    }

    for (auto &g2: setup->g2_powers_jacob) {
        blst_p2_compress(cursor, &g2);
        cursor += blst_p2_sizeof();
    }

    return 0;
}

int ledger_set_SRS(
    void *ledger, 
    const unsigned char *setup, 
    size_t setup_size
) {
    auto l = reinterpret_cast<Ledger*>(ledger);

    size_t EXPECTED_SIZE = (BRANCH_ORDER * blst_p1_sizeof() * 2);
    if (setup_size != EXPECTED_SIZE) return INVALID_SETUP_SIZE;

    std::vector<blst_p1> g1s;
    g1s.reserve(BRANCH_ORDER);

    std::vector<blst_p2> g2s;
    g2s.reserve(BRANCH_ORDER);

    const byte* cursor = setup;

    for (int i{}; i < BRANCH_ORDER; i++) {
        g1s.push_back(p1_from_bytes(cursor));
        cursor += blst_p1_sizeof();
    }

    for (int i{}; i < BRANCH_ORDER; i++) {
        g2s.push_back(p2_from_bytes(cursor));
        cursor += blst_p2_sizeof();
    }

    l->get_gadgets()->settings.setup.set_srs(g1s, g2s);

    return 0;
}


int ledger_put(
    void* ledger, 
    const unsigned char* key,
    size_t key_size,
    const unsigned char* value,
    size_t value_size,
    uint8_t val_idx,
    uint16_t block_id,
    uint16_t prev_block_id = 0
) {
    auto l = reinterpret_cast<Ledger*>(ledger);

    const ByteSlice key_slice((byte*)key, key_size);
    const ByteSlice value_slice((byte*)key, key_size);

    return l->put(key_slice, value_slice, val_idx, block_id, prev_block_id);
}

int ledger_remove(
    void* ledger, 
    const unsigned char* key,
    size_t key_size,
    uint8_t val_idx,
    uint16_t block_id,
    uint16_t prev_block_id = 0
) {
    auto l = reinterpret_cast<Ledger*>(ledger);

    const ByteSlice key_slice((byte*)key, key_size);

    byte zeros[32];
    std::memset(zeros, 0, 32);
    const ByteSlice zero_slice(zeros, 32);

    return l->put(key_slice, zero_slice, val_idx, block_id, prev_block_id);
}

int ledger_finalize(
    void* ledger, 
    uint16_t block_id, 
    void** out,
    size_t* out_size
) {
    Hash h;
    int rc = finalize_block(*(Ledger*)ledger, block_id, &h);
    if (rc == 0) {
        const size_t HASH_SIZE = sizeof(h.h);

        *out = malloc(HASH_SIZE);
        *out_size = HASH_SIZE;

        std::memcpy(*out, h.h, HASH_SIZE);
    }
    return rc;
}

int ledger_prune(void* ledger,  uint16_t block_id) {
    auto l = reinterpret_cast<Ledger*>(ledger);
    return prune_block(*l, block_id);
}

int ledger_justify(void* ledger,  uint16_t block_id) {
    auto l = reinterpret_cast<Ledger*>(ledger);
    return justify_block(*l, block_id);
}

int ledger_create_account(
    void* ledger,
    uint16_t block_id,
    const unsigned char* key,
    size_t key_size
) {
    auto l = reinterpret_cast<Ledger*>(ledger);
    const ByteSlice key_slice((byte*)key, key_size);
    return l->create_account(key_slice, block_id);
}

int ledger_delete_account(
    void* ledger,
    uint16_t block_id,
    const unsigned char* key,
    size_t key_size
) {
    auto l = reinterpret_cast<Ledger*>(ledger);
    const ByteSlice key_slice((byte*)key, key_size);
    return l->delete_account(key_slice, block_id);
}

int ledger_generate_existence_proof(
    void* ledger, 
    uint16_t block_id, 

    const unsigned char* key,
    size_t key_size,
    uint8_t val_idx,

    void** out,
    size_t* out_size
) {
    auto l = reinterpret_cast<Ledger*>(ledger);

    const ByteSlice key_slice((byte*)key, key_size);

    Hash key_hash;
    derive_hash(key_hash.h, key_slice);
    key_hash.h[31] = val_idx;

    std::vector<Commitment> Cs;
    std::vector<Proof> Pis;

    int rc = generate_proof(*l, Cs, Pis, key_hash, block_id);
    if (rc != 0) return rc;

    size_t total_size;
    total_size += sizeof(uint8_t);
    total_size += (Cs.size() * sizeof(Commitment));
    total_size += sizeof(uint8_t);
    total_size += (Pis.size() * sizeof(Proof));

    *out = malloc(total_size);
    *out_size = total_size;

    auto cursor = reinterpret_cast<byte*>(*out);

    *cursor = static_cast<uint8_t>(Cs.size());
    for (auto &commit: Cs) {
        blst_p1_compress(cursor, &commit);
        cursor += sizeof(Commitment);
    }

    *cursor = static_cast<uint8_t>(Pis.size());
    for (auto &proof: Pis) {
        blst_p1_compress(cursor, &proof);
        cursor += sizeof(Proof);
    }

    return 0;
}

int ledger_validate_proof(
    void* ledger, 
    uint16_t block_id, 

    const unsigned char* key,
    size_t key_size,

    const unsigned char* value_hash,
    size_t value_hash_size,
    uint8_t val_idx,

    const unsigned char* proof,
    size_t proof_size
) {
    auto l = reinterpret_cast<Ledger*>(ledger);

    auto cursor = proof;

    uint8_t Cs_size = *cursor;
    cursor++;
    std::vector<Commitment> Cs(Cs_size);;
    for (auto &commit: Cs) {
        commit = p1_from_bytes(cursor);
        cursor += sizeof(Commitment);
    }


    uint8_t Pis_size = *cursor;
    cursor++;
    std::vector<Proof> Pis(Pis_size);
    for (auto &proof: Pis) {
        proof = p1_from_bytes(cursor);
        cursor += sizeof(Proof);
    }


    const ByteSlice key_slice((byte*)key, key_size);
    Hash key_hash;
    derive_hash(key_hash.h, key_slice);
    key_hash.h[31] = val_idx;

    Hash val_hash;
    std::memcpy(val_hash.h, value_hash, value_hash_size);

    std::vector<size_t> Zs;
    std::vector<blst_scalar> Ys;

    derive_Zs_n_Ys(*l, key_hash, val_hash, &Cs, &Pis, &Zs, &Ys);

    return valid_proof(*l, &Cs, &Pis, key_hash, val_hash, val_idx);
}

