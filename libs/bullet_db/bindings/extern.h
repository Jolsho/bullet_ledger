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
#include <cstddef>
#include <cstdint>

extern "C" {
    void* ledger_open(
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

    int ledger_put(
        void* ledger, 
        const unsigned char* key,
        size_t key_size,
        const unsigned char* value,
        size_t value_size,
        uint8_t val_idx,
        uint16_t block_id,
        uint16_t prev_block_id = 0
    );

    int ledger_remove(
        void* ledger, 
        const unsigned char* key,
        size_t key_size,
        uint8_t val_idx,
        uint16_t block_id,
        uint16_t prev_block_id = 0
    );

    int ledger_finalize(
        void* ledger, 
        uint16_t block_id, 
        void** out,
        size_t* out_size
    );

    int ledger_prune(
        void* ledger, 
        uint16_t block_id
    );

    int ledger_justify(
        void* ledger, 
        uint16_t block_id
    );

    int ledger_create_account(
        void* ledger,
        uint16_t block_id,
        const unsigned char* key,
        size_t key_size
    );

    int ledger_delete_account(
        void* ledger,
        uint16_t block_id,
        const unsigned char* key,
        size_t key_size
    );

    int ledger_generate_existence_proof(
        void* ledger, 
        uint16_t block_id, 

        const unsigned char* key,
        size_t key_size,
        uint8_t val_idx,

        void** out,
        size_t* out_size
    );

    int ledger_validate_proof(
        void* ledger, 
        uint16_t block_id, 

        const unsigned char* key,
        size_t key_size,
        uint8_t val_idx,

        const unsigned char* proof,
        size_t proof_size
    );
}
