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

extern "C" {
int ledger_db_store_value(
    void* ledger, 
    const unsigned char* key, size_t key_size,
    const unsigned char* value, size_t value_size
) {
    if (!ledger || !value || !key) return NULL_PARAMETER;
    auto l = reinterpret_cast<Ledger*>(ledger);

    const ByteSlice key_slice((byte*)key, key_size);
    Hash key_hash;
    derive_hash(key_hash.h, key_slice);

    const ByteSlice value_slice((byte*)value, value_size);

    return l->store_value(&key_hash, value_slice);
}

int ledger_db_delete_value(
    void* ledger, 
    const unsigned char* key, size_t key_size
) {
    if (!ledger || !key) return NULL_PARAMETER;
    auto l = reinterpret_cast<Ledger*>(ledger);

    const ByteSlice key_slice((byte*)key, key_size);
    Hash key_hash;
    derive_hash(key_hash.h, key_slice);

    return l->delete_value(&key_hash);
}

int ledger_db_get_value(
    void* ledger, 
    const unsigned char* key, size_t key_size,
    void** out, size_t* out_size
) {
    if (!ledger || !key) return NULL_PARAMETER;
    auto l = reinterpret_cast<Ledger*>(ledger);

    const ByteSlice key_slice((byte*)key, key_size);
    Hash key_hash;
    derive_hash(key_hash.h, key_slice);

    return l->get_value(&key_hash, out, out_size);
}

int ledger_db_value_exists(
    void* ledger, 
    const unsigned char* key, size_t key_size
) {
    if (!ledger || !key) return NULL_PARAMETER;
    auto l = reinterpret_cast<Ledger*>(ledger);

    const ByteSlice key_slice((byte*)key, key_size);
    Hash key_hash;
    derive_hash(key_hash.h, key_slice);

    return l->value_exists(&key_hash);
}
}
