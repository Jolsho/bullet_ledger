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

// extern.h
#include "hashing.h"
#include <cstddef>
#include <cstdint>

extern "C" {
    int ledger_open(
        void** out,
        const char* path, 
        size_t cache_size,
        size_t map_size,
        const char* tag,
        unsigned char* secret,
        size_t secret_size
    );

    int ledger_get_SRS(
        void *ledger, 
        void** out,
        size_t* out_size
    );

    int ledger_set_SRS(
        void* ledger, 
        const unsigned char* setup,
        size_t setup_size
    );

    // prev_block_hash is optional, and defaults to cannonical
    int ledger_create_account(
        void* ledger,
        const unsigned char* key, size_t key_size,
        const Hash* block_hash,
        const Hash* prev_block_hash
    );

    // prev_block_hash is optional, and defaults to cannonical
    int ledger_delete_account(
        void* ledger,
        const unsigned char* key, size_t key_size,
        const Hash* block_hash,
        const Hash* prev_block_hash
    );


    // prev_block_hash is optional, and defaults to cannonical
    int ledger_put(
        void* ledger, 
        const unsigned char* key, size_t key_size,
        const Hash* value_hash, uint8_t val_idx,
        const Hash* block_hash,
        const Hash* prev_block_hash
    );

    // prev_block_hash is optional, and defaults to cannonical
    int replace(
        void* ledger, 
        const unsigned char* key, size_t key_size,
        const Hash* value_hash, uint8_t val_idx,
        const Hash* prev_value_hash,
        const Hash* block_hash,
        const Hash* prev_block_hash
    );

    // prev_block_hash is optional, and defaults to cannonical
    int ledger_remove(
        void* ledger, 
        const unsigned char* key, size_t key_size,
        uint8_t val_idx,
        const Hash* block_hash,
        const Hash* prev_block_hash
    );

    int ledger_finalize(
        void* ledger, 
        const Hash* block_hash, 
        void** out,
        size_t* out_size
    );

    int ledger_prune(
        void* ledger, 
        const Hash* block_hash
    );

    int ledger_justify(
        void* ledger, 
        const Hash* block_hash
    );

    // block_hash is optional, and defaults to cannonical
    int ledger_generate_existence_proof(
        void* ledger, 
        const unsigned char* key, size_t key_size,
        uint8_t val_idx,
        void** out, size_t* out_size,
        const Hash* block_hash
    );

    // block_hash is optional, and defaults to cannonical
    int ledger_validate_proof(
        void* ledger, 
        const unsigned char* key, size_t key_size,
        const Hash* value_hash, uint8_t val_idx,
        const unsigned char* proof, size_t proof_size
    );


    int ledger_db_store_value(
        void* ledger, 
        const unsigned char* key_hash, size_t key_hash_size,
        const unsigned char* value, size_t value_size
    );
    int ledger_db_delete_value(
        void* ledger, 
        const unsigned char* key, size_t key_size
    );
    int ledger_db_get_value(
        void* ledger, 
        const unsigned char* key, size_t key_size,
        void** out, size_t* out_size
    );
    int ledger_db_value_exists(
        void* ledger, 
        const unsigned char* key, size_t key_size
    );
}
