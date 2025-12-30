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

#include "ledger.h"

int ledger_create_account(
    void* ledger,
    uint16_t block_id,
    const unsigned char* key,
    size_t key_size
) {
    if (!ledger || !key) return NULL_PARAMETER;

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
    if (!ledger || !key) return NULL_PARAMETER;
    auto l = reinterpret_cast<Ledger*>(ledger);
    const ByteSlice key_slice((byte*)key, key_size);
    return l->delete_account(key_slice, block_id);
}


int ledger_put(
    void* ledger, 
    const unsigned char* key,
    size_t key_size,
    const unsigned char* val_hash,
    size_t val_hash_size,
    uint8_t val_idx,
    uint16_t block_id,
    uint16_t prev_block_id = 0
) {
    if (!ledger || !key || !val_hash) return NULL_PARAMETER;
    if (val_hash_size != 32) return VAL_HASH_SIZE;
    if (val_idx > LEAF_ORDER) return VAL_IDX_RANGE;

    auto l = reinterpret_cast<Ledger*>(ledger);

    const ByteSlice key_slice((byte*)key, key_size);


    Hash hash;
    std::memcpy(hash.h, val_hash, val_hash_size);

    return l->put(key_slice, hash, val_idx, block_id, prev_block_id);
}

int ledger_replace(
    void* ledger, 
    const unsigned char* key,
    size_t key_size,

    const unsigned char* val_hash,
    size_t val_hash_size,

    const unsigned char* prev_val_hash,
    size_t prev_val_hash_size,

    uint8_t val_idx,
    uint16_t block_id,
    uint16_t prev_block_id = 0
) {
    if (!ledger || !val_hash || !key || !prev_val_hash) return NULL_PARAMETER;
    if (val_hash_size != 32 || prev_val_hash_size != 32) return VAL_HASH_SIZE;
    if (val_idx > LEAF_ORDER) return VAL_IDX_RANGE;

    auto l = reinterpret_cast<Ledger*>(ledger);

    const ByteSlice key_slice((byte*)key, key_size);

    Hash hash;
    std::memcpy(hash.h, val_hash, val_hash_size);

    Hash prev_hash;
    std::memcpy(prev_hash.h, prev_val_hash, prev_val_hash_size);

    return l->replace(key_slice, hash, prev_hash, val_idx, block_id, prev_block_id);
}

int ledger_remove(
    void* ledger, 
    const unsigned char* key,
    size_t key_size,
    uint8_t val_idx,
    uint16_t block_id,
    uint16_t prev_block_id = 0
) {
    if (!ledger || !key) return NULL_PARAMETER;
    if (val_idx < LEAF_ORDER) return VAL_IDX_RANGE;

    auto l = reinterpret_cast<Ledger*>(ledger);

    const ByteSlice key_slice((byte*)key, key_size);

    Hash zero_h;
    std::memset(zero_h.h, 0, 32);

    return l->put(key_slice, zero_h, val_idx, block_id, prev_block_id);
}
