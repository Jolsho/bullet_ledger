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

#include "helpers.h"
#include "processing.h"

int ledger_finalize(
    void* ledger, 
    uint16_t block_id, 
    void** out,
    size_t* out_size
) {
    if (!ledger) return NULL_PARAMETER;
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
    if (!ledger) return NULL_PARAMETER;
    auto l = reinterpret_cast<Ledger*>(ledger);
    return prune_block(*l, block_id);
}

int ledger_justify(void* ledger,  uint16_t block_id) {
    if (!ledger) return NULL_PARAMETER;
    auto l = reinterpret_cast<Ledger*>(ledger);
    return justify_block(*l, block_id);
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
    if (!ledger || !key) return NULL_PARAMETER;
    if (val_idx < LEAF_ORDER) return VAL_IDX_RANGE;
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
    if (!ledger || !key || !value_hash) return NULL_PARAMETER;
    if (value_hash_size != 32) return VAL_HASH_SIZE;
    if (val_idx < LEAF_ORDER) return VAL_IDX_RANGE;

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

